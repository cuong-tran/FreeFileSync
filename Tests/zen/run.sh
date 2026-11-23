#!/usr/bin/env bash
# Build and run the zen platform layer unit tests (Tests/zen/*.cpp against the zen/*.cpp they need)
#
# usage: Tests/zen/run.sh [<test name>...]      (default: all tests; "--list" lists them)
#   CXX:          compiler (default g++; needs C++26, i.e. g++ 14 or newer)
#   ZEN_TEST_BIN: build into this folder instead of a temporary one and keep it (the build always runs), e.g. to debug zen_test
#
# Linux needs the glib/gio dev packages (pkg-config). Nothing is left behind: build output and test folders live in a temporary
# folder that is removed on exit, and HOME/XDG_DATA_HOME point there so the recycle bin test can't touch the user's real trash.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'wait; rm -rf "$WORK"' EXIT # wait: compile jobs still running after a failed one
CXX="${CXX:-g++}"
BIN="${ZEN_TEST_BIN:-$WORK/bin}"
mkdir -p "$BIN"

case "$(uname -s)" in
    Linux)
        PLATFORM_CFLAGS="$(pkg-config --cflags glib-2.0 gio-2.0)" # zstring.cpp, sys_error.cpp, recycler.cpp
        PLATFORM_LIBS="$(pkg-config --libs glib-2.0 gio-2.0)"
        ;;
    *)
        echo "Tests/zen/run.sh: no build setup for $(uname -s) yet" >&2
        exit 2
        ;;
esac

# same language standard and warnings as FreeFileSync/Source/Makefile, but with assert() enabled (no -DNDEBUG)
CXXFLAGS=(-std=c++26 -O1 -g -pthread -DWXINTL_NO_GETTEXT_MACRO -I"$ROOT" -include "zen/i18n.h"
    -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion)
if "$CXX" --version | grep -q -i 'g++\|gcc'; then
    CXXFLAGS+=(-Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op)
fi

ZEN_SOURCES=(file_access file_io file_traverser file_path resolve_path zstring dir_watcher recycler sys_info sys_version process_exec thread sys_error format_unit legacy_compiler)

echo "== build ($("$CXX" --version | head -n 1))"
OBJECTS=()
compile() # <source> <object>: compile in the background, at most nproc jobs
{
    while (( $(jobs -rp | wc -l) >= $(nproc) )); do wait -n; done
    # shellcheck disable=SC2086
    "$CXX" "${CXXFLAGS[@]}" $PLATFORM_CFLAGS -c "$1" -o "$2" &
    OBJECTS+=("$2")
}
for src in "$ROOT"/Tests/zen/*.cpp; do
    compile "$src" "$BIN/$(basename "$src" .cpp).o"
done
for name in "${ZEN_SOURCES[@]}"; do
    compile "$ROOT/zen/$name.cpp" "$BIN/zen_$name.o"
done
while (( $(jobs -rp | wc -l) > 0 )); do wait -n; done # "wait" alone would not report a failed job
# shellcheck disable=SC2086
"$CXX" -pthread "${OBJECTS[@]}" $PLATFORM_LIBS -o "$BIN/zen_test"

echo "== test"
SANDBOX="$WORK/sandbox"
mkdir -p "$SANDBOX/home/.local/share" "$SANDBOX/tests"
# ZEN_TEST_*: expected by test_paths.cpp; zen reads the environment once at startup
HOME="$SANDBOX/home" XDG_DATA_HOME="$SANDBOX/home/.local/share" XDG_CONFIG_HOME="$SANDBOX/home/.config" \
ZEN_TEST_VAR="zen test value" ZEN_TEST_EMPTY="" \
    "$BIN/zen_test" --root "$SANDBOX/tests" "$@"

# the test removes its own folders; anything left is a bug in the test
if [ -n "$(ls -A "$SANDBOX/tests")" ]; then
    echo "test folders left behind:" >&2
    ls -lA "$SANDBOX/tests" >&2
    exit 1
fi
