"""The review CSV: one row per question, in and out."""
import copy
import csv
import io
import re

from . import answer
from .store import QgenError

# Content columns: what a reviewer may edit (approve = edit). Anything else is informational.
CONTENT = ["type", "prompt", "choices", "answer", "accept", "tolerance", "explanation", "curriculum", "difficulty"]
COLUMNS = (["id"] + CONTENT[:1] + CONTENT[1:] +
           ["checker_answer", "agree", "checker_flags", "checker_notes", "checks", "generator_samples",
            "approve", "notes"])
REQUIRED_IN = ["id", "approve"] + CONTENT

QUESTION_KEYS = ["id", "years", "subject", "curriculum", "type", "prompt", "choices", "answer", "tolerance",
                 "accept", "generator", "params", "difficulty", "explanation"]


def generator_label(q):
    params = ", ".join("%s %d-%d" % (k, v[0], v[1]) for k, v in (q.get("params") or {}).items())
    return "[generator %s: %s]" % (q.get("generator"), params)


def content_cells(q):
    """The content columns of a pack entry, as the strings a spreadsheet will hold."""
    t = q["type"]
    if t == "generator":
        return {"type": t, "prompt": generator_label(q), "choices": "", "answer": "", "accept": "",
                "tolerance": "", "explanation": "", "curriculum": q.get("curriculum", ""),
                "difficulty": str(q.get("difficulty", ""))}
    tol = q.get("tolerance")
    return {
        "type": t,
        "prompt": q["prompt"],
        "choices": "\n".join(q.get("choices", [])) if t == "multiple_choice" else "",
        "answer": answer.correct_answer_text(q),
        "accept": "\n".join(q.get("accept", [])) if t == "short_text" else "",
        "tolerance": "" if tol is None else _num(tol),
        "explanation": q.get("explanation", ""),
        "curriculum": q.get("curriculum", ""),
        "difficulty": str(q.get("difficulty", "")),
    }


def _num(v):
    return str(int(v)) if float(v) == int(v) else repr(float(v))


def build_rows(pack, validation, compare, samples):
    rows = []
    for q in pack["questions"]:
        row = {c: "" for c in COLUMNS}
        row["id"] = q["id"]
        row.update(content_cells(q))
        r = (compare or {}).get("results", {}).get(q["id"])
        if r:
            row["checker_answer"] = r.get("checker_answer", "")
            row["agree"] = {True: "yes", False: "no"}.get(r.get("agree"), "")
            row["checker_flags"] = ", ".join(r.get("flags", []))
            row["checker_notes"] = " | ".join(x for x in (r.get("notes", ""), r.get("problem", "")) if x)
        v = (validation or {}).get("questions", {}).get(q["id"], {})
        row["checks"] = "\n".join(v.get("flags", []))
        row["generator_samples"] = "\n".join("%s = %s" % (s["prompt"], s["answer"])
                                             for s in samples.get(q["id"], []))
        rows.append(row)
    return rows


def write_csv(path, rows):
    # UTF-8 with a BOM so Excel and LibreOffice read the accents and the minus signs
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)


def read_csv(path):
    """Returns (rows, missing columns, encoding name). UTF-8 first, then Excel's Windows-1252."""
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise QgenError(f"cannot read {path}: {e.strerror}")
    for encoding, name in (("utf-8-sig", "UTF-8"), ("cp1252", "Windows-1252")):
        try:
            text = raw.decode(encoding)
            break
        except UnicodeDecodeError:
            continue
    else:
        raise QgenError(f"{path} is neither UTF-8 nor Windows-1252 text; save it as CSV UTF-8")
    try:
        reader = csv.DictReader(io.StringIO(text, newline=""))
        rows = list(reader)
    except csv.Error as e:
        raise QgenError(f"{path} is not readable CSV: {e}")
    missing = [c for c in REQUIRED_IN if c not in (reader.fieldnames or [])]
    return (None if missing else rows), missing, name


def _text(s):
    s = (s or "").replace("\r\n", "\n").replace("\r", "\n")
    return "\n".join(line.rstrip() for line in s.split("\n")).strip()


def _canon(column, value, qtype):
    """Compare like with like: a spreadsheet may reformat numbers and trailing spaces."""
    v = _text(value)
    if column == "answer" and qtype == "numeric":
        n = answer.parse_number(v)
        return n if n is not None else v
    if column == "answer" and qtype == "true_false":
        b = answer.parse_bool(v)  # a spreadsheet writes TRUE for True
        return b if b is not None else v
    if column in ("tolerance", "difficulty"):
        try:
            return float(v) if v else ""
        except ValueError:
            return v
    return v


def changed_columns(q, row):
    """Content columns of a CSV row that differ from what export wrote for this question."""
    expected = content_cells(q)
    return [c for c in CONTENT
            if _canon(c, row.get(c), q["type"]) != _canon(c, expected[c], q["type"])]


_MONTH = r"(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*"
_AUTO_FORMATS = [
    re.compile(r"\d{1,4}[/.-]\d{1,2}[/.-]\d{1,4}"),        # 1/04/2026, 2026-04-01
    re.compile(r"\d{1,2}[- ]" + _MONTH + r"([- ]\d{2,4})?", re.I),  # 4-Jan, 1 Apr 2026
    re.compile(_MONTH + r"[- ]\d{1,4}", re.I),               # Apr-01
    re.compile(r"\d(\.\d+)?E[+-]\d+", re.I),                # 1E+05
]


def spreadsheet_mangled(base, row):
    """Changed answer, accept or choices lines that look like a spreadsheet's auto-format
    (a date or scientific notation) where the exported text did not. Returns messages."""
    expected = content_cells(base)
    found = []
    for column in ("answer", "accept", "choices"):
        old = {line.strip() for line in _text(expected[column]).split("\n")}
        for line in _text(row.get(column)).split("\n"):
            line = line.strip()
            if line and line not in old and any(p.fullmatch(line) for p in _AUTO_FORMATS):
                found.append("%s '%s' looks like a spreadsheet auto-format (for example 1/4 turned into a date); "
                             "set the column to Text, retype it and mark edit again" % (column, line))
    return found


def diff_fields(old, new):
    """(field, old, new) for each pack field an edit changed."""
    out = []
    for k in QUESTION_KEYS:
        if old.get(k) != new.get(k):
            out.append((k, old.get(k), new.get(k)))
    return out


def apply_edit(base, row):
    """A copy of the pack entry with the row's content columns applied. Returns (entry, error)."""
    q = copy.deepcopy(base)
    t = base["type"]
    if t == "generator":
        return None, "generator rows cannot be edited in the CSV; edit the pack by hand"
    if _text(row.get("type")) != t:
        return None, "the type cannot be changed"
    q["prompt"] = _text(row.get("prompt"))
    if not q["prompt"]:
        return None, "empty prompt"
    explanation = _text(row.get("explanation"))
    if explanation:
        q["explanation"] = explanation
    else:
        q.pop("explanation", None)
    q["curriculum"] = _text(row.get("curriculum"))
    try:
        q["difficulty"] = int(_text(row.get("difficulty")))
    except ValueError:
        return None, "difficulty must be a whole number"

    given = _text(row.get("answer"))
    if t == "multiple_choice":
        choices = [c.strip() for c in _text(row.get("choices")).split("\n") if c.strip()]
        hits = [i for i, c in enumerate(choices) if c == given]
        if len(hits) != 1:
            return None, "answer must match exactly one of the choices"
        q["choices"], q["answer"] = choices, hits[0]
    elif t == "true_false":
        b = answer.parse_bool(given)
        if b is None:
            return None, "answer must be True or False"
        q["answer"] = b
    elif t == "numeric":
        n = answer.parse_number(given)
        if n is None:
            return None, "answer is not a number"
        q["answer"] = int(n) if n == int(n) else n
        tol = _text(row.get("tolerance"))
        if tol:
            try:
                q["tolerance"] = float(tol)
            except ValueError:
                return None, "tolerance is not a number"
            if q["tolerance"] == int(q["tolerance"]):
                q["tolerance"] = int(q["tolerance"])
        else:
            q.pop("tolerance", None)
    elif t == "short_text":
        accept = [a.strip() for a in _text(row.get("accept")).split("\n") if a.strip()]
        expected = content_cells(base)
        answer_changed = given != _text(expected["answer"])
        if answer_changed and _text(row.get("accept")) == _text(expected["accept"]):
            return None, "answer changed but accept did not; accept lists every accepted answer, so edit that column"
        if answer_changed and given and given not in accept:
            accept.insert(0, given)
        if not accept:
            return None, "no accepted answers"
        q["accept"] = accept
    return q, None


def canonical_question(q):
    """The entry with keys in a fixed order, so files are the same however they were built."""
    out = {k: q[k] for k in QUESTION_KEYS if k in q}
    out.update({k: v for k, v in q.items() if k not in out})
    return out
