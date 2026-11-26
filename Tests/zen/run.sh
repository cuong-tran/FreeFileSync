#!/usr/bin/env bash
# Build and run the zen platform layer unit tests (Tests/zen/*.cpp against the zen/*.cpp they need)
#
# usage: Tests/zen/run.sh [<test name>...]      (default: all tests; "--list" lists them)
#   CXX:          compiler (default g++; needs C++26, i.e. g++ 14 or newer, or clang 20 or newer)
#   STDLIB:       "libc++" to build against LLVM's libc++ instead of the compiler's default C++ library (like the makefiles' STDLIB)
#   CXXFLAGS, LDFLAGS: appended to the compile/link commands
#   ZEN_TEST_BIN: build into this folder instead of a temporary one and keep it (the build always runs), e.g. to debug zen_test;
#                 run directly, zen_test skips the tests that need run.sh's sandbox (see main.cpp)
#   macOS only:
#   ARCH:         arm64 or x86_64 (default: the host's); the other one is cross-compiled and runs under Rosetta 2 if installed
#   ZEN_TEST_COMPILE_ONLY=1: build, but don't run (e.g. x86_64 on an arm64 machine without Rosetta)
#   ZEN_TEST_ALL_ERRORS=1: report all compile errors of each file, not just the first (no -Wfatal-errors)
#
# Linux needs the glib/gio dev packages (pkg-config); macOS needs Xcode or its command line tools (clang, SDK). The macOS deployment
# target is the apps' (FFS_MACOS_DEPLOYMENT_TARGET in .github/build/common.sh). Nothing is left behind: build output and test folders
# live in a temporary folder that is removed on exit, and HOME/XDG_DATA_HOME point there so the recycle bin test can't touch the
# user's real trash (Linux).
set -euo pipefail
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    echo "bash 4.4 or newer needed (this is $BASH_VERSION); on macOS: brew install bash" >&2 # wait -n, empty arrays under set -u
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d "$(t="${TMPDIR:-/tmp}"; echo "${t%/}")/zen-test.XXXXXX")" # explicit template (TMPDIR without its trailing slash): macOS 27's mktemp -d ignores TMPDIR
trap 'wait; rm -rf "$WORK"' EXIT # wait: compile jobs still running after a failed one
BIN="${ZEN_TEST_BIN:-$WORK/bin}"
mkdir -p "$BIN"

# zen sources per platform: shared POSIX code plus the platform's own files (zen/*_mac.*)
ZEN_SOURCES=(file_access file_io file_traverser file_path resolve_path zstring sys_info sys_version process_exec process_priority thread sys_error format_unit legacy_compiler crash_scrub)
case "$(uname -s)" in
    Linux)
        CXX="${CXX:-g++}"
        PLATFORM_CFLAGS="$(pkg-config --cflags glib-2.0 gio-2.0)" # zstring.cpp, sys_error.cpp, recycler.cpp
        PLATFORM_LIBS="$(pkg-config --libs glib-2.0 gio-2.0)"
        ZEN_SOURCES+=(dir_watcher recycler)
        ;;
    Darwin)
        CXX="${CXX:-clang++}" # Xcode's clang
        ARCH="${ARCH:-$(uname -m)}"
        DEPLOYMENT_TARGET="$(sed -nE 's/^FFS_MACOS_DEPLOYMENT_TARGET=([0-9.]+).*/\1/p' "$ROOT/.github/build/common.sh")"
        [ -n "$DEPLOYMENT_TARGET" ] || { echo "no FFS_MACOS_DEPLOYMENT_TARGET in .github/build/common.sh" >&2; exit 2; }
        PLATFORM_CFLAGS="-arch $ARCH -mmacosx-version-min=$DEPLOYMENT_TARGET"
        PLATFORM_LIBS="-arch $ARCH -mmacosx-version-min=$DEPLOYMENT_TARGET -framework CoreFoundation -framework CoreServices -framework Foundation"
        ZEN_SOURCES+=(sys_error_mac.mm dir_watcher_mac.cpp recycler_mac.mm)
        ;;
    *)
        echo "Tests/zen/run.sh: no build setup for $(uname -s) yet" >&2
        exit 2
        ;;
esac

# same language standard and warnings as FreeFileSync/Source/Makefile, but with assert() enabled (no -DNDEBUG)
CXXFLAGS_TEST=(-std=c++26 -O1 -g -pthread -DWXINTL_NO_GETTEXT_MACRO -I"$ROOT" -include "zen/i18n.h"
    -Wall "$([ "${ZEN_TEST_ALL_ERRORS:-0}" = 1 ] && echo -Wno-fatal-errors || echo -Wfatal-errors)" -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion)
# compiler family (detected positively from the predefined macros, like the makefiles) and STDLIB: same flags as the makefiles
COMPILER_MACROS="$("$CXX" -dM -E -x c++ /dev/null 2>/dev/null || true)"
if grep -q __clang__ <<< "$COMPILER_MACROS"; then
    CXXFLAGS_TEST+=(-Wtautological-overlap-compare -Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-lambda-capture)
elif grep -q __GNUC__ <<< "$COMPILER_MACROS"; then
    CXXFLAGS_TEST+=(-Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op)
fi
STDLIB_FLAGS=()
case "${STDLIB:-}" in
    "")     ;;
    libc++) STDLIB_FLAGS=(-stdlib=libc++) ;;
    *)      echo "STDLIB must be empty or \"libc++\", not \"$STDLIB\"" >&2; exit 2 ;;
esac
CXXFLAGS_TEST+=("${STDLIB_FLAGS[@]}")
CXXFLAGS_OBJC=(-fobjc-arc) # *.mm (macOS): Objective-C++ with automatic reference counting
# glib >= 2.78 can make g_free() a function-like macro, which breaks "::g_free(ptr)" in zen/zstring.cpp:
# cap the glib API at 2.76 like .github/build/build-free-file-sync.sh (older glib doesn't define GLIB_VERSION_2_76 and needs no cap)
if [ "$(uname -s)" = Linux ] && pkg-config --atleast-version=2.76 glib-2.0; then
    CXXFLAGS_TEST+=(-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76)
fi
# the caller's CXXFLAGS/LDFLAGS come last, so they can override the defaults above
read -r -a CXXFLAGS_USER <<< "${CXXFLAGS:-}"
read -r -a LDFLAGS_USER  <<< "${LDFLAGS:-}"

echo "== build ($("$CXX" --version | sed -n 1p)${STDLIB:+, $STDLIB})"
FAILED_LIST="$WORK/failed-compiles"
: > "$FAILED_LIST"
OBJECTS=()
JOBS="$(getconf _NPROCESSORS_ONLN)" # nproc is GNU coreutils
compile() # <source> <object> [<extra flags>...]: compile in the background, at most $JOBS jobs; a failed job adds its source to $FAILED_LIST
{
    local src="$1" obj="$2"
    shift 2
    local extra=("$@")
    [[ "$src" == *.mm ]] && extra+=("${CXXFLAGS_OBJC[@]}")
    while (( $(jobs -rp | wc -l) >= JOBS )); do wait -n || true; done # failures are collected through $FAILED_LIST
    rm -f "$obj" # never link a stale object from an earlier build in $ZEN_TEST_BIN
    # shellcheck disable=SC2086
    { "$CXX" "${CXXFLAGS_TEST[@]}" $PLATFORM_CFLAGS "${extra[@]}" "${CXXFLAGS_USER[@]}" -c "$src" -o "$obj" || echo "$src" >> "$FAILED_LIST"; } &
}
OBJECTS=()
for src in "$ROOT"/Tests/zen/*.cpp; do
    OBJECTS+=("$BIN/$(basename "$src" .cpp).o")
    compile "$src" "${OBJECTS[-1]}"
done
for name in "${ZEN_SOURCES[@]}"; do
    [[ "$name" == *.* ]] || name="$name.cpp"
    OBJECTS+=("$BIN/zen_${name%.*}.o")
    compile "$ROOT/zen/$name" "${OBJECTS[-1]}"
done
# zen_test_fallback: the floating-point from_chars() fallback of zen/legacy_compiler.cpp, forced on (see test_legacy_compiler.cpp)
compile "$ROOT/zen/legacy_compiler.cpp"               "$BIN/fallback_legacy_compiler.o"      -DZEN_TEST_FROM_CHARS_FALLBACK
compile "$ROOT/Tests/zen/test_legacy_compiler.cpp"    "$BIN/fallback_test_legacy_compiler.o" -DZEN_TEST_FROM_CHARS_FALLBACK
wait
if [ -s "$FAILED_LIST" ]; then
    echo "== build failed:" >&2
    sed 's/^/   /' "$FAILED_LIST" >&2
    exit 1
fi
# shellcheck disable=SC2086
"$CXX" -pthread "${STDLIB_FLAGS[@]}" "${CXXFLAGS_USER[@]}" "${OBJECTS[@]}" $PLATFORM_LIBS "${LDFLAGS_USER[@]}" -o "$BIN/zen_test"
# shellcheck disable=SC2086
"$CXX" -pthread "${STDLIB_FLAGS[@]}" "${CXXFLAGS_USER[@]}" "$BIN/main.o" "$BIN/fallback_test_legacy_compiler.o" "$BIN/fallback_legacy_compiler.o" \
    $PLATFORM_LIBS "${LDFLAGS_USER[@]}" -o "$BIN/zen_test_fallback"

if [ "${ZEN_TEST_COMPILE_ONLY:-0}" = 1 ]; then
    echo "== built $BIN/zen_test (ZEN_TEST_COMPILE_ONLY=1: not run)"
    exit 0
fi

echo "== test"
SANDBOX="$WORK/sandbox"
mkdir -p "$SANDBOX/home/.local/share" "$SANDBOX/tests"
# ZEN_TEST_SANDBOX: tells zen_test that HOME etc. point into a sandbox, see main.cpp; ZEN_TEST_*: expected by test_paths.cpp
# (zen reads the environment once at startup, so the tests can't set them themselves); ZEN_TEST_REAL_HOME: the macOS trash is
# in the real home (test_recycler.cpp)
ZEN_TEST_REAL_HOME="${HOME:-}" HOME="$SANDBOX/home" XDG_DATA_HOME="$SANDBOX/home/.local/share" XDG_CONFIG_HOME="$SANDBOX/home/.config" \
ZEN_TEST_SANDBOX="$SANDBOX" ZEN_TEST_VAR="zen test value" ZEN_TEST_EMPTY="" \
    "$BIN/zen_test" --root "$SANDBOX/tests" "$@"

# the from_chars() fallback, unless other tests were selected
if [ $# -eq 0 ] || printf '%s\n' "$@" | grep -qx legacyCompilerFromChars; then
    echo "== test (from_chars fallback forced on)"
    "$BIN/zen_test_fallback" --root "$SANDBOX/tests" legacyCompilerFromChars
fi

# the test removes its own folders; anything left is a bug in the test
if [ -n "$(ls -A "$SANDBOX/tests")" ]; then
    echo "test folders left behind:" >&2
    ls -lA "$SANDBOX/tests" >&2
    exit 1
fi
