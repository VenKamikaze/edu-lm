#!/usr/bin/env python3
"""Check spec.json against ACARA's official curriculum workbook.

    curl -L -o /tmp/curriculum-workbook.xlsx \\
      https://www.australiancurriculum.edu.au/content/dam/en/curriculum/ac-version-9/downloads/curriculum-workbook.xlsx
    python3 check_spec_codes.py /tmp/curriculum-workbook.xlsx

Reports, exiting 1 on any problem:
- codes in a cell that are not in the workbook (a descriptor with code null is allowed; it is
  counted separately and must not be marked verified);
- workbook codes for our subjects and years that no cell mentions;
- descriptions that copy ACARA's wording: the longest run of consecutive words shared with the
  workbook's content description must be at most MAX_SHARED_WORDS.
"""
import json
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
MAX_SHARED_WORDS = 6
SUBJECT_PREFIX = {"maths": "AC9M", "science": "AC9S", "hass": "AC9HS", "english": "AC9E"}


def workbook_codes(path):
    """{code: content description} for every AC9 code in the 'Learning areas' sheet (E is Code, J is text)."""
    z = zipfile.ZipFile(path)
    strings = [
        "".join(t.text or "" for t in si.iter(NS + "t"))
        for si in ET.fromstring(z.read("xl/sharedStrings.xml")).iter(NS + "si")
    ]
    codes = {}
    for _, el in ET.iterparse(z.open("xl/worksheets/sheet1.xml")):
        if el.tag != NS + "row":
            continue
        cells = {}
        for c in el.iter(NS + "c"):
            v = c.find(NS + "v")
            if v is not None and c.get("t") == "s":
                cells[re.match(r"[A-Z]+", c.get("r")).group()] = strings[int(v.text)]
        if re.fullmatch(r"AC9[A-Z0-9]+", cells.get("E", "")):
            codes[cells["E"]] = cells.get("J", "")
        el.clear()
    return codes


def words(text):
    return re.findall(r"[a-z0-9]+", text.lower())


def longest_shared_run(a, b):
    """Length of the longest run of consecutive words that both texts contain."""
    a, b = words(a), words(b)
    best = 0
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0] * (len(b) + 1)
        for j, y in enumerate(b, start=1):
            if x == y:
                cur[j] = prev[j - 1] + 1
                best = max(best, cur[j])
        prev = cur
    return best


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    spec = json.loads((Path(__file__).parent / "spec.json").read_text(encoding="utf-8"))
    known = workbook_codes(argv[1])
    problems = 0
    used = set()
    unconfirmed = 0
    for cell in spec["cells"]:
        for d in cell["descriptors"] + cell.get("not_quizzable", []):
            code = d["code"]
            if code is None:
                unconfirmed += 1
                if d.get("verified"):
                    print(f"{cell['id']}: a descriptor without a code is marked verified")
                    problems += 1
                continue
            used.add(code)
            if code not in known:
                print(f"{cell['id']}: {code} is not in the workbook")
                problems += 1
            elif d.get("text"):
                run = longest_shared_run(d["text"], known[code])
                if run > MAX_SHARED_WORDS:
                    print(f"{cell['id']}: {code} shares a run of {run} words with ACARA's text; reword it")
                    problems += 1
    for subject, prefix in SUBJECT_PREFIX.items():
        for year in (3, 4, 5):
            pattern = re.compile(re.escape(prefix) + str(year) + r"[A-Z]+[0-9]+")
            for code in sorted(c for c in known if pattern.fullmatch(c) and c not in used):
                print(f"{subject} year {year}: workbook code {code} is in no cell")
                problems += 1
    print(f"{len(used)} codes checked, {unconfirmed} descriptors without a code, {problems} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
