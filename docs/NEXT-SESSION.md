# Next session brief: grow the question bank (phase 5b, Year 3 wave)

**Update 2026-10-02: phase 5b is DONE.** All 35 cells for Years 3–5 are imported on `main` (523 real questions, 562 with the 39 samples, which stay shipped by user decision). From Year 4 on the orchestrator reviewed the packs in place of the parent (see PROGRESS.md for every n/edit). Next: ask the user what's next. Options: Phase 6 (publish: README, screenshots, licence notes), the open PHASE4-TEST steps, the protocol-version TODO, or a second batch for any cell. The Year 3 wave notes below are history.

Written 2026-10-01 for the next Claude Code session on this repo. Read this first, then `docs/PROGRESS.md` (state and decisions) and `docs/PLAN.md` (design). Repo `main` is at `b683fdc` or later.

## How the user wants you to work

- **You orchestrate.** Large pieces of code go to a Sonnet `implementer` subagent with a written brief, then an Opus `reviewer` tries to refute its claims, fixes go back to the implementer, and you verify independently (Debug `build/` and Release `build-rel/` full ctest, the source, screenshots where relevant) before committing. Small fixes you can do inline.
- The user's global rules: brief comments; **ask rather than guess** (unless they say they're busy, then decide and document); note out-of-scope problems in `TODO.md` (repo root, git-excluded) and say so.
- **A subagent resumed after a rate limit or crash is a cold start:** re-send its full instructions, say what's done, and verify its output files rather than its report. Prefer a fresh agent.
- Commit WIP to a branch rather than leaving work uncommitted; squash-merge into `main` when verified. Git identity is repo-local (`arnie <mick.saunders+ant@gmail.com>`).
- Usage limits are real (one hit already). Keep runs small, and save state to disk before each long step.

## Where things stand

- Phases 1–4 are built and on `main`. One Arch PC has the package installed, and a real quiz login worked, including the on-the-spot answer reveal. The rest of `docs/PHASE4-TEST.md` is the user's to run when they have time; don't block on it.
- Phase 5a (the tooling) is done: the `/qgen` project skill (`.claude/skills/qgen/`) plus `tools/qgen/qgen.py`. There is no Anthropic API access; subagents do the writing and checking.
- Phase 5b pilot is done: `y3-maths-number` (20 entries: 10 written, 10 generators) passed validation first time. The blind checker agreed 10/10, the parent approved all 20 without edits, and it was imported as `data/edu/questions/maths-y3-number.json` (commit `b683fdc`). The bank now has 59 entries (39 samples + 20 real).
- The same commit fixed tests that assumed `data/edu/questions/` held only samples. Tests now use `sample-*.json` as fixed fixtures, so importing new packs must not break any test. If one does, that's a test bug.

## The next chunk: the rest of Year 3

11 cells, target 160 entries (`python3 tools/qgen/qgen.py cells --year 3`):

| cell | target |
|---|---|
| y3-maths-algebra | 12 |
| y3-maths-measurement | 16 |
| y3-maths-space | 12 |
| y3-maths-statistics-probability | 14 |
| y3-science-understanding | 20 |
| y3-science-inquiry-endeavour | 10 |
| y3-hass-knowledge | 20 |
| y3-hass-skills | 10 |
| y3-english-language | 20 |
| y3-english-literature | 12 |
| y3-english-literacy | 14 |

1. **Ask before starting.** This is a wave, so the skill requires the user's go-ahead. Give them the cell list and the cost: at least 2 subagent runs per cell (writer + checker), so 22 or more, plus one per failed validation. Suggest doing it in two or three sittings: the remaining Year 3 maths first (4 cells), then one cell of each other subject as a mini-pilot (they have no generators and are new ground), then the rest.
2. **Run one cell at a time with `/qgen 3 <subject> <strand>`**, following `SKILL.md` exactly: render the prompt, spawn the writer unchanged, check the file, ingest, validate, blind, a fresh checker, compare, export.
3. **Read every exported pack yourself before handing over the CSV**, as was done for the pilot. Agreement alone isn't quality. Report concerns to the user per item id. Things the pilot showed:
   - The Sonnet checker is **lenient on reading level**: it let through a long, abstract true/false (`m3-num-0005`).
   - Numeric answers can trip children: a "how many cents change" question (`m3-num-0007`) marks 3.30 wrong.
   - Explanations sometimes use adult phrasing ("053").
   The parent approved all of these, so they're not blockers. Watch for them, though, and note them in your hand-over.
4. **The parent reviews each `tools/qgen/work/<cell>/review.csv`** (they may edit it in place, as with the pilot). Import only when they say so. Show them any `edit <id>: …` and `skipped:` lines from the import. Then run full ctest in both trees and commit the new pack(s).
5. **Update `docs/PROGRESS.md`** after each cell (stage reached, counts, your notes), so an interruption loses nothing.

## Decisions to raise with the user (don't decide alone)

- **Prompt tuning.** The pilot suggests two small `writer.md` changes: shorter, more concrete sentences for Year 3, and numeric answers in the form a child would naturally type (avoid unit conversions such as dollars to cents). It also suggests a stricter reading-level instruction in `checker.md`. The parent approved the pilot as is, so ask before changing the prompts. If they agree, change the templates only; the scripts render them.
- **The sample packs.** `data/edu/questions/sample-*.json` (39 entries) ship today, and tests use them as fixtures. Once real packs cover all subjects for a year, the user may want samples out of the shipped bank. That means moving them to a test-fixture directory (`src/edu/tests/data/` already exists) and pointing `EDU_SAMPLE_DIR` users there. Ask when Year 3 is complete.
- **Targets.** `spec.json` totals 540 across Years 3–5, above PLAN's 300–500. The implementer chose these numbers; the user hasn't confirmed them.
- **Fractions generator** (TODO.md). Fractions are written questions for now.

## Other open threads (not this chunk)

- **PHASE4-TEST:** the user's remaining real-PC steps, mainly the no-password student (step 11), the pam_access console lock with SDDM still working (step 16), the stock theme failing closed, and the autologin skip.
- **No protocol version** between greeter and daemon. After an upgrade without a reboot the quiz wedges. This is documented in INSTALL-ARCH, with a TODO for a version field in QuizReady.
- **Dev tree install:** `cmake --install build` stops at `/etc/edu-lm` on the Pi (see TODO.md); the greeter screenshot scripts still work.
- **Phase 6** (publish: README, screenshots, licence notes; GPL-2.0-or-later code, a pack licence such as CC BY-SA, ACARA CC BY 4.0 attribution for the codes) comes after the bank.
