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

## Next phases (see PLAN §11)

3. **NEXT UP.** Greeter `QuizClient` (sends `QuizHello` only when the theme opts in; test-mode mock) + kid-friendly edu theme. Check with `tools/dev/greeter-shot.sh`.
4. Arch PKGBUILD, a real end-to-end login test on the user's Arch/KDE PC (the first real run of AuthVerifier/PAM), a `nullok` check, a pam_access snippet, and shipping `edu-lm.conf` as comments only (drop-ins are overridden by the main file).
5. The `tools/qgen` Claude question-generation pipeline (Sonnet 5.5 + Batches + a blind verification pass).

## Environment notes (Raspberry Pi, Debian 13, headless, no sudo for Claude)

- `build/`: Debug, Qt6, systemd on. Configured with `-DBUILD_WITH_QT6=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/build/install -DQML_INSTALL_DIR=$PWD/build/install/lib/qml` plus `SYSTEMD_{SYSTEM_UNIT,SYSUSERS,TMPFILES}_DIR` under `build/install/lib/systemd/`. A full build takes about 7 minutes with `-j3`.
- Always also run a **Release** build's ctest (SDDM's default build type; it caught a `Q_ASSERT` side-effect bug). The Release dir used so far was in the session scratchpad and is not persistent; recreate one under `build-rel/`.
- The user runs sudo for any apt installs.
