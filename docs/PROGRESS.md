# Edu-LM progress checkpoint

Last updated: 2026-09-29. Read with `docs/PLAN.md` (the design) to resume.

## Workflow the user wants

- Claude (main session) orchestrates. A **Sonnet** `implementer` subagent builds each phase from a detailed brief, then an **Opus** `reviewer` subagent tries to refute its claims. Fixes go back to the implementer, the orchestrator verifies them independently (builds, tests, source), and only then commits.
- Commit only after review. Repo-local git identity: `arnie <mick.saunders+ant@gmail.com>`.
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
- After each answer: a brief "Correct!" / "Not quite" (plus "Try again" when AnswerAttempts > 1). Full answers only in the failure review.
- Screenshots: the user installs `xdotool`; a script drives the real UI in `--test-mode`.

## Phase 4 (Arch integration): IN PROGRESS on `phase4-wip`

- Brief and claims live in the session scratchpad only; the WIP commit `6c1393d` holds the implementer's work (PKGBUILD in `packaging/arch/`, Arch PAM copies, comments-only `edu-lm.conf`, sysusers groups, pam_access example, `edu-lm-check`, EduCheckTest + drift test, `docs/INSTALL-ARCH.md`, `docs/PHASE4-TEST.md`). 19/19 ctest per the implementer.
- State: Opus review running. Then: fixes, orchestrator verification, squash-merge into `main`, then the user runs `docs/PHASE4-TEST.md` on the Arch PCs.
- User decisions (2026-09-29): source reaches the Arch PC by rsync of the whole repo; one PC runs LightDM, the other has sddm + plasma-login-manager; pam_access ships as an opt-in example; the package creates groups, ships `edu-lm-check`, and defaults the theme to edu (`/usr/lib/sddm/sddm.conf.d/edu-lm.conf`).
- Finding: upstream develop no longer installs PAM services, so the package ships Arch's sddm 0.21.0-7 PAM files verbatim.

## Next phases (see PLAN §11)

3. Done. Greeter `QuizClient` (sends `QuizHello` only when the theme opts in; test-mode mock) + kid-friendly edu theme. Check with `tools/dev/greeter-shot.sh`.
4. **NEXT UP.** Arch PKGBUILD, a real end-to-end login test on the user's Arch/KDE PC (the first real run of AuthVerifier/PAM), a `nullok` check, a pam_access snippet, and shipping `edu-lm.conf` as comments only (drop-ins are overridden by the main file).
5. The `tools/qgen` Claude question-generation pipeline (Sonnet 5.5 + Batches + a blind verification pass).

## Environment notes (Raspberry Pi, Debian 13, headless, no sudo for Claude)

- `build/`: Debug, Qt6, systemd on. Configured with `-DBUILD_WITH_QT6=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/build/install -DQML_INSTALL_DIR=$PWD/build/install/lib/qml` plus `SYSTEMD_{SYSTEM_UNIT,SYSUSERS,TMPFILES}_DIR` under `build/install/lib/systemd/`. A full build takes about 7 minutes with `-j3`.
- Always also run a **Release** build's ctest (SDDM's default build type; it caught a `Q_ASSERT` side-effect bug). It lives in `build-rel/` (configured like `build/`, git-excluded via `.git/info/exclude`).
- The user runs sudo for any apt installs.
