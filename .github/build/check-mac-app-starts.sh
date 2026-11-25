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
# Window titles come from CGWindowListCopyWindowInfo through JavaScript for Automation (kCGWindowName: needs the screen recording
# permission that screencapture needs as well, which the CI runners grant). The screenshots show the app's own windows only, never
# the rest of the screen (other apps' windows on a developer's Mac): <screenshot.png> is the window whose title matched, every other
# window of the app goes next to it as <screenshot>-window<n>.png. SCREENSHOT_TO_LOG=1: also print a small JPEG of each screenshot
# as base64 into the log (between "-----BEGIN SCREENSHOT <name>-----" and "-----END SCREENSHOT-----").
# NSUserDefaults ignores HOME, so the app's preferences plist lands in the real ~/Library/Preferences; it is removed afterwards
# unless it existed before the run.
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
bundleId="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist" 2>/dev/null || true)"
prefsPlist="$HOME/Library/Preferences/$bundleId.plist"
prefsExisted=yes
[ -z "$bundleId" ] || [ -e "$prefsPlist" ] || prefsExisted=no
cleanup()
{
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
    rm -rf "$work"
    if [ "$prefsExisted" = no ]; then defaults delete "$bundleId" >/dev/null 2>&1 || true; rm -f "$prefsPlist"; fi
}
trap cleanup EXIT
mkdir -p "$work/home"
reports="$HOME/Library/Logs/DiagnosticReports"
touch "$work/marker" # crash reports newer than this belong to this run

HOME="$work/home" "$exe" "$@" > "$work/output.log" 2>&1 &
pid=$!

# "<window number><TAB><title>" of the on-screen windows of $pid, front to back, one per line: layers 0 to 8, i.e. normal (0), floating (3)
# and modal panel windows (8: macOS 27 puts modal dialogs such as About or an error popup there), not the menu bar status item (25)
windowList()
{
    osascript -l JavaScript -e "ObjC.import('CoreGraphics');
        var list = ObjC.deepUnwrap(ObjC.castRefToObject(\$.CGWindowListCopyWindowInfo(\$.kCGWindowListOptionOnScreenOnly, 0))) || [];
        list.filter(function (w) { return w.kCGWindowOwnerPID == $pid && w.kCGWindowLayer >= 0 && w.kCGWindowLayer <= 8; })
            .map(function (w) { return w.kCGWindowNumber + '\t' + (w.kCGWindowName === undefined ? '<no title>' : w.kCGWindowName); }).join('\n')" 2>/dev/null || true
}
windowTitles() { windowList | cut -f 2-; } # titles only, one per line

fail() { echo "::error::$name: $*"; echo "== $name output:"; cat "$work/output.log"; exit 1; }

shown=no
for ((i = 0; i < timeout; ++i)); do
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" && rc=0 || rc=$?
        pid=""
        fail "ended before showing its window (exit code $rc)"
    fi
    windowTitles > "$work/titles.txt"
    if grep -Eq -- "$titleRegex" "$work/titles.txt"; then shown=yes; break; fi
    sleep 1
done
echo "window titles of $name:"
sed 's/^/    /' "$work/titles.txt"
[ "$shown" = yes ] || fail "no window titled like /$titleRegex/ within $timeout s"

sleep 3 # give an error popup at startup time the time to show up next to the window, and the window to finish drawing
windowTitles > "$work/titles.txt"
echo "window titles of $name after 3 s:"
sed 's/^/    /' "$work/titles.txt"

# the app's windows one by one (-l: that window only, -o: without its shadow); the matched one first, as <screenshot.png>
windowList > "$work/windows.txt"
shots=()
matchedId="$(awk -F '\t' -v re="$titleRegex" '$2 ~ re { print $1; exit }' "$work/windows.txt")"
n=1
while IFS=$'\t' read -r id _; do
    [ -n "$id" ] || continue
    if [ "$id" = "$matchedId" ]; then target="$shot"; else n=$((n + 1)); target="${shot%.*}-window$n.png"; fi
    if screencapture -x -o -l"$id" "$target"; then shots+=("$target"); else echo "::warning::screencapture of window $id failed"; fi
done < "$work/windows.txt"
[ -n "$matchedId" ] || echo "::warning::the window titled like /$titleRegex/ is gone: no screenshot $shot"
if [ "${SCREENSHOT_TO_LOG:-0}" = 1 ]; then
    for f in ${shots[@]+"${shots[@]}"}; do
        [ -f "$f" ] && sips -Z 900 -s format jpeg -s formatOptions 55 "$f" --out "$work/small.jpg" >/dev/null || continue
        echo "::group::screenshot $(basename "$f") (base64 JPEG)"
        echo "-----BEGIN SCREENSHOT $(basename "$f")-----"
        base64 -i "$work/small.jpg" | fold -w 120
        echo "-----END SCREENSHOT-----"
        echo "::endgroup::"
    done
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
if grep -Eq -- '^Error:|[Aa]ssert(ion)? .*fail|failed assertion|Terminating app due to uncaught exception|libc\+\+abi: terminating' "$work/output.log"; then
    echo "::error::$name: its output reports an error"; failed=1
fi

crashed=""
case "$rc" in
    132|133|134|135|136|138|139) crashed="exit code $rc (crash signal $((rc - 128)))" ;; #SIGILL, SIGTRAP, SIGABRT, SIGEMT, SIGFPE, SIGBUS, SIGSEGV
esac
sleep 5 # ReportCrash writes the report a few seconds after the crash
if [ -d "$reports" ]; then
    report="$(find "$reports" -newer "$work/marker" -iname "*$name*" 2>/dev/null || true)"
    [ -z "$report" ] || crashed="${crashed:+$crashed, }crash report $report"
fi
[ -z "$crashed" ] || { echo "::error::$name crashed: $crashed"; failed=1; }

[ "$failed" = 0 ] || exit 1
echo "$name started, showed its window and ended with exit code $rc"
