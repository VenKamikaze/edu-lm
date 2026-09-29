import json
import unittest
from pathlib import Path

import support  # noqa: F401  (sets sys.path)
from qgenlib import answer

FIXTURE = json.loads((Path(__file__).parent / "answer_cases.json").read_text(encoding="utf-8"))


class AnswerAgainstCpp(unittest.TestCase):
    def test_is_correct_matches_recorded_cpp_results(self):
        for qtype, fields, given, expected, _ in FIXTURE["cases"]:
            q = dict(fields, type=qtype)
            with self.subTest(type=qtype, given=given, fields=fields):
                self.assertEqual(answer.is_correct(q, given), expected)

    def test_parse_number_matches_recorded_cpp_results(self):
        for qtype, fields, given, _, parsed in FIXTURE["cases"]:
            if qtype == "numeric":
                with self.subTest(given=given):
                    self.assertEqual(answer.parse_number(given) is not None, parsed)

    def test_correct_answer_text_matches_recorded_cpp_results(self):
        for value, expected in FIXTURE["format"]:
            with self.subTest(value=value):
                self.assertEqual(answer.correct_answer_text({"type": "numeric", "answer": value}), expected)

    def test_correct_answer_text_other_types(self):
        self.assertEqual(answer.correct_answer_text({"type": "multiple_choice", "choices": ["a", "b"], "answer": 1}), "b")
        self.assertEqual(answer.correct_answer_text({"type": "true_false", "answer": False}), "False")
        self.assertEqual(answer.correct_answer_text({"type": "short_text", "accept": ["x", "y"]}), "x")
        self.assertEqual(answer.correct_answer_text({"type": "generator"}), "")


class AnswerEdges(unittest.TestCase):
    """Cases named in the brief, spelled out so a failure reads clearly."""

    def test_tolerance_is_inclusive(self):
        q = {"type": "numeric", "answer": 10, "tolerance": 0.5}
        self.assertTrue(answer.is_correct(q, "10.5"))
        self.assertTrue(answer.is_correct(q, "9.5"))
        self.assertFalse(answer.is_correct(q, "10.6"))

    def test_zero_tolerance_is_exact(self):
        q = {"type": "numeric", "answer": 10}
        self.assertTrue(answer.is_correct(q, "10"))
        self.assertFalse(answer.is_correct(q, "10.01"))

    def test_short_text_normalisation(self):
        q = {"type": "short_text", "accept": ["Sydney"]}
        for given in ("sydney", "  SYDNEY ", "Sydney.", "!sydney!"):
            self.assertTrue(answer.is_correct(q, given), given)
        for given in ("syd ney", "", "   "):
            self.assertFalse(answer.is_correct(q, given), given)

    def test_multiple_choice_index_only(self):
        q = {"type": "multiple_choice", "answer": 2, "choices": list("abcd")}
        self.assertTrue(answer.is_correct(q, " 2 "))
        self.assertFalse(answer.is_correct(q, "c"))
        self.assertFalse(answer.is_correct(q, "-2"))


if __name__ == "__main__":
    unittest.main()
