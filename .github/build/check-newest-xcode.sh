#!/usr/bin/env bash
# Informational macOS check (CI, macos-build.yml): the apps are built with the newest Xcode 26.x, but a newer Xcode on the runner
# image may bring an SDK that breaks the build (Xcode 27's SDK no longer includes <sys/types.h> through other headers:
# docs/MAC_TEST_REPORT.md, F1). If an Xcode newer than the selected one (DEVELOPER_DIR, else xcode-select) is installed, every
# compile command of FreeFileSync and RealTimeSync runs with it as -fsyntax-only, against the libraries of the selected one
# (FFS_DEPS_PREFIX of the makefiles), and each failing file is reported as a GitHub warning. Which Xcode counts as newer and
# usable is decided by select-xcode.sh --newer-than. The compiler output goes to the job log only (on the self-hosted Mac covered by
# the job's "Mask private names" step), never to a file that is uploaded.
#   check-newest-xcode.sh          exit code 0 unless the check itself fails; CHECK_XCODE=<Xcode.app>: check with that one
set -euo pipefail
cd "$(dirname "$0")/../.."
[ "${BASH_VERSINFO[0]}" -gt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -ge 4 ]; } ||
    { echo "error: needs bash 4.4 or newer (Homebrew's bash), not $BASH_VERSION" >&2; exit 2; }

xcodeVersion() { /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$1/Contents/Info.plist" 2>/dev/null || echo 0; }

selected="${DEVELOPER_DIR:-$(xcode-select -p)}"
if [ -n "${CHECK_XCODE:-}" ]; then
    newestDev="${CHECK_XCODE%/}/Contents/Developer"
else
    # the newest *usable* Xcode above the selected one, by select-xcode.sh's own candidates and checks (license, first launch,
    # -std=c++26, libc++ 20): nothing printed if there is none
    newestDev="$(.github/build/select-xcode.sh --newer-than "$selected")"
fi
echo "selected Xcode: ${selected%/Contents/Developer} ($(xcodeVersion "${selected%/Contents/Developer}")); newer usable one: ${newestDev:-none}"
if [ -z "$newestDev" ]; then
    echo "no newer Xcode on this machine: nothing to check"
    exit 0
fi
newest="${newestDev%/Contents/Developer}"
newestVersion="$(xcodeVersion "$newest")"

export DEVELOPER_DIR="$newest/Contents/Developer"
echo "checking with $(clang++ --version | head -n 1), macOS SDK $(xcrun --show-sdk-version)"

work="$(mktemp -d "$(t="${TMPDIR:-/tmp}"; echo "${t%/}")/newest-xcode.XXXXXX")" # explicit template: macOS 27's mktemp -d ignores TMPDIR
trap 'rm -rf "$work"' EXIT
jobs="$(sysctl -n hw.ncpu)"
count=0 failed=0
for dir in FreeFileSync/Source FreeFileSync/Source/RealTimeSync; do
    # the compile commands of a full build: object file and dependency output dropped, -fsyntax-only instead
    gmake -C "$dir" -n -B all 2>/dev/null | grep -E '^clang\+\+ .* -c ' |
        sed -E 's/ -MMD -MP / /; s/ -o [^ ]+$/ -fsyntax-only/' > "$work/commands.txt" || true
    [ -s "$work/commands.txt" ] || { echo "::error::no compile commands from gmake -n in $dir"; exit 1; }
    while IFS= read -r cmd; do
        count=$((count + 1))
        while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n || true; done
        (cd "$dir" && eval "$cmd") > "$work/$count.log" 2>&1 || echo "$dir|$cmd" > "$work/$count.failed" &
    done < "$work/commands.txt"
    wait || true
done

for f in "$work"/*.failed; do
    [ -e "$f" ] || continue
    failed=$((failed + 1))
    n="$(basename "$f" .failed)"
    IFS='|' read -r dir cmd < "$f"
    source="$(sed -E 's/.* -c ([^ ]+) -fsyntax-only$/\1/' <<< "$cmd")"
    firstError="$(grep -m 1 -E 'error:' "$work/$n.log" || head -n 1 "$work/$n.log")"
    echo "::warning file=$dir/$source::Xcode $newestVersion: $firstError"
    echo "== $dir/$source"
    cat "$work/$n.log"
done
echo "$count files checked with Xcode $newestVersion: $failed with errors"
