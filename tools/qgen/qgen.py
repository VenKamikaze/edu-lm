#!/usr/bin/env python3
"""Question-generation tooling for Edu-LM: the deterministic half of the /qgen skill.

See README.md (for parents) and .claude/skills/qgen/SKILL.md (the workflow).
"""
import argparse
import sys

from qgenlib import checks, pipeline, prompts
from qgenlib.store import QgenError, default_ctx


def build_parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--spec", help="spec matrix (default: tools/qgen/spec.json)")
    common.add_argument("--work", help="work directory (default: tools/qgen/work)")
    common.add_argument("--data", help="question pack directory (default: data/edu/questions)")
    common.add_argument("--validator", help="path to edu-lm-validate (default: build tree, then PATH)")

    p = argparse.ArgumentParser(prog="qgen.py", description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)

    def add(name, help, *args):
        s = sub.add_parser(name, help=help, parents=[common])
        for a in args:
            s.add_argument(a)
        return s

    s = add("cells", "list the cells and the stage each has reached")
    s.add_argument("--subject")
    s.add_argument("--year", type=int)
    s = add("prompt", "print the writer or checker prompt for a cell", "kind", "cell")
    s.add_argument("--out", help="where the subagent should write its file")
    s.add_argument("--retry", action="store_true", help="writer: include the errors from the last validate")
    s = add("ingest", "assign ids to a writer's draft and write the cell's pack", "cell", "draft")
    s.add_argument("--force", action="store_true", help="replace a draft that has already been exported")
    s = add("validate", "run edu-lm-validate and the extra checks on the cell's pack", "cell")
    s.add_argument("--dup-threshold", type=float, default=checks.NEAR_DUPLICATE)
    add("blind", "write the answer-free file for the checker", "cell")
    add("compare", "compare the checker's answers with the key", "cell", "checker")
    add("export", "write the review CSV for a parent", "cell")
    add("import", "put the approved rows of a reviewed CSV into data/edu/questions", "cell", "csv")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    ctx = default_ctx(args.spec, args.work, args.data, args.validator)
    try:
        if args.command == "cells":
            return pipeline.cmd_cells(ctx, args.subject, args.year)
        if args.command == "prompt":
            print(prompts.build_prompt(ctx, args.kind, args.cell, args.out, args.retry))
            return 0
        if args.command == "ingest":
            return pipeline.cmd_ingest(ctx, args.cell, args.draft, args.force)
        if args.command == "validate":
            return pipeline.cmd_validate(ctx, args.cell, args.dup_threshold)
        if args.command == "blind":
            return pipeline.cmd_blind(ctx, args.cell)
        if args.command == "compare":
            return pipeline.cmd_compare(ctx, args.cell, args.checker)
        if args.command == "export":
            return pipeline.cmd_export(ctx, args.cell)
        if args.command == "import":
            return pipeline.cmd_import(ctx, args.cell, args.csv)
    except QgenError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    sys.exit(main())
