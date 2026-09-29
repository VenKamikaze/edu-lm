import contextlib
import csv
import hashlib
import io
import json
import unittest

import support
from support import CELL, Base, good_draft, mc
from qgenlib import pipeline, review
from qgenlib.store import Cell, QgenError

BLIND_KEYS = {"id", "type", "prompt", "choices"}


def draft_with_leaks():
    entries = good_draft()
    entries[1]["tolerance"] = 0.375
    return entries


class Blind(Base):
    def blind(self, entries=None):
        self.to_validated(entries or draft_with_leaks())
        rc, out = self.run_cmd(pipeline.cmd_blind, CELL)
        self.assertEqual(rc, 0, out)
        path = self.ctx.work_dir / CELL / "blind" / "questions.json"
        return path.read_text(encoding="utf-8"), json.loads(path.read_text(encoding="utf-8"))

    def test_no_answer_key_value_appears_in_the_blind_file(self):
        text, _ = self.blind()
        for secret in ("SENTINEL-EXPLANATION-ONE", "SENTINEL-EXPLANATION-TWO", "SENTINEL-EXPLANATION-THREE",
                       "SENTINEL-EXPLANATION-FOUR", "zqxaccept", "0.375", "623"):
            self.assertNotIn(secret, text)
        for key in ("answer", "accept", "tolerance", "explanation", "curriculum", "difficulty", "generator", "params"):
            self.assertNotIn('"%s"' % key, text)

    def test_only_id_type_prompt_and_choices_are_written(self):
        _, data = self.blind()
        for q in data["questions"]:
            self.assertLessEqual(set(q), BLIND_KEYS)
        mcq = data["questions"][0]
        self.assertEqual(mcq["choices"], ["7 250", "6 350", "16 250", "6 260"])
        self.assertNotIn("choices", data["questions"][1])

    def test_generators_are_left_out(self):
        _, data = self.blind()
        self.assertEqual([q["id"] for q in data["questions"]],
                         ["m3-num-0001", "m3-num-0002", "m3-num-0003", "m3-num-0005"])

    def test_blind_dir_holds_only_the_questions_file(self):
        self.blind()
        self.assertEqual([p.name for p in (self.ctx.work_dir / CELL / "blind").iterdir()], ["questions.json"])

    def test_refused_until_validate_has_passed(self):
        self.need_validator()
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(good_draft()))
        with self.assertRaisesRegex(QgenError, "not been validated"):
            pipeline.cmd_blind(self.ctx, CELL)

    def test_refused_when_the_pack_changed_after_validate(self):
        self.to_validated()
        cell = Cell(self.ctx, self.spec, CELL)
        pack = cell.load_pack()
        pack["questions"][0]["prompt"] += " Changed."
        cell.pack_path.write_text(json.dumps(pack), encoding="utf-8")
        with self.assertRaisesRegex(QgenError, "validate again"):
            pipeline.cmd_blind(self.ctx, CELL)


class Compare(Base):
    def compare(self, answers, entries=None):
        self.to_validated(entries or draft_with_leaks())
        self.run_cmd(pipeline.cmd_blind, CELL)
        path = self.write_checker(answers)
        rc, out = self.run_cmd(pipeline.cmd_compare, CELL, path)
        self.assertEqual(rc, 0, out)
        report = json.loads((self.ctx.work_dir / CELL / "compare.json").read_text(encoding="utf-8"))
        return report["results"], out

    def a(self, n, answer, flags=(), notes=""):
        return {"id": "m3-num-000%d" % n, "answer": answer, "flags": list(flags), "notes": notes}

    def agrees(self, results, n):
        return results["m3-num-000%d" % n]["agree"]

    def all_right(self):
        return [self.a(1, 0), self.a(2, 623), self.a(3, "1/4"), self.a(5, True)]

    def test_all_correct_agree(self):
        results, _ = self.compare(self.all_right())
        self.assertEqual([self.agrees(results, n) for n in (1, 2, 3, 5)], [True] * 4)

    def test_multiple_choice_compares_the_index(self):
        answers = self.all_right()
        answers[0] = self.a(1, 1)
        results, _ = self.compare(answers)
        self.assertIs(self.agrees(results, 1), False)
        self.assertEqual(results["m3-num-0001"]["checker_answer"], "6 350")

    def test_multiple_choice_index_as_text_and_float(self):
        for given in ("0", " 0 ", 0.0):
            answers = self.all_right()
            answers[0] = self.a(1, given)
            with self.subTest(given=given):
                results, _ = self.compare(answers)
                self.assertIs(self.agrees(results, 1), True)

    def test_multiple_choice_out_of_range_or_text_disagrees(self):
        for given in (7, -1, "7 250"):
            answers = self.all_right()
            answers[0] = self.a(1, given)
            with self.subTest(given=given):
                results, _ = self.compare(answers)
                self.assertIs(self.agrees(results, 1), False)

    def test_numeric_tolerance_edges(self):
        # the numeric question has tolerance 0.375 around 623
        for given, expected in [(623.375, True), (622.625, True), (623.4, False), (622.6, False),
                                ("623.375", True), ("623.4", False), ("623", True), ("6 23", False)]:
            answers = self.all_right()
            answers[1] = self.a(2, given)
            with self.subTest(given=given):
                results, _ = self.compare(answers)
                self.assertIs(self.agrees(results, 2), expected)

    def test_numeric_uses_the_daemons_number_formats(self):
        entries = good_draft()
        entries[1]["answer"] = 12345
        entries[1]["prompt"] = "What is 12 000 + 345?"
        for given, expected in [("12 345", True), ("12 345", True), ("12,345", True), ("12345", True),
                                ("12 34", False), ("1e4", False)]:
            answers = [self.a(1, 0), self.a(2, given), self.a(3, "1/4"), self.a(5, True)]
            with self.subTest(given=given):
                results, _ = self.compare(answers, entries)
                self.assertIs(self.agrees(results, 2), expected)

    def test_short_text_uses_the_daemons_normalisation(self):
        for given, expected in [("1/4", True), (" 1/4. ", True), ("ZQXACCEPT", True), ("!zqxaccept!", True),
                                ("zqx accept", False), ("", False), ("2/4", False)]:
            answers = self.all_right()
            answers[2] = self.a(3, given)
            with self.subTest(given=given):
                results, _ = self.compare(answers)
                self.assertIs(self.agrees(results, 3), expected)

    def test_true_false(self):
        for given, expected in [(True, True), (False, False), ("true", True), ("False", False), ("yes", False)]:
            answers = self.all_right()
            answers[3] = self.a(5, given)
            with self.subTest(given=given):
                results, _ = self.compare(answers)
                self.assertIs(self.agrees(results, 5), expected)

    def test_a_missing_answer_goes_to_review(self):
        results, out = self.compare(self.all_right()[:2])
        self.assertIsNone(self.agrees(results, 3))
        self.assertIn("gave no answer", results["m3-num-0003"]["problem"])
        self.assertIn("review: m3-num-0003", out)

    def test_flags_are_kept_and_reported_even_when_the_answer_agrees(self):
        answers = self.all_right()
        answers[0] = self.a(1, 0, [{"flag": "ambiguous", "note": "Two right."}, "factual", "reading_level: too hard"],
                            "Overall.")
        results, out = self.compare(answers)
        r = results["m3-num-0001"]
        self.assertTrue(r["agree"])
        self.assertEqual(r["flags"], ["ambiguous", "factual", "reading_level"])
        self.assertEqual(r["notes"], "Overall. ambiguous: Two right. reading_level: too hard")
        self.assertIn("review: m3-num-0001", out)

    def test_disagreement_deletes_nothing(self):
        self.to_validated(draft_with_leaks())
        self.run_cmd(pipeline.cmd_blind, CELL)
        cell = Cell(self.ctx, self.spec, CELL)
        before = cell.pack_path.read_bytes()
        path = self.write_checker([self.a(1, 3, ["factual"]), self.a(2, 1), self.a(3, "no"), self.a(5, False)])
        self.run_cmd(pipeline.cmd_compare, CELL, path)
        self.assertEqual(cell.pack_path.read_bytes(), before)
        self.assertEqual(len(self.pack()["questions"]), 5)
        report = json.loads(cell.compare_path.read_text(encoding="utf-8"))
        self.assertEqual(report["summary"]["disagree"], 4)

    def test_malformed_checker_file_is_refused(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        bad = self.tmp / "bad.json"
        bad.write_text("[1, 2]", encoding="utf-8")
        with self.assertRaisesRegex(QgenError, "'answers' list"):
            pipeline.cmd_compare(self.ctx, CELL, bad)


class Review(Base):
    def exported(self, checker=True):
        self.to_validated(draft_with_leaks())
        self.run_cmd(pipeline.cmd_blind, CELL)
        if checker:
            path = self.write_checker([
                {"id": "m3-num-0001", "answer": 0, "flags": [{"flag": "reading_level", "note": "Spaced numbers."}], "notes": ""},
                {"id": "m3-num-0002", "answer": 620, "flags": [], "notes": ""}])
            self.run_cmd(pipeline.cmd_compare, CELL, path)
        rc, out = self.run_cmd(pipeline.cmd_export, CELL)
        self.assertEqual(rc, 0, out)
        return Cell(self.ctx, self.spec, CELL)

    def rows(self, cell):
        with open(cell.csv_path, encoding="utf-8-sig", newline="") as f:
            return list(csv.DictReader(f))

    def write_rows(self, rows, name="reviewed.csv"):
        path = self.tmp / name
        with open(path, "w", encoding="utf-8-sig", newline="") as f:
            w = csv.DictWriter(f, fieldnames=review.COLUMNS)
            w.writeheader()
            w.writerows(rows)
        return path

    def approve(self, cell, decisions, edits=None):
        rows = self.rows(cell)
        for r in rows:
            r["approve"] = decisions.get(r["id"][-4:], "")
            for column, value in (edits or {}).get(r["id"][-4:], {}).items():
                r[column] = value
        return self.write_rows(rows)

    def target(self, cell):
        return json.loads(cell.target_path.read_text(encoding="utf-8"))

    def test_csv_has_a_bom_and_one_row_per_question(self):
        cell = self.exported()
        self.assertEqual(cell.csv_path.read_bytes()[:3], b"\xef\xbb\xbf")
        self.assertEqual([r["id"] for r in self.rows(cell)], ["m3-num-000%d" % n for n in range(1, 6)])

    def test_csv_columns_and_values(self):
        cell = self.exported()
        rows = {r["id"][-4:]: r for r in self.rows(cell)}
        self.assertEqual(list(rows["0001"]), review.COLUMNS)
        r = rows["0001"]
        self.assertEqual((r["answer"], r["agree"], r["checker_flags"], r["checker_notes"]),
                         ("7 250", "yes", "reading_level", "reading_level: Spaced numbers."))
        self.assertEqual(r["choices"], "7 250\n6 350\n16 250\n6 260")
        self.assertEqual((rows["0002"]["agree"], rows["0002"]["checker_answer"], rows["0002"]["tolerance"]),
                         ("no", "620", "0.375"))
        self.assertEqual(rows["0003"]["accept"], "1/4\nzqxaccept")
        self.assertEqual(rows["0003"]["agree"], "")  # the checker never answered it
        self.assertEqual(rows["0005"]["answer"], "True")
        gen = rows["0004"]
        self.assertEqual(gen["type"], "generator")
        self.assertEqual(len(gen["generator_samples"].splitlines()), pipeline.SAMPLES_PER_GENERATOR)
        self.assertRegex(gen["generator_samples"].splitlines()[0], r"^What is \d+ \+ \d+\? = \d+$")
        self.assertTrue(all(r["approve"] == "" and r["notes"] == "" for r in rows.values()))

    def test_export_without_a_checker_still_works(self):
        cell = self.exported(checker=False)
        self.assertTrue(all(r["agree"] == "" for r in self.rows(cell)))

    def test_round_trip_export_approve_import(self):
        cell = self.exported()
        path = self.approve(cell, {"0001": "y", "0002": "y", "0003": "y", "0004": "y", "0005": "n"})
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 0, out)
        pack = self.target(cell)
        self.assertEqual([q["id"] for q in pack["questions"]], ["m3-num-000%d" % n for n in range(1, 5)])
        source = {q["id"]: q for q in self.pack()["questions"]}
        for q in pack["questions"]:
            self.assertEqual(q, source[q["id"]])
        self.assertEqual(pack["pack"], "maths-y3-number")
        self.assertEqual(cell.stage, "imported")

    def test_only_approved_rows_are_imported(self):
        cell = self.exported()
        path = self.approve(cell, {"0001": "y", "0002": "n", "0003": "", "0004": "maybe"})
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 1)  # the unknown approve value is reported
        self.assertIn("approve must be y, n or edit", out)
        self.assertEqual([q["id"] for q in self.target(cell)["questions"]], ["m3-num-0001"])

    def test_nothing_approved_writes_nothing(self):
        cell = self.exported()
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {}))
        self.assertEqual(rc, 0)
        self.assertIn("nothing written", out)
        self.assertFalse(cell.target_path.exists())

    def test_reimport_is_idempotent(self):
        cell = self.exported()
        path = self.approve(cell, {"0001": "y", "0002": "y", "0004": "y"})
        self.run_cmd(pipeline.cmd_import, CELL, path)
        first = cell.target_path.read_bytes()
        self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(cell.target_path.read_bytes(), first)
        ids = [q["id"] for q in self.target(cell)["questions"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(ids), 3)

    def test_reimport_of_more_rows_adds_and_keeps_undecided_ones(self):
        cell = self.exported()
        self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {"0001": "y", "0002": "y"}))
        self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {"0001": "y", "0003": "y"}))
        self.assertEqual([q["id"] for q in self.target(cell)["questions"]], ["m3-num-0001", "m3-num-0002", "m3-num-0003"])
        self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {"0002": "n"}))
        self.assertEqual([q["id"] for q in self.target(cell)["questions"]], ["m3-num-0001", "m3-num-0003"])

    def test_edit_rows_apply_valid_changes(self):
        cell = self.exported()
        edits = {"0001": {"prompt": "Which number is 1000 more than 6 250? Choose one.",
                          "choices": "7 250\n6 350\n16 250\n6 260\n6 251", "answer": "7 250"},
                 "0002": {"answer": "624", "tolerance": ""},
                 "0003": {"accept": "1/4\none quarter\nquarter"}}
        path = self.approve(cell, {"0001": "edit", "0002": "edit", "0003": "edit"}, edits)
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 0, out)
        q = {q["id"][-4:]: q for q in self.target(cell)["questions"]}
        self.assertEqual(q["0001"]["choices"][-1], "6 251")
        self.assertEqual(q["0001"]["answer"], 0)
        self.assertEqual((q["0002"]["answer"], "tolerance" in q["0002"]), (624, False))
        self.assertEqual(q["0003"]["accept"], ["1/4", "one quarter", "quarter"])

    def test_edit_rows_that_do_not_validate_are_skipped(self):
        cell = self.exported()
        edits = {"0001": {"answer": "not one of the choices"},
                 "0002": {"explanation": ""},
                 "0003": {"answer": "3/4"},
                 "0005": {"difficulty": "9"}}
        path = self.approve(cell, {"0001": "edit", "0002": "edit", "0003": "edit", "0005": "edit", "0004": "y"}, edits)
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 1)
        self.assertIn("m3-num-0001: edit not accepted: answer must match exactly one of the choices", out)
        self.assertIn("m3-num-0002: edit not accepted: written question has no explanation", out)
        self.assertIn("m3-num-0003: edit not accepted: answer changed but accept did not", out)
        self.assertIn("m3-num-0005: edit not accepted: difficulty", out)
        self.assertEqual([q["id"] for q in self.target(cell)["questions"]], ["m3-num-0004"])

    def test_edit_rows_go_through_the_cpp_validator_too(self):
        cell = self.exported()
        edits = {"0001": {"choices": "7 250\n6 350\n6 350\n6 260"}}  # a duplicate choice
        path = self.approve(cell, {"0001": "edit", "0004": "y"}, edits)
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 1)
        self.assertIn("m3-num-0001: edit not accepted: duplicate choice", out)

    def test_approving_a_row_whose_content_changed_is_refused(self):
        cell = self.exported()
        path = self.approve(cell, {"0001": "y", "0002": "y"}, {"0001": {"prompt": "Sneaky change?"}})
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 1)
        self.assertIn("m3-num-0001: approve is y but prompt changed", out)
        self.assertEqual([q["id"] for q in self.target(cell)["questions"]], ["m3-num-0002"])

    def test_spreadsheet_reformatting_of_numbers_and_spacing_is_tolerated(self):
        cell = self.exported()
        edits = {"0002": {"answer": "623.0", "tolerance": "0.3750", "difficulty": "2.0"},
                 "0001": {"prompt": "Which number is 1000 more than 6 250?  \r\n", "choices": "7 250\r\n6 350\r\n16 250\r\n6 260"}}
        path = self.approve(cell, {"0001": "y", "0002": "y"}, edits)
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 0, out)

    def test_generator_rows_can_be_approved_but_not_edited(self):
        cell = self.exported()
        path = self.approve(cell, {"0004": "y", "0001": "y"})
        self.run_cmd(pipeline.cmd_import, CELL, path)
        gen = [q for q in self.target(cell)["questions"] if q["type"] == "generator"][0]
        self.assertEqual(gen["params"], {"a": [10, 99], "b": [10, 99]})
        path = self.approve(cell, {"0004": "edit"}, {"0004": {"prompt": "[generator add: a 1-9]"}})
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, path)
        self.assertEqual(rc, 1)
        self.assertIn("generator rows cannot be edited", out)

    def test_import_is_rolled_back_when_the_whole_directory_fails(self):
        cell = self.exported()
        other = {"schema": 1, "pack": "zzz", "subject": "maths", "questions": [
            {"id": "m3-num-0001", "years": [3], "type": "numeric", "prompt": "Unrelated?", "answer": 1}]}
        (self.data / "zzz-collides.json").write_text(json.dumps(other), encoding="utf-8")
        path = self.approve(cell, {"0001": "y"})
        with self.assertRaisesRegex(QgenError, "put back"), contextlib.redirect_stdout(io.StringIO()):
            pipeline.cmd_import(self.ctx, CELL, path)
        self.assertFalse(cell.target_path.exists())
        # and an earlier good import is restored byte for byte
        (self.data / "zzz-collides.json").unlink()
        self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {"0002": "y"}))
        good = cell.target_path.read_bytes()
        (self.data / "zzz-collides.json").write_text(json.dumps(other), encoding="utf-8")
        with self.assertRaises(QgenError), contextlib.redirect_stdout(io.StringIO()):
            pipeline.cmd_import(self.ctx, CELL, self.approve(cell, {"0001": "y"}))
        self.assertEqual(cell.target_path.read_bytes(), good)

    def test_import_touches_nothing_but_the_cells_pack(self):
        cell = self.exported()
        digest = lambda: {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.data.iterdir()}
        before = digest()
        self.run_cmd(pipeline.cmd_import, CELL, self.approve(cell, {"0001": "y"}))
        after = digest()
        self.assertEqual(set(after) - set(before), {"maths-y3-number.json"})
        self.assertEqual({k: v for k, v in after.items() if k in before}, before)

    def test_csv_missing_columns_is_refused(self):
        cell = self.exported()
        bad = self.tmp / "bad.csv"
        bad.write_text("id,prompt\nm3-num-0001,hi\n", encoding="utf-8")
        with self.assertRaisesRegex(QgenError, "missing columns"):
            pipeline.cmd_import(self.ctx, CELL, bad)

    def test_rows_for_other_cells_are_refused(self):
        cell = self.exported()
        rows = self.rows(cell)
        rows[0]["id"] = "m4-num-0001"
        rows[0]["approve"] = "y"
        rc, out = self.run_cmd(pipeline.cmd_import, CELL, self.write_rows(rows))
        self.assertIn("m4-num-0001: not a question of this cell", out)

    def test_cells_lists_the_stage(self):
        self.exported()
        rc, out = self.run_cmd(pipeline.cmd_cells, subject="maths", year=3)
        line = [l for l in out.splitlines() if l.startswith(CELL)][0]
        self.assertIn("exported", line)
        self.assertIn("new", [l for l in out.splitlines() if l.startswith("y3-maths-algebra")][0])


if __name__ == "__main__":
    unittest.main()
