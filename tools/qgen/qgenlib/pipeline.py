"""The qgen commands. Each takes a Ctx and returns a process exit code."""
import hashlib
import json
import math
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from . import answer, checks, review
from .store import Cell, QgenError, Spec, find_validator, id_number, read_json, sha256_file, write_json
from .validator import run_validator

WRITER_KEYS = {"type", "prompt", "choices", "answer", "tolerance", "accept", "generator", "params",
               "difficulty", "explanation", "curriculum"}
# stamped or assigned by ingest, so a writer's value is dropped without comment
STAMPED_KEYS = {"id", "years", "subject"}
CHECKER_FLAGS = {"ambiguous", "reading_level", "factual", "not_australian", "unsuitable"}
SAMPLES_PER_GENERATOR = 5


def cmd_cells(ctx, subject=None, year=None):
    spec = Spec.load(ctx)
    print("%-34s %-10s %6s %8s" % ("cell", "stage", "target", "entries"))
    for cid, info in spec.cells.items():
        if subject and info["subject"] != subject or year and info["year"] != year:
            continue
        status = Cell(ctx, spec, cid).status()
        print("%-34s %-10s %6d %8s" % (cid, status.get("stage", "new"), info["target"], status.get("entries", "-")))
    return 0


def read_draft(path):
    """The writer's draft as a list of entries; anything not well-formed JSON is refused."""
    try:
        raw = Path(path).read_bytes()
    except OSError as e:
        raise QgenError(f"cannot read draft {path}: {e.strerror}")
    try:
        data = json.loads(raw.decode("utf-8-sig"))
    except UnicodeDecodeError as e:
        raise QgenError(f"draft {path} is not UTF-8 text: {e}")
    except json.JSONDecodeError as e:
        raise QgenError(f"draft {path} is not valid JSON: {e.msg} at line {e.lineno} column {e.colno}")
    entries = data.get("questions") if isinstance(data, dict) else data
    if not isinstance(entries, list):
        raise QgenError(f"draft {path}: expected a JSON list of questions, or an object with a 'questions' list")
    if not entries:
        raise QgenError(f"draft {path} has no questions")
    for i, e in enumerate(entries):
        if not isinstance(e, dict):
            raise QgenError(f"draft {path}: entry {i + 1} is not a JSON object")
    return entries


def assign_id(cell_info, n):
    return "%s-%04d" % (cell_info["id_prefix"], n)


def _shorten(value, width=100):
    text = (value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)).replace("\n", " / ")
    text = text if len(text) <= width else text[:width] + "..."
    return repr(text) if isinstance(value, str) else text


def cmd_ingest(ctx, cell_id, draft, force=False):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    entries = read_draft(draft)
    if cell.reached("exported") and not force:
        raise QgenError(f"{cell_id} is at stage '{cell.stage}'; a new draft would replace reviewed work "
                        "(pass --force if that is what you want)")
    # ids continue after every id already handed out (imported or exported), so a second batch
    # never reuses one; before export a redraft of the same batch gets the same ids again
    offset = cell.highest_id_number(id_number)
    if offset + len(entries) > 9999:
        raise QgenError("a cell holds at most 9999 questions")
    codes = spec.codes(cell.info)
    questions, dropped = [], set()
    for n, e in enumerate(entries, start=offset + 1):
        dropped |= set(e) - WRITER_KEYS - STAMPED_KEYS
        q = {k: v for k, v in e.items() if k in WRITER_KEYS}
        q.update(id=assign_id(cell.info, n), years=[cell.info["year"]], subject=cell.info["subject"])
        if len(codes) == 1 and not q.get("curriculum"):
            q["curriculum"] = codes[0]
        questions.append(review.canonical_question(q))
    pack = {"schema": 1, "pack": cell.info["pack"], "subject": cell.info["subject"], "questions": questions}
    write_json(cell.pack_path, pack)
    write_json(cell.dir / "draft.json", entries)
    cell.set_stage("ingested", reset=True, entries=len(questions), source=str(draft), id_offset=offset)
    cell.clear_writer()
    print(f"{cell_id}: {len(questions)} entries, ids {questions[0]['id']} to {questions[-1]['id']}")
    print(f"pack: {cell.pack_path}")
    if dropped:
        print("note: ignored unknown fields: " + ", ".join(sorted(dropped)))
    return 0


def cmd_validate(ctx, cell_id, threshold=checks.NEAR_DUPLICATE):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    cell.require("ingested", "run ingest first")
    pack = cell.load_pack()
    cpp = run_validator(ctx, [cell.pack_path], sample=SAMPLES_PER_GENERATOR)
    existing = checks.load_existing(ctx.data_dir)
    per_question, notes = checks.check_pack(pack, cell.info, spec, existing, threshold, own_file=cell.target_path.name)
    for e in cpp["errors"]:
        per_question.setdefault(e["id"] or "", {"errors": [], "flags": []})["errors"].append("edu-lm-validate: " + e["message"])
    samples = {}
    for s in cpp["samples"]:
        samples.setdefault(s["id"], []).append({"prompt": s["prompt"], "answer": s["answer"]})
    n_errors = sum(len(v["errors"]) for v in per_question.values())
    if cpp["loaded"] is not None and cpp["loaded"] != len(pack["questions"]) and not cpp["errors"]:
        raise QgenError(f"edu-lm-validate loaded {cpp['loaded']} of {len(pack['questions'])} questions")
    ok = n_errors == 0 and cpp["returncode"] == 0
    write_json(cell.validation_path, {
        "validated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "pack_sha256": sha256_file(cell.pack_path), "ok": ok, "cpp_warnings": cpp["warnings"],
        "questions": per_question, "cell_notes": notes, "generator_samples": samples})
    for qid, v in per_question.items():
        for m in v["errors"]:
            print(f"error: {qid or '(pack)'}: {m}")
        for m in v["flags"]:
            print(f"flag: {qid}: {m}")
    for w in cpp["warnings"]:
        print(f"warning: {w['id'] or '(pack)'}: {w['message']}")
    for m in notes:
        print(f"note: {m}")
    n_flags = sum(len(v["flags"]) for v in per_question.values())
    print(f"{cell_id}: {len(pack['questions'])} entries, {n_errors} errors, {n_flags} flags")
    if ok:
        cell.set_stage("validated")
    else:
        print("not validated: send these errors back to a fresh writer (prompt writer --retry)")
    return 0 if ok else 1


def load_valid_pack(cell):
    """The pack, refusing if validate has not passed on exactly this pack."""
    cell.require("ingested", "run ingest first")
    if not cell.validation_path.exists():
        raise QgenError(f"{cell.id} has not been validated; run validate first")
    v = read_json(cell.validation_path, "validation report")
    if not v.get("ok") or v.get("pack_sha256") != sha256_file(cell.pack_path):
        raise QgenError(f"{cell.id}: validate has not passed on the current pack; run validate again")
    return cell.load_pack(), v


def blind_token(questions):
    """A fingerprint of the blind questions; the checker copies it back so compare can tell
    that its answers belong to this exact file, not an older one."""
    text = json.dumps(questions, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def cmd_blind(ctx, cell_id):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    pack, _ = load_valid_pack(cell)
    out = []
    for q in pack["questions"]:
        if q["type"] == "generator":
            continue
        b = {"id": q["id"], "type": q["type"], "prompt": q["prompt"]}
        if q["type"] == "multiple_choice":
            b["choices"] = q["choices"]
        out.append(b)
    write_json(cell.blind_path, {"token": blind_token(out), "questions": out})
    cell.set_stage("blinded", blind_questions=len(out))
    print(f"{cell_id}: {len(out)} questions (generators left out) -> {cell.blind_path}")
    return 0


def _answer_text(given):
    """The checker's answer as the text a child would type."""
    if isinstance(given, bool):
        return "true" if given else "false"
    if isinstance(given, float) and math.isfinite(given) and given == int(given):
        given = int(given)
    return "" if given is None else str(given)


def _matches(q, given):
    if isinstance(given, (int, float)) and not isinstance(given, bool) and q["type"] == "numeric":
        return abs(given - q["answer"]) <= q.get("tolerance", 0) + 1e-9
    return answer.is_correct(q, _answer_text(given))


def _shown(q, given):
    """What to show a reviewer, like displayAnswer: the choice text for a multiple choice index."""
    text = _answer_text(given)
    if q["type"] == "multiple_choice":
        i = answer.parse_index(text)
        if i is not None and 0 <= i < len(q["choices"]):
            return q["choices"][i]
    if q["type"] == "true_false":
        b = answer.parse_bool(text)
        if b is not None:
            return "True" if b else "False"
    return " ".join(text.split())


def parse_flags(raw):
    """The canonical shape is [{"flag": name, "note": text}]; a plain name and "name: note" are
    accepted too. Returns ([(name, note)], [problems])."""
    if raw is None:
        return [], []
    if not isinstance(raw, list):
        return [], ["flags must be a list, not %s" % json.dumps(raw)[:40]]
    flags, problems = [], []
    for item in raw:
        if isinstance(item, dict) and isinstance(item.get("flag"), str) and set(item) <= {"flag", "note"}:
            name, note = item["flag"], item.get("note", "")
        elif isinstance(item, str):
            name, _, note = item.partition(":")
        else:
            problems.append("flag %s is not {\"flag\": ..., \"note\": ...} or a string" % json.dumps(item)[:40])
            continue
        name, note = name.strip(), str(note or "").strip()
        if name not in CHECKER_FLAGS:
            problems.append("unknown flag '%s' (allowed: %s)" % (name, ", ".join(sorted(CHECKER_FLAGS))))
        else:
            flags.append((name, note))
    return flags, problems


def cmd_compare(ctx, cell_id, checker_path):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    cell.require("blinded", "run blind first")
    pack = cell.load_pack()
    blind = read_json(cell.blind_path, "blind file")
    if blind.get("token") != blind_token(blind.get("questions", [])):
        raise QgenError("the blind file has been altered; run blind again")
    data = read_json(checker_path, "checker file")
    rows = data.get("answers") if isinstance(data, dict) else None
    if not isinstance(rows, list):
        raise QgenError(f"checker file {checker_path}: expected an object with an 'answers' list")
    if data.get("token") != blind["token"]:
        raise QgenError(f"checker file {checker_path} has a missing or wrong token; it does not belong to the "
                        "current blind file (an older run?). Re-run the checker")

    problems, by_id = [], {}
    for i, r in enumerate(rows, start=1):
        if not isinstance(r, dict) or not isinstance(r.get("id"), str):
            problems.append("answer %d has no string id" % i)
        elif r["id"] in by_id:
            problems.append("%s is answered twice" % r["id"])
        else:
            by_id[r["id"]] = r
    keyed = [q for q in pack["questions"] if q["type"] != "generator"]
    parsed = {}
    for r in by_id.values():
        flags, bad = parse_flags(r.get("flags"))
        problems += ["%s: %s" % (r["id"], m) for m in bad]
        parsed[r["id"]] = flags
    if problems:
        raise QgenError("checker file %s is malformed (nothing was compared); re-run the checker:\n  %s"
                        % (checker_path, "\n  ".join(problems[:10])))

    results = {}
    counts = {"agree": 0, "disagree": 0, "missing": 0, "flagged": 0}
    for q in keyed:
        r = by_id.get(q["id"])
        if r is None or r.get("answer") is None:
            results[q["id"]] = {"agree": None, "checker_answer": "", "flags": [], "notes": "",
                                "problem": "the checker gave no answer"}
            counts["missing"] += 1
            counts["flagged"] += 1
            continue
        flags = parsed[q["id"]]
        notes = [str(r.get("notes") or "")] + ["%s: %s" % (n, t) for n, t in flags if t]
        agree = _matches(q, r["answer"])
        results[q["id"]] = {"agree": agree, "checker_answer": _shown(q, r["answer"]),
                            "flags": [n for n, _ in flags], "notes": " ".join(" ".join(notes).split()),
                            "problem": ""}
        counts["agree" if agree else "disagree"] += 1
        counts["flagged"] += bool(flags)
    extra = sorted(set(by_id) - {q["id"] for q in keyed})
    write_json(cell.compare_path, {"checker_file": str(checker_path), "summary": counts, "results": results})
    cell.set_stage("compared", **{"agree": counts["agree"], "disagree": counts["disagree"]})
    for qid, r in results.items():
        if r["agree"] is not True or r["flags"] or r["problem"]:
            line = "agree=%s" % {True: "yes", False: "NO", None: "missing"}[r["agree"]]
            print(f"review: {qid}: {line} flags={','.join(r['flags']) or '-'} {r['problem']}".rstrip())
    if extra:
        print("note: the checker answered ids not in the blind file: " + ", ".join(extra))
    print(f"{cell_id}: {counts['agree']} agree, {counts['disagree']} disagree, {counts['missing']} missing, "
          f"{counts['flagged']} flagged (nothing is removed; all of it goes to the CSV)")
    return 0


def cmd_export(ctx, cell_id):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    pack, validation = load_valid_pack(cell)
    compare = read_json(cell.compare_path, "compare report") if cell.compare_path.exists() else None
    if compare is None:
        print("warning: no checker comparison; the checker columns will be empty")
    rows = review.build_rows(pack, validation, compare, validation.get("generator_samples", {}))
    review.write_csv(cell.csv_path, rows)
    cell.set_stage("exported")
    cell.commit_ids(pack["questions"])
    print(f"{cell_id}: {len(rows)} rows -> {cell.csv_path}")
    return 0


def _pack_doc(cell, questions):
    return {"schema": 1, "pack": cell.info["pack"], "subject": cell.info["subject"], "questions": questions}


def _validate_alone(ctx, cell, question):
    """Load one edited entry through the real validator; returns error messages."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "one.json"
        write_json(path, _pack_doc(cell, [question]))
        res = run_validator(ctx, [path])
    return [e["message"] for e in res["errors"]]


def cmd_import(ctx, cell_id, csv_path):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    find_validator(ctx)  # fail before anything is written
    pack = cell.load_pack()
    rows, missing, encoding = review.read_csv(csv_path)
    if rows is None:
        raise QgenError(f"{csv_path} is missing columns: {', '.join(missing)}")
    print(f"read {csv_path} as {encoding}")

    previous = None
    kept = {}
    if cell.target_path.exists():
        previous = cell.target_path.read_bytes()
        kept = {q["id"]: q for q in read_json(cell.target_path, "existing pack")["questions"]}
    # rows may belong to the current batch or to an earlier, already imported one
    base = dict(kept)
    base.update({q["id"]: q for q in pack["questions"]})
    added, edited, removed, undecided, rejected, changes = 0, 0, 0, 0, [], []
    for row in rows:
        qid = (row.get("id") or "").strip()
        if not qid:
            continue
        choice = (row.get("approve") or "").strip().lower()
        if qid not in base:
            rejected.append((qid, "not a question of this cell"))
        elif choice in ("y", "yes"):
            changed = review.changed_columns(base[qid], row)
            if changed:
                hint = " (a spreadsheet can reformat cells such as 3/4 into a date)" if set(changed) & {"answer", "accept", "choices"} else ""
                rejected.append((qid, "approve is y but %s changed; mark it 'edit' to keep the change%s"
                                 % (", ".join(changed), hint)))
                continue
            added += qid not in kept
            kept[qid] = base[qid]
        elif choice == "edit":
            problems = review.spreadsheet_mangled(base[qid], row)
            q = None
            if not problems:
                q, problem = review.apply_edit(base[qid], row)
                problems = [problem] if problem else []
            if q:
                problems = checks.question_errors(q, cell.info, spec) or _validate_alone(ctx, cell, q)
            if problems:
                rejected.append((qid, "edit not accepted: " + "; ".join(problems)))
                continue
            edited += 1
            q = review.canonical_question(q)
            for field, old, new in review.diff_fields(base[qid], q):
                changes.append("edit %s: %s: %s -> %s" % (qid, field, _shorten(old), _shorten(new)))
            kept[qid] = q
        elif choice in ("n", "no"):
            removed += kept.pop(qid, None) is not None
        elif choice == "":
            undecided += 1
        else:
            rejected.append((qid, f"approve must be y, n or edit, not '{choice}'"))

    for qid, why in rejected:
        print(f"skipped: {qid}: {why}")
    for line in changes:
        print(line)
    if not kept:
        print(f"{cell_id}: nothing approved; nothing written")
        return 1 if rejected else 0

    def restore():
        if previous is None:
            cell.target_path.unlink(missing_ok=True)
        else:
            cell.target_path.write_bytes(previous)

    questions = [kept[i] for i in sorted(kept)]
    cell.ctx.data_dir.mkdir(parents=True, exist_ok=True)
    try:
        write_json(cell.target_path, _pack_doc(cell, questions))
        res = run_validator(ctx, [ctx.data_dir])
    except BaseException:
        restore()  # a crashing validator must not leave an unchecked pack behind
        raise
    if res["returncode"] != 0:
        restore()
        for e in res["errors"]:
            print(f"error: {e['file']}: {e['id'] or ''} {e['message']}")
        raise QgenError("edu-lm-validate rejected the data directory; the pack file was put back as it was")
    cell.set_stage("imported", imported=len(questions))
    cell.commit_ids(questions)
    print(f"{cell_id}: {len(questions)} questions in {cell.target_path} "
          f"({added} added, {edited} edited, {removed} removed, {undecided} undecided, {len(rejected)} skipped)")
    print(f"edu-lm-validate on {ctx.data_dir}: ok, {res['loaded']} questions in the bank")
    return 1 if rejected else 0
