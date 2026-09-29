#!/usr/bin/env bash
# Render a greeter theme headlessly (Xvfb + --test-mode) and save a screenshot.
# Usage: tools/dev/greeter-shot.sh <theme-dir> <out.png> [WxH]
set -euo pipefail

theme=${1:?theme dir}
out=${2:?output png}
size=${3:-1280x800}
root=$(cd "$(dirname "$0")/../.." && pwd)
greeter="$root/build/install/bin/sddm-greeter-qt6"
disp=:${DISPLAY_NUM:-99}
log=$(mktemp)

Xvfb "$disp" -screen 0 "${size}x24" -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
trap 'kill $greeter_pid $xvfb 2>/dev/null || true; rm -f "$log"' EXIT

for _ in $(seq 50); do xwininfo -display "$disp" -root >/dev/null 2>&1 && break; sleep 0.1; done

# Xvfb has no working GL for Qt Quick; the software backend renders correctly
DISPLAY=$disp QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software} "$greeter" --test-mode --theme "$theme" >"$log" 2>&1 &
greeter_pid=$!

# Wait for a mapped top-level window, then give QML a moment to finish painting
for _ in $(seq 100); do
    xwininfo -display "$disp" -root -children 2>/dev/null | grep -q '[0-9]x[0-9]' && break
    kill -0 $greeter_pid 2>/dev/null || { cat "$log"; exit 1; }
    sleep 0.2
done
sleep 2

import -display "$disp" -window root "$out"
[ -n "${VERBOSE:-}" ] && cat "$log" || grep -iE "warn|error|qrc|file://" "$log" || true
echo "saved $out"
