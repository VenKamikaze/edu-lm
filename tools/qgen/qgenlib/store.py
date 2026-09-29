"""Paths, the spec matrix, per-cell work directories and status.json."""
import hashlib
import json
import os
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = TOOL_DIR.parent.parent

# Order matters: a later stage means more of the pipeline has run.
STAGES = ["new", "ingested", "validated", "blinded", "compared", "exported", "imported"]

# Files a stage produces, relative to the cell directory (removed when a new draft is ingested).
STAGE_FILES = {
    "ingested": ["pack.json", "draft.json"],
    "validated": ["validation.json"],
    "blinded": ["blind"],
    "compared": ["compare.json", "checker"],
    "exported": ["review.csv"],
}


class QgenError(Exception):
    """A problem to show the user as one line, without a traceback."""


@dataclass
class Ctx:
    spec_path: Path
    work_dir: Path
    data_dir: Path
    validator: str = ""


def default_ctx(spec=None, work=None, data=None, validator=None):
    return Ctx(
        spec_path=Path(spec) if spec else TOOL_DIR / "spec.json",
        work_dir=Path(work) if work else TOOL_DIR / "work",
        data_dir=Path(data) if data else REPO_DIR / "data" / "edu" / "questions",
        validator=validator or "",
    )


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def read_json(path, what="file"):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise QgenError(f"{what} not found: {path}")
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise QgenError(f"{what} {path} is not valid JSON: {e}")


class Spec:
    def __init__(self, data):
        self.data = data
        self.cells = {c["id"]: c for c in data["cells"]}

    @classmethod
    def load(cls, ctx):
        return cls(read_json(ctx.spec_path, "spec"))

    def cell(self, cell_id):
        if cell_id not in self.cells:
            raise QgenError(f"unknown cell '{cell_id}'; known cells: {', '.join(self.cells)}")
        return self.cells[cell_id]

    def year(self, cell):
        return self.data["years"][str(cell["year"])]

    def ranges(self, cell):
        return self.data["generator_ranges"][str(cell["year"])]

    @staticmethod
    def codes(cell):
        """Codes a question in this cell may carry; empty when none is verified."""
        return [d["code"] for d in cell["descriptors"] if d.get("verified") and d.get("code")]


def id_number(cell_info, qid):
    """The number in an id like m3-num-0007, or None if the id is not of this cell."""
    prefix = cell_info["id_prefix"] + "-"
    if qid.startswith(prefix) and qid[len(prefix):].isdigit():
        return int(qid[len(prefix):])
    return None


class Cell:
    """A cell's work directory and its status.json."""

    def __init__(self, ctx, spec, cell_id):
        self.ctx = ctx
        self.spec = spec
        self.info = spec.cell(cell_id)
        self.id = cell_id
        self.dir = ctx.work_dir / cell_id
        self.pack_path = self.dir / "pack.json"
        self.validation_path = self.dir / "validation.json"
        self.blind_path = self.dir / "blind" / "questions.json"
        self.compare_path = self.dir / "compare.json"
        self.csv_path = self.dir / "review.csv"
        self.target_path = ctx.data_dir / f"{self.info['pack']}.json"

    def status(self):
        path = self.dir / "status.json"
        if not path.exists():
            return {"cell": self.id, "stage": "new"}
        return read_json(path, "status")

    @property
    def stage(self):
        stage = self.status().get("stage", "new")
        return stage if stage in STAGES else "new"

    def reached(self, stage):
        return STAGES.index(self.stage) >= STAGES.index(stage)

    def set_stage(self, stage, reset=False, **detail):
        """Record a stage. It only moves forward unless reset (a new draft): then later files go."""
        current = self.stage
        if reset:
            for later in STAGES[STAGES.index(stage) + 1:]:
                for name in STAGE_FILES.get(later, []):
                    target = self.dir / name
                    if target.is_dir():
                        shutil.rmtree(target)
                    elif target.exists():
                        target.unlink()
        elif STAGES.index(stage) < STAGES.index(current):
            stage = current
        status = self.status()
        status.update({"cell": self.id, "stage": stage,
                       "updated": datetime.now(timezone.utc).isoformat(timespec="seconds")})
        status.update(detail)
        write_json(self.dir / "status.json", status)

    def require(self, stage, hint):
        if not self.reached(stage):
            raise QgenError(f"{self.id} is at stage '{self.stage}'; {hint}")

    def clear_writer(self):
        """Remove the writer's output so an old draft can never be ingested again."""
        shutil.rmtree(self.dir / "writer", ignore_errors=True)

    def highest_id_number(self, id_number):
        """Highest id number this cell has handed out: in the imported pack, or in status.json
        (set when a batch is exported or imported, so a deleted id is never reused)."""
        used = [self.status().get("committed_max", 0)]
        if self.target_path.exists():
            questions = read_json(self.target_path, "existing pack").get("questions", [])
            used += [n for n in (id_number(self.info, q.get("id", "")) for q in questions) if n]
        return max(used)

    def commit_ids(self, questions):
        """Record the ids of a batch that has left the work directory (exported or imported)."""
        nums = [n for n in (id_number(self.info, q.get("id", "")) for q in questions) if n]
        if nums:
            self.set_stage(self.stage, committed_max=max(nums + [self.status().get("committed_max", 0)]))

    def load_pack(self):
        if not self.pack_path.exists():
            raise QgenError(f"{self.id} has no pack yet; run ingest first")
        return read_json(self.pack_path, "pack")


def find_validator(ctx):
    """--validator, else $QGEN_VALIDATOR, the repo's build trees, then PATH."""
    if ctx.validator:
        if not (Path(ctx.validator).is_file() and os.access(ctx.validator, os.X_OK)):
            raise QgenError(f"--validator {ctx.validator} is not an executable file")
        return ctx.validator
    candidates = [os.environ.get("QGEN_VALIDATOR", "")]
    candidates += [str(REPO_DIR / d / "src" / "edu" / "edu-lm-validate") for d in ("build", "build-rel")]
    for c in candidates:
        if c and Path(c).is_file() and os.access(c, os.X_OK):
            return c
    found = shutil.which("edu-lm-validate")
    if found:
        return found
    raise QgenError("edu-lm-validate not found; build it (cmake --build build --target edu-lm-validate), "
                    "put it on PATH, or pass --validator")
