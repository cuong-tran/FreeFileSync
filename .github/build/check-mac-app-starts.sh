#!/usr/bin/env bash
# macOS start check for an app bundle (CI), the counterpart of check-app-starts.sh: starts the app with a fresh HOME (so a fresh
# configuration in ~/Library/Application Support) and the given arguments, waits until the process shows a window whose title
# matches <title regex>, gives a startup error popup 3 s to appear, takes a screenshot (GitHub's runners only), quits the app with SIGTERM and fails if
#   - the app ended before showing that window, or ended by itself afterwards (it must still run when it gets SIGTERM),
#   - no window title matched within $WAIT_SECONDS (default 60),
#   - a window of the app is titled like an error or warning popup,
#   - its output (stdout/stderr) reports an error (a wx "Error:" line, a failed assertion, an uncaught exception),
#   - it crashed: ended by a crash signal, or left a crash report in ~/Library/Logs/DiagnosticReports (searched after giving
#     ReportCrash a few seconds to write it).
#   check-mac-app-starts.sh <bundle.app> <title regex> <screenshot.png> [<app argument>...]
#     e.g. check-mac-app-starts.sh FreeFileSync.app '^FreeFileSync [0-9.]+ ' FreeFileSync.png
# Windows come from CGWindowListCopyWindowInfo through JavaScript for Automation: layers 0 to 8, i.e. normal (0), floating (3) and
# modal panel windows (8: a modal dialog such as About or an error popup can be there, seen on macOS 27.2), not the menu bar status
# item (25). Their titles (kCGWindowName) are readable only
# with the screen recording permission, which GitHub's runners grant; the check uses titles once the window list shows one for
# a window of the app ("window titles: readable" in the log), else (the owner's self-hosted Mac, unless they granted it) it
# counts the app's windows: EXPECT_WINDOWS=<n> (also checked when titles are readable, so GitHub's runners keep the counts
# honest), otherwise at least one. On GitHub's runners (RUNNER_ENVIRONMENT=github-hosted) titles must be readable: if the app
# shows windows for 10 s and none has a title in the list, the check fails instead of falling back to the count, so that the
# title regex is always tested somewhere.
# Screenshots: only on GitHub's runners (RUNNER_ENVIRONMENT=github-hosted), of the app's own windows one by one: <screenshot.png>
# is the window whose title matched (the frontmost one if none did), every other window of the app goes next to it as
# <screenshot>-window<n>.png; anywhere else (the self-hosted runner, a local run on someone's Mac) none is taken and
# <screenshot.png> is not created. SCREENSHOT_TO_LOG=1: also print a small JPEG of each screenshot as base64 into the log (between
# "-----BEGIN SCREENSHOT <name>-----" and "-----END SCREENSHOT-----"); GitHub's runners only, too.
# NSUserDefaults ignores HOME, so the app's preferences plist lands in the real ~/Library/Preferences; it is removed afterwards
# unless it existed before the run.
set -euo pipefail
[ $# -ge 3 ] || { echo "usage: $0 <bundle.app> <title regex> <screenshot.png> [<app argument>...]" >&2; exit 2; }
titleRegex="$2" shot="$3"
# absolute and resolved, so that the started process has an absolute path (mac-runner.sh cleanup finds it) and crash reports,
# which hold the resolved path, can be matched
[ -d "$1" ] || { echo "::error::no app bundle $1"; exit 1; }
app="$(cd "$1" && pwd)"
appResolved="$(cd -P "$1" && pwd -P)"
shift 3
timeout="${WAIT_SECONDS:-60}"
name="$(basename "$app" .app)"
exe="$app/Contents/MacOS/$name"
[ -x "$exe" ] || { echo "::error::no executable $exe"; exit 1; }

work="$(mktemp -d "$(t="${TMPDIR:-/tmp}"; echo "${t%/}")/start-check.XXXXXX")" # explicit template (TMPDIR without its trailing slash): macOS 27's mktemp -d ignores TMPDIR
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

hosted=no
[ "${RUNNER_ENVIRONMENT:-}" = github-hosted ] && hosted=yes
titles=unknown # "readable" once a window of the app shows a title in the window list; decided from what the list returns
unreadableSince="" # GitHub's runners: the poll since which the app shows windows, none with a title

windowList() # on-screen windows (layers 0 to 8, see the header) of $pid, front to back, one per line: "<window id> <width>x<height> <title or <no title>>"
{
    osascript -l JavaScript -e "ObjC.import('CoreGraphics');
        var list = ObjC.deepUnwrap(ObjC.castRefToObject(\$.CGWindowListCopyWindowInfo(\$.kCGWindowListOptionOnScreenOnly, 0))) || [];
        list.filter(function (w) { return w.kCGWindowOwnerPID == $pid && w.kCGWindowLayer >= 0 && w.kCGWindowLayer <= 8; })
            .map(function (w) { return w.kCGWindowNumber + ' ' + Math.round(w.kCGWindowBounds.Width) + 'x' + Math.round(w.kCGWindowBounds.Height) + ' ' +
                                       (typeof w.kCGWindowName === 'string' && w.kCGWindowName !== '' ? w.kCGWindowName : '<no title>'); }).join('\n')" 2>/dev/null || true
}
# titles are readable once any window of the app has one (without the permission, the list has no kCGWindowName at all)
noteTitles()
{
    local t
    t="$(cut -d' ' -f3- "$1")" # captured first: "| grep -q" could end cut with SIGPIPE, which pipefail turns into "no"
    if [ "$titles" != readable ] && [ -n "$t" ] && grep -qvx '<no title>' <<< "$t"; then
        titles=readable
        echo "window titles: readable"
    fi
}
windowTitles() { windowList | cut -d' ' -f3-; }
windowCount() { grep -c . "$1" || true; }
# shown: the expected window is there: a title matches (when readable) and the count is EXPECT_WINDOWS (when given)
shown()
{
    local count; count="$(windowCount "$1")"
    if [ -n "${EXPECT_WINDOWS:-}" ] && [ "$count" -ne "$EXPECT_WINDOWS" ]; then return 1; fi
    if [ "$titles" = readable ]; then local t; t="$(cut -d' ' -f3- "$1")"; grep -Eq -- "$titleRegex" <<< "$t"; else [ "$count" -ge 1 ]; fi
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
    noteTitles "$work/windows.txt"
    # (with titles unreadable, wait a moment after the first window: a title may still turn up, or the expected count; on
    # GitHub's runners the count alone never passes: see the header)
    if [ "$titles" = readable ]; then
        if shown "$work/windows.txt"; then shown=yes; break; fi
    elif [ "$hosted" = yes ]; then
        if [ "$(windowCount "$work/windows.txt")" -ge 1 ]; then
            unreadableSince="${unreadableSince:-$i}"
            [ $((i - unreadableSince)) -lt 10 ] || break
        fi
    elif shown "$work/windows.txt" && [ "$i" -ge 2 ]; then
        shown=yes; break
    fi
    sleep 1
done
echo "windows of $name (id, size, title):"
sed 's/^/    /' "$work/windows.txt"
if [ "$titles" != readable ] && [ "$hosted" = yes ] && [ -n "$unreadableSince" ]; then
    fail "window titles not readable on a GitHub-hosted runner (they grant the screen recording permission; has the image changed?): the title regex /$titleRegex/ was not tested"
fi
[ "$titles" = readable ] || echo "window titles: not readable (no screen recording permission): the app's windows are counted (EXPECT_WINDOWS=${EXPECT_WINDOWS:-at least 1})"
if [ "$shown" != yes ]; then
    if [ "$titles" = readable ]; then
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

if [ "$hosted" = yes ]; then # (never on a person's Mac: see the header)
    # the app's windows one by one (-l: that window only, -o: without its shadow); the matched one (else the frontmost) first
    shots=() matchedId="" n=1
    while read -r id _ title; do
        if [ -z "$matchedId" ] && [[ "$title" =~ $titleRegex ]]; then matchedId="$id"; fi
    done < "$work/windows.txt"
    [ -n "$matchedId" ] || matchedId="$(head -n 1 "$work/windows.txt" | cut -d' ' -f1)"
    while read -r id _; do
        [ -n "$id" ] || continue
        if [ "$id" = "$matchedId" ]; then target="$shot"; else n=$((n + 1)); target="${shot%.*}-window$n.png"; fi
        if screencapture -x -o -l"$id" "$target"; then shots+=("$target"); else echo "::warning::screencapture of window $id failed"; fi
    done < "$work/windows.txt"
fi
if [ "${SCREENSHOT_TO_LOG:-0}" = 1 ] && [ "$hosted" = yes ]; then
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
    # (the path as started and resolved, each also as JSON writes it in .ips files: "/" as "\/")
    report="$(find "$reports" -newer "$work/marker" -iname "*$name*" -exec grep -lF -e "$app/" -e "$appResolved/" \
        -e "$(sed 's#/#\\/#g' <<< "$app/")" -e "$(sed 's#/#\\/#g' <<< "$appResolved/")" {} + 2>/dev/null || true)"
    [ -z "$report" ] || crashed="${crashed:+$crashed, }crash report $report"
fi
[ -z "$crashed" ] || { echo "::error::$name crashed: $crashed"; failed=1; }

[ "$failed" = 0 ] || exit 1
echo "$name started, showed its window and ended with exit code $rc"
