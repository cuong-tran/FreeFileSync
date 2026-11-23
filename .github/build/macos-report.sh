#!/usr/bin/env bash
# Informational reports for the macOS port, run by the macOS build workflow. Never fails the build: every check prints
# its result instead.
#
# Usage: macos-report.sh toolchain
#            For Xcode's clang and, if installed, Homebrew LLVM: compiler and libc++ versions, which of -std=c++26 and
#            -std=c++2c they accept, a syntax check of zen code that has no platform-specific includes, and the oldest
#            deployment target at which the C++ library features zen uses are available.
#        macos-report.sh port-todo <arch> [<deps prefix>]
#            Syntax-checks every zen/*.cpp with $CXX (default: Xcode's clang++) for macOS on <arch>, with the libraries
#            in <deps prefix> on the include path: the work list for porting zen/ to macOS. One line per file: "ok" or
#            the first error.
set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." &>/dev/null && pwd)"
SDK="$(xcrun --show-sdk-path)"
# the deployment target the libraries are built for (common.sh; read, not sourced: sourcing takes the build locks)
DEPLOYMENT_TARGET="$(sed -nE 's/^FFS_MACOS_DEPLOYMENT_TARGET=([0-9.]+).*/\1/p' "$ROOT/.github/build/common.sh")"

# same language and warning flags as the makefiles' clang block, plus the macOS SDK
BASE_FLAGS=(-isysroot "$SDK" -fsyntax-only -I"$ROOT" -I"$ROOT/zenXml" -include zen/i18n.h -DWXINTL_NO_GETTEXT_MACRO -DNDEBUG
    -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion -Wtautological-overlap-compare -Wno-potentially-evaluated-expression
    -Wno-unused-const-variable -Wno-unused-lambda-capture)

# first_error <compiler output file>: the first error line with the line before it (often "In file included from")
first_error() {
    grep -m1 -B1 -E 'error:' "$1" | sed 's/^/        /'
}

# std_flag <compiler>: the first of -std=c++26 and -std=c++2c the compiler accepts
std_flag() {
    local std
    for std in c++26 c++2c; do
        if echo 'int main() {}' | "$1" -isysroot "$SDK" -std="$std" -x c++ -fsyntax-only - 2>/dev/null; then echo "-std=$std"; return; fi
    done
    echo "-std=c++2b"
}

# check_file <compiler> <file> <flags...>: syntax-check a .cpp, or a header on its own; prints "ok" or the first error
check_file() {
    local cxx="$1" file="$2" log
    shift 2
    log="$(mktemp)"
    if [[ "$file" == *.h ]]; then
        (cd "$ROOT" && "$cxx" "$@" -x c++ -include "$file" /dev/null) >"$log" 2>&1
    else
        (cd "$ROOT" && "$cxx" "$@" "$file") >"$log" 2>&1
    fi
    if grep -q 'error:' "$log"; then
        echo "    $file: FAILED"
        first_error "$log"
    else
        echo "    $file: ok"
    fi
    rm -f "$log"
}

toolchain_report() {
    # code without platform-specific includes; it compiles with g++/libstdc++ and clang/libc++ on Linux, so a failure
    # here is a gap of the macOS toolchain (json.h needs utf.h first, as everywhere)
    local probeFiles=(zen/string_tools.h zen/utf.h zen/base64.h zen/crc.h zen/stl_tools.h zen/string_base.h zen/ring_buffer.h
                      zen/format_unit.h zen/time.h zen/zstring.h zen/scope_guard.h zen/globals.h zen/thread.h zen/legacy_compiler.cpp)
    # C++ library features that Apple's libc++ marks with a minimum macOS version, one probe each
    # (floating-point to_chars/from_chars: zen/legacy_compiler.cpp; the others are candidates for later code)
    local features=(
        'to_chars(double)|#include <charconv>
int main() { char b[64]; (void)std::to_chars(b, b + 64, 1.5); }'
        'from_chars(double)|#include <charconv>
int main() { char b[64] = "1.5"; double d = 0; (void)std::from_chars(b, b + 3, d); }'
        'filesystem|#include <filesystem>
int main() { (void)std::filesystem::temp_directory_path(); }'
        'format|#include <format>
int main() { (void)std::format("{} {}", 42, 1.5); }'
        'jthread/stop_token|#include <thread>
int main() { std::jthread t([](std::stop_token) {}); }'
        'shared_mutex|#include <shared_mutex>
int main() { std::shared_mutex m; m.lock_shared(); m.unlock_shared(); }'
    )
    local compilers=("Xcode clang|$(xcrun -f clang++)")
    if command -v brew >/dev/null && [ -x "$(brew --prefix)/opt/llvm/bin/clang++" ]; then
        compilers+=("Homebrew LLVM|$(brew --prefix)/opt/llvm/bin/clang++")
    else
        compilers+=("Homebrew LLVM|(not installed)")
    fi

    echo "macOS $(sw_vers -productVersion), SDK $(xcrun --show-sdk-version), Xcode $(xcodebuild -version 2>/dev/null | head -n 1)"
    echo "deployment target in common.sh: $DEPLOYMENT_TARGET"
    local entry name cxx std dt log file
    for entry in "${compilers[@]}"; do
        name="${entry%%|*}"
        cxx="${entry#*|}"
        echo
        echo "== $name: $cxx"
        [ -x "$cxx" ] || { echo "    not available"; continue; }
        echo "    $("$cxx" --version | head -n 1)"
        echo "    libc++: $(echo '#include <version>' | "$cxx" -isysroot "$SDK" -std=c++2c -x c++ -dM -E - 2>/dev/null | awk '$2 == "_LIBCPP_VERSION" { print $3 }')"
        for std in c++26 c++2c; do
            if echo 'int main() {}' | "$cxx" -isysroot "$SDK" -std="$std" -x c++ -fsyntax-only - 2>/dev/null; then
                echo "    -std=$std: accepted"
            else
                echo "    -std=$std: rejected"
            fi
        done
        std="$(std_flag "$cxx")"
        echo "    zen probe ($std, -mmacosx-version-min=$DEPLOYMENT_TARGET):"
        for file in "${probeFiles[@]}"; do
            check_file "$cxx" "$file" "$std" -mmacosx-version-min="$DEPLOYMENT_TARGET" "${BASE_FLAGS[@]}"
        done
        check_file "$cxx" zen/json.h "$std" -mmacosx-version-min="$DEPLOYMENT_TARGET" -include zen/utf.h "${BASE_FLAGS[@]}"
        echo "    oldest deployment target (of 11.0 ... 26.0) that each library feature compiles for:"
        log="$(mktemp)"
        local feature fname code oldest
        for feature in "${features[@]}"; do
            fname="${feature%%|*}"
            code="${feature#*|}"
            oldest=""
            for dt in 11.0 12.0 13.0 13.3 14.0 14.4 15.0 15.4 26.0; do
                if echo "$code" | "$cxx" -isysroot "$SDK" "$std" -mmacosx-version-min="$dt" -x c++ -fsyntax-only - >"$log" 2>&1; then
                    oldest="$dt"
                    break
                fi
            done
            if [ -n "$oldest" ]; then
                echo "        $fname: $oldest"
            else
                echo "        $fname: not available"
                grep -m2 'error:' "$log" | sed 's/^/            /'
            fi
        done
        rm -f "$log"
    done
}

port_todo() {
    local arch="${1:?usage: macos-report.sh port-todo <arch> [<deps prefix>]}" prefix="${2:-}"
    local cxx="${CXX:-$(xcrun -f clang++)}" std file
    std="$(std_flag "$cxx")"
    local flags=("$std" -arch "$arch" -mmacosx-version-min="$DEPLOYMENT_TARGET" "${BASE_FLAGS[@]}")
    [ -n "$prefix" ] && flags+=(-I"$prefix/include")
    echo "zen/*.cpp on macOS $arch: $("$cxx" --version | head -n 1), $std, deployment target $DEPLOYMENT_TARGET${prefix:+, libraries from $prefix}"
    local ok=0 failed=0 out
    for file in "$ROOT"/zen/*.cpp; do
        out="$(check_file "$cxx" "zen/$(basename "$file")" "${flags[@]}")"
        echo "$out"
        if grep -q ': ok$' <<<"$out"; then ok=$((ok + 1)); else failed=$((failed + 1)); fi
    done
    echo "== $ok compile, $failed fail"
}

case "${1:-}" in
    toolchain) toolchain_report ;;
    port-todo) shift; port_todo "$@" ;;
    *)         echo "usage: macos-report.sh toolchain | port-todo <arch> [<deps prefix>]" >&2; exit 2 ;;
esac
exit 0
