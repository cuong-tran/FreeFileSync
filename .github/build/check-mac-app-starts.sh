#!/usr/bin/env bash
# macOS start check for an app bundle (CI), the counterpart of check-app-starts.sh: starts the app with a fresh HOME (so a fresh
# configuration in ~/Library/Application Support) and the given arguments, waits until the process shows a window whose title
# matches <title regex>, gives a startup error popup 3 s to appear, takes a screenshot, quits the app with SIGTERM and fails if
#   - the app ended before showing that window, or ended by itself afterwards (it must still run when it gets SIGTERM),
#   - no window title matched within $WAIT_SECONDS (default 60),
#   - a window of the app is titled like an error or warning popup,
#   - its output (stdout/stderr) reports an error (a wx "Error:" line, a failed assertion, an uncaught exception),
#   - it crashed: ended by a crash signal, or left a crash report in ~/Library/Logs/DiagnosticReports (searched after giving
#     ReportCrash a few seconds to write it).
#   check-mac-app-starts.sh <bundle.app> <title regex> <screenshot.png> [<app argument>...]
#     e.g. check-mac-app-starts.sh FreeFileSync.app '^FreeFileSync [0-9.]+ ' FreeFileSync.png
# Windows come from CGWindowListCopyWindowInfo through JavaScript for Automation. Their titles (kCGWindowName) need the screen
# recording permission, which GitHub's runners grant; without it (the owner's self-hosted Mac, unless they granted it) titles
# are not readable and the check counts the app's windows instead: EXPECT_WINDOWS=<n> (also checked when titles are readable,
# so GitHub's runners keep the counts honest), otherwise at least one.
# Screenshots: on GitHub's runners (RUNNER_ENVIRONMENT=github-hosted) the whole screen, as before; anywhere else (the
# self-hosted runner, a local run) only the app's own windows (screencapture -l <window id>, checked to be exactly that window's
# size), and only with the screen recording permission: never the rest of a person's screen, and no permission prompt.
# SCREENSHOT_TO_LOG=1: also print a small JPEG of the screenshot as base64 into the log (between "-----BEGIN SCREENSHOT
# <name>-----" and "-----END SCREENSHOT-----"); on GitHub's runners only.
set -euo pipefail
[ $# -ge 3 ] || { echo "usage: $0 <bundle.app> <title regex> <screenshot.png> [<app argument>...]" >&2; exit 2; }
app="$1" titleRegex="$2" shot="$3"
shift 3
timeout="${WAIT_SECONDS:-60}"
name="$(basename "$app" .app)"
exe="$app/Contents/MacOS/$name"
[ -x "$exe" ] || { echo "::error::no executable $exe"; exit 1; }

work="$(mktemp -d)"
pid=""
cleanup()
{
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
    rm -rf "$work"
}
trap cleanup EXIT
mkdir -p "$work/home"
reports="$HOME/Library/Logs/DiagnosticReports"
touch "$work/marker" # crash reports newer than this belong to this run

HOME="$work/home" "$exe" "$@" > "$work/output.log" 2>&1 &
pid=$!

hosted=no
[ "${RUNNER_ENVIRONMENT:-}" = github-hosted ] && hosted=yes
# screen recording permission of this process (no prompt: Preflight only reads it); titles and window captures need it
canRecord="$(osascript -l JavaScript -e "ObjC.import('CoreGraphics'); ObjC.bindFunction('CGPreflightScreenCaptureAccess', ['bool', []]);
    \$.CGPreflightScreenCaptureAccess() ? 'yes' : 'no'" 2>/dev/null || echo no)"
[ "$canRecord" = yes ] || echo "note: no screen recording permission: window titles are not readable, the app's windows are counted (EXPECT_WINDOWS=${EXPECT_WINDOWS:-at least 1}) and no screenshot is taken"

windowList() # on-screen windows (layer 0: normal windows) of $pid, one per line: "<window id> <width>x<height> <title or <no title>>"
{
    osascript -l JavaScript -e "ObjC.import('CoreGraphics');
        var list = ObjC.deepUnwrap(ObjC.castRefToObject(\$.CGWindowListCopyWindowInfo(\$.kCGWindowListOptionOnScreenOnly, 0))) || [];
        list.filter(function (w) { return w.kCGWindowOwnerPID == $pid && w.kCGWindowLayer == 0; })
            .map(function (w) { return w.kCGWindowNumber + ' ' + Math.round(w.kCGWindowBounds.Width) + 'x' + Math.round(w.kCGWindowBounds.Height) + ' ' +
                                       (w.kCGWindowName === undefined ? '<no title>' : w.kCGWindowName); }).join('\n')" 2>/dev/null || true
}
windowTitles() { windowList | cut -d' ' -f3-; }
windowCount() { grep -c . "$1" || true; }
# shown: the expected window is there: a title matches (when readable) and the count is EXPECT_WINDOWS (when given)
shown()
{
    local count; count="$(windowCount "$1")"
    if [ -n "${EXPECT_WINDOWS:-}" ] && [ "$count" -ne "$EXPECT_WINDOWS" ]; then return 1; fi
    if [ "$canRecord" = yes ]; then cut -d' ' -f3- "$1" | grep -Eq -- "$titleRegex"; else [ "$count" -ge 1 ]; fi
}

fail() { echo "::error::$name: $*"; echo "== $name output:"; cat "$work/output.log"; exit 1; }

shown=no
for ((i = 0; i < timeout; ++i)); do
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" && rc=0 || rc=$?
        pid=""
        fail "ended before showing its window (exit code $rc)"
    fi
    windowList > "$work/windows.txt"
    if shown "$work/windows.txt"; then shown=yes; break; fi
    sleep 1
done
echo "windows of $name (id, size, title):"
sed 's/^/    /' "$work/windows.txt"
if [ "$shown" != yes ]; then
    if [ "$canRecord" = yes ]; then
        fail "no window titled like /$titleRegex/${EXPECT_WINDOWS:+ among exactly $EXPECT_WINDOWS windows} within $timeout s"
    else
        fail "not ${EXPECT_WINDOWS:-at least 1} window(s) within $timeout s (titles not readable)"
    fi
fi

sleep 3 # give an error popup at startup time the time to show up next to the window, and the window to finish drawing
windowList > "$work/windows.txt"
cut -d' ' -f3- "$work/windows.txt" > "$work/titles.txt"
echo "windows of $name after 3 s:"
sed 's/^/    /' "$work/windows.txt"
countAfter="$(windowCount "$work/windows.txt")"

if [ "$hosted" = yes ]; then
    screencapture -x "$shot" || echo "::warning::screencapture failed"
elif [ "$canRecord" = yes ]; then
    # only the app's own windows, one image each (<shot>, <shot base>-2.png, ...); each must have exactly its window's size
    # (in points or in pixels at a backing scale of 2 or 3), or it is deleted
    i=0
    while read -r id size _; do
        i=$((i + 1))
        out="$shot"; [ "$i" = 1 ] || out="${shot%.png}-$i.png"
        if screencapture -x -o -l "$id" "$out" 2>/dev/null && [ -f "$out" ]; then
            got="$(sips -g pixelWidth -g pixelHeight "$out" 2>/dev/null | awk '/pixelWidth/ { w = $2 } /pixelHeight/ { h = $2 } END { print w "x" h }')"
            w="${size%x*}" h="${size#*x}"
            if [ "$got" != "$w"x"$h" ] && [ "$got" != "$((w * 2))x$((h * 2))" ] && [ "$got" != "$((w * 3))x$((h * 3))" ]; then
                echo "::warning::$name: the capture of window $id is $got, not the window's $size: deleted"
                rm -f "$out"
            fi
        else
            echo "::warning::$name: screencapture of window $id failed"
        fi
    done < "$work/windows.txt"
fi
if [ "${SCREENSHOT_TO_LOG:-0}" = 1 ] && [ "$hosted" = yes ] && [ -f "$shot" ]; then
    if sips -Z 900 -s format jpeg -s formatOptions 55 "$shot" --out "$work/small.jpg" >/dev/null; then
        echo "::group::screenshot $(basename "$shot") (base64 JPEG)"
        echo "-----BEGIN SCREENSHOT $(basename "$shot")-----"
        base64 -i "$work/small.jpg" | fold -w 120
        echo "-----END SCREENSHOT-----"
        echo "::endgroup::"
    fi
fi

kill -0 "$pid" 2>/dev/null || { wait "$pid" && rc=0 || rc=$?; pid=""; fail "ended by itself after showing its window (exit code $rc)"; }
kill -TERM "$pid" 2>/dev/null || true
for ((i = 0; i < 20; ++i)); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "$pid" 2>/dev/null; then
    echo "::warning::$name did not end on SIGTERM within 10 s: killed"
    kill -KILL "$pid" 2>/dev/null || true
fi
wait "$pid" && rc=0 || rc=$?
pid=""
echo "== $name output:"
cat "$work/output.log"

failed=0
if grep -Eiq '(^|[^a-z])(error|warning)([^a-z]|$)' "$work/titles.txt"; then
    echo "::error::$name: a window is titled like an error or warning popup"; failed=1
fi
if [ -n "${EXPECT_WINDOWS:-}" ] && [ "$countAfter" -ne "$EXPECT_WINDOWS" ]; then
    echo "::error::$name: $countAfter windows 3 s after start, expected $EXPECT_WINDOWS (a popup?)"; failed=1
fi
if grep -Eq -- '^Error:|[Aa]ssert(ion)? .*fail|failed assertion|Terminating app due to uncaught exception|libc\+\+abi: terminating' "$work/output.log"; then
    echo "::error::$name: its output reports an error"; failed=1
fi

crashed=""
case "$rc" in
    132|133|134|135|136|138|139) crashed="exit code $rc (crash signal $((rc - 128)))" ;; #SIGILL, SIGTRAP, SIGABRT, SIGEMT, SIGFPE, SIGBUS, SIGSEGV
esac
sleep 5 # ReportCrash writes the report a few seconds after the crash
if [ -d "$reports" ]; then
    # only reports of this binary (its path is in the report): not of another copy of the app the user runs meanwhile
    # (.ips files are JSON, which writes "/" as "\/")
    appPath="$(cd "$app" && pwd)/"
    report="$(find "$reports" -newer "$work/marker" -iname "*$name*" -exec grep -lF -e "$appPath" -e "$(sed 's#/#\\/#g' <<< "$appPath")" {} + 2>/dev/null || true)"
    [ -z "$report" ] || crashed="${crashed:+$crashed, }crash report $report"
fi
[ -z "$crashed" ] || { echo "::error::$name crashed: $crashed"; failed=1; }

[ "$failed" = 0 ] || exit 1
echo "$name started, showed its window and ended with exit code $rc"
