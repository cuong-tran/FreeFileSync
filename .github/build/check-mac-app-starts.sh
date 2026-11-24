#!/usr/bin/env bash
# macOS start check for an app bundle (CI): starts the app with a fresh HOME (so a fresh configuration in
# ~/Library/Application Support), waits until the process has an on-screen window, takes a screenshot, quits it
# with SIGTERM and fails if the app exited early, never showed a window, crashed (a crash report in
# ~/Library/Logs/DiagnosticReports) or ended by a crash signal.
#   check-mac-app-starts.sh <bundle.app> <screenshot.png> [<seconds to wait for the window, default 60>]
# Windows are found with CGWindowListCopyWindowInfo through JavaScript for Automation (no accessibility permission
# needed; window titles would need screen recording permission, so only the owner PID and the layer are used).
set -euo pipefail
app="$1" shot="$2" timeout="${3:-60}"
name="$(basename "$app" .app)"
exe="$app/Contents/MacOS/$name"
[ -x "$exe" ] || { echo "::error::no executable $exe"; exit 1; }

home="$(mktemp -d)"
reports="$HOME/Library/Logs/DiagnosticReports"
marker="$(mktemp)" # crash reports newer than this belong to this run
HOME="$home" "$exe" > "$home/output.log" 2>&1 &
pid=$!

windowCount() # on-screen windows (layer 0: normal windows) of $pid
{
    osascript -l JavaScript -e "ObjC.import('CoreGraphics');
        var list = ObjC.deepUnwrap(ObjC.castRefToObject(\$.CGWindowListCopyWindowInfo(\$.kCGWindowListOptionOnScreenOnly, 0))) || [];
        list.filter(function (w) { return w.kCGWindowOwnerPID == $pid && w.kCGWindowLayer == 0; }).length" 2>/dev/null || echo 0
}

shown=no
for ((i = 0; i < timeout; ++i)); do
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" && rc=0 || rc=$?
        echo "::error::$name ended before showing a window (exit code $rc)"
        cat "$home/output.log"
        exit 1
    fi
    if [ "$(windowCount)" -gt 0 ]; then shown=yes; break; fi
    sleep 1
done
sleep 2 # let the window finish drawing
screencapture -x "$shot" || echo "::warning::screencapture failed"
#SCREENSHOT_TO_LOG=1: also print a small JPEG of it as base64 into the log, for readers who can reach the log but not the artifacts
if [ "${SCREENSHOT_TO_LOG:-0}" = 1 ] && [ -f "$shot" ]; then
    small="$(mktemp).jpg"
    if sips -Z 900 -s format jpeg -s formatOptions 55 "$shot" --out "$small" >/dev/null; then
        echo "::group::screenshot $(basename "$shot") (base64 JPEG)"
        echo "-----BEGIN SCREENSHOT $(basename "$shot")-----"
        base64 -i "$small" | fold -w 120
        echo "-----END SCREENSHOT-----"
        echo "::endgroup::"
    fi
    rm -f "$small"
fi

kill -TERM "$pid" 2>/dev/null || true
for ((i = 0; i < 20 && $(kill -0 "$pid" 2>/dev/null && echo 1 || echo 0); ++i)); do sleep 0.5; done
if kill -0 "$pid" 2>/dev/null; then
    echo "::warning::$name did not end on SIGTERM within 10 s: killed"
    kill -KILL "$pid" 2>/dev/null || true
fi
wait "$pid" && rc=0 || rc=$?
echo "== $name output:"
cat "$home/output.log"

crashed=""
if [ -d "$reports" ]; then
    crashed="$(find "$reports" -newer "$marker" -iname "*$name*" 2>/dev/null || true)"
fi
case "$rc" in
    132|133|134|135|136|138|139) crashed="${crashed:+$crashed }exit code $rc (crash signal $((rc - 128)))" ;; #SIGILL, SIGTRAP, SIGABRT, SIGEMT, SIGFPE, SIGBUS, SIGSEGV
esac
rm -rf "$home" "$marker"
[ "$shown" = yes ] || { echo "::error::$name showed no window within $timeout s"; exit 1; }
[ -z "$crashed" ] || { echo "::error::$name crashed: $crashed"; exit 1; }
echo "$name started, showed a window and ended with exit code $rc"
