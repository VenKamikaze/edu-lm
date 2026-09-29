#!/usr/bin/env python3
"""ctest helper for edu-lm-validate --sample: checks exit codes and exact counts.

    check_sample.py good <edu-lm-validate> <pack dir>   --sample 3 succeeds, 3 samples per generator
    check_sample.py bad  <edu-lm-validate> <pack dir>   --sample x and --sample 0 exit 2, with a message
"""
import collections
import json
import subprocess
import sys


def run(validator, *args):
    return subprocess.run([validator, *args], capture_output=True, text=True, encoding="utf-8")


def good(validator, packs):
    p = run(validator, "--sample", "3", packs)
    if p.returncode != 0:
        sys.exit("exit code %d, expected 0" % p.returncode)
    counts = collections.Counter()
    for line in p.stdout.splitlines():
        if line.startswith("sample "):
            o = json.loads(line[len("sample "):])
            if set(o) != {"id", "prompt", "answer"} or not o["prompt"] or not o["answer"]:
                sys.exit("bad sample line: " + line)
            counts[o["id"]] += 1
    if not counts:
        sys.exit("no samples printed")
    if any(n != 3 for n in counts.values()):
        sys.exit("expected 3 samples per generator, got %s" % dict(counts))


def bad(validator, packs):
    for value in ("x", "0", "-2"):
        p = run(validator, "--sample", value, packs)
        if p.returncode != 2 or "needs a positive number" not in p.stderr:
            sys.exit("--sample %s: exit %d, stderr %r" % (value, p.returncode, p.stderr))
    if "sample " in run(validator, packs).stdout.replace("samples", ""):
        sys.exit("samples printed without --sample")


if __name__ == "__main__":
    {"good": good, "bad": bad}[sys.argv[1]](sys.argv[2], sys.argv[3])
