#!/bin/bash
# Select the Xcode for the macOS builds and tests and print its Developer folder (for DEVELOPER_DIR), or fail naming the
# requirement. Used by every macOS CI job, on GitHub's runners and on the self-hosted one; also works locally on a Mac.
#
# Requirement (what the code needs): Apple clang that accepts -std=c++26, with libc++ 20 or newer: Xcode 26 or newer.
# Candidates: /Applications/Xcode*.app (GitHub's images name them Xcode_26.3.app etc., a Mac usually has Xcode.app or
# Xcode-beta.app) and the Xcode that xcode-select points to. Preference: the newest 26.x (what GitHub's runners use and what the
# port was made with), else the newest later version. Nothing is changed: no xcode-select -s, no license or first-launch step;
# the caller sets DEVELOPER_DIR for its own job only.
#
# usage: select-xcode.sh [--github-env]   (--github-env: also append DEVELOPER_DIR=... to $GITHUB_ENV)
#        select-xcode.sh --newer-than <Developer folder>
#            the Developer folder of the newest usable Xcode with a higher version than the given one, or nothing (exit code 0)
#            if there is none (for check-newest-xcode.sh: same candidates and checks)
# Runs with macOS's bash 3.2 (it may run before Homebrew's bash is known to exist).
set -euo pipefail

mode=select githubEnv=no newerThan=""
case "${1:-}" in
    (--github-env) githubEnv=yes ;;
    (--newer-than) mode=newer; newerThan="${2:?usage: select-xcode.sh --newer-than <Developer folder>}" ;;
    ("") ;;
    (*) echo "usage: select-xcode.sh [--github-env] | --newer-than <Developer folder>" >&2; exit 2 ;;
esac

apps=()
for app in /Applications/Xcode*.app; do
    [ -d "$app/Contents/Developer" ] && apps+=("$app")
done
selected="$(xcode-select -p 2>/dev/null || true)"
case "$selected" in
    (*.app/Contents/Developer)
        app="${selected%/Contents/Developer}"
        known=no
        for a in ${apps[@]+"${apps[@]}"}; do [ "$a" = "$app" ] && known=yes; done
        [ "$known" = yes ] || apps+=("$app")
        ;;
esac

# "<major> <minor> <patch> <path>" per candidate, from Info.plist (fast; the checks below run xcodebuild for the chosen one only)
list="$(for app in ${apps[@]+"${apps[@]}"}; do
    v="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist" 2>/dev/null || echo 0)"
    IFS=. read -r major minor patch <<< "$v.0.0"
    echo "${major:-0} ${minor:-0} ${patch:-0} $app"
done)"
if [ "$mode" = select ]; then
    # 26.x newest first, then later versions newest first; older ones are listed only to say why they were not taken
    ordered="$( { grep -E '^26 ' <<< "$list" | sort -k2,2nr -k3,3nr || true; } ;
                { awk '$1 > 26' <<< "$list" | sort -k1,1nr -k2,2nr -k3,3nr || true; } )"
else
    # every version above the given Xcode's, newest first
    v="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${newerThan%/Contents/Developer}/Contents/Info.plist" 2>/dev/null || true)"
    if ! [[ "$v" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then # not an Xcode (e.g. the Command Line Tools; PlistBuddy reports errors on stdout):
        #no version to compare with, so nothing counts as newer
        echo "select-xcode.sh: no Xcode version for $newerThan" >&2
        exit 0
    fi
    IFS=. read -r bMajor bMinor bPatch <<< "$v.0.0"
    ordered="$(awk -v a="${bMajor:-0}" -v b="${bMinor:-0}" -v c="${bPatch:-0}" \
                   '$1 > a || ($1 == a && ($2 > b || ($2 == b && $3 > c)))' <<< "$list" | sort -k1,1nr -k2,2nr -k3,3nr || true)"
fi

rejected=()
while read -r _ _ _ app <&3; do # fd 3: xcodebuild and xcrun must not eat the list
    [ -n "$app" ] || continue
    dev="$app/Contents/Developer"
    name="$(basename "$app")"
    if ! version="$(DEVELOPER_DIR="$dev" xcodebuild -version 2>&1)"; then
        rejected+=("$name: xcodebuild fails ($(sed -n 1p <<< "$version")): open Xcode once, or run 'sudo xcodebuild -license accept' and 'xcodebuild -runFirstLaunch'")
        continue
    fi
    if ! echo 'int main() {}' | DEVELOPER_DIR="$dev" xcrun clang++ -std=c++26 -x c++ -fsyntax-only - 2>/dev/null; then
        rejected+=("$name: its clang rejects -std=c++26")
        continue
    fi
    libcxx="$(echo '#include <version>' | DEVELOPER_DIR="$dev" xcrun clang++ -std=c++26 -x c++ -dM -E - 2>/dev/null | awk '$2 == "_LIBCPP_VERSION" { print $3 }')"
    if [ -z "$libcxx" ] || [ "$libcxx" -lt 200000 ]; then
        rejected+=("$name: libc++ ${libcxx:-unknown} is older than 20")
        continue
    fi
    echo "using $name: $(tr '\n' ' ' <<< "$version"); $(DEVELOPER_DIR="$dev" xcrun clang++ --version | sed -n 1p); SDK $(DEVELOPER_DIR="$dev" xcrun --show-sdk-version); libc++ $libcxx" >&2
    if [ "$githubEnv" = yes ]; then echo "DEVELOPER_DIR=$dev" >> "$GITHUB_ENV"; fi
    echo "$dev"
    exit 0
done 3<<< "$ordered"

if [ "$mode" = newer ]; then # none newer that works: nothing to print
    [ ${#rejected[@]} -eq 0 ] || { echo "newer Xcode(s) not usable:" >&2; printf '    %s\n' "${rejected[@]}" >&2; }
    exit 0
fi
while read -r major minor patch app; do
    if [ -n "$app" ] && [ "$major" -lt 26 ]; then rejected+=("$(basename "$app"): version $major.$minor.$patch, older than 26"); fi
done <<< "$list"
echo "::error::no usable Xcode on this Mac: the macOS build needs Xcode 26 or newer (Apple clang accepting -std=c++26, libc++ 20)." \
     "Install it into /Applications (App Store or developer.apple.com) and open it once; the Command Line Tools alone are not enough." >&2
if [ ${#rejected[@]} -eq 0 ]; then
    echo "    no Xcode*.app in /Applications; xcode-select points to ${selected:-nothing}" >&2
else
    printf '    %s\n' "${rejected[@]}" >&2
fi
exit 1
