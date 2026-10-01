import json
import re
import unittest

import support
from support import CELL, Base, good_draft
from qgenlib import pipeline, prompts
from qgenlib.store import QgenError
from qgenlib.validator import run_validator

# Year 3 maths codes as listed in ACARA's v9 workbook (see spec-sources.md)
YEAR3_MATHS = ["AC9M3N0%d" % n for n in range(1, 8)] + ["AC9M3A0%d" % n for n in range(1, 4)] + \
    ["AC9M3M0%d" % n for n in range(1, 7)] + ["AC9M3SP01", "AC9M3SP02", "AC9M3ST01", "AC9M3ST02", "AC9M3ST03",
                                              "AC9M3P01", "AC9M3P02"]
# param bounds from src/edu/core/Generators.cpp
CPP_PARAMS = {"add": {"a", "b"}, "subtract": {"a", "b"}, "multiply": {"a", "b"},
              "divide": {"quotient", "divisor"}, "place_value": {"digits"}}


class Spec(Base):
    def test_year3_maths_is_complete_and_verified(self):
        codes = []
        for c in self.spec.data["cells"]:
            if c["subject"] == "maths" and c["year"] == 3:
                codes += [d["code"] for d in c["descriptors"] + c.get("not_quizzable", [])]
                self.assertTrue(all(d["verified"] for d in c["descriptors"]))
        self.assertEqual(sorted(codes), sorted(YEAR3_MATHS))

    def test_every_code_is_well_formed_and_verified_or_null(self):
        for c in self.spec.data["cells"]:
            self.assertTrue(c["descriptors"], c["id"])
            for d in c["descriptors"]:
                if d["verified"]:
                    self.assertRegex(d["code"], r"^AC9(M|S|HS|E)[345][A-Z]+\d\d$")
                else:
                    self.assertIsNone(d["code"])
                self.assertTrue(d["text"].strip())

    def test_cells_cover_every_subject_and_year(self):
        seen = {(c["subject"], c["year"]) for c in self.spec.data["cells"]}
        self.assertEqual(seen, {(s, y) for s in ("maths", "science", "hass", "english") for y in (3, 4, 5)})
        for c in self.spec.data["cells"]:
            self.assertEqual(c["id"], "y%d-%s-%s" % (c["year"], c["subject"], c["strand"]))
            self.assertRegex(c["id_prefix"], r"^[mshe][345]-[a-z]{3}$")
            self.assertGreater(c["target"], 0)

    def test_generator_ranges_are_ones_the_daemon_accepts(self):
        for year, gens in self.spec.data["generator_ranges"].items():
            self.assertEqual(set(gens), set(CPP_PARAMS))
            for g, params in gens.items():
                self.assertEqual(set(params), CPP_PARAMS[g])
                for p, (lo, hi) in params.items():
                    upper = 7 if g == "place_value" else 100000
                    lower = 2 if g == "place_value" else (1 if p == "divisor" else 0)
                    self.assertTrue(lower <= lo <= hi <= upper, (year, g, p))

    def test_only_number_and_algebra_cells_ask_for_generators(self):
        for c in self.spec.data["cells"]:
            if c.get("generator_share"):
                self.assertEqual((c["subject"], c["strand"]), ("maths", c["strand"]))
                self.assertIn(c["strand"], ("number", "algebra"))


class Prompts(Base):
    def test_writer_prompt_is_fully_filled(self):
        text = prompts.build_prompt(self.ctx, "writer", CELL)
        self.assertNotRegex(text, r"\{\{|\}\}")
        self.assertIn("AC9M3N01", text)
        self.assertIn("Year 3", text)
        self.assertIn(str((self.ctx.work_dir / CELL / "writer" / "draft.json").resolve()), text)
        self.assertIn("10 written questions and 10 generator entries", text)
        self.assertIn("add: a in [10, 999], b in [10, 999]", text)
        self.assertNotIn("second attempt", text)

    def test_writer_prompt_states_the_schema_types(self):
        text = prompts.build_prompt(self.ctx, "writer", CELL)
        for word in ("multiple_choice", "true_false", "numeric", "short_text", "generator", "0-based index"):
            self.assertIn(word, text)

    def test_writer_prompt_without_generators(self):
        text = prompts.build_prompt(self.ctx, "writer", "y3-science-understanding", out=self.tmp / "d.json")
        self.assertNotIn("place_value", text)
        self.assertIn("0 generator entries", text)
        self.assertIn(str(self.tmp / "d.json"), text)

    def test_retry_needs_a_failed_validation_and_lists_its_errors(self):
        self.need_validator()
        with self.assertRaisesRegex(QgenError, "no failed validation"):
            prompts.build_prompt(self.ctx, "writer", CELL, retry=True)
        entries = good_draft()
        del entries[1]["explanation"]
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries))
        self.run_cmd(pipeline.cmd_validate, CELL)
        text = prompts.build_prompt(self.ctx, "writer", CELL, retry=True)
        self.assertIn("- entry 2 (m3-num-0002): written question has no explanation", text)
        self.assertIn(str((self.ctx.work_dir / CELL / "draft.json").resolve()), text)

    def test_checker_prompt_names_only_the_blind_file(self):
        self.to_validated()
        self.run_cmd(pipeline.cmd_blind, CELL)
        text = prompts.build_prompt(self.ctx, "checker", CELL)
        self.assertNotRegex(text, r"\{\{|\}\}")
        blind = str((self.ctx.work_dir / CELL / "blind" / "questions.json").resolve())
        self.assertEqual(text.count(blind), 1)
        for flag in ("ambiguous", "reading_level", "factual", "not_australian", "unsuitable"):
            self.assertIn("`%s`" % flag, text)
        self.assertIn("data/edu/questions/", text)  # told not to read it
        self.assertNotIn("draft.json", text)

    def test_checker_prompt_needs_the_blind_file(self):
        with self.assertRaisesRegex(QgenError, "run blind first"):
            prompts.build_prompt(self.ctx, "checker", CELL)

    def test_unknown_placeholder_is_an_error(self):
        with self.assertRaisesRegex(QgenError, "no value"):
            prompts.render("hello {{nope}}", {})


class ValidatorSample(Base):
    def test_sample_prints_n_instances_per_generator(self):
        self.need_validator()
        res = run_validator(self.ctx, [self.data], sample=3)
        by_id = {}
        for s in res["samples"]:
            by_id.setdefault(s["id"], []).append(s)
        self.assertEqual(len(by_id), 6)  # the six generator entries in the sample maths pack
        self.assertTrue(all(len(v) == 3 for v in by_id.values()))
        self.assertRegex(by_id["sm-m-gen-add"][0]["prompt"], r"^What is \d+ \+ \d+\?$")
        for s in by_id["sm-m-gen-mul3"]:
            a, b = map(int, re.findall(r"\d+", s["prompt"]))
            self.assertEqual(int(s["answer"]), a * b)

    def test_no_samples_without_the_option(self):
        self.need_validator()
        self.assertEqual(run_validator(self.ctx, [self.data])["samples"], [])


if __name__ == "__main__":
    unittest.main()
