#!/usr/bin/env bash
# End-to-end test of crash reporting (zen/crash_report.h) against a fake Sentry endpoint on 127.0.0.1 (fake_sentry.py):
#   (a) FFS_TEST_CRASH=segv (FreeFileSync) and abort (RealTimeSync): a minidump upload arrives, with the expected release,
#       environment and app tag, and no user name, host name or home folder in sentry-native's event and breadcrumbs
#   (b) FFS_TEST_CRASH=internal-error (FreeFileSync): an envelope arrives whose message has its paths scrubbed
#   (c) reporting turned off: GlobalSettings.xml <CrashReports Enabled="false"/> (both apps): a real crash, nothing arrives;
#       FFS_NO_CRASH_REPORT=1: no reporter, so no test crash either: the app starts normally, nothing arrives
#   (d) crashpad_handler missing: FreeFileSync starts normally (.github/build/check-app-starts.sh), nothing arrives
#   (f) not asked yet (no answer stored): an internal error and a crash are kept, not sent; the next start with the answer
#       "send" sends both, the answer "don't send" deletes them
#   (g) RealTimeSync running with "send" follows "don't send" stored meanwhile (by FreeFileSync), without a restart
#   (h) two FreeFileSync processes: the user answers the first-start notice with "don't send" (keyboard, xdotool) in a
#       running one, which stores it at once; a second one started then crashes and sends nothing. And a process with no
#       answer of its own (another GlobalSettings.xml) leaves the recorded "send" as it is but sends nothing of its own
#   (o) screenshot of the options dialog with the crash report checkbox (SCREENSHOT_DIR=<folder> keeps it); without an
#       answer, OK in the options dialog and a normal exit store no consent
#   (a), (b) and (f) store the answer "send" in GlobalSettings.xml: without an answer nothing is sent
#   (e) only with --default-build: the given binaries of a default build contain no sentry symbols or strings, and not the DSN
#
# macOS: the same scenarios with the app bundles (crashpad_handler in Contents/MacOS, settings and the report database in
#   ~/Library/Application Support/FreeFileSync), started without Xvfb; (c)'s and (d)'s start checks use check-mac-app-starts.sh.
#   (h) and (o) drive the GUI with xdotool and are skipped there, saying so in the log. <bin folder> is Build/Bin/macos-<arch>.
#
# Usage: Tests/sentry/run.sh [<bin folder>]          (a) to (d) on a FFS_SENTRY=1 build (default: FreeFileSync/Build/Bin)
#        Tests/sentry/run.sh --default-build <binary>...   (e)
#   SENTRY_TEST_DSN          the DSN the Sentry build was made with (default http://testkey@127.0.0.1:38917/1); the fake
#                            endpoint listens on its port. A real DSN in SENTRY_DSN is also looked for in (e), never printed.
#   SENTRY_TEST_ENVIRONMENT  environment the build was made with (default ci)
#   FFS_EXE, RTS_EXE         binary names in the bin folder (default FreeFileSync_<arch>, RealTimeSync_<arch>)
#   WAIT_SECONDS             longest wait for an upload (default 60); waits poll, they don't sleep for fixed times
#   SCREENSHOT_DIR           keep the screenshots of (h) and (o) there
# Build first, e.g.: FFS_SENTRY=1 SENTRY_DSN=http://testkey@127.0.0.1:38917/1 SENTRY_ENVIRONMENT=ci .github/build/build-free-file-sync.sh
# Needs python3, xvfb-run and xdotool (and for (d) what check-app-starts.sh needs). Started as root, the apps run as "nobody":
# as root FreeFileSync keeps its settings in root's home, not in $HOME.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/Tests/sentry"
DSN="${SENTRY_TEST_DSN:-http://testkey@127.0.0.1:38917/1}"
WAIT_SECONDS="${WAIT_SECONDS:-60}"

#------------------------------------------------------------------------------------------------------------------------
# (e) default build: no Sentry code, no DSN
if [ "${1:-}" = --default-build ]; then
    shift
    [ $# -gt 0 ] || { echo "usage: $0 --default-build <binary>..." >&2; exit 2; }
    failed=0
    for bin in "$@"; do
        [ -f "$bin" ] || { echo "error: $bin not found" >&2; exit 2; }
        # sentry-native's own markers only: not "sentry" alone, which is also in C++ library names (std::basic_ostream::sentry,
        # mangled "...6sentryC1...") and, case-insensitively, in wxWidgets' "HasEntry". Captured first, so that grep -q can't
        # kill strings/nm with SIGPIPE.
        strs="$(strings -a "$bin")"
        syms="$(nm -C "$bin" 2>/dev/null || true)" # nothing for a stripped binary: the strings check covers it
        # matches captured first: in "grep | head" grep can die of SIGPIPE, which pipefail turns into "no match"
        found="$(grep -E 'sentry[_.-]|getsentry|[Cc]rashpad' <<<"$strs" || true)"
        if [ -n "$found" ]; then sed -n 1,5p <<<"$found"; echo "FAILED: $bin contains sentry strings" >&2; failed=1; fi
        found="$(grep -E 'sentry_|crashpad::' <<<"$syms" || true)"
        if [ -n "$found" ]; then sed -n 1,5p <<<"$found"; echo "FAILED: $bin contains sentry symbols" >&2; failed=1; fi
        for dsn in "$DSN" "${SENTRY_DSN:-}"; do
            [ -n "$dsn" ] || continue
            if grep -qF -- "$dsn" <<<"$strs"; then echo "FAILED: $bin contains a DSN" >&2; failed=1; fi
        done
        [ "$failed" = 0 ] && echo "ok: $(basename "$bin") has no sentry code and no DSN"
    done
    exit "$failed"
fi

#------------------------------------------------------------------------------------------------------------------------
OS="$(uname -s)"
ARCH="${ARCH:-$(uname -m)}"
if [ "$OS" = Darwin ]; then
    ((BASH_VERSINFO[0] >= 4)) || { echo "error: needs bash 4 or later (macOS: brew install bash)" >&2; exit 2; }
    BIN_DIR="$(realpath "${1:-$ROOT/FreeFileSync/Build/Bin/macos-$ARCH}")"
    FFS="$BIN_DIR/FreeFileSync.app/Contents/MacOS/FreeFileSync"
    RTS="$BIN_DIR/RealTimeSync.app/Contents/MacOS/RealTimeSync"
    HANDLERS=("$(dirname "$FFS")/crashpad_handler" "$(dirname "$RTS")/crashpad_handler")
    RES_DIR="$BIN_DIR/FreeFileSync.app/Contents/Resources"
else
    BIN_DIR="$(realpath -- "${1:-$ROOT/FreeFileSync/Build/Bin}")"
    FFS="$BIN_DIR/${FFS_EXE:-FreeFileSync_$ARCH}"
    RTS="$BIN_DIR/${RTS_EXE:-RealTimeSync_$ARCH}"
    HANDLERS=("$BIN_DIR/crashpad_handler")
    RES_DIR="$(dirname "$BIN_DIR")/Resources"
fi
# cfg_dir <home>: the apps' configuration folder (GlobalSettings.xml, CrashReports/)
cfg_dir()
{
    if [ "$OS" = Darwin ]; then echo "$1/Library/Application Support/FreeFileSync"; else echo "$1/.config/FreeFileSync"; fi
}
ENVIRONMENT="${SENTRY_TEST_ENVIRONMENT:-ci}"
VERSION="$(sed -n 's/^const char ffsVersion\[\] = "\([^"]*\)".*/\1/p' "$ROOT/FreeFileSync/Source/version/version.h")"
RELEASE_PREFIX="freefilesync@$VERSION+"
PORT="$(sed -nE 's#^[a-z]+://[^@/]*@127\.0\.0\.1:([0-9]+)/.*#\1#p' <<<"$DSN")"
[ -n "$PORT" ] || { echo "error: SENTRY_TEST_DSN must point to 127.0.0.1:<port>" >&2; exit 2; }
for f in "$FFS" "$RTS" "${HANDLERS[@]}"; do
    [ -x "$f" ] || { echo "error: $f not found: build with FFS_SENTRY=1 first (see the header of $0)" >&2; exit 2; }
done
grep -qF -- "$DSN" <(strings -a "$FFS") || { echo "error: $FFS was not built with SENTRY_DSN=$DSN" >&2; exit 2; }

WORK="$(mktemp -d)"
SERVER_PID=""
cleanup()
{
    [ -z "$SERVER_PID" ] || kill "$SERVER_PID" 2>/dev/null || true
    pkill -f -- "--database=$WORK/" 2>/dev/null || true # crashpad_handler processes of this run
    rm -rf "$WORK"
}
trap cleanup EXIT
chmod 755 "$WORK"

# the user the apps run as: the caller, or "nobody" when started as root (see above)
RUN_AS=()
APP_USER="$(id -un)"
if [ "$(id -u)" -eq 0 ]; then
    RUN_AS=(setpriv --reuid="$(id -u nobody)" --regid="$(id -g nobody)" --init-groups --)
    APP_USER=nobody
fi
HOST_NAME="$(hostname 2>/dev/null || true)"
# texts that must not be in an event: user and host name (only when long enough to be meaningful), the app's home folder
forbidden()
{
    local words=("$1")
    [ "${#APP_USER}" -ge 3 ] && words+=("$APP_USER")
    [ "${#HOST_NAME}" -ge 3 ] && words+=("$HOST_NAME")
    printf '%s\n' "${words[@]}"
}

# placeholder for the image the upstream source zip lacks (see linux-build.yml): a first start would show an error otherwise
if [ ! -f "$RES_DIR/Animal.dat" ]; then
    python3 -I - "$RES_DIR/Animal.dat" <<'EOF' # an 8x8 gray JPEG (the welcome dialog loads it as JPEG)
import base64, sys
open(sys.argv[1], "wb").write(base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAAAAAAD/2wBDABALDA4MChAODQ4SERATGCgaGBYWGDEjJR0oOjM9PDkzODdASFxOQERXRTc4UG1RV19iZ2hnPk1xeXBkeFxlZ2P/wAALCAAIAAgBAREA/8QAFAABAAAAAAAAAAAAAAAAAAAAAf/EABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAD8AH//Z"))
EOF
    PLACEHOLDER="$RES_DIR/Animal.dat"
    trap 'cleanup; rm -f "$PLACEHOLDER"' EXIT
fi

FAILED=()
pass() { echo "== ok: $1"; }
fail() { echo "== FAILED: $1" >&2; FAILED+=("$1"); }

# start_server <name>: fake endpoint for one scenario; its requests go to $WORK/<name>/received
start_server()
{
    mkdir -p "$WORK/$1/received"
    python3 -I "$HERE/fake_sentry.py" "$WORK/$1/received" "$PORT" > "$WORK/$1/server.log" 2>&1 &
    SERVER_PID=$!
    local i
    for (( i = 0; i < 100; ++i )); do
        grep -q '^port ' "$WORK/$1/server.log" 2>/dev/null && return 0
        kill -0 "$SERVER_PID" 2>/dev/null || break
        sleep 0.1
    done
    cat "$WORK/$1/server.log" >&2
    echo "error: the fake endpoint did not start on port $PORT" >&2
    exit 2
}
stop_server() { kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; SERVER_PID=""; }

# new_home <name>: an empty home folder for the app's user
new_home()
{
    mkdir -p "$WORK/$1/home"
    [ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$1"
    echo "$WORK/$1/home"
}

# run_app <name> <binary> <env assignments...>: start the app under Xvfb; return once it has exited by itself, or once
# "$STOP_WHEN <name>" is true (default: a request arrived; the app is stopped then), WAIT_SECONDS at most. Output: $WORK/<name>/app.log, exit code: $WORK/<name>/exit.
# A crashed app's crashpad_handler keeps running to upload the report; cleanup() and wait_handler_done() end it.
run_app()
{
    local name="$1" bin="$2"
    shift 2
    local home="$WORK/$name/home"
    rm -f "$WORK/$name/exit" # from an earlier run of the same scenario
    # crashpad_handler gets --database=<home>/.config/...: wait_handler_done() and cleanup() find it by that
    ( rc=0 # not "cmd; echo $?": set -e would end the subshell at a crashed app
      if [ "$OS" = Darwin ]; then # the window server is there: no Xvfb
          env -i PATH="$PATH" HOME="$home" LANG=en_US.UTF-8 "$@" "$bin" > "$WORK/$name/app.log" 2>&1 || rc=$?
      else
          "${RUN_AS[@]}" env -i PATH="$PATH" HOME="$home" XDG_CONFIG_HOME="$home/.config" LANG=C.UTF-8 NO_AT_BRIDGE=1 "$@" \
              xvfb-run -a -s "-screen 0 1280x1024x24" "$bin" > "$WORK/$name/app.log" 2>&1 || rc=$?
      fi
      echo "$rc" > "$WORK/$name/exit" ) &
    local pid=$! i
    for (( i = 0; i < WAIT_SECONDS * 10; ++i )); do
        [ -f "$WORK/$name/exit" ] && break
        "${STOP_WHEN:-request_arrived}" "$name" && break
        sleep 0.1
    done
    if [ ! -f "$WORK/$name/exit" ]; then
        # still running: SIGTERM normally ends FreeFileSync and RealTimeSync right away (their shutdown handler), then xvfb-run
        # ends. That handler isn't async-signal-safe (upstream says so) and can deadlock, rarely: SIGKILL after 10 s.
        local pattern; pattern="$(sed 's/[][\\.*^$+?(){}|]/\\&/g' <<<"$bin")"
        pkill -TERM -x -f -- "$pattern" 2>/dev/null || true
        for (( i = 0; i < 100; ++i )); do
            [ -f "$WORK/$name/exit" ] && break
            sleep 0.1
        done
        if [ ! -f "$WORK/$name/exit" ]; then
            echo "note: $(basename "$bin") did not end on SIGTERM within 10 s, killed" >&2
            pkill -KILL -x -f -- "$pattern" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true
}

request_arrived() { [ -n "$(ls "$WORK/$1/received" 2>/dev/null)" ]; }

# first_request <name> [kind]: folder of the first saved request (of that kind); a glob, not "ls | head" (SIGPIPE, see above)
first_request()
{
    local dirs=("$WORK/$1/received"/*"${2:+-$2}"/)
    echo "${dirs[0]}"
}

# wait_for_request <name> [count]: true once <count> (default 1) complete requests arrived (WAIT_SECONDS at most)
wait_for_request()
{
    local i files
    for (( i = 0; i < WAIT_SECONDS * 10; ++i )); do
        files=("$WORK/$1/received"/*/request.json)
        [ -e "${files[0]}" ] && [ "${#files[@]}" -ge "${2:-1}" ] && return 0
        sleep 0.1
    done
    return 1
}

# the report database of an app in a scenario, and what is kept there for a later consent (see crash_report_sentry.cpp)
database() { echo "$(cfg_dir "$WORK/$1/home")/CrashReports/$2"; }
kept_reports() # <name> <app>: number of envelopes and crash dumps kept for a later consent
{
    local db; db="$(database "$1" "$2")"
    find "$db/cache" "$db/pending" -type f \( -name '*.envelope' -o -name '*.dmp' \) 2>/dev/null | wc -l
}

# settings <name> <true|false>: GlobalSettings.xml with this answer to the first-start notice. RealTimeSync needs Language
# and ColorTheme as well (it ignores a file without them); FreeFileSync takes defaults for the other settings. The welcome
# dialog counts as seen, so that no modal dialog is in the way of GUI driving.
settings()
{
    local dir; dir="$(cfg_dir "$WORK/$1/home")"
    mkdir -p "$dir"
    printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' '<FreeFileSync XmlType="GLOBAL" XmlFormat="28">' \
        '    <Language Code="en_US"/>' '    <ColorTheme Appearance="Default"/>' \
        "    <WelcomeDialogVersion>$VERSION</WelcomeDialogVersion>" \
        "    <CrashReports Enabled=\"$2\"/>" '</FreeFileSync>' > "$dir/GlobalSettings.xml"
    [ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$1"
}

# wait_handler_done <name>: crashpad_handler of this scenario has exited (it writes, and maybe uploads, the report first)
wait_handler_done()
{
    local i
    for (( i = 0; i < WAIT_SECONDS * 10; ++i )); do
        pgrep -f -- "--database=$WORK/$1/" >/dev/null || return 0
        sleep 0.1
    done
    pkill -f -- "--database=$WORK/$1/" 2>/dev/null || true
    return 0
}

show_logs() { sed 's/^/    app: /' "$WORK/$1/app.log" | tail -20 >&2 || true; }

#------------------------------------------------------------------------------------------------------------------------
# (a) crashes, with the user's consent
crash_test() # <name> <binary> <app tag> <FFS_TEST_CRASH value>
{
    local name="$1" bin="$2" app="$3" kind="$4"
    start_server "$name"
    local home; home="$(new_home "$name")"
    settings "$name" true
    run_app "$name" "$bin" FFS_TEST_CRASH="$kind"
    if wait_for_request "$name"; then
        local forbid; mapfile -t forbid < <(forbidden "$home")
        if python3 -I "$HERE/check_report.py" crash "$(first_request "$name")" "$RELEASE_PREFIX" "$ENVIRONMENT" "$app" "${forbid[@]}"; then
            pass "$name: crash report of $app ($kind) arrived and is clean"
        else
            fail "$name: crash report of $app ($kind)"
        fi
    else
        show_logs "$name"
        fail "$name: no crash report of $app ($kind) within $WAIT_SECONDS s"
    fi
    wait_handler_done "$name"
    stop_server
}
crash_test a-segv  "$FFS" FreeFileSync segv
crash_test a-abort "$RTS" RealTimeSync abort

# (b) internal error with paths in the message, with the user's consent
name=b-internal
start_server "$name"
home="$(new_home "$name")"
settings "$name" true
run_app "$name" "$FFS" FFS_TEST_CRASH=internal-error
if wait_for_request "$name"; then
    mapfile -t forbid < <(forbidden "$home")
    if python3 -I "$HERE/check_report.py" internal "$(first_request "$name")" "$RELEASE_PREFIX" "$ENVIRONMENT" FreeFileSync "${forbid[@]}"; then
        pass "$name: internal error arrived, scrubbed"
    else
        fail "$name: internal error report"
    fi
else
    show_logs "$name"
    fail "$name: no internal error report within $WAIT_SECONDS s"
fi
wait_handler_done "$name"
stop_server

# (c) turned off in GlobalSettings.xml: a real crash, nothing arrives, and the dump is not kept for later
for app in FreeFileSync RealTimeSync; do
    name="c-setting-$app"
    bin="$FFS"; [ "$app" = RealTimeSync ] && bin="$RTS"
    start_server "$name"; new_home "$name" >/dev/null; settings "$name" false
    run_app "$name" "$bin" FFS_TEST_CRASH=segv
    wait_handler_done "$name"
    sleep 1 # the handler is gone, but let a request that is already in flight land
    rc="$(cat "$WORK/$name/exit" 2>/dev/null || echo none)"
    dumps="$(find "$(database "$name" "$app")" -name '*.dmp' 2>/dev/null | wc -l)"
    if [ "$rc" != 1 ]; then # the crash handler discards the crash and ends the process with EXIT_FAILURE
        show_logs "$name"
        fail "$name: the app did not crash, or its crash was not discarded (exit code $rc)"
    elif [ "$dumps" -ne 0 ]; then
        fail "$name: turned off, but a crash dump was kept ($dumps)"
    elif [ -n "$(ls "$WORK/$name/received")" ]; then
        fail "$name: turned off, but a report arrived: $(ls "$WORK/$name/received")"
    else
        pass "$name: turned off: crashed, no dump kept, nothing arrived"
    fi
    stop_server
done

# (c) FFS_NO_CRASH_REPORT=1: no reporter at all, so FFS_TEST_CRASH is ignored: the app starts normally, nothing arrives
name=c-env
start_server "$name"
# start_check <binary> <screenshot name>: the platform's start check, with the environment of the caller
start_check()
{
    if [ "$OS" = Darwin ]; then
        WAIT_SECONDS=30 "$ROOT/.github/build/check-mac-app-starts.sh" "${1%/Contents/MacOS/*}" '^FreeFileSync [0-9.]+ ' "$WORK/$2.png"
    else
        WAIT_SECONDS=30 "$ROOT/.github/build/check-app-starts.sh" "$1" '^FreeFileSync [0-9.]+ '
    fi
}
if FFS_NO_CRASH_REPORT=1 FFS_TEST_CRASH=segv start_check "$FFS" "$name" > "$WORK/$name/check.log" 2>&1 &&
   [ -z "$(ls "$WORK/$name/received")" ]; then
    pass "$name: FFS_NO_CRASH_REPORT=1: starts normally, no test crash, nothing arrived"
else
    sed 's/^/    /' "$WORK/$name/check.log" >&2
    fail "$name: FFS_NO_CRASH_REPORT=1: the app did not start normally, or a report arrived"
fi
stop_server

# (f) not asked yet (no answer in GlobalSettings.xml, e.g. batch runs before the first GUI start): an internal error and a
# crash are kept, not sent; the next start with the answer "send" sends both, the answer "don't send" deletes them
kept_test() # <name> <answer: true|false>
{
    local name="$1" answer="$2"
    start_server "$name"
    new_home "$name" >/dev/null
    has_kept_envelope() { [ -n "$(find "$(database "$1" FreeFileSync)/cache" -name '*.envelope' 2>/dev/null)" ]; }
    STOP_WHEN=has_kept_envelope run_app "$name" "$FFS" FFS_TEST_CRASH=internal-error
    run_app "$name" "$FFS" FFS_TEST_CRASH=segv
    wait_handler_done "$name"
    local kept; kept="$(kept_reports "$name" FreeFileSync)"
    if [ -n "$(ls "$WORK/$name/received")" ]; then
        fail "$name: not asked yet, but a report arrived: $(ls "$WORK/$name/received")"; stop_server; return
    elif [ "$kept" -ne 2 ]; then
        show_logs "$name"
        fail "$name: not asked yet: expected 1 envelope and 1 crash dump kept, found $kept"; stop_server; return
    fi

    settings "$name" "$answer"
    if [ "$answer" = true ]; then
        run_app_bg_until_requests "$name" 2
    else
        no_kept_reports() { [ "$(kept_reports "$1" FreeFileSync)" -eq 0 ]; }
        STOP_WHEN=no_kept_reports run_app "$name" "$FFS"
        wait_handler_done "$name"
        sleep 1
        if [ -n "$(ls "$WORK/$name/received")" ]; then
            fail "$name: answer \"don't send\", but the kept reports arrived: $(ls "$WORK/$name/received")"
        elif [ "$(kept_reports "$name" FreeFileSync)" -ne 0 ]; then
            fail "$name: answer \"don't send\", but the kept reports were not deleted"
        else
            pass "$name: kept while not asked, deleted after \"don't send\", nothing arrived"
        fi
    fi
    stop_server
}
# kept reports sent on the next start with consent: one envelope (the internal error) and one minidump (the crash)
run_app_bg_until_requests() # <name> <count>
{
    local name="$1" count="$2"
    enough_requests() { local f=("$WORK/$1/received"/*/request.json); [ -e "${f[0]}" ] && [ "${#f[@]}" -ge "$count" ]; }
    STOP_WHEN=enough_requests run_app "$name" "$FFS"
    if wait_for_request "$name" "$count"; then
        local kinds; kinds="$(cd "$WORK/$name/received" && ls | sed 's/^[0-9]*-//' | sort | tr '\n' ' ')"
        local env_dir; env_dir="$(first_request "$name" envelope)"
        local dmp_dir; dmp_dir="$(first_request "$name" minidump)"
        local forbid; mapfile -t forbid < <(forbidden "$WORK/$name/home")
        if [ "$kinds" = "envelope minidump " ] &&
           python3 -I "$HERE/check_report.py" internal "$env_dir" "$RELEASE_PREFIX" "$ENVIRONMENT" FreeFileSync "${forbid[@]}" &&
           python3 -I "$HERE/check_report.py" crash "$dmp_dir" "$RELEASE_PREFIX" "$ENVIRONMENT" FreeFileSync "${forbid[@]}"; then
            pass "$name: kept while not asked, both sent after \"send\""
        else
            fail "$name: after \"send\": expected the kept envelope and minidump, got: $kinds"
        fi
    else
        show_logs "$name"
        fail "$name: after \"send\": the kept reports did not arrive within $WAIT_SECONDS s ($(ls "$WORK/$name/received" | tr '\n' ' '))"
    fi
    wait_handler_done "$name"
}
kept_test f-kept-then-sent    true
kept_test f-kept-then-deleted false

# poll <seconds> <command...>: true once the command succeeds (every 0.1 s)
poll()
{
    local limit="$1" i
    shift
    for (( i = 0; i < limit * 10; ++i )); do
        "$@" && return 0
        sleep 0.1
    done
    return 1
}
consent_is() { [ "$(cat "$(database "$1" "$2")/user-consent" 2>/dev/null)" = "$3" ]; } # <name> <app> <0|1>

# in_x <name> <script>: run a bash script as the app's user under its own Xvfb, with the scenario's HOME (for GUI driving)
in_x()
{
    local name="$1" script="$2" home="$WORK/$1/home"
    [ "${#RUN_AS[@]}" -eq 0 ] || chown nobody: "$script"
    "${RUN_AS[@]}" env -i PATH="$PATH" HOME="$home" XDG_CONFIG_HOME="$home/.config" LANG=C.UTF-8 NO_AT_BRIDGE=1 \
        FFS="$FFS" OUT="$WORK/$name" xvfb-run -a -s "-screen 0 1280x1024x24" bash "$script" > "$WORK/$name/x.log" 2>&1 || true
}
keep_screenshot() { [ -z "${SCREENSHOT_DIR:-}" ] || { mkdir -p "$SCREENSHOT_DIR"; cp "$1" "$SCREENSHOT_DIR/"; }; }

# (g) RealTimeSync follows an answer stored while it runs (crash_report_init.h: the setting watch)
name=g-rts-follows
start_server "$name"; new_home "$name" >/dev/null; settings "$name" true
rts_running() { false; } # stopped by the checks below
( STOP_WHEN=rts_running WAIT_SECONDS=$(( WAIT_SECONDS + 30 )) run_app "$name" "$RTS" FFS_CRASH_REPORT_WATCH_MS=200 ) &
rtsRun=$!
if poll "$WAIT_SECONDS" consent_is "$name" RealTimeSync 1; then
    settings "$name" false # what FreeFileSync writes when the user answers "don't send"
    if poll 10 consent_is "$name" RealTimeSync 0; then
        pass "$name: RealTimeSync applied \"don't send\" stored while it ran"
    else
        fail "$name: RealTimeSync did not apply \"don't send\" stored while it ran"
    fi
else
    show_logs "$name"; fail "$name: RealTimeSync did not apply the stored \"send\" at start"
fi
pkill -TERM -x -f -- "$(sed 's/[][\\.*^$+?(){}|]/\\&/g' <<<"$RTS")" 2>/dev/null || true
wait "$rtsRun" 2>/dev/null || true
wait_handler_done "$name"
[ -z "$(ls "$WORK/$name/received")" ] || fail "$name: a report arrived: $(ls "$WORK/$name/received")"
stop_server

# (h) and (o) drive the GUI with xdotool under Xvfb: Linux only
if [ "$OS" = Darwin ]; then
    echo "== skipped on macOS: (h) two processes and (o) options dialog: they drive the GUI by keyboard with xdotool under Xvfb, which macOS lacks"
else
# (h) the answer given in a running FreeFileSync reaches a second process at once
name=h-two-processes
start_server "$name"
new_home "$name" >/dev/null
mkdir -p "$WORK/$name/home/.config/FreeFileSync"
printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' '<FreeFileSync XmlType="GLOBAL" XmlFormat="28">' \
    '    <WelcomeDialogVersion>'"$VERSION"'</WelcomeDialogVersion>' '</FreeFileSync>' > "$WORK/$name/home/.config/FreeFileSync/GlobalSettings.xml"
[ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$name"
cat > "$WORK/$name/drive.sh" <<'EOF'
# A: first start without an answer => the notice; answer "don't send": focus the checkbox (shift+Tab), untick it, close
"$FFS" > "$OUT/a.log" 2>&1 &
a=$!
for i in $(seq 1 300); do w=$(xdotool search --onlyvisible --name '^Crash reports$' 2>/dev/null | head -n 1); [ -n "$w" ] && break; sleep 0.1; done
[ -n "$w" ] || { echo "no notice"; kill $a; exit 0; }
xdotool windowactivate --sync "$w" 2>/dev/null; xdotool windowfocus --sync "$w"; sleep 0.5
xdotool key --window "$w" shift+Tab; sleep 0.2; xdotool key --window "$w" space; sleep 0.2
import -window root "$OUT/notice.png" 2>/dev/null
xdotool key --window "$w" Return
for i in $(seq 1 100); do grep -q 'CrashReports Enabled="false"' "$HOME/.config/FreeFileSync/GlobalSettings.xml" && break; sleep 0.1; done
cp "$HOME/.config/FreeFileSync/GlobalSettings.xml" "$OUT/settings-after-answer.xml"
kill -0 $a 2>/dev/null && echo "A still running after the answer"
# B: started now, crashes
FFS_TEST_CRASH=segv "$FFS" > "$OUT/b.log" 2>&1; echo "B exit $?"
kill $a; wait $a
EOF
in_x "$name" "$WORK/$name/drive.sh"
wait_handler_done "$name"; sleep 1
cat "$WORK/$name/x.log" >&2
[ -f "$WORK/$name/notice.png" ] && keep_screenshot "$WORK/$name/notice.png"
if grep -q 'CrashReports Enabled="false"' "$WORK/$name/settings-after-answer.xml" 2>/dev/null &&
   grep -q "<WelcomeDialogVersion>$VERSION</WelcomeDialogVersion>" "$WORK/$name/settings-after-answer.xml" &&
   grep -q 'A still running after the answer' "$WORK/$name/x.log" && grep -q 'B exit 1$' "$WORK/$name/x.log" &&
   [ -z "$(ls "$WORK/$name/received")" ] && consent_is "$name" FreeFileSync 0; then
    pass "$name: \"don't send\" answered in a running FreeFileSync was stored at once (other settings kept); a second process crashed and sent nothing"
else
    fail "$name: answer in a running FreeFileSync not stored at once, or the second process sent a report: $(ls "$WORK/$name/received" | tr '\n' ' ')"
fi
# ...and a process without an answer of its own (another, missing GlobalSettings.xml) next to a recorded "send"
settings "$name" true
send_recorded() { consent_is "$1" FreeFileSync 1; }
STOP_WHEN=send_recorded run_app "$name" "$FFS" # applies "send": the shared database records it
if consent_is "$name" FreeFileSync 1; then
    cat > "$WORK/$name/drive2.sh" <<'EOF'
FFS_TEST_CRASH=segv "$FFS" "$OUT/other/GlobalSettings.xml" > "$OUT/c.log" 2>&1; echo "C exit $?"
FFS_TEST_CRASH=internal-error timeout 5 "$FFS" "$OUT/other/GlobalSettings.xml" > "$OUT/d.log" 2>&1; echo "D exit $?"
EOF
    in_x "$name" "$WORK/$name/drive2.sh"
    wait_handler_done "$name"; sleep 1
    if consent_is "$name" FreeFileSync 1 && [ -z "$(ls "$WORK/$name/received")" ] && grep -q 'C exit 1$' "$WORK/$name/x.log"; then
        pass "$name: a process without an answer left the recorded \"send\" alone and sent nothing of its own"
    else
        cat "$WORK/$name/x.log" >&2
        fail "$name: a process without an answer changed the recorded consent ($(cat "$(database "$name" FreeFileSync)/user-consent")) or sent: $(ls "$WORK/$name/received" | tr '\n' ' ')"
    fi
else
    fail "$name: \"send\" was not recorded"
fi
stop_server

# (o) the options dialog shows the crash report checkbox: screenshot (Tools menu by keyboard: alt+t, then "o")
name=o-options
new_home "$name" >/dev/null; settings "$name" true
cat > "$WORK/$name/drive.sh" <<'EOF'
"$FFS" > "$OUT/a.log" 2>&1 &
a=$!
for i in $(seq 1 300); do w=$(xdotool search --onlyvisible --name '^FreeFileSync [0-9.]+ ' 2>/dev/null | head -n 1); [ -n "$w" ] && break; sleep 0.1; done
sleep 2; xdotool windowactivate --sync "$w" 2>/dev/null; xdotool windowfocus --sync "$w"; sleep 0.5
xdotool key --window "$w" alt+t; sleep 0.5
for i in $(seq 1 20); do o=$(xdotool search --onlyvisible --name '^Options$' 2>/dev/null | head -n 1); [ -n "$o" ] && break; xdotool key o; sleep 0.5; done
sleep 1; [ -n "$o" ] && import -window root "$OUT/options.png" 2>/dev/null && echo "options shown"
kill $a; wait $a
EOF
in_x "$name" "$WORK/$name/drive.sh"
if grep -q 'options shown' "$WORK/$name/x.log"; then
    keep_screenshot "$WORK/$name/options.png"
    pass "$name: options dialog screenshot${SCREENSHOT_DIR:+ in $SCREENSHOT_DIR}"
else
    fail "$name: could not open the options dialog"
fi

# ...and without an answer (no notice: FFS_NO_CRASH_REPORT=1), OK in the options dialog and a normal exit (File > Exit, which
# saves GlobalSettings.xml) store no consent: the checkbox shows unticked and the setting changes only if the user changes it
name=o-options-no-answer
new_home "$name" >/dev/null
mkdir -p "$WORK/$name/home/.config/FreeFileSync"
printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' '<FreeFileSync XmlType="GLOBAL" XmlFormat="28">' \
    '    <WelcomeDialogVersion>'"$VERSION"'</WelcomeDialogVersion>' '</FreeFileSync>' > "$WORK/$name/home/.config/FreeFileSync/GlobalSettings.xml"
[ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$name"
cat > "$WORK/$name/drive.sh" <<'EOF'
FFS_NO_CRASH_REPORT=1 "$FFS" > "$OUT/a.log" 2>&1 &
a=$!
for i in $(seq 1 300); do w=$(xdotool search --onlyvisible --name '^FreeFileSync [0-9.]+ ' 2>/dev/null | head -n 1); [ -n "$w" ] && break; sleep 0.1; done
sleep 2; xdotool windowactivate --sync "$w" 2>/dev/null; xdotool windowfocus --sync "$w"; sleep 0.5
xdotool key --window "$w" alt+t; sleep 0.5
for i in $(seq 1 20); do o=$(xdotool search --onlyvisible --name '^Options$' 2>/dev/null | head -n 1); [ -n "$o" ] && break; xdotool key o; sleep 0.5; done
[ -n "$o" ] || { echo "no options dialog"; kill $a; exit 0; }
sleep 1; import -window root "$OUT/options-no-answer.png" 2>/dev/null
xdotool key --window "$o" Return; sleep 1
xdotool windowfocus --sync "$w"; xdotool key --window "$w" alt+f; sleep 0.5; xdotool key x # File > Exit
for i in $(seq 1 100); do kill -0 $a 2>/dev/null || break; sleep 0.1; done
if kill -0 $a 2>/dev/null; then kill $a; else echo "exited normally"; fi
EOF
in_x "$name" "$WORK/$name/drive.sh"
cfgFile="$WORK/$name/home/.config/FreeFileSync/GlobalSettings.xml"
if grep -q 'exited normally' "$WORK/$name/x.log" && [ "$(grep -c . "$cfgFile")" -gt 10 ] && ! grep -q '<CrashReports' "$cfgFile"; then
    keep_screenshot "$WORK/$name/options-no-answer.png"
    pass "$name: OK in the options dialog without an answer stored no consent"
else
    cat "$WORK/$name/x.log" >&2
    fail "$name: OK in the options dialog without an answer stored a consent, or the app did not exit and save normally"
fi
fi

# (d) no crash handler: starts normally (main window, no error), reporting off, nothing arrives
name=d-no-handler
start_server "$name"
if [ "$OS" = Darwin ]; then # a copy of the bundle without the handler, signed again (ad hoc) since its content changed
    ditto "$BIN_DIR/FreeFileSync.app" "$WORK/$name/app/FreeFileSync.app"
    rm "$WORK/$name/app/FreeFileSync.app/Contents/MacOS/crashpad_handler"
    codesign --force --sign - --timestamp=none "$WORK/$name/app/FreeFileSync.app"
    noHandlerFfs="$WORK/$name/app/FreeFileSync.app/Contents/MacOS/FreeFileSync"
else
    mkdir -p "$WORK/$name/app/Bin"
    cp -a "$FFS" "$WORK/$name/app/Bin/"
    for lib in "$BIN_DIR"/*.so*; do # a package's bundled libraries ($ORIGIN rpath)
        [ -e "$lib" ] && cp -a "$lib" "$WORK/$name/app/Bin/"
    done
    ln -s "$RES_DIR" "$WORK/$name/app/Resources"
    [ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$name"
    noHandlerFfs="$WORK/$name/app/Bin/$(basename "$FFS")"
fi
if start_check "$noHandlerFfs" "$name" > "$WORK/$name/check.log" 2>&1; then
    if grep -q 'Crash reporting is off: Crash handler not found' "$WORK/$name/check.log" && [ -z "$(ls "$WORK/$name/received")" ]; then
        pass "$name: FreeFileSync starts without crashpad_handler, reporting off, nothing arrived"
    else
        sed 's/^/    /' "$WORK/$name/check.log" >&2
        fail "$name: no \"reporting is off\" note, or a report arrived"
    fi
else
    sed 's/^/    /' "$WORK/$name/check.log" >&2
    fail "$name: FreeFileSync does not start without crashpad_handler"
fi
stop_server

if [ "${#FAILED[@]}" -gt 0 ]; then
    echo "== sentry tests: ${#FAILED[@]} failed" >&2
    exit 1
fi
echo "== sentry tests: all passed"
