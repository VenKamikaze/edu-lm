import io
import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from pathlib import Path

import support  # noqa: F401  (sets sys.path)

TOOL = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOL))
import check_spec_codes as csc  # noqa: E402

SHEET = ('<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>%s</sheetData></worksheet>')


def make_workbook(path, rows):
    """A minimal workbook: sheet1 with code in column E and text in column J."""
    strings = []

    def s(text):
        strings.append(text)
        return len(strings) - 1
    body = ""
    for n, (code, text) in enumerate(rows, start=1):
        body += '<row r="%d"><c r="E%d" t="s"><v>%d</v></c><c r="J%d" t="s"><v>%d</v></c></row>' % (n, n, s(code), n, s(text))
    sst = '<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">%s</sst>' % "".join(
        "<si><t>%s</t></si>" % t for t in strings)
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("xl/sharedStrings.xml", sst)
        z.writestr("xl/worksheets/sheet1.xml", SHEET % body)


class SharedRun(unittest.TestCase):
    def test_longest_run(self):
        self.assertEqual(csc.longest_shared_run("a b c d e", "x b c d y"), 3)
        self.assertEqual(csc.longest_shared_run("Add, and SUBTRACT numbers", "add and subtract numbers now"), 4)
        self.assertEqual(csc.longest_shared_run("one two", "three four"), 0)

    def test_words_ignore_case_and_punctuation(self):
        self.assertEqual(csc.words("Two- and three-digit."), ["two", "and", "three", "digit"])


class Main(unittest.TestCase):
    ACARA = "add and subtract two and three digit numbers using place value to partition and regroup"

    def run_main(self, spec_cells):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        make_workbook(tmp / "wb.xlsx", [("AC9M3N03", self.ACARA), ("AC9M3N04", "other text")])
        spec = {"cells": [{"id": "c", "descriptors": spec_cells}]}
        real_read = Path.read_text

        def fake_read(self_, *a, **k):
            return json.dumps(spec) if self_.name == "spec.json" else real_read(self_, *a, **k)
        Path.read_text = fake_read
        try:
            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = csc.main(["x", str(tmp / "wb.xlsx")])
        finally:
            Path.read_text = real_read
        return rc, buf.getvalue()

    def cell(self, code, text, verified=True):
        return {"code": code, "text": text, "verified": verified}

    def test_copied_wording_fails(self):
        rc, out = self.run_main([self.cell("AC9M3N03", "add and subtract two and three digit numbers using place value"),
                                 self.cell("AC9M3N04", "fresh words")])
        self.assertEqual(rc, 1)
        self.assertIn("shares a run of 11 words", out)

    def test_own_words_pass(self):
        rc, out = self.run_main([self.cell("AC9M3N03", "split numbers into hundreds, tens and ones to add them up"),
                                 self.cell("AC9M3N04", "fresh words")])
        self.assertEqual(rc, 0, out)

    def test_a_null_code_is_allowed_and_counted_but_not_verified(self):
        ok = [self.cell("AC9M3N03", "split into parts"), self.cell("AC9M3N04", "x"), self.cell(None, "no code yet", False)]
        rc, out = self.run_main(ok)
        self.assertEqual(rc, 0, out)
        self.assertIn("1 descriptors without a code", out)
        ok[2]["verified"] = True
        rc, out = self.run_main(ok)
        self.assertEqual(rc, 1)
        self.assertIn("without a code is marked verified", out)

    def test_unknown_and_unmentioned_codes_fail(self):
        rc, out = self.run_main([self.cell("AC9M3N03", "x"), self.cell("AC9M3N99", "y")])
        self.assertEqual(rc, 1)
        self.assertIn("AC9M3N99 is not in the workbook", out)
        self.assertIn("workbook code AC9M3N04 is in no cell", out)


if __name__ == "__main__":
    unittest.main()
