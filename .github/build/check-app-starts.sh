#!/bin/bash
# Smoke test for a built app on Linux/X11: start it under Xvfb with an empty HOME and check that it shows its main window
# and nothing else that looks like an error. Fails if the app exits early, its main window doesn't appear within
# $WAIT_SECONDS (default 30), a visible window is titled like an error/warning popup, or stderr reports an error.
# Needs xvfb-run and xdotool.
# Usage: check-app-starts.sh <binary> <main window title regex>
#   e.g. check-app-starts.sh FreeFileSync/Build/Bin/FreeFileSync_x86_64 '^FreeFileSync [0-9.]+ '
set -euo pipefail

[ $# -eq 2 ] || { echo "usage: $0 <binary> <main window title regex>" >&2; exit 2; }
BIN="$(realpath -- "$1")"
TITLE_REGEX="$2"
WAIT_SECONDS="${WAIT_SECONDS:-30}"
[ -x "$BIN" ] || { echo "error: $BIN is not an executable" >&2; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/home"

# runs inside xvfb-run: start the app, poll the visible window titles, then stop the app
cat > "$WORK/inside.sh" <<'EOF'
set -uo pipefail
"$BIN" 2> "$WORK/stderr.txt" &
pid=$!
result=timeout
for (( i = 0; i < WAIT_SECONDS * 2; ++i )); do
    sleep 0.5
    if ! kill -0 "$pid" 2>/dev/null; then wait "$pid"; echo "exit code $?" > "$WORK/exited.txt"; result=exited; break; fi
    : > "$WORK/titles.txt"
    for w in $(xdotool search --onlyvisible --name '' 2>/dev/null); do
        xdotool getwindowname "$w" >> "$WORK/titles.txt" 2>/dev/null || true
    done
    if grep -Eq -- "$TITLE_REGEX" "$WORK/titles.txt"; then
        sleep 3 #give an error popup at startup time to show up next to the main window
        for w in $(xdotool search --onlyvisible --name '' 2>/dev/null); do xdotool getwindowname "$w" 2>/dev/null || true; done > "$WORK/titles.txt"
        kill -0 "$pid" 2>/dev/null && result=ok || result=exited
        break
    fi
done
echo "$result" > "$WORK/result.txt"
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
exit 0
EOF

export BIN TITLE_REGEX WAIT_SECONDS WORK
HOME="$WORK/home" LANG=C.UTF-8 xvfb-run -a -s "-screen 0 1280x1024x24" bash "$WORK/inside.sh"

result="$(cat "$WORK/result.txt" 2>/dev/null || echo "no result")"
echo "== $(basename "$BIN"): $result"
echo "visible window titles:"
grep -v '^$' "$WORK/titles.txt" 2>/dev/null | sed 's/^/    /' || true
if [ -s "$WORK/stderr.txt" ]; then
    echo "stderr:"
    sed 's/^/    /' "$WORK/stderr.txt"
fi

failed=0
case "$result" in
    ok)      ;;
    exited)  echo "error: the app exited early ($(cat "$WORK/exited.txt" 2>/dev/null))" >&2; failed=1 ;;
    timeout) echo "error: no window titled like /$TITLE_REGEX/ within $WAIT_SECONDS s" >&2; failed=1 ;;
    *)       echo "error: $result" >&2; failed=1 ;;
esac
if grep -Eiq '(^|[^a-z])(error|warning)([^a-z]|$)' "$WORK/titles.txt" 2>/dev/null; then
    echo "error: a window is titled like an error or warning popup" >&2; failed=1
fi
if grep -Eiq 'error|critical|assert' "$WORK/stderr.txt" 2>/dev/null; then
    echo "error: stderr reports an error" >&2; failed=1
fi
exit "$failed"
