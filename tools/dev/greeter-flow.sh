#!/usr/bin/env bash
# Drive the Edu-LM theme headlessly (Xvfb + sddm-greeter-qt6 --test-mode + xdotool) and save a
# screenshot of each screen of the quiz flow.
# Usage: [RUNS="A B C D E F"] [PASS_SEED=n] tools/dev/greeter-flow.sh [out-dir] [WxH]   (default /tmp/edu-flow, 1280x720)
# Runs: A failed quiz, B passed quiz, C no student passwords, D broken config, E keyboard checks
# (with ShowCorrectAnswers=false),
# F the answer shown after a missed question (Next).
# Needs a built and installed tree (cmake --build build && cmake --install build).
# The mock daemon's answers come from the greeter log (see EDU_LM_TEST_* in GreeterGlue.cpp).
set -euo pipefail

out=${1:-/tmp/edu-flow}
size=${2:-1280x720}
root=$(cd "$(dirname "$0")/../.." && pwd)
greeter="$root/build/install/bin/sddm-greeter-qt6"
theme="$root/build/install/share/sddm/themes/edu"
disp=:${DISPLAY_NUM:-98}
work=$(mktemp -d)
mkdir -p "$out"
xvfb="" greeter_pid=""

cleanup() {
    [ -n "$greeter_pid" ] && kill "$greeter_pid" 2>/dev/null || true
    [ -n "$xvfb" ] && kill "$xvfb" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

Xvfb "$disp" -screen 0 "${size}x24" -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
for _ in $(seq 50); do xwininfo -display "$disp" -root >/dev/null 2>&1 && break; sleep 0.1; done
export DISPLAY=$disp

# start_greeter <conf-file-or-empty> [ENV=value ...]: a fresh greeter with its own log
start_greeter() { # SEED overrides the question order
    local conf=$1; shift
    stop_greeter
    log="$work/greeter.log"
    : >"$log"
    seen=0
    env QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software EDU_LM_TEST_SEED=${SEED:-3} \
        ${conf:+EDU_LM_TEST_CONF=$conf} "$@" "$greeter" --test-mode --theme "$theme" >"$log" 2>&1 &
    greeter_pid=$!
    for _ in $(seq 100); do
        xwininfo -root -children 2>/dev/null | grep -q '[0-9]x[0-9]' && break
        kill -0 "$greeter_pid" 2>/dev/null || { cat "$log"; exit 1; }
        sleep 0.2
    done
    sleep 1.5
    # no window manager here: keep the pointer out of the way of the tiles
    xdotool mousemove 2 2
}

stop_greeter() {
    if [ -n "$greeter_pid" ]; then
        kill "$greeter_pid" 2>/dev/null || true
        wait "$greeter_pid" 2>/dev/null || true
        greeter_pid=""
    fi
}

shot() { import -window root "$out/$1.png"; echo "saved $out/$1.png"; }
key() { xdotool key --delay 80 "$@"; sleep 0.25; }
type_text() { xdotool type --delay 60 -- "$1"; sleep 0.25; }

# The next "EDU_LM_TEST_ANSWER <id> key:N|text:...", waiting for it to be logged.
# Sets answer_kind (key|text) and answer_value.
next_answer() {
    local line
    for _ in $(seq 60); do
        if [ "$(grep -c EDU_LM_TEST_ANSWER "$log")" -gt "$seen" ]; then
            seen=$((seen + 1))
            line=$(grep EDU_LM_TEST_ANSWER "$log" | sed -n "${seen}p")
            line=${line#*EDU_LM_TEST_ANSWER }
            local spec=${line#* }
            answer_kind=${spec%%:*}
            answer_value=${spec#*:}
            sleep 0.4
            return
        fi
        sleep 0.2
    done
    echo "no question was logged"; cat "$log"; exit 1
}

give_right_answer() {
    if [ "$answer_kind" = key ]; then key "$answer_value"; else type_text "$answer_value"; key Return; fi
}

give_wrong_answer() {
    if [ "$answer_kind" = key ]; then
        key $(( answer_value == 1 ? 2 : 1 ))
    else
        type_text "wrong answer"; key Return
    fi
}


# ---- run A: a failed quiz, two tries per question ----
if [[ " ${RUNS:-A B C D E F} " == *" A "* ]]; then
cat >"$work/tries.conf" <<'CONF'
[Quiz]
AnswerAttempts=2
FailCooldownSeconds=25
CONF
start_greeter "$work/tries.conf" EDU_LM_TEST_VERIFY_MS=2500
shot 01-login-grid
key Return
shot 02-password
type_text "secret"
key Return
sleep 0.5
shot 03-checking
sleep 2.5

first_choice=1 first_retry=1
for q in 1 2 3 4 5; do
    next_answer
    if [ "$answer_kind" = key ] && [ $first_choice = 1 ]; then shot 04a-question-true-false; first_choice=0; fi
    if [ "$q" = 3 ]; then
        shot 04b-question-choice
        key Escape
        shot 06-back-confirm
        key Escape # Stay
    fi
    give_wrong_answer
    sleep 0.4
    if [ $first_retry = 1 ] && [ "$answer_kind" = key ]; then
        shot 07-feedback-try-again
        sleep 1.1
        key s # swap: refused, this question has been tried
        sleep 0.4
        shot 08-swap-refused
        first_retry=0
    else
        sleep 1.3
    fi
    give_wrong_answer
    sleep 0.4
    [ "$q" = 2 ] && shot 09-feedback-wrong
    key Return # Next: the answer is shown until it is pressed
done
sleep 1
shot 10-result-failed
key Return
sleep 0.5
shot 11-cooldown
# back to the login screen and try again during the cooldown: the cooldown, not "wrong password"
key Return # Back
sleep 0.5
type_text "secret"
key Return
sleep 2.5
shot 12-cooldown-on-retry
fi

# ---- run B: a passed quiz ----
if [[ " ${RUNS:-A B C D E F} " == *" B "* ]]; then
SEED=${PASS_SEED:-5} start_greeter "" EDU_LM_TEST_SESSION_MS=6000 EDU_LM_TEST_VERIFY_MS=300
key Return
type_text "secret"
key Return
first_text=1
for q in 1 2 3 4 5; do
    next_answer
    [ "$q" = 1 ] && shot 13-question-first
    if [ "$answer_kind" = text ] && [ $first_text = 1 ]; then shot 05-question-text; first_text=0; fi
    give_right_answer
    sleep 0.4
    [ "$q" = 1 ] && shot 14-feedback-correct
    sleep 1.3
done
sleep 0.3
shot 15-result-passed
sleep 6
shot 16-logged-in
fi

# ---- run C: no student passwords ----
if [[ " ${RUNS:-A B C D E F} " == *" C "* ]]; then
printf '[Quiz]\nStudentPasswords=false\n' >"$work/nopass.conf"
start_greeter "$work/nopass.conf"
key Return
sleep 0.8
shot 17-no-password-start
fi

# ---- run D: a broken configuration ----
if [[ " ${RUNS:-A B C D E F} " == *" D "* ]]; then
printf '[Quiz]\nRequiredCorrect=9\n' >"$work/broken.conf"
start_greeter "$work/broken.conf" EDU_LM_TEST_VERIFY_MS=300
key Return
type_text "secret"
key Return
sleep 2
shot 18-error
fi

# ---- run E: the keys work after Tab, and the "go back" dialog is modal ----
if [[ " ${RUNS:-A B C D E F} " == *" E "* ]]; then
printf '[Quiz]\nShowCorrectAnswers=false\n' >"$work/hidden.conf"
start_greeter "$work/hidden.conf" EDU_LM_TEST_SESSION_MS=6000 EDU_LM_TEST_VERIFY_MS=300
key Return
type_text "wrong"
key Return
sleep 1.5
shot 22-wrong-password
type_text "secret"
key Return
next_answer
key Tab
key 2 # a shortcut after Tab must still answer
sleep 0.5
shot 19-answer-after-tab
sleep 1.6
next_answer
key Escape # the dialog opens, since an answer was given
key Tab
key Tab
key space # Tab cannot leave the dialog, so this is "Stay", not an answer
sleep 0.4
shot 20-dialog-modal-then-stay
key 1
sleep 0.3
key Escape # ignored while the feedback shows
sleep 0.2
shot 21-escape-during-feedback-ignored
fi

# ---- run F: a missed question shows the answer and waits for Next ----
if [[ " ${RUNS:-A B C D E F} " == *" F "* ]]; then
start_greeter "" EDU_LM_TEST_VERIFY_MS=300
key Return
type_text "secret"
key Return
next_answer
give_wrong_answer
sleep 2.5 # well past the 1 s feedback time: it stays
shot 23-feedback-revealed
key Return # Next
next_answer
sleep 0.4
shot 24-question-after-next
fi
stop_greeter
