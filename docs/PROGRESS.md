# Edu-LM progress checkpoint

Last updated: 2026-10-02. **Start with `docs/NEXT-SESSION.md`** (the brief for the next chunk), then this file and `docs/PLAN.md` (the design).

## Workflow the user wants

- Claude (main session) orchestrates. A **Sonnet** `implementer` subagent builds each phase from a detailed brief, then an **Opus** `reviewer` subagent tries to refute its claims. Fixes go back to the implementer, the orchestrator verifies them independently (builds, tests, source), and only then commits.
- Commit only after review. Git identity is repo-local (already configured).
- User's CLAUDE.md: brief comments, ask rather than guess, and note problems in the local `TODO.md` (git-excluded).

## Done (on `main`)

| Commit | What |
|---|---|
| `2084ab6` | PLAN.md + `tools/dev/greeter-shot.sh` (headless greeter screenshots via Xvfb, software Qt Quick backend) |
| `9b6aae1` | Phase 1: `edu-core` (question bank, generators, answer checking, QuizSession, cooldown, policy), EduConfig, `edu-lm-validate`, sample packs. Reviewed; 19 fixes applied |
| `0bc6fb6` | Phase 1b: `EndEarly=no\|pass\|fail\|both`; a student gets the union of all matched YearGroups (multi-year). Reviewed clean |

## Phase 2 (daemon quiz gate): DONE, squash-merged into `main`

- It covers: `src/edu/protocol/` (wire protocol), `src/edu/daemon/` (QuizGate, AuthVerifier with a 60 s timeout, GateFactory, the edu-daemon-glue INTERFACE lib), the tests (QuizGateTest, EduProtocolTest, AuthVerifierTest using fake `sddm-helper` scripts in `src/edu/tests/fakehelper/`), and upstream hooks in `src/daemon/{Display,SocketServer}.{h,cpp}` + one token in `src/daemon/CMakeLists.txt` (+41/−1).
- It was reviewed by Opus, and all 11 fixes were applied, including blocker B1 (a helper crash crashed the root daemon). 16/16 ctest in Debug and Release.
- **Never run for real:** the AuthVerifier success path (real PAM), Display's replay closure/startAuth, and the autologin skip. These are checked by review and compile only. They get their first real test in phase 4 on the user's Arch PC.
- Remaining note (TODO.md): upstream's whole-message read assumption on the socket.

## Decisions made with the user

- Password first, then quiz. `StudentPasswords=true/false` is global.
- Swaps are single questions from one shared pool per attempt.
- On failure, show the correct answers, then a forced cooldown in seconds. The password is discarded; the child logs in again.
- Abandoning after ≥1 answer counts as a failure.
- Brand only: internal names stay `sddm` for merge-friendliness. Target is Arch + KDE, a few family PCs, and a public GPL repo later.
- Question bank: local JSON files, root-only (0600/0700). No REST service.

## Phase 3 (greeter + theme): DONE, squash-merged into `main`

- It covers: `src/edu/greeter/` (QuizClient exposed as `eduQuiz`, MockDaemon running the real QuizGate in `--test-mode`, GreeterGlue with `EDU_LM_TEST_*` env vars), `data/themes/edu/`, QuizClientTest, and `tools/dev/greeter-flow.sh` (xdotool-driven screenshots of every screen).
- Protocol additions (replacing a timing heuristic): `QuizReady` answers `QuizHello`; every login the gate sees gets a verdict on the quiz socket before `LoginFailed` (`QuizLoginRefused(wrong-password|busy|refused)`, `QuizCooldown`, `QuizError`, `QuizStarted`, or `QuizStatus(required=false)`); `QuizStatus` carries questionCount/requiredCorrect. One more upstream hook: the `sddm`-user refusal in `Display::login`. The Checking screen offers "Go back" after 10 s.
- Two Opus review rounds (round 1: 2 major + 8 minor; round 2: 1 blocker, a wedged Checking state, + 1 major); all fixed. Orchestrator verified 17/17 ctest in Debug and Release, and viewed the screenshots.
- **Never run for real:** the greeter's real-mode wiring (GreeterProxy + quiz socket against a live daemon). First real run is phase 4.
- Leftover nits are in TODO.md.

## Phase 3 decisions (user, 2026-09-29)

- Look: Breeze-like calm layout with friendly touches (rounded, bigger text and buttons, cheerful accent); colours and background overridable in `theme.conf`.
- Login: an avatar grid of users plus "Other…" for a typed name.
- After each answer: a brief "Correct!" / "Not quite" (plus "Try again" when AnswerAttempts > 1). Full answers only in the failure review. (Superseded 2026-09-30: answers are now shown on the spot, see the answer-reveal branch.)
- Screenshots: the user installs `xdotool`; a script drives the real UI in `--test-mode`.

## Phase 4 (Arch integration): BUILT, squash-merged into `main`; awaiting the user's real-PC test

- It covers: `packaging/arch/PKGBUILD` (mirrors Arch's sddm 0.21.0-7, Qt6 only; builds from the rsync'd checkout, no network), `packaging/arch/pam/` (upstream develop no longer installs PAM services, so these are Arch's files verbatim), comments-only `/etc/edu-lm/edu-lm.conf` (+ `conf.d/`, `questions.d/` 0700), `/usr/lib/sddm/sddm.conf.d/edu-lm.conf` (`Current=edu`), sysusers groups (`edu-students`, `edu-year3..5`), the opt-in pam_access example (`-:(edu-students):tty1 … tty12`), the `edu-lm-check` diagnostics tool (EduCheckTest, 35 cases), a drift test for the comments-only config, `docs/INSTALL-ARCH.md` and `docs/PHASE4-TEST.md`.
- One Opus review (1 major: the rollback used `pacman -Rns`, which fails with sddm-kcm installed; 8 minor; 6 nits); all fixed. Orchestrator verified 19/19 ctest in Debug and Release, a mutation check, and edu-lm-check against the reviewer's fixtures.
- Research (from source and Arch package files): pam_access sees origin `sddm` at SDDM's account stage (PAM_TTY is set only at openSession; the helper's stdin is a pipe), so a ttyN rule can't block SDDM; kscreenlocker never runs the account stage. plasma-login-manager does not conflict with sddm; only one unit can own `display-manager.service`. sddm-kcm hard-depends on `sddm` (satisfied by `provides`).
- User decisions (2026-09-29): source goes to the Arch PC by rsync of the whole repo; one PC runs LightDM, the other has sddm + plasma-login-manager; pam_access ships opt-in; the package creates groups, ships `edu-lm-check`, and defaults the theme to edu.
- **Phase 4 is done when:** the user runs `docs/PHASE4-TEST.md` on an Arch PC and confirms a real login round-trip. Still never run for real: everything listed under phases 2 and 3, plus the package install itself (only simulated on the Pi).

## Answer reveal (after the first real test, 2026-09-30): DONE, squash-merged into `main`

- First real run on one Arch PC: package installed; a normal user and an `edu-students,edu-year3` child logged in, the child passed 4/5 through real PAM and the replay. Remaining PHASE4-TEST steps not yet reported.
- makepkg's check() failed QuizGateTest on x86_64 (GCC 16 + LTO reordered `startQuiz() + runToEnd()`); fixed in `d0cf368`. A Pi build with Arch's makepkg flags + LTO passes 19/19.
- User decision: show the right answer **on the spot** after a finished wrong question (supersedes "answers only in the failure review"). `QuizFeedback` carries `revealed/correctAnswer/explanation`; the daemon waits for a new `QuizNext` before the next question/result/login replay; Next is inert for 700 ms and ignores key auto-repeat (a double or held Enter skipped it); a failed quiz's cooldown is recorded at the failing answer. Follows `ShowCorrectAnswers`.
- One Opus review (2 major: double-Enter skip, new greeter vs old running daemon wedges; 4 minor; nits); all fixed. The version mismatch is docs-only for now ("reboot or restart sddm after installing/upgrading"); a protocol version is in TODO.md. Orchestrator verified 19/19 Debug + Release, a guard mutation, and the double/held-Enter repro screenshots.

## Phase 5 (question bank): 5a DONE; 5b DONE: Years 3, 4 and 5 imported (523 real questions)

- User has **no Anthropic API access** (2026-09-30), so PLAN §8's Batches pipeline is replaced: a Claude Code session runs the `/qgen` project skill (`.claude/skills/qgen/`), spawning Sonnet subagents to write and to blind-check; stdlib-only Python scripts in `tools/qgen/` do ingest, validate (via `edu-lm-validate`), blind stripping, compare, CSV export for the parent, and import of approved rows into `data/edu/questions/`.
- User decisions (2026-09-30): Sonnet for both writer and checker; pilot first (Year 3 maths, ~20 entries), review, tune prompts, then scale; maths about half generator entries (there is no fractions generator); workflow as a project skill.
- 5a (implementer, brief in the session scratchpad): the scripts + tests, `tools/qgen/spec.json` (ACARA v9 codes, verified or marked unverified), the skill and its writer/checker prompts, PLAN §8/§13 rewrite. 5b (orchestrator): the Year 3 maths pilot via the skill, then the parent's CSV review.
- 5a result: `tools/qgen/qgen.py` (cells, prompt, ingest, validate, blind, compare, export, import), `tools/qgen/spec.json` (36 cells, 193 ACARA v9 descriptors, all verified against ACARA's curriculum workbook; own-words check <= 6 shared words), `.claude/skills/qgen/` (SKILL.md, writer.md, checker.md), `edu-lm-validate --sample N`. One Opus review (4 major: batch-2 id reuse, mangled edit rows, stale checker files, dropped flags; 8 minor; nits); all fixed. Orchestrator verified 125 Python tests, 22/22 ctest Debug + Release, the batch-2 and mangled-edit scenarios, and the spec check.
- 5b pilot run 2026-09-30: `y3-maths-number` drafted (20 entries: 10 written, 10 generators), validated first time (0 errors, 0 flags), blind check 10/10 agree, 0 flagged; exported to `tools/qgen/work/y3-maths-number/review.csv` (git-ignored). Parent approved all 20 with no edits on 2026-10-01; imported as `data/edu/questions/maths-y3-number.json` (commit `b683fdc`, which also made tests use only `sample-*.json` as fixtures). Orchestrator's notes for tuning: m3-num-0005 reading level too high (checker didn't flag it, so a Sonnet checker is lenient on reading level); m3-num-0007 asks for change in cents (typing 3.30 would be marked wrong); m3-num-0001 explanation wording; AC9M3N04 only covered by generators.
- **5b (original plan):** run `/qgen` for Year 3 maths (number cell first), ~20 entries, about half generators; ask the user before starting (uses their Claude Code allowance). Then the user reviews `tools/qgen/work/<cell>/review.csv` and we tune writer.md/checker.md.
- **5b Year 3 wave (started 2026-10-01, branch `qgen-y3-wave`):** user go-ahead for all 11 remaining Year 3 cells in one sitting, checkpointing per cell; keep spec.json targets (540 total); prompts tuned first (writer: short concrete sentences, child-typed numeric answers, plain explanations; checker: strict reading level, unit-form ambiguity), commit `9fb819d`. Per-cell progress:
  - (none yet; `python3 tools/qgen/qgen.py cells --year 3` shows each cell's stage)
  - y3-maths-algebra: exported (12: 6 written, 6 generators; validate 0/0 first time; blind 6/6 agree, 0 flagged). Orchestrator read all: no concerns; the 10 times table has no generator (the Year 3 multiply range caps a at 5).
  - y3-maths-measurement: exported (16 written; validate 0/0 first time; blind 16/16 agree, 1 flagged: m3-mea-0001 mm vs cm for a pencil). Orchestrator: m3-mea-0011 says the hour hand is "just past the 3" at 3:30 (it is halfway to 4).
  - y3-maths-space: exported (12 written; validate 0/0 first time; blind 12/12 agree, 0 flagged). Orchestrator: m3-spa-0004 opens with an awkward "A cube has corners."
  - y3-maths-statistics-probability: exported (14 written; validate 0/0 first time; blind 14/14 agree, 1 flagged: m3-sta-0014 ambiguous + reading_level). Orchestrator: m3-sta-0004 gives the answer away ("categories, like colours"); m3-sta-0014 "4 five times, 5 four times" is hard to parse (the checker agreed).
  - y3-science-understanding: exported (20 written; validate 0 errors, 1 flag: s3-und-0011 near-duplicate of sample sm-s-001; blind 20/20 agree, 1 flagged: s3-und-0005 reading_level). Orchestrator: s3-und-0005 is a negated true/false ("Fire is not a living thing, even though...").
  - y3-hass-knowledge: exported (20 written; validate 0/0 first time; blind 20/20 agree, 2 flagged: h3-kno-0014 and h3-kno-0015 reading_level). Orchestrator: h3-kno-0014 uses "looked-after feature" undefined (ACARA's term is "managed"); h3-kno-0012 puts First Nations peoples in the past tense ("used"); h3-kno-0010/0011/0020 the right answer is the long, detailed choice (guessable).
  - y3-english-language: exported (20 written; validate 0/0 first time; blind 20/20 agree, 1 flagged: e3-lan-0004 ambiguous). Orchestrator: e3-lan-0004 asks "which word" but the choices are sentences; e3-lan-0003 calls modal strength a "feeling".
  - y3-science-inquiry-endeavour: exported (10 written; validate 0/0 first time; blind 10/10 agree, 1 flagged: s3-inq-0009 reading_level). Orchestrator: s3-inq-0008 (car count by colour) repeats m3-sta-0003 and s3-inq-0007 (fruit votes, tallest column) repeats m3-sta-0005 across cells; validate only compares with imported packs.
  - y3-hass-skills: exported (10 written; validate 0/0 first time; blind 10/10 agree, 0 flagged). Prompts carry line breaks for timelines and tables (plain QML Text, WordWrap; not yet seen on screen).
  - y3-english-literature: exported (12 written; validate 0/0 first time; blind 12/12 agree, 0 flagged). Orchestrator: e3-lit-0007 the answer "He is always grumpy" overreaches the simile; e3-lit-0001 needs to know Bondi is in Sydney.
  - y3-english-literacy: exported (14 written; validate 0 errors, 1 flag: e3-lty-0011 near-duplicate of sample sm-e-002; blind 14/14 agree, 0 flagged).
  - **Wave result (2026-10-01):** all 11 cells exported, 160 entries (154 written, 6 generators), 22 subagent runs, no validation retries, blind check 154/154 agree, 7 flagged. The Sonnet checker flagged reading level more often with the stricter prompt (5 vs 0 in the pilot). Parent reviewed all 11 on 2026-10-01: 159 approved, 1 rejected (h3-kno-0014, "looked-after feature", agreeing with the checker), 1 edited (m3-mea-0008: 10 -> 30 seconds; they marked it `y`, so import skipped it until the cell was set to `edit` with their OK). Imported; the bank is 218 entries (39 samples + 179 real). ctest 22/22 Debug + Release, 125 qgen tests OK. Squash-merged into `main`.
- **User decisions (2026-10-01, after Year 3):** keep the sample packs in the shipped bank. From Year 4 on, the user steps out of the CSV review: the orchestrator reads every pack, marks approve y/n/edit in a reviewed copy (`tools/qgen/work/<cell>/reviewed.csv`; `review.csv` is kept as exported), imports, and records every n/edit here. To catch cross-cell repeats, each cell is imported before the next is validated.
- **5b Year 4 wave (started 2026-10-01, branch `qgen-y4-wave`):** 12 cells, 180 entries. Per-cell progress:
  - y4-maths-algebra: imported 12/12 (6 generators; validate 0/0; blind 6/6 agree, 0 flagged).
  - y4-maths-number: imported 18/20 (validate 0/0; blind 10/10, 0 flagged). Rejected m4-num-0017/0018: same generator and params as m4-alg-0010/0011 (validate doesn't catch cross-pack generator repeats; TODO.md).
  - y4-maths-measurement: imported 16/16 (validate 0/0; blind 16/16, 0 flagged).
  - y4-maths-space: imported 12/12 (validate 0/0; blind 12/12, 0 flagged). Edited m4-spa-0008: added which way the grid letters and numbers run (moving "up" was ambiguous).
  - y4-maths-statistics-probability: imported 13/14 (validate 0/0; blind 14/14, 1 flagged: m4-sta-0009 reading_level on independent events, kept: it is the descriptor's concept). Rejected m4-sta-0012: near-verbatim copy of Year 3 m3-sta-0013 (coin 10 times, 6 heads). Kept m4-sta-0011 (rolling a 7): same idea as m3-sta-0011 but a different question.
  - y4-science-understanding: imported 20/20 (validate 0/0; blind 20/20, 0 flagged).
  - y4-hass-knowledge: imported 20/20 (validate 0/0; blind 20/20, 0 flagged).
  - y4-science-inquiry-endeavour: imported 9/10 (validate 0/0; blind 10/10, 0 flagged). Rejected s4-inq-0003: same thermometer question as m4-mea-0003 (validate missed it).
  - y4-hass-skills: imported 9/10 (validate 1 flag; blind 10/10, 0 flagged). Rejected h4-ski-0003: word-for-word copy of h3-ski-0005 (flagged by validate).
  - y4-english-language: imported 20/20. First draft failed validate (e4-lan-0020, a capitalisation question whose choices differed only in case); the retry passed (0 errors, 2 flags: near-duplicates of samples sm-e-004/008, kept because they use different words or years). Blind 20/20, 0 flagged. Edited e4-lan-0004: "Which word" -> "Which sentence" (the choices are sentences).
  - y4-english-literature: imported 12/12 (validate 0/0; blind 12/12, 1 flagged). Edited e4-lit-0010: the line "The wind whispered..." is also alliteration (the checker's flag), so it became "The breeze whispered in the gum trees."
  - y4-english-literacy: imported 12/14 (validate 0/0; blind 14/14, 0 flagged). Rejected e4-lty-0009 (kangaroo syllables) and e4-lty-0011 (hop + ing): near-verbatim copies of Year 3 e3-lty-0007 and e3-lty-0010.
  - **Wave result (2026-10-01):** all 12 cells imported, 173 of 180 kept (7 rejected as duplicates, 3 small edits), 1 validation retry, 25 subagent runs. Blind check: 174/174 written answers agree, 3 flagged. The bank is 391 entries (39 samples + 352 real). ctest 22/22 Debug + Release, 125 qgen tests OK. Squash-merged into `main`.
  - Review rule used from here: reject near-verbatim copies (same scenario and numbers) of any question in the bank; keep the same idea asked differently.
- **5b Year 5 wave (started 2026-10-01, branch `qgen-y5-wave`):** user go-ahead; 12 cells, 180 entries; reviewed by the orchestrator as for Year 4. Per-cell progress:
  - y5-maths-algebra: imported 12/12 (validate 0/0; blind 6/6, 0 flagged). Edited m5-alg-0005's muddled explanation.
  - y5-maths-measurement: imported 15/16 (validate 0/0; blind 16/16, 0 flagged). Rejected m5-mea-0013 (120 degrees is obtuse): near-verbatim copy of m4-mea-0013.
  - y5-maths-number: imported 19/20 (validate 1 flag: m5-num-0001 vs m3-num-0001, kept since one is decimals and one whole numbers; blind 10/10, 0 flagged). Rejected m5-num-0015 (generator almost identical to m5-alg-0011). Rewrote the explanations of m5-num-0001 and m5-num-0005.
  - y5-maths-space: imported 11/12 (validate 1 flag; blind 12/12, 0 flagged). Rejected m5-spa-0012: a near-copy of m5-spa-0011 (flagged by validate).
  - y5-maths-statistics-probability: imported 14/14 (validate 0/0; blind 14/14, 0 flagged).
  - y5-science-understanding: imported 20/20 (validate 0/0; blind 20/20, 0 flagged).
  - y5-hass-knowledge: imported 20/20 (validate 0/0; blind 20/20, 0 flagged). Edited h5-kno-0003: Moreton Bay in 1824 was a convict settlement, not a colony (Queensland became a colony in 1859). Edited h5-kno-0007: Stuart's 1862 expedition "crossed the continent and returned safely" rather than "the first European one to succeed", since Burke and Wills reached the Gulf in 1861 but died on the way back.
  - y5-science-inquiry-endeavour: imported 9/10 (validate 0/0; blind 10/10, 0 flagged). Rejected s5-inq-0009: same sunlight-bean-plant result (12 cm vs 3 cm) as s3-inq-0010.
  - y5-hass-skills: imported 10/10 (validate 0/0; blind 10/10, 0 flagged).
  - y5-english-language: imported 18/20 (validate 1 flag; blind 20/20, 0 flagged). Rejected e5-lan-0010 (near-copy of e4-lan-0009, flagged) and e5-lan-0001 (the same "Dear Mrs Nguyen, may I please..." item as e4-lan-0001).
  - y5-english-literature: imported 11/12 (validate 0/0; blind 12/12, 0 flagged). Rejected e5-lit-0009: "The wind whispered..." is also alliteration (the problem fixed in e4-lit-0010) and it repeats that item. Edited e5-lit-0002: 1855 -> 1853, since monthly gold licences gave way to the Miner's Right in 1855.
  - y5-english-literacy: imported 12/14 (validate 0/0; blind 14/14, 0 flagged). Rejected e5-lty-0001 (the same kerosene-lamp-and-horse setting as e5-lit-0001) and e5-lty-0003 (the same school veggie garden persuasive line as e4-lty-0003).
  - **Wave result (2026-10-02):** all 12 cells imported, 171 of 180 kept (9 rejected as duplicates or ambiguous, 6 edits: 3 explanations, 3 factual fixes), no validation retries. Blind check: 174/174 written answers agree, 0 flagged. The bank is 562 entries (39 samples + 523 real across Years 3–5). ctest 22/22 Debug + Release, 125 qgen tests OK. Squash-merged into `main`.
- Phase 4 real-PC testing: the user is not testing on 2026-09-30; PHASE4-TEST steps beyond the first login and the answer reveal are still open.

## Protocol version (2026-10-02): DONE, squash-merged into `main`

- User decision: one-sided check. The greeter's `QuizHello` is unchanged, because an old daemon would read any extra bytes as SDDM message ids (2 = PowerOff). A new daemon sends `QuizVersion(EduProtocolVersion = 2)` just before `QuizReady`. A new greeter that gets `QuizReady` with no version, or a different version, shows "The quiz was updated. Please restart the computer.", closes the quiz socket and stays disconnected. Students then fail closed through the non-quiz-theme path, and parents still log in. An old greeter with a new daemon can't read `QuizVersion`, so it drops the connection and shows "Lost the connection to the quiz".
- Also: the greeter sends nothing but `QuizHello` until the version is confirmed (the outbox waits for a matching `QuizReady`). Frozen forever: `QuizHello`, `QuizVersion`'s id and payload, and the Version-before-Ready order. The banner's OK button is hidden while mismatched. Test mode: `EDU_LM_TEST_PROTOCOL_VERSION=<n>` shows the mismatch screen.
- One Opus review (0 blocker or major; 3 minor: queued `QuizQuery` sent before the version check, stale daemon info text, untested resets; plus nits). All fixed, and the re-review was clean apart from 2 nits, which the orchestrator fixed. The reviewer ran a cross-version harness with the real gate: an old daemon gets only the hello, students are refused, parents log in, and an old greeter with a new daemon shows "Lost the connection" with no reconnect storm. Orchestrator verified 22/22 ctest in Debug and Release and screenshots of the normal and mismatch screens. (`greeter-shot.sh` runs `build/install`, so run `cmake --install build` first; it still stops at `/etc/edu-lm` but installs the greeter.)

## Phase 6 (publish): DONE 2026-10-02, squash-merged into `main`; the user pushes

- User decisions (2026-10-02): question packs CC BY-SA 4.0; the edu theme's QML MIT (like upstream's QML; `metadata.desktop.in` said GPL); README.md becomes Edu-LM's own and upstream's moves to `README.sddm.md` (linked); no CSV-to-pack tool; a user guide `docs/QUESTIONS.md` with examples of adding custom questions via `/etc/edu-lm/questions.d/`, plus an installed example pack. Claude prepares everything ready to push (merged to `main`, with a checklist); **the user creates the public repo and pushes**, Claude touches no remote.
- Gaps the guide must cover (found 2026-10-02): the format is only in PLAN §6; no example pack; packs must be root 0600 in 0700 dirs; ids are unique across all dirs (a clashing user id is skipped, a shipped question can't be replaced); a new subject needs `Subjects=`; years other than 3-5 need a group plus `YearGroups=`. Packs are reloaded at each quiz start (no restart needed).

- Phase 6 built on branch `phase6-publish` (2026-10-02, Sonnet implementer; the brief is in the session scratchpad): README.md rewritten (upstream's is README.sddm.md), `docs/QUESTIONS.md`, `docs/PUBLISH.md`, `data/edu/examples/example-pack.json` (installed to `share/edu-lm/examples/`, test `QuestionBankTest::examplePackIsClean`), licence files `LICENSE.MIT` and `LICENSE.CC-BY-SA-4.0` (exact text from creativecommons.org), SPDX MIT headers and `License=MIT` for the edu theme, `data/edu/questions/README.md` (pack licence note; not installed or loaded), PKGBUILD licences, five screenshots in `docs/screenshots/`. ACARA's terms page was read in full on 2026-10-02 and its attribution wording is in the README (see `tools/qgen/spec-sources.md`). Still to do: the user reviews, sets `Website=`, creates the repo and pushes (`docs/PUBLISH.md`).
- One Opus review (0 blocker, 1 major: the guide's `sudo chmod …/questions.d/*.json` fails because the user's shell can't expand a glob in a 0700 dir, now `sudo find … -exec`; 6 minor: README overstated the console lock (`su`), every non-hidden drop-in file is read (backups like `x.conf~` or `family-old.json` win), an ambiguous walk-through question, a place_value spacing example, ACARA's citation includes the no-endorsement paragraph, PUBLISH.md's remote facts; nits incl. the README licence table and example-003). All fixed; new ctest `QuestionsDocJson` (`src/edu/tests/check_docs.py`) keeps QUESTIONS.md's JSON equal to the example pack and the walk-throughs valid.
- User decisions in the fix round (2026-10-02): ACARA accessed date 30 September 2026; the git identity removed from tracked docs (it stays in commit history, see PUBLISH.md §3); upstream's `.github/workflows/` deleted. The orchestrator also added `.claude/settings.local.json` to `.gitignore` (one upstream line). Licence choices made without the user: docs and `.claude/skills/` GPL-2.0-or-later, screenshots CC BY-SA 4.0, copyright holder "Edu-LM contributors".
- Orchestrator verified 24/24 ctest in Debug and Release, 125 qgen tests, the example pack and bank 0 errors/0 warnings, no identity or `/home` paths in tracked files, the README against the files, and the screenshots.

## Next phases (see PLAN §11)

3. Done. Greeter `QuizClient` (sends `QuizHello` only when the theme opts in; test-mode mock) + kid-friendly edu theme. Check with `tools/dev/greeter-shot.sh`.
4. **Built; waiting on the user's real-PC test** (`docs/PHASE4-TEST.md`). Arch PKGBUILD, a real end-to-end login test on the user's Arch/KDE PC (the first real run of AuthVerifier/PAM), a `nullok` check, a pam_access snippet, and shipping `edu-lm.conf` as comments only (drop-ins are overridden by the main file).
5. The `tools/qgen` Claude question-generation pipeline (Sonnet 5.5 + Batches + a blind verification pass).

## Environment notes (Raspberry Pi, Debian 13, headless, no sudo for Claude)

- `build/`: Debug, Qt6, systemd on. Configured with `-DBUILD_WITH_QT6=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/build/install -DQML_INSTALL_DIR=$PWD/build/install/lib/qml` plus `SYSTEMD_{SYSTEM_UNIT,SYSUSERS,TMPFILES}_DIR` under `build/install/lib/systemd/`. A full build takes about 7 minutes with `-j3`.
- Always also run a **Release** build's ctest (SDDM's default build type; it caught a `Q_ASSERT` side-effect bug). It lives in `build-rel/` (configured like `build/`, git-excluded via `.git/info/exclude`).
- The user runs sudo for any apt installs.
