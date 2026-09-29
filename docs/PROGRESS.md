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

## Next phases (see PLAN §11)

3. Done. Greeter `QuizClient` (sends `QuizHello` only when the theme opts in; test-mode mock) + kid-friendly edu theme. Check with `tools/dev/greeter-shot.sh`.
4. **Built; waiting on the user's real-PC test** (`docs/PHASE4-TEST.md`). Arch PKGBUILD, a real end-to-end login test on the user's Arch/KDE PC (the first real run of AuthVerifier/PAM), a `nullok` check, a pam_access snippet, and shipping `edu-lm.conf` as comments only (drop-ins are overridden by the main file).
5. The `tools/qgen` Claude question-generation pipeline (Sonnet 5.5 + Batches + a blind verification pass).

## Environment notes (Raspberry Pi, Debian 13, headless, no sudo for Claude)

- `build/`: Debug, Qt6, systemd on. Configured with `-DBUILD_WITH_QT6=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/build/install -DQML_INSTALL_DIR=$PWD/build/install/lib/qml` plus `SYSTEMD_{SYSTEM_UNIT,SYSUSERS,TMPFILES}_DIR` under `build/install/lib/systemd/`. A full build takes about 7 minutes with `-j3`.
- Always also run a **Release** build's ctest (SDDM's default build type; it caught a `Q_ASSERT` side-effect bug). It lives in `build-rel/` (configured like `build/`, git-excluded via `.git/info/exclude`).
- The user runs sudo for any apt installs.
