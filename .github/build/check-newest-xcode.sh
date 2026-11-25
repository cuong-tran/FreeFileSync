#!/usr/bin/env bash
# Informational macOS check (CI, macos-build.yml): the apps are built with the newest Xcode 26.x, but a newer Xcode on the runner
# image may bring an SDK that breaks the build (Xcode 27's SDK no longer includes <sys/types.h> through other headers:
# docs/MAC_TEST_REPORT.md, F1). If an Xcode newer than the selected one (DEVELOPER_DIR, else xcode-select) is installed, every
# compile command of FreeFileSync and RealTimeSync runs with it as -fsyntax-only, against the libraries of the selected one
# (FFS_DEPS_PREFIX of the makefiles), and each failing file is reported as a GitHub warning.
#   check-newest-xcode.sh          exit code 0 unless the check itself fails; CHECK_XCODE=<Xcode.app>: check with that one
set -euo pipefail
cd "$(dirname "$0")/../.."
[ "${BASH_VERSINFO[0]}" -gt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -ge 4 ]; } ||
    { echo "error: needs bash 4.4 or newer (Homebrew's bash), not $BASH_VERSION" >&2; exit 2; }

xcodeVersion() { /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$1/Contents/Info.plist" 2>/dev/null || echo 0; }
newerThan() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ]; } # newerThan <v1> <v2>

selected="${DEVELOPER_DIR:-$(xcode-select -p)}"
selected="${selected%/Contents/Developer}"
selectedVersion="$(xcodeVersion "$selected")"

newest="${CHECK_XCODE:-}" newestVersion=0
if [ -n "$newest" ]; then
    newestVersion="$(xcodeVersion "$newest")"
else
    for app in /Applications/Xcode*.app; do
        [ -d "$app" ] || continue
        version="$(xcodeVersion "$app")"
        if newerThan "$version" "$newestVersion"; then newest="$app" newestVersion="$version"; fi
    done
fi
echo "selected Xcode: $selected ($selectedVersion); newest installed: ${newest:-none} ($newestVersion)"
if [ -z "${CHECK_XCODE:-}" ] && { [ -z "$newest" ] || ! newerThan "$newestVersion" "$selectedVersion"; }; then
    echo "no newer Xcode on this machine: nothing to check"
    exit 0
fi

export DEVELOPER_DIR="$newest/Contents/Developer"
echo "checking with $(clang++ --version | head -n 1), macOS SDK $(xcrun --show-sdk-version)"

work="$(mktemp -d)"
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
