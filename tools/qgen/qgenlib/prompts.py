"""Fills the skill's writer.md and checker.md templates for one cell."""
import re
from pathlib import Path

from .store import REPO_DIR, Cell, QgenError, Spec, id_number, read_json

TEMPLATE_DIR = REPO_DIR / ".claude" / "skills" / "qgen"
SUBJECT_LABEL = {"maths": "Mathematics", "science": "Science", "hass": "Humanities and Social Sciences (HASS)",
                 "english": "English"}

GENERATOR_FORMAT = """
generator (maths only: the daemon makes a fresh question each time, so there is no prompt or answer):
  {"type": "generator", "curriculum": "<code>", "generator": "add", "params": {"a": [10, 99], "b": [10, 99]}, "difficulty": 1}
  - `generator` is one of add, subtract, multiply, divide, place_value. Every param must be given as [min, max] whole numbers, inside the ranges for the year listed below.
  - No `prompt`, `answer`, `choices` or `explanation` on a generator entry.
"""

GENERATOR_SECTION = """
## Generator entries

Write {generator_count} generator entries among the {target}. What each one makes:
- add: "What is a + b?" with a and b drawn from their ranges.
- subtract: a − b, the larger number first, so never negative.
- multiply: a × b.
- divide: dividend ÷ divisor with no remainder; the params are `quotient` (the answer) and `divisor`.
- place_value: "What is the value of the 4 in 3405?" for a number with `digits` digits; the digit asked about is never 0 and appears only once in the number. Numbers of five or more digits are shown with a space between groups of three.

Ranges you must stay within for {year_label} (a param outside them is rejected):
{ranges}

Make the entries different from each other: no two with the same generator and params. Use narrower ranges for difficulty 1 and wider ones for 3, and cover the descriptors the generators suit (place value, addition, subtraction, multiplication and division facts).
"""

RETRY_SECTION = """
## This is a second attempt

An earlier draft was checked and these problems were found. You may read that one earlier draft, at {previous}, and no other file. Fix every problem and write the complete corrected draft (all the entries, not just the fixed ones) to the output path below. Entry N below is the Nth entry of the list in that draft. Problems:

{errors}
"""


def render(template, values):
    def sub(m):
        if m.group(1) not in values:
            raise QgenError(f"template placeholder {{{{{m.group(1)}}}}} has no value")
        return str(values[m.group(1)])
    text = re.sub(r"\{\{(\w+)\}\}", sub, template)
    return text


def _descriptors(cell):
    lines = ["- %s: %s" % (d["code"], d["text"]) for d in cell["descriptors"] if d.get("code")]
    unverified = [d for d in cell["descriptors"] if not d.get("code")]
    if unverified:
        lines.append("(no confirmed code yet for: %s; leave `curriculum` out)" % "; ".join(d["text"] for d in unverified))
    return "\n".join(lines)


def build_prompt(ctx, kind, cell_id, out=None, retry=False):
    spec = Spec.load(ctx)
    cell = Cell(ctx, spec, cell_id)
    info = cell.info
    year = spec.year(info)
    values = {
        "cell_id": cell_id, "subject_label": SUBJECT_LABEL[info["subject"]], "year_label": year["label"],
        "age": year["age"], "reading_level": year["reading_level"], "strand_title": info["title"],
    }
    if kind == "writer":
        gens = round(info["target"] * info.get("generator_share", 0))
        out = Path(out) if out else cell.dir / "writer" / "draft.json"
        values.update(target=info["target"], generator_count=gens, written_count=info["target"] - gens,
                      descriptors=_descriptors(info), output_path=out.resolve())
        ranges = "\n".join("- %s: %s" % (g, ", ".join("%s in %s" % (p, r) for p, r in ps.items()))
                           for g, ps in spec.ranges(info).items())
        values["generator_format"] = GENERATOR_FORMAT if gens else ""
        values["generator_section"] = GENERATOR_SECTION.format(
            generator_count=gens, target=info["target"], year_label=year["label"], ranges=ranges) if gens else ""
        values["retry_section"] = ""
        if retry:
            report = read_json(cell.validation_path, "validation report") if cell.validation_path.exists() else None
            if not report or report.get("ok"):
                raise QgenError(f"{cell_id} has no failed validation to retry; run validate first")
            offset = cell.status().get("id_offset", 0)
            errors = ["- entry %d (%s): %s" % (id_number(info, qid) - offset, qid, m)
                      for qid, v in report["questions"].items() for m in v["errors"] if qid]
            errors += ["- (whole pack): %s" % m for m in report["questions"].get("", {}).get("errors", [])]
            values["retry_section"] = RETRY_SECTION.format(
                previous=(cell.dir / "draft.json").resolve(), errors="\n".join(errors))
    elif kind == "checker":
        if not cell.blind_path.exists():
            raise QgenError(f"{cell_id} has no blind file; run blind first")
        out = Path(out) if out else cell.dir / "checker" / "checker.json"
        values.update(blind_path=cell.blind_path.resolve(), output_path=out.resolve())
    else:
        raise QgenError("prompt kind must be writer or checker")
    return render((TEMPLATE_DIR / f"{kind}.md").read_text(encoding="utf-8"), values)
