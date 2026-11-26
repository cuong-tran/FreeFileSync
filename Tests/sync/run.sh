#!/usr/bin/env bash
# Build and run the headless sync tests: Tests/sync/*.cpp linked against the engine library (libffs_engine.a from
# "make engine" in FreeFileSync/Source), which compares and synchronizes temporary folder trees through the real engine.
#
# usage: Tests/sync/run.sh [<test name>...]      (default: all tests; "--list" lists them)
#   CXX:           compiler (default g++ on Linux, Xcode's clang++ on macOS; C++26: g++ 14 or newer, clang 20 or newer); may include a
#                  wrapper, e.g. "ccache clang++"
#   STDLIB:        "libc++" to build against LLVM's libc++ (like the makefiles' STDLIB)
#   CXXFLAGS, LDFLAGS: passed on to the makefile, which appends its own flags (they apply to the engine and the tests)
#   FFS_DEPS_PREFIX: the libraries built by .github/build/build.sh (default: <repo>/.build/deps on Linux if it exists, else the
#                  system's; macOS: <repo>/.build/deps/macos-<arch>, see FreeFileSync/Source/platform.mk)
#   SYNC_TEST_OBJ: object folder for the engine (default: a temporary one, so every run builds from scratch); set it to keep the
#                  objects between runs (make rebuilds what changed). The makefile also writes "<folder>.crashreport" next to it.
#   SYNC_TEST_BIN: build the test binary into this folder and keep it (default: the temporary folder)
#   MAKE:          GNU make 4 or newer (default: make on Linux, gmake on macOS)
#   macOS only:
#   ARCH:          arm64 or x86_64 (default: the host's); the other one is cross-compiled and runs under Rosetta 2 if installed
#   SYNC_TEST_COMPILE_ONLY=1: build, but don't run
#
# Linux needs what the FreeFileSync build needs (wxWidgets 3.3, libcurl, libssh2, OpenSSL, GTK 3 dev packages; see CONTRIBUTING.md)
# plus python3 for the ZIP fixture. Nothing is left behind: test folders live in a temporary folder that is removed on exit,
# and HOME/XDG_DATA_HOME point there, so moving items to the recycle bin can't touch the user's trash on Linux. macOS has one
# trash per volume that can't be redirected: the tests delete what they moved there (test_deletion.cpp).
set -euo pipefail
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    echo "bash 4.4 or newer needed (this is $BASH_VERSION); on macOS: brew install bash" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d "$(t="${TMPDIR:-/tmp}"; echo "${t%/}")/sync-test.XXXXXX")" # explicit template (TMPDIR without its trailing slash): macOS 27's mktemp -d ignores TMPDIR
trap 'wait; rm -rf "$WORK"' EXIT
BIN="${SYNC_TEST_BIN:-$WORK/bin}"
OBJ="${SYNC_TEST_OBJ:-$WORK/obj}"
mkdir -p "$BIN"
SRC_DIR="$ROOT/FreeFileSync/Source" # the makefile's flags have include paths relative to it

case "$(uname -s)" in
    Linux)
        export CXX="${CXX:-g++}"
        MAKE="${MAKE:-make}"
        PREFIX="${FFS_DEPS_PREFIX:-$ROOT/.build/deps}"
        if [ -d "$PREFIX" ]; then # the libraries from .github/build/build.sh win over the system's, as in build-free-file-sync.sh
            export PATH="$PREFIX/bin:$PATH"
            export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
            export LDFLAGS="-L$PREFIX/lib -Wl,-rpath,$PREFIX/lib${LDFLAGS:+ $LDFLAGS}"
        fi
        # glib >= 2.78 can make g_free() a function-like macro, which breaks "::g_free(ptr)": cap the API at 2.76 like build-free-file-sync.sh
        if pkg-config --atleast-version=2.76 glib-2.0; then
            export CXXFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76${CXXFLAGS:+ $CXXFLAGS}"
        fi
        MAKE_ARGS=()
        ;;
    Darwin)
        export CXX="${CXX:-clang++}"
        MAKE="${MAKE:-gmake}"
        ARCH="${ARCH:-$(uname -m)}"
        MAKE_ARGS=(ARCH="$ARCH") # the makefile adds -arch and the deployment target
        ;;
    *)
        echo "Tests/sync/run.sh: no build setup for $(uname -s) yet" >&2
        exit 2
        ;;
esac
MAKE_VERSION="$("$MAKE" --version 2>/dev/null || true)" # captured first: "| grep -q" could kill make with SIGPIPE under pipefail
[[ "$MAKE_VERSION" =~ GNU\ Make\ [4-9] ]] || { echo "GNU make 4 or newer needed as \$MAKE (macOS: brew install make)" >&2; exit 2; }

JOBS="$(getconf _NPROCESSORS_ONLN)"
read -r -a CXX_WORDS <<< "$CXX" # may be a command with arguments, e.g. "ccache clang++"
echo "== engine ($("${CXX_WORDS[@]}" --version | sed -n 1p)${STDLIB:+, $STDLIB}${ARCH:+, $ARCH})"
if ! "$MAKE" -C "$SRC_DIR" -k -j "$JOBS" engine objPath="$OBJ" "${MAKE_ARGS[@]}" > "$WORK/engine.log" 2>&1; then # -k: every failing file
    grep -E "error:|Error [0-9]" "$WORK/engine.log" >&2 || cat "$WORK/engine.log" >&2
    echo "== engine build failed" >&2
    exit 1
fi
grep -E "warning:" "$WORK/engine.log" || true

# the flags the engine was built with, from the makefile (one "NAME=value" line each; values are shell words, e.g. -include "zen/i18n.h")
FLAGS="$("$MAKE" -s -C "$SRC_DIR" --no-print-directory engine-flags objPath="$OBJ" "${MAKE_ARGS[@]}")"
ENGINE_CXX=""; ENGINE_CXXFLAGS=""; ENGINE_LDFLAGS=""; ENGINE_LIB=""
while IFS= read -r line; do
    case "$line" in
        CXX=*)        ENGINE_CXX="${line#CXX=}" ;;
        CXXFLAGS=*)   ENGINE_CXXFLAGS="${line#CXXFLAGS=}" ;;
        LDFLAGS=*)    ENGINE_LDFLAGS="${line#LDFLAGS=}" ;;
        ENGINE_LIB=*) ENGINE_LIB="${line#ENGINE_LIB=}" ;;
    esac
done <<< "$FLAGS"
[ -f "$ENGINE_LIB" ] || { echo "engine library missing: $ENGINE_LIB" >&2; exit 1; }
eval "CXX_CMD=($ENGINE_CXX)"           # our own makefile's output: split into words like the shell would
eval "CXX_FLAGS=($ENGINE_CXXFLAGS)"
eval "LD_FLAGS=($ENGINE_LDFLAGS)"

echo "== build tests"
FAILED_LIST="$WORK/failed-compiles"
: > "$FAILED_LIST"
OBJECTS=()
for src in "$ROOT"/Tests/sync/*.cpp; do
    obj="$BIN/$(basename "$src" .cpp).o"
    OBJECTS+=("$obj")
    while (( $(jobs -rp | wc -l) >= JOBS )); do wait -n || true; done
    rm -f "$obj"
    ( cd "$SRC_DIR" && "${CXX_CMD[@]}" "${CXX_FLAGS[@]}" -c "$src" -o "$obj" ) || echo "$src" >> "$FAILED_LIST" &
done
wait
if [ -s "$FAILED_LIST" ]; then
    echo "== build failed:" >&2
    sed 's/^/   /' "$FAILED_LIST" >&2
    exit 1
fi
( cd "$SRC_DIR" && "${CXX_CMD[@]}" -o "$BIN/sync_test" "${OBJECTS[@]}" "$ENGINE_LIB" "${LD_FLAGS[@]}" )

if [ "${SYNC_TEST_COMPILE_ONLY:-0}" = 1 ]; then
    echo "== built $BIN/sync_test (SYNC_TEST_COMPILE_ONLY=1: not run)"
    exit 0
fi

echo "== fixtures"
SANDBOX="$WORK/sandbox"
mkdir -p "$SANDBOX/home/.local/share" "$SANDBOX/tests" "$SANDBOX/fixtures"
# ZIP archive as a read-only sync source (test_zip.cpp): made by Python's zipfile from a reference folder the test compares against
ZIP_REF="$SANDBOX/fixtures/zip reference"
mkdir -p "$ZIP_REF/sub dir/nested" "$ZIP_REF/empty folder"
echo "hello from the archive" > "$ZIP_REF/a.txt"
head -c 100000 /dev/urandom > "$ZIP_REF/sub dir/random.bin"
echo "nested" > "$ZIP_REF/sub dir/nested/n.txt"
echo "accents" > "$ZIP_REF/Données été.txt"
python3 -I - "$ZIP_REF" "$SANDBOX/fixtures/archive.zip" <<'EOF'
import os, sys, zipfile
ref, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
    for dirpath, dirnames, filenames in os.walk(ref):
        for name in dirnames + filenames:
            path = os.path.join(dirpath, name)
            zf.write(path, os.path.relpath(path, ref))
EOF

echo "== test"
# ZEN_TEST_SANDBOX: HOME etc. point into a sandbox (see main.cpp); SYNC_TEST_REAL_HOME: the macOS trash is in the real home
SYNC_TEST_REAL_HOME="${HOME:-}" \
HOME="$SANDBOX/home" XDG_DATA_HOME="$SANDBOX/home/.local/share" XDG_CONFIG_HOME="$SANDBOX/home/.config" \
ZEN_TEST_SANDBOX="$SANDBOX" SYNC_TEST_ZIP="$SANDBOX/fixtures/archive.zip" SYNC_TEST_ZIP_REF="$ZIP_REF" \
    "$BIN/sync_test" --root "$SANDBOX/tests" "$@"

# the tests remove their own folders; anything left is a bug in the test
if [ -n "$(ls -A "$SANDBOX/tests")" ]; then
    echo "test folders left behind:" >&2
    ls -lA "$SANDBOX/tests" >&2
    exit 1
fi
