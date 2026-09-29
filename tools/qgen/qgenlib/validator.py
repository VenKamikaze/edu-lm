"""Runs the real edu-lm-validate and parses what it prints. The C++ rules are not reimplemented."""
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

from .store import QgenError, find_validator

_ISSUE = re.compile(r"^(error|warning): (.*?)(?:: question (\S+))?: (.*)$")


_PROBE_PACK = {"schema": 1, "pack": "probe", "subject": "maths", "questions": [
    {"id": "p1", "years": [3], "type": "generator", "generator": "add", "params": {"a": [1, 2], "b": [1, 2]}}]}
_supported = set()


def _check_sample_support(ctx, exe):
    """An older edu-lm-validate treats '--sample 1' as missing files; say so instead of a
    confusing pile of errors. Checked once per binary."""
    st = os.stat(exe)
    key = (exe, st.st_mtime_ns, st.st_size)
    if key in _supported:
        return
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "probe.json"
        path.write_text(json.dumps(_PROBE_PACK), encoding="utf-8")
        try:
            proc = subprocess.run([exe, "--sample", "1", str(path)], capture_output=True, text=True,
                                  encoding="utf-8", timeout=60)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise QgenError(f"could not run {exe}: {e}")
    if proc.returncode != 0 or "sample {" not in proc.stdout:
        raise QgenError(f"{exe} does not support --sample (or failed a trivial run); "
                        "rebuild it: cmake --build build --target edu-lm-validate")
    _supported.add(key)


def run_validator(ctx, paths, sample=0):
    """Returns {returncode, errors, warnings, loaded, samples}; errors/warnings are dicts."""
    exe = find_validator(ctx)
    _check_sample_support(ctx, exe)
    cmd = [exe]
    if sample:
        cmd += ["--sample", str(sample)]
    cmd += [str(p) for p in paths]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise QgenError(f"could not run {cmd[0]}: {e}")
    result = {"returncode": proc.returncode, "errors": [], "warnings": [], "loaded": None, "samples": []}
    for line in proc.stderr.splitlines():
        m = _ISSUE.match(line)
        if not m:
            continue
        kind, file, question, message = m.groups()
        result["errors" if kind == "error" else "warnings"].append(
            {"file": file, "id": question, "message": message})
    for line in proc.stdout.splitlines():
        if line.startswith("sample "):
            try:
                result["samples"].append(json.loads(line[len("sample "):]))
            except json.JSONDecodeError:
                raise QgenError(f"unreadable --sample line from the validator: {line!r}")
        elif m := re.match(r"(\d+) questions loaded", line):
            result["loaded"] = int(m.group(1))
    if proc.returncode not in (0, 1):
        raise QgenError(f"{cmd[0]} exited {proc.returncode}: {proc.stderr.strip()}")
    return result
