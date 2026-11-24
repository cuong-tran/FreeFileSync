#!/usr/bin/env bash
# End-to-end test of crash reporting (zen/crash_report.h) against a fake Sentry endpoint on 127.0.0.1 (fake_sentry.py):
#   (a) FFS_TEST_CRASH=segv (FreeFileSync) and abort (RealTimeSync): a minidump upload arrives, with the expected release,
#       environment and app tag, and no user name, host name or home folder in sentry-native's event and breadcrumbs
#   (b) FFS_TEST_CRASH=internal-error (FreeFileSync): an envelope arrives whose message has its paths scrubbed
#   (c) reporting turned off (GlobalSettings.xml <CrashReports Enabled="false"/>, for both apps; FFS_NO_CRASH_REPORT=1):
#       a real crash, nothing arrives
#   (d) crashpad_handler missing: FreeFileSync starts normally (.github/build/check-app-starts.sh), nothing arrives
#   (e) only with --default-build: the given binaries of a default build contain no sentry symbols or strings, and not the DSN
#
# Usage: Tests/sentry/run.sh [<bin folder>]          (a) to (d) on a FFS_SENTRY=1 build (default: FreeFileSync/Build/Bin)
#        Tests/sentry/run.sh --default-build <binary>...   (e)
#   SENTRY_TEST_DSN          the DSN the Sentry build was made with (default http://testkey@127.0.0.1:38917/1); the fake
#                            endpoint listens on its port. A real DSN in SENTRY_DSN is also looked for in (e), never printed.
#   SENTRY_TEST_ENVIRONMENT  environment the build was made with (default ci)
#   FFS_EXE, RTS_EXE         binary names in the bin folder (default FreeFileSync_<arch>, RealTimeSync_<arch>)
#   WAIT_SECONDS             longest wait for an upload (default 60); waits poll, they don't sleep for fixed times
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
        # case-sensitive: wxWidgets has "HasEntry"; captured first, so grep -q can't kill strings/nm with SIGPIPE
        strs="$(strings -a "$bin")"
        syms="$(nm -C "$bin" 2>/dev/null || true)" # nothing for a stripped binary: the strings check covers it
        if grep -E 'sentry|crashpad_client|Crashpad' <<<"$strs" | head -5 | grep .; then echo "FAILED: $bin contains sentry strings" >&2; failed=1; fi
        if grep -E 'sentry_|crashpad::' <<<"$syms" | head -5 | grep .; then echo "FAILED: $bin contains sentry symbols" >&2; failed=1; fi
        for dsn in "$DSN" "${SENTRY_DSN:-}"; do
            [ -n "$dsn" ] || continue
            if grep -qF -- "$dsn" <<<"$strs"; then echo "FAILED: $bin contains a DSN" >&2; failed=1; fi
        done
        [ "$failed" = 0 ] && echo "ok: $(basename "$bin") has no sentry code and no DSN"
    done
    exit "$failed"
fi

#------------------------------------------------------------------------------------------------------------------------
BIN_DIR="$(realpath -- "${1:-$ROOT/FreeFileSync/Build/Bin}")"
ARCH="$(uname -m)"
FFS="$BIN_DIR/${FFS_EXE:-FreeFileSync_$ARCH}"
RTS="$BIN_DIR/${RTS_EXE:-RealTimeSync_$ARCH}"
RES_DIR="$(dirname "$BIN_DIR")/Resources"
ENVIRONMENT="${SENTRY_TEST_ENVIRONMENT:-ci}"
VERSION="$(sed -n 's/^const char ffsVersion\[\] = "\([^"]*\)".*/\1/p' "$ROOT/FreeFileSync/Source/version/version.h")"
RELEASE_PREFIX="freefilesync@$VERSION+"
PORT="$(sed -nE 's#^[a-z]+://[^@/]*@127\.0\.0\.1:([0-9]+)/.*#\1#p' <<<"$DSN")"
[ -n "$PORT" ] || { echo "error: SENTRY_TEST_DSN must point to 127.0.0.1:<port>" >&2; exit 2; }
for f in "$FFS" "$RTS" "$BIN_DIR/crashpad_handler"; do
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

# run_app <name> <binary> <env assignments...>: start the app under Xvfb; return once it has exited by itself, or once a
# request arrived (the app is stopped then), WAIT_SECONDS at most. Output: $WORK/<name>/app.log, exit code: $WORK/<name>/exit.
# A crashed app's crashpad_handler keeps running to upload the report; cleanup() and wait_handler_done() end it.
run_app()
{
    local name="$1" bin="$2"
    shift 2
    local home="$WORK/$name/home"
    # crashpad_handler gets --database=<home>/.config/...: wait_handler_done() and cleanup() find it by that
    ( rc=0 # not "cmd; echo $?": set -e would end the subshell at a crashed app
      "${RUN_AS[@]}" env -i PATH="$PATH" HOME="$home" XDG_CONFIG_HOME="$home/.config" LANG=C.UTF-8 NO_AT_BRIDGE=1 "$@" \
          xvfb-run -a -s "-screen 0 1280x1024x24" "$bin" > "$WORK/$name/app.log" 2>&1 || rc=$?
      echo "$rc" > "$WORK/$name/exit" ) &
    local pid=$! i
    for (( i = 0; i < WAIT_SECONDS * 10; ++i )); do
        [ -f "$WORK/$name/exit" ] && break
        [ -n "$(ls "$WORK/$name/received" 2>/dev/null)" ] && break
        sleep 0.1
    done
    if [ ! -f "$WORK/$name/exit" ]; then
        # still running: SIGTERM ends FreeFileSync and RealTimeSync right away (their shutdown handler); then xvfb-run ends
        pkill -TERM -x -f -- "$(sed 's/[][\\.*^$+?(){}|]/\\&/g' <<<"$bin")" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
}

# wait_for_request <name>: true once a complete request arrived (WAIT_SECONDS at most)
wait_for_request()
{
    local i
    for (( i = 0; i < WAIT_SECONDS * 10; ++i )); do
        compgen -G "$WORK/$1/received/*/request.json" >/dev/null && return 0
        sleep 0.1
    done
    return 1
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
# (a) crashes
crash_test() # <name> <binary> <app tag> <FFS_TEST_CRASH value>
{
    local name="$1" bin="$2" app="$3" kind="$4"
    start_server "$name"
    local home; home="$(new_home "$name")"
    run_app "$name" "$bin" FFS_TEST_CRASH="$kind"
    if wait_for_request "$name"; then
        local forbid; mapfile -t forbid < <(forbidden "$home")
        if python3 -I "$HERE/check_report.py" crash "$(ls -d "$WORK/$name/received"/*/ | head -n 1)" "$RELEASE_PREFIX" "$ENVIRONMENT" "$app" "${forbid[@]}"; then
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

# (b) internal error with paths in the message
name=b-internal
start_server "$name"
home="$(new_home "$name")"
run_app "$name" "$FFS" FFS_TEST_CRASH=internal-error
if wait_for_request "$name"; then
    mapfile -t forbid < <(forbidden "$home")
    if python3 -I "$HERE/check_report.py" internal "$(ls -d "$WORK/$name/received"/*/ | head -n 1)" "$RELEASE_PREFIX" "$ENVIRONMENT" FreeFileSync "${forbid[@]}"; then
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

# (c) turned off: a real crash, and nothing may arrive (waits until crashpad_handler is done with it)
nothing_test() # <name> <binary> <description> <expect a local report: yes|no> <env assignments...>
{
    local name="$1" bin="$2" descr="$3" localReport="$4"
    shift 4
    run_app "$name" "$bin" FFS_TEST_CRASH=segv "$@"
    wait_handler_done "$name"
    sleep 1 # the handler is gone, but let a request that is already in flight land
    local rc; rc="$(cat "$WORK/$name/exit" 2>/dev/null || echo none)"
    local dumps; dumps="$(find "$WORK/$name/home" -name '*.dmp' 2>/dev/null | wc -l)"
    if [ "$rc" != 139 ]; then
        show_logs "$name"
        fail "$name: $descr: the app did not crash with SIGSEGV (exit code $rc)"
    elif [ -n "$(ls "$WORK/$name/received")" ]; then
        fail "$name: $descr, but a report arrived: $(ls "$WORK/$name/received")"
    elif [ "$localReport" = yes ] && [ "$dumps" -eq 0 ]; then
        fail "$name: $descr: no minidump kept locally, so the crash was not caught at all"
    else
        pass "$name: $descr: crashed (minidumps kept locally: $dumps), nothing arrived"
    fi
    stop_server
}
opt_out_setting() # <name>: GlobalSettings.xml with crash reports off; RealTimeSync needs Language and ColorTheme as well
{                  # (it ignores a file without them); FreeFileSync takes defaults for the other settings
    mkdir -p "$WORK/$1/home/.config/FreeFileSync"
    printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' '<FreeFileSync XmlType="GLOBAL" XmlFormat="28">' \
        '    <Language Code="en_US"/>' '    <ColorTheme Appearance="Default"/>' \
        '    <CrashReports Enabled="false"/>' '</FreeFileSync>' > "$WORK/$1/home/.config/FreeFileSync/GlobalSettings.xml"
    [ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$1"
}
for app in FreeFileSync RealTimeSync; do
    name="c-setting-$app"
    start_server "$name"; new_home "$name" >/dev/null; opt_out_setting "$name"
    bin="$FFS"; [ "$app" = RealTimeSync ] && bin="$RTS"
    nothing_test "$name" "$bin" "$app with <CrashReports Enabled=\"false\"/>" yes
done
name=c-env
start_server "$name"; new_home "$name" >/dev/null
nothing_test "$name" "$FFS" "FreeFileSync with FFS_NO_CRASH_REPORT=1" no FFS_NO_CRASH_REPORT=1

# (d) no crash handler: starts normally (main window, no error), reporting off, nothing arrives
name=d-no-handler
start_server "$name"
mkdir -p "$WORK/$name/app/Bin"
cp -a "$FFS" "$WORK/$name/app/Bin/"
for lib in "$BIN_DIR"/*.so*; do # a package's bundled libraries ($ORIGIN rpath)
    [ -e "$lib" ] && cp -a "$lib" "$WORK/$name/app/Bin/"
done
ln -s "$RES_DIR" "$WORK/$name/app/Resources"
[ "${#RUN_AS[@]}" -eq 0 ] || chown -R nobody: "$WORK/$name"
if WAIT_SECONDS=30 "$ROOT/.github/build/check-app-starts.sh" "$WORK/$name/app/Bin/$(basename "$FFS")" '^FreeFileSync [0-9.]+ ' > "$WORK/$name/check.log" 2>&1; then
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
