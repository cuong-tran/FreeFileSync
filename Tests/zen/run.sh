#!/usr/bin/env bash
# Build and run the zen platform layer unit tests (Tests/zen/*.cpp against the zen/*.cpp they need)
#
# usage: Tests/zen/run.sh [<test name>...]      (default: all tests; "--list" lists them)
#   CXX:          compiler (default g++; needs C++26, i.e. g++ 14 or newer, or clang 20 or newer)
#   STDLIB:       "libc++" to build against LLVM's libc++ instead of the compiler's default C++ library (like the makefiles' STDLIB)
#   CXXFLAGS, LDFLAGS: appended to the compile/link commands
#   ZEN_TEST_BIN: build into this folder instead of a temporary one and keep it (the build always runs), e.g. to debug zen_test;
#                 run directly, zen_test skips the tests that need run.sh's sandbox (see main.cpp)
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
CXXFLAGS_TEST=(-std=c++26 -O1 -g -pthread -DWXINTL_NO_GETTEXT_MACRO -I"$ROOT" -include "zen/i18n.h"
    -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion)
# compiler family and STDLIB: same flags as the makefiles
if "$CXX" --version | grep -q clang; then
    CXXFLAGS_TEST+=(-Wtautological-overlap-compare -Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-lambda-capture)
else
    CXXFLAGS_TEST+=(-Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op)
fi
STDLIB_FLAGS=()
case "${STDLIB:-}" in
    "")     ;;
    libc++) STDLIB_FLAGS=(-stdlib=libc++) ;;
    *)      echo "STDLIB must be empty or \"libc++\", not \"$STDLIB\"" >&2; exit 2 ;;
esac
CXXFLAGS_TEST+=("${STDLIB_FLAGS[@]}")
# glib >= 2.78 can make g_free() a function-like macro, which breaks "::g_free(ptr)" in zen/zstring.cpp:
# cap the glib API at 2.76 like .github/build/build-free-file-sync.sh (older glib doesn't define GLIB_VERSION_2_76 and needs no cap)
if [ "$(uname -s)" = Linux ] && pkg-config --atleast-version=2.76 glib-2.0; then
    CXXFLAGS_TEST+=(-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76)
fi
# the caller's CXXFLAGS/LDFLAGS come last, so they can override the defaults above
read -r -a CXXFLAGS_USER <<< "${CXXFLAGS:-}"
read -r -a LDFLAGS_USER  <<< "${LDFLAGS:-}"

ZEN_SOURCES=(file_access file_io file_traverser file_path resolve_path zstring dir_watcher recycler sys_info sys_version process_exec thread sys_error format_unit legacy_compiler)

echo "== build ($("$CXX" --version | head -n 1)${STDLIB:+, $STDLIB})"
FAILED_LIST="$WORK/failed-compiles"
: > "$FAILED_LIST"
OBJECTS=()
compile() # <source> <object>: compile in the background, at most nproc jobs; a failed job adds its source to $FAILED_LIST
{
    while (( $(jobs -rp | wc -l) >= $(nproc) )); do wait -n || true; done # failures are collected through $FAILED_LIST
    rm -f "$2" # never link a stale object from an earlier build in $ZEN_TEST_BIN
    # shellcheck disable=SC2086
    { "$CXX" "${CXXFLAGS_TEST[@]}" $PLATFORM_CFLAGS "${CXXFLAGS_USER[@]}" -c "$1" -o "$2" || echo "$1" >> "$FAILED_LIST"; } &
    OBJECTS+=("$2")
}
for src in "$ROOT"/Tests/zen/*.cpp; do
    compile "$src" "$BIN/$(basename "$src" .cpp).o"
done
for name in "${ZEN_SOURCES[@]}"; do
    compile "$ROOT/zen/$name.cpp" "$BIN/zen_$name.o"
done
wait
if [ -s "$FAILED_LIST" ]; then
    echo "== build failed:" >&2
    sed 's/^/   /' "$FAILED_LIST" >&2
    exit 1
fi
# shellcheck disable=SC2086
"$CXX" -pthread "${STDLIB_FLAGS[@]}" "${CXXFLAGS_USER[@]}" "${OBJECTS[@]}" $PLATFORM_LIBS "${LDFLAGS_USER[@]}" -o "$BIN/zen_test"

echo "== test"
SANDBOX="$WORK/sandbox"
mkdir -p "$SANDBOX/home/.local/share" "$SANDBOX/tests"
# ZEN_TEST_SANDBOX: tells zen_test that HOME etc. point into a sandbox, see main.cpp; ZEN_TEST_*: expected by test_paths.cpp
# (zen reads the environment once at startup, so the tests can't set them themselves)
HOME="$SANDBOX/home" XDG_DATA_HOME="$SANDBOX/home/.local/share" XDG_CONFIG_HOME="$SANDBOX/home/.config" \
ZEN_TEST_SANDBOX="$SANDBOX" ZEN_TEST_VAR="zen test value" ZEN_TEST_EMPTY="" \
    "$BIN/zen_test" --root "$SANDBOX/tests" "$@"

# the test removes its own folders; anything left is a bug in the test
if [ -n "$(ls -A "$SANDBOX/tests")" ]; then
    echo "test folders left behind:" >&2
    ls -lA "$SANDBOX/tests" >&2
    exit 1
fi
