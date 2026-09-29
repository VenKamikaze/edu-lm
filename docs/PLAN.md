# Edu-LM — Implementation Plan (draft 2)

**Status:** updated with the decisions from the 2026-09-29 Q&A. Based on upstream SDDM `develop` (its default branch) at `da0b64d` (2026-08-19).
Remaining open items are in §13.

---

## 1. Summary

Edu-LM is SDDM plus a **quiz gate**. Users in a configured group must log in (with or without a password, set globally), then answer N questions with at least K correct. They can swap individual questions for new ones, drawing on a shared pool of R swaps. After a failed attempt they see the correct answers and wait out a cooldown before trying again. Everyone else logs in normally.

**Target:** Arch Linux + KDE Plasma (Qt6 SDDM), a few family PCs. It should also work on any distro that ships SDDM (§10).
**Scope:** functional first, with light but useful tests on the gate logic. The code will be published later as a public GPL-2.0-or-later fork, which follows from the SDDM licence.

Key design choices:

- **The daemon enforces the quiz, not the theme.** The root daemon holds the question bank, checks answers and counts swaps. The greeter never sees answer keys. With a theme that has no quiz support, quiz users can't log in (it fails closed), and nobody can skip the quiz.
- **Local JSON question files**, readable only by root. No network is involved at login.
- **Minimal upstream diff:** new code lives in `src/edu/`, `data/themes/edu/` and `tools/`, and SDDM files get small hooks marked `// EDU-LM:`. Internal names stay `sddm`; only the package, theme and docs are branded Edu-LM.

---

## 2. How SDDM is put together (what matters for us)

```
 sddm (daemon, root)                         sddm-greeter (user "sddm", QML UI)
 ┌──────────────────────────────┐   local   ┌──────────────────────────────┐
 │ Display  ── one per seat/VT  │  socket   │ GreeterApp                   │
 │   SocketServer ◄─────────────┼───────────┼─► GreeterProxy  ("sddm" in QML)│
 │   Auth ──► sddm-helper (PAM) │  QDataStream  UserModel/SessionModel     │
 │   Greeter (spawns greeter)   │           │ Theme Main.qml               │
 └──────────────────────────────┘           └──────────────────────────────┘
```

- **Protocol** (`src/common/Messages.h`) is a flat enum over a `QDataStream`. The greeter sends `Connect`, `Login(user, password, session)` and power commands. The daemon replies with `LoginSucceeded`, `LoginFailed` and `InformationMessage`.
- **Every greeter login goes through `Display::login()`** (`src/daemon/Display.cpp:331`), which already refuses logins as the `sddm` user. That is our main hook.
- **The socket is per-Display and accepts several connections.** The greeter opens a second connection for quiz traffic, so `GreeterProxy` doesn't change.
- **Unknown messages** fall into `default:` in `SocketServer::readyRead()` (`SocketServer.cpp:186`). That is the delegation point for the quiz messages.
- **Config macros** (`src/common/Configuration.h`) can define a second config object backed by its own file, as `StateConfig` does. `EduConfig` uses them without touching that header.
- **Verify-only PAM:** `Auth::start()` with no session path doesn't pass `--start`. `sddm-helper` then authenticates, reports the result and exits without opening a session (`Auth.cpp:370`, `HelperApp.cpp:163-187`). That gives us a password check before the quiz using only existing code.
- **Greeter context properties** are set in `GreeterApp.cpp:187-194`. One extra line exposes `eduQuiz` to QML.
- **Test modes:** `sddm-greeter --test-mode --theme <dir>` needs no daemon, and the daemon has `--test-mode` (Xephyr).

---

## 3. Architecture

### 3.1 New components

| Component | Where | Runs in | Job |
|---|---|---|---|
| `edu-core` lib | `src/edu/core/` | anywhere | Quiz rules and the question bank. Depends only on **QtCore**, with no SDDM types, so it could later back a LightDM greeter or a PAM module (§10) |
| `QuestionBank` | `src/edu/core/QuestionBank.{h,cpp}` | daemon | Loads and validates JSON packs; filters by year and subject; runs maths generators |
| `QuizSession` | `src/edu/core/QuizSession.{h,cpp}` | daemon | One attempt: N questions, answers, shared swap pool, pass/fail, cooldown deadline. Pure logic with unit tests |
| `EduConfig` | `src/edu/EduConfiguration.{h,cpp}` | daemon + greeter | Reads `/etc/edu-lm/edu-lm.conf` (+ `.conf.d/`) using SDDM's config macros |
| `QuizGate` | `src/edu/daemon/QuizGate.{h,cpp}` | daemon | Per-Display: intercepts logins, runs the verify-only `Auth`, owns the `QuizSession`, replays the login on a pass |
| `EduMessages` | `src/edu/EduMessages.h` | both | Quiz message IDs, numbered from `0x45440000` ("ED") so they can't collide with upstream enums |
| `QuizClient` | `src/edu/greeter/QuizClient.{h,cpp}` | greeter | QML object `eduQuiz` on a second socket connection. In `--test-mode` it runs a local `QuizSession` over a sample bank so the theme can be built without a daemon |
| Edu theme | `data/themes/edu/` | greeter | Child-friendly login screen, based on the Breeze look so it fits KDE, plus a reusable `EduQuiz.qml` component |
| `edu-lm-validate` | `src/edu/tools/` | admin CLI | Checks question packs against the schema |
| Question generator | `tools/qgen/` (Python) | dev machine | Offline Claude pipeline that drafts packs (§8) |

### 3.2 Upstream hooks (the full list of changes to SDDM files)

| File | Change | ~Lines |
|---|---|---|
| `src/CMakeLists.txt` | `add_subdirectory(edu)` | 1 |
| `src/daemon/CMakeLists.txt`, `src/greeter/CMakeLists.txt` | link `edu-daemon` / `edu-greeter` | 2 |
| `src/daemon/Display.{h,cpp}` | own a `QuizGate`. At the top of `login()`: `if (m_quizGate->intercept(socket, user, password, session)) return;`. Give the gate a callback into `startAuth()` for replay | ~12 |
| `src/daemon/Display.cpp` (autologin) | skip autologin for quiz users | ~3 |
| `src/daemon/SocketServer.{h,cpp}` | `setExtension()`, and in `default:` `if (m_ext && m_ext->handle(socket, message, input)) break;` | ~8 |
| `src/greeter/GreeterApp.cpp` | create `QuizClient`; `setContextProperty("eduQuiz", …)` | ~4 |
| `data/themes/CMakeLists.txt` | install the `edu` theme | 1 |

The password-first flow lives almost entirely inside `QuizGate`, which has its own `Auth` instance, so `Display` grows only by the lines above. `tools/check-hooks.sh` lists every `// EDU-LM:` marker so each upstream merge is quick to audit.

### 3.3 Quiz protocol (second socket connection)

Greeter → daemon: `QuizHello`, `QuizQuery(user)`, `QuizAnswer(questionId, answer)`, `QuizSwap(questionId)`, `QuizAbandon`

Daemon → greeter: `QuizReady` (reply to `QuizHello`), `QuizStatus(user, required, studentPasswords, cooldownSecs, questionCount, requiredCorrect)`, `QuizLoginRefused(reason)` (`wrong-password`, `busy` or `refused` for the reserved `sddm` user), `QuizStarted(user, n, k, maxSwaps)`, `QuizQuestion(id, index, count, prompt, type, choices[], swapsLeft, attemptsLeft)`, `QuizFeedback(id, correct, attemptsLeft, finished)`, `QuizSwapRefused(reason)`, `QuizResult(passed, correct, required, asked, review[], cooldownSecs)`, `QuizCooldown(secs)`, `QuizError(message)`

Message IDs start at `0x45440000`. `QuizHello` marks a connection as quiz-capable; the greeter sends it only when the theme opts in, so a stock theme still fails closed. The daemon answers it with `QuizReady`.

The login result (`LoginFailed`) travels on the other socket and can be read before or after the quiz messages, so the daemon writes a **verdict** on the quiz socket, before any `LoginFailed`, for every login the gate sees: `QuizStatus(required=false)` for a user with no quiz, `QuizLoginRefused` for a wrong password or a busy gate, `QuizCooldown`, `QuizError` for a broken configuration, or `QuizStarted`. The greeter reads a `LoginFailed` only in the light of that verdict, with no timers. Logins with no possible verdict are those from a greeter with no ready quiz connection (refused by design: fail closed) and the few upstream paths that send no `LoginFailed` at all; for those the theme offers a "Go back" escape from its Checking screen after ten seconds, and `QuizAbandon` discards any login the gate still holds.

`review[]` is sent only after a failed attempt. It lists each missed question with the correct answer and an optional explanation. Answer keys are never sent before an answer is given.

---

## 4. Configuration — `/etc/edu-lm/edu-lm.conf`

```ini
[Quiz]
Enabled=true
Groups=edu-students         # users in these groups get the quiz
Users=                      # optional explicit list
ExemptUsers=                # parents/admin

StudentPasswords=true       # false = quiz users log in with no password (§5.2)
QuestionCount=5             # N
RequiredCorrect=4           # K  (K <= N, checked at startup)
MaxSwaps=3                  # R  shared pool of single-question swaps per attempt
ShowCorrectAnswers=true     # on failure, show missed questions with answers
FailCooldownSeconds=60      # forced wait after a failed attempt
AnswerAttempts=1            # tries per question before it counts as wrong
EndEarly=no                 # no | pass (stop once K correct) | fail (stop once K is unreachable) | both

[Content]
QuestionDirs=/usr/share/edu-lm/questions,/etc/edu-lm/questions.d
Subjects=maths,science,hass,english
YearGroups=edu-year3:3,edu-year4:4,edu-year5:5   # union of every group the user is in
DefaultYear=4
```

The cooldown is enforced by the daemon and tracked per user, so restarting the greeter or picking another user and coming back doesn't reset it. It is held in memory only, so a reboot or daemon restart clears it. Admin setup is `usermod -aG edu-students,edu-year3 alice`. A child working above their year joins two year groups (e.g. `edu-year4,edu-year5`) and gets questions from both.

---

## 5. Login flow — password first, then quiz

### 5.1 Students with passwords (`StudentPasswords=true`)

```
Theme                 GreeterProxy / QuizClient        QuizGate (daemon)                Display
 │ user + password ──► sddm.login() ─────────────────────────────────────────────────► login()
 │                                                    intercept()? ◄───────────────── hook
 │                                                    not a quiz user → false → normal startAuth()
 │                                                    quiz user:
 │                                                      cooldown active? → QuizCooldown(s)
 │                                                      stash {socket,user,pw,session}
 │                                                      verify-only Auth (PAM, no session)
 │ ◄── LoginFailed (wrong password: same as stock SDDM) ◄┤ fail
 │ ◄── eduQuiz.quizRequired ◄── QuizRequired ◄─────────┤ ok → new QuizSession
 │ answer / swap ──────────► QuizAnswer / QuizSwap ───► score, swapsLeft--
 │ ◄── QuizQuestion / QuizFeedback ◄──────────────────┤
 │                                                    passed → replay stashed login → startAuth()
 │ ◄── LoginSucceeded ◄───────────────────────────────────────────────────────── (normal path)
 │                                                    failed → QuizResult(review[]) + cooldown +
 │                                                             LoginFailed; password discarded, so
 │                                                             the child logs in again afterwards
```

- The stashed password is kept only in daemon memory (SDDM already does this in `m_passPhrase`). It is wiped on pass, on failure, on `QuizAbandon` (the child presses Back) and on greeter disconnect.
- Abandoning a quiz after at least one answer attempt counts as a failure (cooldown applies), so a child can't dodge the cooldown or re-roll questions indefinitely. Abandoning before answering anything costs nothing.
- A theme without quiz support gets the standard `LoginFailed` plus `InformationMessage("This account needs the Edu-LM login screen")`, so it fails closed.

### 5.2 Students without passwords (`StudentPasswords=false`)

- The theme hides the password field for quiz users. The flow is otherwise identical, with an empty password.
- PAM has to accept empty passwords for those accounts (`passwd -d <student>` plus `nullok` on `pam_unix`). Arch's `system-auth` is expected to include `nullok` already, but **this must be verified on the Arch machine** before relying on it. If it doesn't, we ship a small `/etc/pam.d/sddm` adjustment in packaging and document it.

---

## 6. Question format

One JSON file per pack, e.g. `maths-y3.json`:

```json
{
  "schema": 1,
  "pack": "maths-y3",
  "subject": "maths",
  "questions": [
    {
      "id": "m3-0042",
      "years": [3],
      "curriculum": "AC9M3N01",
      "type": "multiple_choice",
      "prompt": "Which number is 1000 more than 4 527?",
      "choices": ["4 627", "5 527", "14 527", "4 537"],
      "answer": 1,
      "difficulty": 2,
      "explanation": "Adding 1000 changes the thousands digit: 4 → 5."
    },
    {
      "id": "m-gen-mul",
      "years": [3, 4],
      "type": "generator",
      "generator": "multiply",
      "params": { "a": [2, 10], "b": [2, 10] }
    }
  ]
}
```

Types:
- `multiple_choice`: the main type.
- `true_false`.
- `numeric`: with optional `tolerance`.
- `short_text`: `accept: [...]`; checking ignores case and whitespace.
- `generator`: procedural maths (add, subtract, multiply, divide-with-remainder, place value, simple fractions).

A swap draws a question that hasn't appeared yet in this attempt. Files are root-owned with mode 0600/0700 dirs, and only the daemon reads them.

---

## 7. Local files, not a REST service

At family-PC scale, a service adds a network dependency and forces a bad choice when it's down: skip the quiz, or lock the child out. It also adds attack surface to a root daemon, and brings no real benefit. Plain JSON is also easy for a parent to review and edit, and it diffs cleanly in git. Qt already parses JSON, so no new dependency is needed.

Sharing packs between the family PCs is a `git pull` or `rsync` into `/etc/edu-lm/questions.d`. A sync timer and a central results store stay on the "later" list (§11).

---

## 8. Generating the question bank with Claude

Pipeline (`tools/qgen/`, Python, run offline on a dev machine):

1. **Spec matrix:** subject × year (3–5) × strand, keyed to ACARA v9 content descriptors. The questions are original, written to each descriptor, not copied from curriculum materials.
2. **Generate** with **Claude Sonnet 5.5** (`claude-sonnet-5-5`), using structured outputs (`output_config.format` with the §6 schema), about 20 questions per request, through the **Message Batches API** (50% cheaper). The prompt pins Australian spelling, metric units, AUD and Australian HASS context.
3. **Independent check:** a second model answers each question blind and flags ambiguity and reading level. Anything that disagrees or gets flagged goes to human review.
4. **Automatic checks:** schema, duplicates, answer index in range, distinct choices.
5. **Human review:** export to CSV for a parent to skim, then import the approved items.

The maths balance is about 50% generators. Estimated cost: a few dollars at most for a few hundred items. **Needs:** an Anthropic API key or `ant auth login` on the dev machine (§13).

---

## 9. Staying mergeable with upstream SDDM

- The repo is a real **git fork**: `upstream` = `sddm/sddm` (default branch `develop`), and our work lives on `main`. An `origin` remote for the public edu-lm repo gets added at publish time. Merge upstream commits or tags into `main` rather than rebasing.
- Done in phase 0: the exploration clone was unshallowed and became the repo root, and this plan moved to `docs/PLAN.md`.
- New code only in `src/edu/` (core lib, config, tools, tests), `data/edu/` (sample question packs), `data/themes/edu/`, `tools/` (dev scripts), `packaging/`, with `EDU-LM:` hook markers.
- **Brand only:** binaries, `sddm.conf`, the PAM service and the `sddm` user stay unchanged. The Arch package `edu-lm` has `provides=(sddm) conflicts=(sddm)`.
- Build with `-DBUILD_WITH_QT6=ON`, matching Arch and Plasma 6.

---

## 10. Other distros and display managers

- **Other distros:** nothing here is Arch-specific. Any distro that ships SDDM with Qt6 (Debian/Ubuntu/Kubuntu, Fedora KDE, openSUSE, Raspberry Pi OS) can build and run it. Only packaging differs. We ship a PKGBUILD first and document a plain `cmake --install` for everything else. The one distro-specific item is the PAM `nullok` detail in §5.2.
- **Other display managers** (GDM, LightDM, greetd): this implementation is SDDM-specific, because the gate hooks into SDDM's daemon and the UI is an SDDM QML theme. Two ways to reach other display managers later:
  - **A PAM module (`pam_edu_quiz`).** Any DM that shows PAM prompts (GDM, LightDM greeters, even a text console) would get the quiz. The catch is the user experience: text prompts one at a time, with no multiple-choice buttons, swap button or review screen. Notably, **SDDM itself doesn't pass arbitrary PAM prompts through to the greeter** (`Display::slotRequestChanged` only fills a username and password), so it wouldn't help our main target.
  - **A native greeter for another DM,** e.g. a LightDM or greetd greeter reusing `edu-core`.

  Keeping `edu-core` free of SDDM dependencies (§3.1) keeps both options cheap. Neither is in scope now.

---

## 11. Phases

| Phase | Deliverable | Done when |
|---|---|---|
| **0. Setup** | Full git fork, upstream remote, build deps on the Pi (Debian: Qt6 + cmake + PAM dev), Xvfb for screenshots | Stock SDDM builds on the Pi; stock greeter renders in `--test-mode` under Xvfb |
| **1. Core logic** | `edu-core`: `QuestionBank` (JSON + generators), `QuizSession` (N/K/R, swaps, cooldown, review), `EduConfig`, sample packs | Small QtTest suite on the quiz rules passes |
| **2. Daemon gate** | `EduMessages`, `QuizGate` (intercept, verify-only auth, replay, cooldown per user), SocketServer/Display hooks | Quiz user is blocked with the stock theme; non-quiz users unaffected |
| **3. Greeter + theme** | `QuizClient` (with test-mode mock), Edu theme with password-optional login, quiz, swap button, review and countdown screens | Full flow works in greeter `--test-mode` (screenshots from the Pi) |
| **4. Arch integration** | PKGBUILD, default config, `nullok` check, pam_access snippet for text consoles, install docs. Ship `/etc/edu-lm/edu-lm.conf` as comments only: SDDM's ConfigReader reads `.conf.d` first and the main file last, so any key set in the main file overrides drop-ins | You confirm a real login round-trip on an Arch/KDE PC |
| **5. Content** | `tools/qgen` pipeline, first reviewed bank (~300–500 items) | Bank passes validation and your review |
| **6. Publish** | README, screenshots, licence notes (GPL-2.0-or-later for code; a licence for question packs, e.g. CC BY-SA), public repo | — |
| **Later** | Results log for parents, per-child year override, images, quiz on screen unlock, pack sync, other DMs | — |

---

## 12. Enforcement boundaries

- **Text consoles** (Ctrl+Alt+F3): deny the quiz group on consoles with `pam_access`. A snippet ships with the packaging.
- **Screen unlock** on KDE goes through kscreenlocker, not SDDM, so unlocking doesn't trigger a quiz. For v1, the lock-screen/idle-logout policy is documented instead; "quiz on unlock" is on the later list.
- **Plasma's System Settings → Login Screen (SDDM)** can switch the theme. Quiz users are then refused with an explanation rather than let through, which is safe but confusing, so it's documented. The edu theme is set in `/etc/sddm.conf.d/edu-lm.conf`.
- **Autologin** is refused for quiz users.
- SSH, recovery mode and GRUB are outside our scope and covered by normal hardening.

---

## 13. Open items

Needed later, not blocking phases 0–3:

1. **Teacher/parent override:** a way for an adult to let a child in on the spot (e.g. entering an exempt user's password on the quiz screen)? Proposed default: no, v1 leaves it out.
2. **Results log** of per-child questions and right/wrong answers for parents? Proposed default: a simple JSONL log at `/var/lib/edu-lm/results.jsonl`, off by default.
3. **Anthropic API access** on the machine that will run `tools/qgen` (needed at phase 5).
4. **Phase 0 on the Pi** needs `sudo apt install` of the build dependencies (list to be confirmed when we start).
