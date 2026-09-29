---
name: qgen
description: Generate and check Edu-LM quiz questions for one curriculum cell (year, subject, strand) with Claude Code subagents, then produce a CSV for a parent to review. Use when asked to write, draft or extend the question bank, or when the user runs /qgen <year> <subject> [strand]. Also use to resume an interrupted cell or to import a reviewed CSV.
---

# /qgen: question generation with subagents

There is no API access. You (the orchestrating session) spawn Sonnet subagents to draft and blind-check questions; plain Python (`tools/qgen/qgen.py`, stdlib, no network) does everything deterministic. Run every command from the repository root. Read `tools/qgen/README.md` for what the pipeline produces and `docs/PLAN.md` section 8 for why.

`/qgen <year> <subject> [strand]`: year is 3 to 5, subject is maths, science, hass or english, strand is optional (`number`, `algebra`, ...). List the cells and their stages with:

    python3 tools/qgen/qgen.py cells [--year N] [--subject S]

Cell ids look like `y3-maths-number`. Work for each cell lives in `tools/qgen/work/<cell>/` (git-ignored) with a `status.json` that records the stage reached. Only `import` writes to `data/edu/questions/`.

## Before you start

1. `edu-lm-validate` must exist: `ls build/src/edu/edu-lm-validate`. If not, build it: `cmake --build build --target edu-lm-validate`. (`--validator PATH` overrides the lookup on any command.)
2. If the request covers more than one cell (no strand given, or several years), it is a wave. Each cell costs at least two subagent runs (writer, checker) plus one more per failed validation. Tell the user the cell list and that count, and wait for a go-ahead before spawning anything. A single cell needs no confirmation.
3. Never edit `pack.json`, drafts or CSVs by hand to get past an error. Errors go back to a writer.

## Per cell

Check `python3 tools/qgen/qgen.py cells` first and resume from the stage shown:

| stage | next step |
|---|---|
| new | 1 (write) |
| ingested | 2 (validate) |
| validated | 3 (blind) |
| blinded | 4 (check) |
| compared | 5 (export) |
| exported | tell the user where the CSV is (step 5) and stop |
| imported | done. A second batch for the same cell (the target is not yet met, or the user wants more) is a new draft: ask the user first, then step 1 and `ingest --force`. New ids continue after every id already exported or imported, and validate compares against the already imported pack, so nothing from the first batch is replaced or repeated |

### 1. Write

    python3 tools/qgen/qgen.py prompt writer <cell>

Spawn one subagent with the Agent tool: `subagent_type: general-purpose`, `model: sonnet`, and the printed text, unchanged, as its prompt. It asks for about 20 entries (the cell's target), which keeps a run small; if the run is interrupted little is lost. Do not add to the prompt and do not pass it anything else.

When it returns, do not trust its report. Check the file it was told to write exists (`tools/qgen/work/<cell>/writer/draft.json`) and ingest it:

    python3 tools/qgen/qgen.py ingest <cell> tools/qgen/work/<cell>/writer/draft.json

`ingest` refuses anything that is not well-formed JSON, and it clears the `writer/` folder after reading the draft, so an old draft can never be ingested twice. If ingest refuses, or the writer died before writing the file, spawn a fresh writer (never continue a dead one) with the same prompt; there is no old file for it to be mistaken for.

### 2. Validate

    python3 tools/qgen/qgen.py validate <cell>

This runs the real `edu-lm-validate` plus extra checks (year range of generator params, missing explanations, choices that differ only in case or spacing, near-duplicate prompts). Errors mean the pack is not ready. Send them back:

    python3 tools/qgen/qgen.py prompt writer <cell> --retry

and spawn a fresh Sonnet writer with that text. It reads the previous draft, fixes the listed problems and rewrites the whole draft. Ingest and validate again. After two retries with errors remaining, stop and show the user the errors instead of looping.

Flags (near-duplicates) and notes about counts are not errors: they go to the parent in the CSV.

### 3. Blind file

    python3 tools/qgen/qgen.py blind <cell>

This writes `tools/qgen/work/<cell>/blind/questions.json` with only id, type, prompt and choices. Generator entries are left out (they have no fixed answer; the CSV shows samples instead).

### 4. Check

    python3 tools/qgen/qgen.py prompt checker <cell>

Spawn a NEW subagent (`subagent_type: general-purpose`, `model: sonnet`), fresh context, with that text as its whole prompt. Do not tell it anything about the writer, the draft or the key. Never reuse the writer's agent for this.

The blind file carries a `token` that the checker copies into its output; `compare` refuses a checker file with a missing or wrong token (for example one left over from an older draft) and a checker file whose flags are not in the documented shape. If it refuses, re-run the checker with a fresh subagent; never edit the checker's file by hand.

The checker is told to read only the blind file and write only `tools/qgen/work/<cell>/checker/checker.json`. That separation is by instruction only: nothing enforces it, and a subagent could still open other files. It is a real safeguard against the checker seeing the key by accident, not against a checker that ignores its instructions.

Verify the checker's file exists, then:

    python3 tools/qgen/qgen.py compare <cell> tools/qgen/work/<cell>/checker/checker.json

`compare` marks each answer agree or disagree (multiple choice by index, numbers within tolerance, short text with the daemon's own normalisation). Disagreement and flags (`ambiguous`, `reading_level`, `factual`, `not_australian`, `unsuitable`) send an item to the parent; nothing is deleted.

### 5. Export, then stop

    python3 tools/qgen/qgen.py export <cell>

Tell the user the path (`tools/qgen/work/<cell>/review.csv`), the counts from `compare` (agree, disagree, flagged), and that `tools/qgen/README.md` explains the columns and how to mark approve. Then stop. Do not import.

## Importing (a separate step)

Only when the user says the CSV is reviewed, and gives its path (the reviewed copy, possibly saved elsewhere):

    python3 tools/qgen/qgen.py import <cell> <reviewed.csv>

This writes approved rows to `data/edu/questions/<subject>-y<year>-<strand>.json` and runs `edu-lm-validate` on the whole directory (rolling back if it fails, including if the validator crashes). It installs nothing and touches nothing outside the repository. A CSV from an earlier batch of the same cell can be imported again (its rows can still be marked `n`). Report its output, and always show the user the `edit <id>: <field>: old -> new` lines it printed, so they can see exactly what their edits changed; also list any `skipped:` rows with the reason. Committing is the user's call.

## If an agent is interrupted

An agent resumed after a rate limit or crash may come back with no memory of its task. Treat every such resume as a cold start: re-send its full instructions (the same rendered prompt), say what is already done, and verify its work against the output files rather than its report. Prefer spawning a fresh writer or checker over resuming one. The `status.json` stage tells you which step to redo.

## Cost

Report before starting any wave of several cells (see above). Cells are independent, so a usage limit part-way through loses at most the step in progress; the stage in `cells` shows where to resume.
