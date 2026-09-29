import copy
import json
import unittest

import support
from support import CELL, Base, good_draft, mc
from qgenlib import pipeline
from qgenlib.store import QgenError
from qgenlib.validator import run_validator


class Ingest(Base):
    def ingest(self, entries=None):
        return self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries or good_draft()))

    def test_ids_are_sequential_with_the_cell_prefix(self):
        self.ingest()
        ids = [q["id"] for q in self.pack()["questions"]]
        self.assertEqual(ids, ["m3-num-000%d" % n for n in range(1, 6)])

    def test_ids_are_stable_for_the_same_draft(self):
        self.ingest()
        first = self.pack()
        self.ingest()
        self.assertEqual(first, self.pack())

    def test_writer_ids_are_replaced(self):
        entries = good_draft()
        entries[0]["id"] = "sm-m-001"
        self.ingest(entries)
        self.assertEqual(self.pack()["questions"][0]["id"], "m3-num-0001")

    def test_cell_stamps_years_and_subject(self):
        entries = good_draft()
        entries[0].update(years=[9], subject="science")
        self.ingest(entries)
        for q in self.pack()["questions"]:
            self.assertEqual(q["years"], [3])
            self.assertEqual(q["subject"], "maths")
        self.assertEqual(self.pack()["pack"], "maths-y3-number")

    def test_single_code_cell_stamps_curriculum(self):
        spec = json.loads(self.ctx.spec_path.read_text(encoding="utf-8"))
        for c in spec["cells"]:
            if c["id"] == CELL:
                c["descriptors"] = c["descriptors"][:1]
        path = self.tmp / "spec.json"
        path.write_text(json.dumps(spec), encoding="utf-8")
        self.ctx.spec_path = path
        entries = good_draft()
        del entries[0]["curriculum"]
        self.ingest(entries)
        self.assertEqual(self.pack()["questions"][0]["curriculum"], "AC9M3N01")

    def test_ids_do_not_collide_with_the_sample_packs(self):
        self.need_validator()
        self.ingest()
        res = run_validator(self.ctx, [self.data, self.ctx.work_dir / CELL / "pack.json"])
        self.assertEqual(res["errors"], [])
        self.assertEqual(res["loaded"], 39 + 5)

    def test_prefixes_are_unique_across_the_spec(self):
        prefixes = [c["id_prefix"] for c in self.spec.data["cells"]]
        self.assertEqual(len(prefixes), len(set(prefixes)))
        packs = [c["pack"] for c in self.spec.data["cells"]]
        self.assertEqual(len(packs), len(set(packs)))

    def test_not_json_is_rejected_with_a_clear_message(self):
        bad = self.tmp / "bad.json"
        bad.write_text('{"questions": [ {"type": ', encoding="utf-8")
        with self.assertRaisesRegex(QgenError, r"not valid JSON.*line 1"):
            pipeline.cmd_ingest(self.ctx, CELL, bad)
        self.assertFalse((self.ctx.work_dir / CELL / "pack.json").exists())

    def test_wrong_shapes_are_rejected(self):
        for text, pattern in [('"hi"', "expected a JSON list"), ('{"questions": {}}', "expected a JSON list"),
                              ('{"questions": []}', "no questions"), ('[1]', "entry 1 is not a JSON object"),
                              ("", "not valid JSON")]:
            path = self.tmp / "d.json"
            path.write_text(text, encoding="utf-8")
            with self.subTest(text=text), self.assertRaisesRegex(QgenError, pattern):
                pipeline.cmd_ingest(self.ctx, CELL, path)

    def test_missing_draft_is_rejected(self):
        with self.assertRaisesRegex(QgenError, "cannot read draft"):
            pipeline.cmd_ingest(self.ctx, CELL, self.tmp / "nope.json")

    def test_bare_list_is_accepted(self):
        path = self.tmp / "list.json"
        path.write_text(json.dumps(good_draft()), encoding="utf-8")
        self.run_cmd(pipeline.cmd_ingest, CELL, path)
        self.assertEqual(len(self.pack()["questions"]), 5)

    def test_reviewed_work_is_not_replaced_without_force(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        self.run_cmd(pipeline.cmd_export, CELL)
        with self.assertRaisesRegex(QgenError, "--force"):
            self.ingest()
        rc, _ = self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(good_draft()), force=True)
        self.assertEqual(rc, 0)
        self.assertFalse((self.ctx.work_dir / CELL / "review.csv").exists())


class Validate(Base):
    def validate(self, entries):
        self.need_validator()
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries))
        rc, out = self.run_cmd(pipeline.cmd_validate, CELL)
        report = json.loads((self.ctx.work_dir / CELL / "validation.json").read_text(encoding="utf-8"))
        return rc, out, report["questions"]

    def problems(self, entries, qid="m3-num-0001", kind="errors"):
        return self.validate(entries)[2].get(qid, {}).get(kind, [])

    def test_good_draft_passes_and_reaches_validated(self):
        rc, out, _ = self.validate(good_draft())
        self.assertEqual(rc, 0, out)
        self.assertEqual(pipeline.Cell(self.ctx, self.spec, CELL).stage, "validated")

    def test_cpp_validator_errors_are_reported(self):
        entries = good_draft()
        entries[0]["answer"] = 9
        rc, out, q = self.validate(entries)
        self.assertEqual(rc, 1)
        self.assertTrue(any("edu-lm-validate" in m and "answer must be an index" in m for m in q["m3-num-0001"]["errors"]))

    def test_missing_explanation_on_written_question(self):
        entries = good_draft()
        del entries[0]["explanation"]
        self.assertIn("written question has no explanation", self.problems(entries))

    def test_generator_needs_no_explanation(self):
        self.assertEqual(self.problems(good_draft(), "m3-num-0004"), [])

    def test_choices_differing_only_in_case_or_spacing(self):
        for a, b in [("Cat", "cat"), ("4 527", "4527"), ("ab", "a b")]:
            entries = good_draft()
            entries[0] = mc("Which is right?", [a, b, "x", "y"], 0)
            with self.subTest(a=a, b=b):
                self.assertTrue(any("differ only in case or spacing" in m for m in self.problems(entries)))

    def test_distinct_choices_are_fine(self):
        self.assertEqual(self.problems(good_draft()), [])

    def test_generator_params_outside_the_year_range(self):
        entries = good_draft()
        entries[3]["params"] = {"a": [10, 5000], "b": [10, 99]}
        self.assertTrue(any("outside the Year 3 range" in m for m in self.problems(entries, "m3-num-0004")))
        entries[3]["params"] = {"a": [1, 99], "b": [10, 99]}
        self.assertTrue(any("outside the Year 3 range" in m for m in self.problems(entries, "m3-num-0004")))

    def test_generator_params_must_be_explicit(self):
        entries = good_draft()
        entries[3]["params"] = {"a": [10, 99]}
        self.assertTrue(any("'b' must be given explicitly" in m for m in self.problems(entries, "m3-num-0004")))

    def test_difficulty_out_of_range(self):
        for bad in (0, 4, "2", None):
            entries = good_draft()
            entries[0]["difficulty"] = bad
            with self.subTest(bad=bad):
                self.assertTrue(any("difficulty" in m for m in self.problems(entries)))

    def test_curriculum_must_be_a_cell_code(self):
        entries = good_draft()
        entries[0]["curriculum"] = "AC9M9N99"
        self.assertTrue(any("not one of this cell's codes" in m for m in self.problems(entries)))
        del entries[0]["curriculum"]
        self.assertTrue(any("curriculum is missing" in m for m in self.problems(entries)))

    def test_near_duplicate_within_the_cell_flags_the_later_one(self):
        entries = good_draft()
        entries.append(mc("Which number is 1000 more than 6 250 ?", ["7 250", "6 350", "16 250", "6 260"], 0))
        _, _, q = self.validate(entries)
        self.assertEqual(q["m3-num-0001"]["flags"], [])
        self.assertTrue(any("near_duplicate of m3-num-0001" in f for f in q["m3-num-0006"]["flags"]))
        self.assertEqual(q["m3-num-0006"]["errors"], [])

    def test_near_duplicate_against_an_existing_pack(self):
        entries = good_draft()
        entries[0] = mc("What is the value of the 3 in 6350?", ["3", "30", "300", "3000"], 2)  # sample sm-m-003
        flags = self.problems(entries, kind="flags")
        self.assertTrue(any("sm-m-003" in f for f in flags), flags)

    def test_similar_prompts_with_different_numbers_are_not_duplicates(self):
        entries = good_draft()
        entries[0] = mc("What is 6 × 7?", ["36", "42", "48", "49"], 1)  # sample sm-m-001 is 6 × 7 too
        entries[1] = mc("What is 6 × 8?", ["42", "48", "56", "49"], 1)
        _, _, q = self.validate(entries)
        self.assertTrue(any("sm-m-001" in f for f in q["m3-num-0001"]["flags"]))
        self.assertEqual(q["m3-num-0002"]["flags"], [])

    def test_own_previously_imported_pack_is_not_a_duplicate_of_itself(self):
        self.to_validated()
        cell = pipeline.Cell(self.ctx, self.spec, CELL)
        cell.target_path.write_text(cell.pack_path.read_text(encoding="utf-8"), encoding="utf-8")
        rc, out = self.run_cmd(pipeline.cmd_validate, CELL)  # the same batch, now also imported
        self.assertEqual(rc, 0, out)
        self.assertNotIn("near_duplicate", out)

    def test_repeat_of_an_already_imported_question_is_flagged(self):
        self.to_validated()
        cell = pipeline.Cell(self.ctx, self.spec, CELL)
        cell.target_path.write_text(cell.pack_path.read_text(encoding="utf-8"), encoding="utf-8")
        entries = [good_draft()[0]]  # the first question of the imported batch, word for word
        _, _, q = self.validate(entries)
        self.assertTrue(any("near_duplicate of m3-num-0001 (maths-y3-number.json)" in f
                            for f in q["m3-num-0006"]["flags"]), q)

    def test_id_used_in_another_pack_is_an_error(self):
        self.need_validator()
        other = {"schema": 1, "pack": "other", "subject": "maths", "questions": [
            {"id": "m3-num-0001", "years": [3], "type": "numeric", "prompt": "Unrelated?", "answer": 1}]}
        (self.data / "other.json").write_text(json.dumps(other), encoding="utf-8")
        self.assertTrue(any("already used in other.json" in m for m in self.problems(good_draft())))

    def test_duplicate_generator_entries_are_flagged(self):
        entries = good_draft() + [copy.deepcopy(good_draft()[3])]
        _, _, q = self.validate(entries)
        self.assertTrue(any("same generator and params" in f for f in q["m3-num-0006"]["flags"]))

    def test_generators_refused_in_a_cell_that_wants_none(self):
        spec = json.loads(self.ctx.spec_path.read_text(encoding="utf-8"))
        for c in spec["cells"]:
            if c["id"] == CELL:
                c["generator_share"] = 0
        path = self.tmp / "spec.json"
        path.write_text(json.dumps(spec), encoding="utf-8")
        self.ctx.spec_path = path
        self.assertTrue(any("not wanted" in m for m in self.problems(good_draft(), "m3-num-0004")))

    def test_generator_samples_are_recorded(self):
        _, _, _ = self.validate(good_draft())
        report = json.loads((self.ctx.work_dir / CELL / "validation.json").read_text(encoding="utf-8"))
        samples = report["generator_samples"]["m3-num-0004"]
        self.assertEqual(len(samples), pipeline.SAMPLES_PER_GENERATOR)
        self.assertRegex(samples[0]["prompt"], r"^What is \d+ \+ \d+\?$")

    def test_failed_validation_does_not_advance_the_stage(self):
        entries = good_draft()
        del entries[0]["explanation"]
        self.validate(entries)
        self.assertEqual(pipeline.Cell(self.ctx, self.spec, CELL).stage, "ingested")


if __name__ == "__main__":
    unittest.main()
