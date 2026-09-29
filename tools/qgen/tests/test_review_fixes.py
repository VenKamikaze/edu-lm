"""Regression tests for the review of phase 5a: batches, mangled edits, stale files, flags, validators."""
import contextlib
import csv
import io
import json
import os
import stat
import unittest

import support
from support import CELL, Base, good_draft, mc
from qgenlib import pipeline, review, validator
from qgenlib.store import Cell, QgenError, find_validator


def second_batch():
    return [mc("Which is the largest number?", ["4 527", "4 725", "4 275", "4 572"], 1, explanation="Compare hundreds."),
            mc("Which is the smallest number?", ["3 105", "3 015", "3 150", "3 510"], 1, explanation="Compare tens.")]


class Flow(Base):
    def rows(self):
        with open(self.ctx.work_dir / CELL / "review.csv", encoding="utf-8-sig", newline="") as f:
            return list(csv.DictReader(f))

    def write_rows(self, rows, name="reviewed.csv", encoding="utf-8-sig"):
        path = self.tmp / name
        with open(path, "w", encoding=encoding, newline="") as f:
            w = csv.DictWriter(f, fieldnames=review.COLUMNS)
            w.writeheader()
            w.writerows(rows)
        return path

    def batch(self, entries, decisions=None, force=False, name="reviewed.csv", edits=None):
        """ingest, validate, blind, export, then import with the given approve values (default all y)."""
        self.need_validator()
        rc, out = self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries), force=force)
        self.assertEqual(rc, 0, out)
        rc, out = self.run_cmd(pipeline.cmd_validate, CELL)
        self.assertEqual(rc, 0, out)
        self.run_cmd(pipeline.cmd_blind, CELL)
        self.run_cmd(pipeline.cmd_export, CELL)
        rows = self.rows()
        for r in rows:
            r["approve"] = (decisions or {}).get(r["id"][-4:], "y")
            r.update((edits or {}).get(r["id"][-4:], {}))
        path = self.write_rows(rows, name)
        return path, self.run_cmd(pipeline.cmd_import, CELL, path)

    def target(self):
        cell = Cell(self.ctx, self.spec, CELL)
        return {q["id"]: q for q in json.loads(cell.target_path.read_text(encoding="utf-8"))["questions"]}


class SecondBatch(Flow):
    def test_ids_continue_and_first_batch_is_untouched(self):
        first_csv, (rc, out) = self.batch(good_draft(), name="batch1.csv")
        self.assertEqual(rc, 0, out)
        before = self.target()
        self.assertEqual(sorted(before), ["m3-num-000%d" % n for n in range(1, 6)])
        _, (rc, out) = self.batch(second_batch(), {"0007": "n"}, force=True, name="batch2.csv")
        self.assertEqual(rc, 0, out)
        self.assertEqual([q["id"] for q in self.pack()["questions"]], ["m3-num-0006", "m3-num-0007"])
        after = self.target()
        self.assertEqual(sorted(after), ["m3-num-000%d" % n for n in range(1, 7)])  # 0007 was rejected: not added
        for qid, q in before.items():
            self.assertEqual(after[qid], q)

    def test_a_removed_id_is_never_reused(self):
        self.batch(good_draft(), {"0005": "n"})
        self.assertNotIn("m3-num-0005", self.target())
        self.batch(second_batch(), force=True)
        self.assertEqual([q["id"] for q in self.pack()["questions"]], ["m3-num-0006", "m3-num-0007"])

    def test_ids_are_reused_only_when_a_batch_never_left_the_work_dir(self):
        self.need_validator()
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(good_draft()))
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(good_draft()))  # a redraft before export
        self.assertEqual(self.pack()["questions"][0]["id"], "m3-num-0001")

    def test_an_exact_repeat_of_an_imported_question_is_flagged(self):
        self.batch(good_draft())
        entries = second_batch() + [good_draft()[0]]
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries), force=True)
        self.run_cmd(pipeline.cmd_validate, CELL)
        report = json.loads((self.ctx.work_dir / CELL / "validation.json").read_text(encoding="utf-8"))
        flags = report["questions"]["m3-num-0008"]["flags"]
        self.assertTrue(any("near_duplicate of m3-num-0001 (maths-y3-number.json)" in f for f in flags), flags)

    def test_an_older_batchs_csv_still_imports_and_can_reject_its_own_rows(self):
        first_csv, _ = self.batch(good_draft(), name="batch1.csv")
        self.batch(second_batch(), force=True, name="batch2.csv")
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, first_csv)
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(self.target()), 7)
        with open(first_csv, encoding="utf-8-sig", newline="") as f:
            rows = list(csv.DictReader(f))
        for r in rows:
            r["approve"] = "n" if r["id"].endswith("0002") else "y"
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, self.write_rows(rows, "batch1b.csv"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(sorted(self.target()), ["m3-num-0001", "m3-num-0003", "m3-num-0004", "m3-num-0005",
                                                "m3-num-0006", "m3-num-0007"])


class MangledEdits(Flow):
    def test_a_spreadsheet_date_in_accept_is_refused(self):
        entries = good_draft()
        entries[2]["accept"] = ["1/4"]
        path, (rc, out) = self.batch(entries, {"0001": "y", "0002": "y", "0003": "edit", "0004": "y", "0005": "y"},
                                     edits={"0003": {"answer": "1/04/2026", "accept": "1/04/2026",
                                                     "explanation": "One of four equal slices."}})
        self.assertEqual(rc, 1)
        self.assertIn("m3-num-0003: edit not accepted: answer '1/04/2026' looks like a spreadsheet auto-format", out)
        self.assertNotIn("m3-num-0003", self.target())

    def test_date_like_and_scientific_forms(self):
        base = {"type": "short_text", "prompt": "x", "accept": ["1/4"], "difficulty": 1}
        for bad in ("4-Jan", "1-Apr", "1/04/2026", "2026-04-01", "1E+05"):
            self.assertTrue(review.spreadsheet_mangled(base, {"answer": bad, "accept": bad}), bad)
        for fine in ("1/4", "three quarters", "1788", "12 500"):
            self.assertEqual(review.spreadsheet_mangled(base, {"answer": fine, "accept": fine}), [], fine)

    def test_a_date_that_was_already_in_the_export_is_not_a_mangling(self):
        entries = good_draft()
        entries[2]["accept"] = ["1/04/2026"]
        base = self.batch(entries)[1]
        self.assertEqual(base[0], 0, base[1])

    def test_the_change_list_names_each_changed_field(self):
        edits = {"0002": {"answer": "624", "explanation": "348 + 276 = 624."}}
        _, (rc, out) = self.batch(good_draft(), {"0002": "edit"}, edits=edits)
        self.assertIn("edit m3-num-0002: answer: 623 -> 624", out)
        self.assertIn("edit m3-num-0002: explanation: 'SENTINEL-EXPLANATION-TWO' -> '348 + 276 = 624.'", out)
        self.assertNotIn("edit m3-num-0002: prompt", out)

    def test_true_false_case_from_a_spreadsheet_is_accepted(self):
        _, (rc, out) = self.batch(good_draft(), edits={"0005": {"answer": "TRUE"}})
        self.assertEqual(rc, 0, out)
        self.assertIn("m3-num-0005", self.target())


class Encodings(Flow):
    def test_cp1252_csv_is_read_and_reported(self):
        entries = good_draft()
        entries[0]["explanation"] = "Add 1000: caf\u00e9"
        self.need_validator()
        path, _ = self.batch(entries, {"0001": "n"})
        rows = self.rows()
        for r in rows:
            r["approve"] = "y"
        path = self.write_rows(rows, "excel.csv", encoding="cp1252")
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 0, out)
        self.assertIn("as Windows-1252", out)
        self.assertEqual(self.target()["m3-num-0001"]["explanation"], "Add 1000: caf\u00e9")

    def test_utf8_is_reported(self):
        _, (rc, out) = self.batch(good_draft())
        self.assertIn("as UTF-8", out)

    def test_undecodable_csv_is_a_clean_error(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        bad = self.tmp / "bad.csv"
        bad.write_bytes(b"id,approve\n\x81\x8d\x8f\x90\n")  # undefined in Windows-1252
        with self.assertRaisesRegex(QgenError, "neither UTF-8 nor Windows-1252"):
            pipeline.cmd_import(self.ctx, CELL, bad)


class FakeValidators(Flow):
    def script(self, name, body):
        real = find_validator(self.ctx)
        path = self.tmp / name
        path.write_text("#!/bin/sh\nREAL=%s\n%s\n" % (real, body), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return str(path)

    def test_a_crashing_validator_leaves_no_pack_behind(self):
        self.need_validator()
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        self.run_cmd(pipeline.cmd_export, CELL)
        rows = self.rows()
        for r in rows:
            r["approve"] = "y"
        path = self.write_rows(rows)
        self.ctx.validator = self.script("crash.sh", '[ "$1" = "--sample" ] && exec "$REAL" "$@"\nkill -SEGV $$')
        with self.assertRaises(QgenError), contextlib.redirect_stdout(io.StringIO()):
            pipeline.cmd_import(self.ctx, CELL, path)
        self.assertFalse(Cell(self.ctx, self.spec, CELL).target_path.exists())

    def test_a_crash_restores_the_previous_pack(self):
        self.batch(good_draft(), {"0001": "y", "0002": "y", "0003": "", "0004": "", "0005": ""})
        cell = Cell(self.ctx, self.spec, CELL)
        good = cell.target_path.read_bytes()
        rows = self.rows()
        for r in rows:
            r["approve"] = "y"
        path = self.write_rows(rows)
        self.ctx.validator = self.script("crash.sh", '[ "$1" = "--sample" ] && exec "$REAL" "$@"\nkill -SEGV $$')
        with self.assertRaises(QgenError), contextlib.redirect_stdout(io.StringIO()):
            pipeline.cmd_import(self.ctx, CELL, path)
        self.assertEqual(cell.target_path.read_bytes(), good)

    def test_a_missing_validator_fails_before_anything_is_written(self):
        self.need_validator()
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        self.run_cmd(pipeline.cmd_export, CELL)
        path = self.write_rows([dict(r, approve="y") for r in self.rows()])
        self.ctx.validator = str(self.tmp / "nope")
        with self.assertRaisesRegex(QgenError, "not an executable"):
            pipeline.cmd_import(self.ctx, CELL, path)
        self.assertFalse(Cell(self.ctx, self.spec, CELL).target_path.exists())

    def test_a_validator_without_sample_is_named_not_blamed_on_the_writer(self):
        self.need_validator()
        old = self.script("old.sh", '[ "$1" = "--sample" ] && { echo "error: --sample: no such file or directory" >&2; exit 1; }\nexec "$REAL" "$@"')
        self.ctx.validator = old
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(good_draft()))
        with self.assertRaisesRegex(QgenError, "does not support --sample.*rebuild"):
            pipeline.cmd_validate(self.ctx, CELL)


class StaleFiles(Flow):
    def test_ingest_clears_the_writer_folder(self):
        self.need_validator()
        draft = self.tmp / "work" / CELL / "writer" / "draft.json"
        draft.parent.mkdir(parents=True)
        draft.write_text(json.dumps({"questions": good_draft()}), encoding="utf-8")
        self.run_cmd(pipeline.cmd_ingest, CELL, draft)
        self.assertFalse(draft.exists())
        with self.assertRaisesRegex(QgenError, "cannot read draft"):
            pipeline.cmd_ingest(self.ctx, CELL, draft)  # a dead retry writer cannot resurrect it

    def test_reingest_clears_the_checker_folder_and_old_checker_files_are_refused(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        old = self.write_checker([{"id": "m3-num-0001", "answer": 0}], "old.json")
        checker_file = self.ctx.work_dir / CELL / "checker" / "checker.json"
        checker_file.parent.mkdir(parents=True)
        checker_file.write_text(old.read_text(encoding="utf-8"), encoding="utf-8")
        entries = good_draft()
        entries[0]["prompt"] = "Which number is 1000 more than 6 251?"
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries))
        self.assertFalse(checker_file.exists())
        self.run_cmd(pipeline.cmd_validate, CELL)
        self.run_cmd(pipeline.cmd_blind, CELL)
        with self.assertRaisesRegex(QgenError, "missing or wrong token"):
            pipeline.cmd_compare(self.ctx, CELL, old)

    def test_missing_token_is_refused(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        path = self.tmp / "c.json"
        path.write_text(json.dumps({"answers": []}), encoding="utf-8")
        with self.assertRaisesRegex(QgenError, "missing or wrong token"):
            pipeline.cmd_compare(self.ctx, CELL, path)

    def test_the_token_changes_when_the_questions_change(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        first = json.loads((self.ctx.work_dir / CELL / "blind" / "questions.json").read_text(encoding="utf-8"))["token"]
        entries = good_draft()
        entries[1]["prompt"] = "What is 348 + 276?"
        self.to_validated(entries)
        self.run_cmd(pipeline.cmd_blind, CELL)
        second = json.loads((self.ctx.work_dir / CELL / "blind" / "questions.json").read_text(encoding="utf-8"))["token"]
        self.assertNotEqual(first, second)

    def test_a_blind_file_edited_by_hand_is_refused(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        path = self.ctx.work_dir / CELL / "blind" / "questions.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["questions"][0]["prompt"] += "!"
        path.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(QgenError, "altered"):
            pipeline.cmd_compare(self.ctx, CELL, self.write_checker([]))


class CheckerShapes(Flow):
    def setUp(self):
        super().setUp()
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)

    def answers(self, first_flags="OMIT", **first):
        a = [{"id": "m3-num-0001", "answer": 0}, {"id": "m3-num-0002", "answer": 623},
             {"id": "m3-num-0003", "answer": "1/4"}, {"id": "m3-num-0005", "answer": True}]
        if first_flags != "OMIT":
            a[0]["flags"] = first_flags
        a[0].update(first)
        return a

    def compare(self, answers):
        return self.run_cmd(pipeline.cmd_compare, CELL, self.write_checker(answers))

    def test_canonical_and_lenient_shapes(self):
        rc, out = self.compare(self.answers([{"flag": "ambiguous", "note": "a"}, "factual", "reading_level: b"]))
        self.assertEqual(rc, 0)
        report = json.loads((self.ctx.work_dir / CELL / "compare.json").read_text(encoding="utf-8"))
        r = report["results"]["m3-num-0001"]
        self.assertEqual((r["flags"], r["notes"]), (["ambiguous", "factual", "reading_level"], "ambiguous: a reading_level: b"))
        self.assertEqual(report["summary"]["flagged"], 1)

    def test_bad_shapes_refuse_the_whole_file(self):
        for flags in ("factual", {"flag": "factual"}, [{"name": "factual"}], [{"flag": "factual", "why": "x"}], [3],
                      ["made_up"], "not a flag: note"):
            with self.subTest(flags=flags):
                with self.assertRaisesRegex(QgenError, "re-run the checker"):
                    pipeline.cmd_compare(self.ctx, CELL, self.write_checker(self.answers(flags)))
        self.assertFalse((self.ctx.work_dir / CELL / "compare.json").exists())

    def test_unusable_answers_entries_refuse_the_file(self):
        for answers in ([1, 2], [{"answer": 1}], self.answers() + [{"id": "m3-num-0001", "answer": 1}]):
            with self.subTest(answers=answers), self.assertRaisesRegex(QgenError, "re-run the checker"):
                pipeline.cmd_compare(self.ctx, CELL, self.write_checker(answers))

    def test_a_missing_answer_counts_as_flagged(self):
        rc, out = self.compare(self.answers()[:2])
        report = json.loads((self.ctx.work_dir / CELL / "compare.json").read_text(encoding="utf-8"))
        self.assertEqual((report["summary"]["missing"], report["summary"]["flagged"]), (2, 2))

    def test_nan_and_infinity_answers_disagree_instead_of_crashing(self):
        blind = self.ctx.work_dir / CELL / "blind" / "questions.json"
        token = json.loads(blind.read_text(encoding="utf-8"))["token"]
        path = self.tmp / "nan.json"
        path.write_text('{"token": "%s", "answers": [{"id": "m3-num-0001", "answer": NaN}, '
                        '{"id": "m3-num-0002", "answer": Infinity}, {"id": "m3-num-0003", "answer": -Infinity}, '
                        '{"id": "m3-num-0005", "answer": NaN}]}' % token, encoding="utf-8")
        rc, out = self.run_cmd(pipeline.cmd_compare, CELL, path)
        self.assertEqual(rc, 0, out)
        self.assertIn("4 disagree", out)


class ValidatorProbe(Base):
    def test_probe_accepts_the_real_validator(self):
        self.need_validator()
        validator._check_sample_support(self.ctx, find_validator(self.ctx))


if __name__ == "__main__":
    unittest.main()
