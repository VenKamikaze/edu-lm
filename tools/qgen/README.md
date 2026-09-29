# qgen: question review guide

This folder holds the tools that turn a draft set of quiz questions into approved packs in
`data/edu/questions/`. Claude Code writes and double-checks the questions (the `/qgen` skill in
`.claude/skills/qgen/`); you decide what goes in. Nothing reaches the quiz until you approve it.

## What you get

For each topic (a "cell", like `y3-maths-number`) you get `tools/qgen/work/<cell>/review.csv`.
Open it in LibreOffice Calc or Excel. It is UTF-8, so accents and minus signs display correctly.
One row is one question.

## The columns

| column | what it is |
|---|---|
| id | the question's permanent id, e.g. `m3-num-0007`. Do not change it |
| type | multiple_choice, true_false, numeric, short_text, or generator |
| prompt | the question the child sees |
| choices | multiple choice only: one choice per line |
| answer | the right answer as the child would see it (for multiple choice, the text of the right choice) |
| accept | short_text only: every answer that counts as right, one per line (the first is `answer`) |
| tolerance | numeric only: how far off an answer can be and still count (blank means exact) |
| explanation | the short reason shown with the answer |
| curriculum | the Australian Curriculum v9 code the question was written to |
| difficulty | 1 easy, 2 typical, 3 a stretch |
| checker_answer | what a second, cold reviewer (a separate Claude run that never saw the answers) answered |
| agree | yes if the checker got the same answer as the key, no if not, blank if it gave none |
| checker_flags | problems the checker raised: ambiguous, reading_level, factual, not_australian, unsuitable |
| checker_notes | its one-line reason |
| checks | automatic warnings, e.g. `near_duplicate of sm-m-003 (sample-maths.json)` |
| generator_samples | generator rows only: real examples the quiz would produce, `question = answer` |
| approve | yours to fill in (below) |
| notes | yours, for anything you want to remember; not used by the import |

Generator rows have no fixed question: the quiz builds a fresh sum each time from the settings in
`prompt` (for example `[generator add: a 10-99, b 10-99]`). Look at the samples to judge them.
Start with the rows where `agree` is `no`, `agree` is blank (the checker gave no answer), or `checker_flags` is not empty.

## Marking a row

Fill the `approve` column:

- `y`: use this question exactly as it is.
- `n`: do not use it.
- `edit`: use it after the changes you made in this row (prompt, choices, answer, accept, tolerance, explanation, curriculum, difficulty). For a multiple choice question, the `answer` cell must match one choice exactly; for a short_text question, edit `accept` (the list of every right answer). An edit that does not pass the checks is skipped and reported, and you can fix it and import again.
- blank: no decision yet. It stays out (or stays in, if you approved it in an earlier import).

After an import, each accepted edit is printed as `edit <id>: <field>: old -> new`. Read those lines: they are exactly what your edit changed. An edit whose answer, accept or choices cell looks like a spreadsheet's auto-format (a date such as `1/04/2026` where you exported `1/4`) is refused.

If you change a row but mark it `y`, the import refuses that row and asks for `edit`, so nothing changes by accident. Generator rows can be `y` or `n` only.

Spreadsheets sometimes turn `3/4` into a date. If a row is refused for that reason, mark it `edit` after fixing the cell, or open the CSV with every column set to Text.

## Importing

Save the CSV (CSV UTF-8 is best; Excel's plain "CSV (Comma delimited)" is read as Windows-1252, and the import says which it read; characters Windows-1252 cannot hold, like the minus sign, come back as `?` and make rows show as changed), then from the repository root:

    python3 tools/qgen/qgen.py import <cell> path/to/review.csv

It writes the approved rows to `data/edu/questions/<subject>-y<year>-<strand>.json`, then checks the
whole `data/edu/questions/` folder with `edu-lm-validate`. If that check fails, your pack file is put
back as it was. You can import the same CSV again: nothing is duplicated, and a row you later change
to `n` is removed.

A cell can have more than one batch. A second batch continues the numbering (ids are never
reused, even for questions you removed), is checked for repeats against what you already
approved, and its CSV imports alongside the first: importing an older batch's CSV again still
works, and its rows can still be marked `n`. The import does not install anything; copying packs to `/etc/edu-lm/questions.d`
is the usual install step (see `docs/INSTALL-ARCH.md`).

## Other commands (for whoever runs the pipeline)

`python3 tools/qgen/qgen.py cells` lists every cell and how far it has got. `qgen.py --help` lists the
steps: `prompt`, `ingest`, `validate`, `blind`, `compare`, `export`, `import`.

Tests: `python3 -m unittest discover tools/qgen/tests` (also run by `ctest` as `QgenTools`).
Curriculum codes and their sources: `spec-sources.md`; re-check them with `check_spec_codes.py`.
