"""Shared helpers: a scratch work/data directory per test, a draft for one cell, the real validator."""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from qgenlib import pipeline  # noqa: E402
from qgenlib.store import REPO_DIR, Ctx, QgenError, Spec, default_ctx, find_validator  # noqa: E402

CELL = "y3-maths-number"
SAMPLE_PACKS = REPO_DIR / "data" / "edu" / "questions"


def mc(prompt, choices, answer, **extra):
    q = {"type": "multiple_choice", "curriculum": "AC9M3N01", "prompt": prompt, "choices": choices,
         "answer": answer, "difficulty": 2, "explanation": "Because."}
    q.update(extra)
    return q


def good_draft():
    return [
        mc("Which number is 1000 more than 6 250?", ["7 250", "6 350", "16 250", "6 260"], 0,
           explanation="SENTINEL-EXPLANATION-ONE"),
        {"type": "numeric", "curriculum": "AC9M3N03", "prompt": "What is 348 + 275?", "answer": 623,
         "difficulty": 2, "explanation": "SENTINEL-EXPLANATION-TWO"},
        {"type": "short_text", "curriculum": "AC9M3N02", "prompt": "Cut a pizza into 4 equal slices. What fraction is one slice? Write it like 1/4.",
         "accept": ["1/4", "zqxaccept"], "difficulty": 1, "explanation": "SENTINEL-EXPLANATION-THREE"},
        {"type": "generator", "curriculum": "AC9M3N03", "generator": "add", "params": {"a": [10, 99], "b": [10, 99]},
         "difficulty": 1},
        {"type": "true_false", "curriculum": "AC9M3N06", "prompt": "Change from $5 for a $3.50 drink is $1.50.",
         "answer": True, "difficulty": 1, "explanation": "SENTINEL-EXPLANATION-FOUR"},
    ]


class Base(unittest.TestCase):
    """Fresh work and data directories (data starts as a copy of the sample packs)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="qgen-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.data = self.tmp / "data"
        # only the fixed sample packs: the real packs beside them grow with every import
        self.data.mkdir()
        for pack in SAMPLE_PACKS.glob("sample-*.json"):
            shutil.copy2(pack, self.data)
        self.ctx = default_ctx(work=self.tmp / "work", data=self.data)
        self.spec = Spec.load(self.ctx)

    def need_validator(self):
        try:
            find_validator(self.ctx)
        except QgenError as e:
            self.skipTest(str(e))

    def write_draft(self, entries, name="draft.json"):
        path = self.tmp / name
        path.write_text(json.dumps({"questions": entries}, ensure_ascii=False), encoding="utf-8")
        return path

    def run_cmd(self, fn, *args, **kw):
        """Run a command, returning (exit code, stdout)."""
        import contextlib
        import io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = fn(self.ctx, *args, **kw)
        return rc, buf.getvalue()

    def to_validated(self, entries=None):
        self.need_validator()
        self.run_cmd(pipeline.cmd_ingest, CELL, self.write_draft(entries or good_draft()))
        rc, out = self.run_cmd(pipeline.cmd_validate, CELL)
        self.assertEqual(rc, 0, out)

    def write_checker(self, answers, name="checker.json", **extra):
        """A checker file for the current blind file, with its token (extra can override it)."""
        blind = json.loads((self.ctx.work_dir / CELL / "blind" / "questions.json").read_text(encoding="utf-8"))
        doc = {"token": blind["token"], "answers": answers}
        doc.update(extra)
        path = self.tmp / name
        path.write_text(json.dumps(doc), encoding="utf-8")
        return path

    def pack(self):
        return json.loads((self.ctx.work_dir / CELL / "pack.json").read_text(encoding="utf-8"))
