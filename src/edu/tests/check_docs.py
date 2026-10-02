#!/usr/bin/env python3
"""ctest helper: the JSON in docs/QUESTIONS.md must stay valid.

    check_docs.py <QUESTIONS.md> <example-pack.json> <edu-lm-validate>

The block under "One example per type" must equal the example pack, and every block under
the walk-throughs ("(a)", "(b)", "(c)") must load with no errors and no warnings.
"""
import json
import re
import subprocess
import sys
import tempfile
import os


def blocks(text):
    return re.findall(r"```json\n(.*?)```", text, re.S)


def main(doc, example, validator):
    text = open(doc, encoding="utf-8").read()
    start = text.index("## One example per type")
    end = text.index("### Generators")
    shown = blocks(text[start:end])
    if len(shown) != 1 or json.loads(shown[0]) != json.load(open(example, encoding="utf-8")):
        sys.exit("the example block in QUESTIONS.md differs from the example pack")

    walk = text[text.index("## Walk-throughs"):text.index("## Writing good questions")]
    packs = blocks(walk)
    if len(packs) < 3:
        sys.exit("expected at least 3 walk-through packs, found %d" % len(packs))
    with tempfile.TemporaryDirectory() as tmp:
        for i, block in enumerate(packs):
            path = os.path.join(tmp, "pack%d.json" % i)
            with open(path, "w", encoding="utf-8") as f:
                f.write(block)
            p = subprocess.run([validator, path], capture_output=True, text=True, encoding="utf-8")
            if p.returncode != 0 or "0 errors, 0 warnings" not in p.stdout:
                sys.exit("walk-through block %d: %s%s" % (i + 1, p.stdout, p.stderr))


if __name__ == "__main__":
    main(*sys.argv[1:4])
