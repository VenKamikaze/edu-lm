"""Checks the C++ validator does not do. Errors go back to the writer; flags go to the parent."""
import difflib
import json
import re
from pathlib import Path

NEAR_DUPLICATE = 0.85
DIFFICULTY = (1, 3)
GENERATOR_PARAMS = {"add": ("a", "b"), "subtract": ("a", "b"), "multiply": ("a", "b"),
                    "divide": ("quotient", "divisor"), "place_value": ("digits",)}


def normalise_prompt(text):
    """Lower case, punctuation gone, spaces collapsed: what 'the same question' means."""
    return " ".join(re.sub(r"[^\w]+", " ", text.casefold()).split())


def _squash(text):
    return re.sub(r"\s+", "", text).casefold()


def question_errors(q, cell, spec):
    """Problems with one entry that the writer can fix. Uses only the entry and the cell."""
    errors = []
    is_gen = q.get("type") == "generator"

    codes = spec.codes(cell)
    if codes:
        if not q.get("curriculum"):
            errors.append("curriculum is missing (use one of: %s)" % ", ".join(codes))
        elif q["curriculum"] not in codes:
            errors.append("curriculum '%s' is not one of this cell's codes (%s)" % (q["curriculum"], ", ".join(codes)))

    d = q.get("difficulty")
    if not isinstance(d, int) or isinstance(d, bool) or not DIFFICULTY[0] <= d <= DIFFICULTY[1]:
        errors.append("difficulty must be an integer %d-%d" % DIFFICULTY)

    if not is_gen and not str(q.get("explanation") or "").strip():
        errors.append("written question has no explanation")

    choices = q.get("choices")
    if q.get("type") == "multiple_choice" and isinstance(choices, list) and all(isinstance(c, str) for c in choices):
        for i in range(len(choices)):
            for j in range(i + 1, len(choices)):
                if choices[i].strip() != choices[j].strip() and _squash(choices[i]) == _squash(choices[j]):
                    errors.append("choices %d and %d differ only in case or spacing ('%s', '%s')"
                                  % (i, j, choices[i], choices[j]))

    if is_gen:
        errors += _generator_errors(q, cell, spec)
    return errors


def _generator_errors(q, cell, spec):
    if not cell.get("generator_share"):
        return ["generator entries are not wanted in this cell"]
    name = q.get("generator")
    allowed = spec.ranges(cell).get(name)
    if allowed is None:
        return []  # an unknown generator is the C++ validator's error
    params = q.get("params")
    if not isinstance(params, dict):
        return ["params must be given for generator '%s': %s" % (name, ", ".join(GENERATOR_PARAMS[name]))]
    errors = []
    for pname, (lo, hi) in allowed.items():
        r = params.get(pname)
        if not (isinstance(r, list) and len(r) == 2 and all(isinstance(x, int) for x in r)):
            errors.append("param '%s' must be given explicitly as [min, max] (Year %d range %s)"
                          % (pname, cell["year"], [lo, hi]))
        elif r[0] < lo or r[1] > hi:
            errors.append("param '%s' %s is outside the Year %d range %s" % (pname, r, cell["year"], [lo, hi]))
    return errors


def load_existing(data_dir):
    """(id, prompt, file name) for every question in the existing packs."""
    found = []
    for path in sorted(Path(data_dir).glob("*.json")):
        try:
            pack = json.loads(path.read_text(encoding="utf-8"))
            entries = pack["questions"]
            for e in entries:
                found.append((str(e.get("id", "")), str(e.get("prompt", "")), path.name))
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            continue  # a bad pack is the C++ validator's business
    return found


def check_pack(pack, cell, spec, existing, threshold=NEAR_DUPLICATE, own_file=None):
    """Returns ({id: {'errors': [...], 'flags': [...]}}, [cell-level notes]).

    existing includes the cell's own imported pack (own_file): an earlier batch counts for
    duplicates. Its same-id entry is the same question (this pack re-validated after import).
    """
    result = {}
    notes = []
    questions = pack["questions"]
    existing_ids = {}
    for i, _, f in existing:
        if f != own_file:
            existing_ids.setdefault(i, f)
    seen_ids = set()
    for q in questions:
        qid = q.get("id", "")
        entry = result.setdefault(qid, {"errors": [], "flags": []})
        entry["errors"] += question_errors(q, cell, spec)
        if qid in existing_ids:
            entry["errors"].append("id already used in %s" % existing_ids[qid])
        if qid in seen_ids:
            entry["errors"].append("id repeated in this pack")
        seen_ids.add(qid)

    # near-duplicates: within the cell (flag the later one) and against the existing packs
    written = [(q["id"], normalise_prompt(q["prompt"])) for q in questions if q.get("prompt")]
    others = [(i, f, normalise_prompt(p)) for i, p, f in existing if p]
    for n, (qid, text) in enumerate(written):
        candidates = [(i, i, t) for i, t in written[:n]]
        candidates += [(i, "%s (%s)" % (i, f), t) for i, f, t in others if not (f == own_file and i == qid)]
        for _, label, other in candidates:
            if _similar(text, other, threshold):
                result[qid]["flags"].append("near_duplicate of %s" % label)
    seen_gen = {}
    for q in questions:
        if q.get("type") == "generator":
            key = json.dumps([q.get("generator"), q.get("params")], sort_keys=True)
            if key in seen_gen:
                result[q["id"]]["flags"].append("near_duplicate of %s (same generator and params)" % seen_gen[key])
            seen_gen.setdefault(key, q["id"])

    target = cell["target"]
    if not 0.75 * target <= len(questions) <= 1.25 * target:
        notes.append("%d entries against a target of %d" % (len(questions), target))
    share = cell.get("generator_share", 0)
    gens = sum(1 for q in questions if q.get("type") == "generator")
    if share and abs(gens - round(target * share)) > max(2, 0.3 * target * share):
        notes.append("%d generator entries; the cell asks for about %d" % (gens, round(target * share)))
    return result, notes


def _similar(a, b, threshold):
    if a == b:
        return True
    # "6 x 7" and "6 x 8" read alike but ask different things
    if re.findall(r"\d+", a) != re.findall(r"\d+", b):
        return False
    m = difflib.SequenceMatcher(None, a, b)
    return m.real_quick_ratio() > threshold and m.quick_ratio() > threshold and m.ratio() > threshold
