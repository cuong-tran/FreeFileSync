#!/bin/bash
# Helpers for the macOS CI jobs, above all for the owner's self-hosted runner (a person's own Mac, not a throwaway VM; see
# "Self-hosted macOS runner" in CONTRIBUTING.md). On GitHub's runners (RUNNER_ENVIRONMENT=github-hosted) every subcommand only
# reports, except "cache", which the workflows use on the self-hosted runner only.
#
#   mac-runner.sh setup <need>...      first step of every macOS job. Outputs (GITHUB_OUTPUT): self_hosted (yes/no), host_arch
#                                      (arm64/x86_64), run_arm64 and run_x86_64 (yes/no: binaries of that architecture run here),
#                                      gui (yes/no: a window server session, so apps can start), ccache (yes/no).
#                                      Self-hosted only: masks the user, host and computer names in the log; checks the tools of
#                                      each <need> and fails with one "brew install ..." line for everything missing (installs
#                                      nothing); fails below $FFS_CI_MIN_FREE_GB (default 10) GB of free disk space; points
#                                      TMPDIR into $RUNNER_TEMP; sets the trash log the tests write (FFS_TEST_TRASH_LOG); maps
#                                      the home folder out of compiler paths (-ffile-prefix-map) and saves what "cleanup" restores.
#                                      Needs: bash (bash 4.4+), build (coreutils, flock, pkg-config, GNU make, cmake), timeout
#                                      (gtimeout), python3, ccache (optional), create-dmg (optional).
#   mac-runner.sh require-gui          fails on the self-hosted runner if its jobs have no window server session (apps cannot start)
#   mac-runner.sh cache restore <key> <path>       self-hosted: a persistent cache folder next to the checkout
#   mac-runner.sh cache save <kind> <key> <path>   ($RUNNER_WORKSPACE/ffs-ci-cache); "restore" copies an entry to <path> (APFS
#                                      clones: instant, no extra space) and outputs hit=true/false; "save" stores <path> under <key>
#                                      and keeps only the two newest entries of <kind> (entries unused for 14 days go as well)
#   mac-runner.sh cleanup              last step of every macOS job (if: always(), so also after a failure or a cancellation).
#                                      Self-hosted: ends processes started from the checkout or $RUNNER_TEMP, detaches disk images
#                                      that live there, deletes what the tests moved to the trash (from the trash log), deletes
#                                      crash reports of binaries from the checkout or $RUNNER_TEMP, unregisters test app bundles
#                                      from Launch Services, restores the apps' preferences and saved window state as found,
#                                      removes the build output, evicts old cache entries
#   mac-runner.sh scrub <file>...      self-hosted: replaces the home folder, user, host and computer names in files about to be
#                                      uploaded as artifacts (build logs); a no-op on GitHub's runners
#   mac-runner.sh check-binaries <file>...   self-hosted: lists home folder paths left in built binaries (warning)
#
# Runs with macOS's bash 3.2: it has to work before Homebrew's bash is known to exist.
set -euo pipefail

selfHosted=no
[ "${RUNNER_ENVIRONMENT:-}" = self-hosted ] && selfHosted=yes
BUNDLE_IDS="io.github.cuong-tran.freefilesync io.github.cuong-tran.realtimesync" # FreeFileSync/Source/mac_bundle.sh
STATE_DIR="${RUNNER_TEMP:-/tmp}/ffs-ci-state" # what setup saved for cleanup (inside $RUNNER_TEMP, removed by cleanup)
CACHE_ROOT="${FFS_CI_CACHE:-${RUNNER_WORKSPACE:-$HOME}/ffs-ci-cache}"

output() { [ -z "${GITHUB_OUTPUT:-}" ] || echo "$1=$2" >> "$GITHUB_OUTPUT"; echo "$1=$2"; }
setenv() { [ -z "${GITHUB_ENV:-}" ] || echo "$1=$2" >> "$GITHUB_ENV"; }

# the private names of this machine: user (login and full name), host and computer names, at least 3 characters long
privateNames()
{
    local v
    for v in "$(id -un)" "$(id -F 2>/dev/null || true)" "$(hostname -s 2>/dev/null || true)" "$(hostname 2>/dev/null || true)" \
             "$(scutil --get ComputerName 2>/dev/null || true)" "$(scutil --get LocalHostName 2>/dev/null || true)"; do
        [ "${#v}" -lt 3 ] || echo "$v"
    done
}

# yes if this process has a window server session (a launchd job with SessionCreate has none, so no app can start)
windowServer()
{
    local r
    r="$(osascript -l JavaScript -e "ObjC.import('CoreGraphics'); \$.CGSessionCopyCurrentDictionary() ? 'yes' : 'no'" 2>/dev/null || echo no)"
    [ "$r" = yes ] && echo yes || echo no
}

guiAdvice()
{
    echo "the runner's jobs have no window server session, so FreeFileSync and RealTimeSync cannot start: the runner runs as a" \
         "launch agent with SessionCreate=true (what svc.sh installs). Remedy on the Mac: in ~/Library/LaunchAgents/actions.runner.*.plist" \
         "delete the SessionCreate key (or set it to false), then restart the runner (./svc.sh stop && ./svc.sh start); or run it" \
         "with ./run.sh in a terminal of the logged-in session. Or set the repository variable MACOS_RUNNER=hosted."
}

cmd_setup()
{
    output self_hosted "$selfHosted"
    output host_arch "$(uname -m)"
    local a
    for a in arm64 x86_64; do
        if arch "-$a" /usr/bin/true 2>/dev/null; then output "run_$a" yes; else output "run_$a" no; fi
    done
    output gui "$(windowServer)"
    if command -v ccache >/dev/null; then output ccache yes; else output ccache no; fi
    [ "$selfHosted" = yes ] || return 0

    local name
    while IFS= read -r name; do echo "::add-mask::$name"; done < <(privateNames)

    # tools: checked, never installed (this is the owner's Homebrew)
    local need missing="" optional="" prefix
    prefix="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
    for need in "$@"; do
        case "$need" in
            bash)
                local major minor # the bash on PATH, which "shell: bash" steps and the scripts' "#!/usr/bin/env bash" use
                read -r major minor <<< "$(bash -c 'echo "${BASH_VERSINFO[0]} ${BASH_VERSINFO[1]}"')"
                [ "$major" -gt 4 ] || { [ "$major" -eq 4 ] && [ "$minor" -ge 4 ]; } || missing="$missing bash" ;;
            build)
                [ -d "$prefix/opt/coreutils/libexec/gnubin" ] || missing="$missing coreutils"
                command -v flock >/dev/null || missing="$missing flock"
                command -v pkg-config >/dev/null || missing="$missing pkgconf"
                command -v gmake >/dev/null || missing="$missing make"
                command -v cmake >/dev/null || missing="$missing cmake" ;;
            timeout)    command -v gtimeout >/dev/null || missing="$missing coreutils" ;;
            python3)    command -v python3 >/dev/null || missing="$missing python" ;;
            ccache)     command -v ccache >/dev/null || optional="$optional ccache" ;;
            create-dmg) command -v create-dmg >/dev/null || optional="$optional create-dmg" ;;
            *) echo "mac-runner.sh setup: unknown need $need" >&2; exit 2 ;;
        esac
    done
    if [ -n "$optional" ]; then
        echo "::notice::optional on this Mac, not installed:$optional (brew install$optional); the job works without"
    fi
    if [ -n "$missing" ]; then
        missing="$(tr ' ' '\n' <<< "$missing" | awk 'NF && !seen[$0]++' | tr '\n' ' ')"
        echo "::error::tools missing on the self-hosted Mac: ${missing% }. CI installs nothing there; install them once with: brew install ${missing% }"
        exit 1
    fi

    # disk space: the library builds alone take several GB
    local freeGb minGb="${FFS_CI_MIN_FREE_GB:-10}"
    freeGb="$(df -k "${GITHUB_WORKSPACE:-$PWD}" | awk 'NR == 2 { print int($4 / 1048576) }')"
    echo "free disk space: $freeGb GB"
    if [ "$freeGb" -lt "$minGb" ]; then
        echo "::error::only $freeGb GB free on the self-hosted Mac (the job needs about $minGb GB); free some space, or set the repository variable MACOS_RUNNER=hosted"
        exit 1
    fi

    # everything temporary inside $RUNNER_TEMP (mktemp, the tests' folders and disk images), so that cleanup finds it
    mkdir -p "$RUNNER_TEMP/tmp" "$STATE_DIR"
    setenv TMPDIR "$RUNNER_TEMP/tmp/"
    setenv FFS_TEST_TRASH_LOG "$STATE_DIR/trash.log"
    touch "$STATE_DIR/job-start"

    # compiler paths (__FILE__, debug info) relative to "~" instead of the home folder; libraries built here are cached locally
    # with these flags (their cache keys say so)
    case "$HOME" in
        (*" "*) echo "::warning::the home folder path has a blank: compiler paths keep it" ;;
        (*) local flag="-ffile-prefix-map=$HOME=~" var
            for var in CFLAGS CXXFLAGS OBJCFLAGS OBJCXXFLAGS; do setenv "$var" "$flag"; done ;;
    esac

    # the apps' preferences and saved window state, as found: cleanup puts them back (the start checks give the apps a fresh
    # HOME, but macOS keeps both per bundle id in the real home folder)
    local id
    for id in $BUNDLE_IDS; do
        if defaults export "$id" "$STATE_DIR/$id.plist" 2>/dev/null && [ "$(defaults read "$id" 2>/dev/null | wc -c)" -gt 4 ]; then
            :
        else
            rm -f "$STATE_DIR/$id.plist"
        fi
        local saved="$HOME/Library/Saved Application State/$id.savedState"
        if [ -e "$saved" ]; then ditto "$saved" "$STATE_DIR/$id.savedState"; fi
    done
    echo "self-hosted runner prepared: names masked, TMPDIR in \$RUNNER_TEMP, app preferences saved for cleanup"
}

cmd_require_gui()
{
    if [ "$(windowServer)" = yes ]; then return 0; fi
    if [ "$selfHosted" = yes ]; then echo "::error::$(guiAdvice)"; exit 1; fi
    echo "::warning::no window server session on this runner"
}

cmd_cache()
{
    [ "$selfHosted" = yes ] || { echo "mac-runner.sh cache: self-hosted runner only" >&2; exit 2; }
    local op="${1:-}"
    case "$op" in
        restore)
            local key="$2" path="$3"
            local entry="$CACHE_ROOT/$key"
            if [ -f "$entry/.complete" ]; then
                rm -rf "$path"
                mkdir -p "$(dirname "$path")"
                cp -Rc "$entry/data" "$path" 2>/dev/null || { rm -rf "$path"; cp -R "$entry/data" "$path"; } # -c: APFS clones
                touch "$entry/.complete"
                echo "local cache hit: $key"
                output hit true
            else
                echo "local cache miss: $key"
                output hit false
            fi ;;
        save)
            local kind="$2" key="$3" path="$4"
            local entry="$CACHE_ROOT/$key" tmp="$CACHE_ROOT/.tmp.$$"
            [ -d "$path" ] || { echo "nothing to cache at $path"; return 0; }
            if [ ! -f "$entry/.complete" ]; then
                mkdir -p "$tmp"
                cp -Rc "$path" "$tmp/data" 2>/dev/null || { rm -rf "$tmp/data"; cp -R "$path" "$tmp/data"; }
                echo "$kind" > "$tmp/.kind"
                touch "$tmp/.complete"
                rm -rf "$entry"
                mv "$tmp" "$entry"
                echo "local cache saved: $key ($(du -sh "$entry" | cut -f1))"
            fi
            # the two most recently used entries of this kind stay
            local e n=0
            for e in $(ls -t "$CACHE_ROOT"/*/.complete 2>/dev/null); do
                e="$(dirname "$e")"
                [ "$(cat "$e/.kind" 2>/dev/null)" = "$kind" ] || continue
                n=$((n + 1))
                if [ "$n" -gt 2 ]; then echo "local cache: evicting $(basename "$e")"; rm -rf "$e"; fi
            done ;;
        *) echo "usage: mac-runner.sh cache restore <key> <path> | save <kind> <key> <path>" >&2; exit 2 ;;
    esac
}

# paths of this job: the checkout and $RUNNER_TEMP (a prefix test that keeps the trailing slash)
ourPath()
{
    case "$1" in
        ("${GITHUB_WORKSPACE:-/nonexistent}"/*|"${RUNNER_TEMP:-/nonexistent}"/*) return 0 ;;
    esac
    return 1
}

cmd_cleanup()
{
    [ "$selfHosted" = yes ] || return 0
    [ -n "${GITHUB_WORKSPACE:-}" ] && [ -n "${RUNNER_TEMP:-}" ] || { echo "cleanup: GITHUB_WORKSPACE/RUNNER_TEMP not set" >&2; exit 2; }

    # 1. processes started from the checkout or $RUNNER_TEMP (apps, crashpad_handler, test binaries) that are still running
    local pid exe n=0
    while read -r pid exe; do
        [ "$pid" != $$ ] || continue
        if ourPath "$exe"; then kill -TERM "$pid" 2>/dev/null && n=$((n + 1)) || true; fi
    done < <(ps -axo pid=,comm=)
    if [ "$n" -gt 0 ]; then
        sleep 2
        while read -r pid exe; do
            if ourPath "$exe"; then kill -KILL "$pid" 2>/dev/null || true; fi
        done < <(ps -axo pid=,comm=)
    fi
    echo "cleanup: $n leftover process(es) ended"

    # 2. disk images attached from the checkout or $RUNNER_TEMP (the tests' FAT/APFS images, the DMG checks)
    local dev n=0
    while IFS= read -r dev; do
        [ -n "$dev" ] || continue
        hdiutil detach -force "$dev" >/dev/null 2>&1 && n=$((n + 1)) || echo "::warning::cleanup: could not detach $dev"
    done < <(hdiutil info -plist | python3 -I -c '
import plistlib, sys
roots = tuple(r.rstrip("/") + "/" for r in sys.argv[1:])
for image in plistlib.loads(sys.stdin.buffer.read()).get("images", []):
    if image.get("image-path", "").startswith(roots):
        devs = sorted(e.get("dev-entry", "") for e in image.get("system-entities", []) if e.get("dev-entry"))
        if devs: print(devs[0])
' "$GITHUB_WORKSPACE" "$RUNNER_TEMP")
    echo "cleanup: $n disk image(s) detached"

    # 3. what the tests moved to the trash and did not delete themselves (cancelled run): only listed items, only inside a
    #    trash folder, only with the tests' name prefixes
    local item n=0
    if [ -f "$STATE_DIR/trash.log" ]; then
        while IFS= read -r item; do
            case "$item" in
                (*/.Trash/zen_test\ *|*/.Trash/sync_test\ *|*/.Trashes/*/zen_test\ *|*/.Trashes/*/sync_test\ *)
                    if [ -e "$item" ] || [ -L "$item" ]; then rm -rf "$item" && n=$((n + 1)) || echo "::warning::cleanup: could not delete a test item from the trash"; fi ;;
            esac
        done < "$STATE_DIR/trash.log"
    fi
    echo "cleanup: $n test item(s) deleted from the trash"

    # 4. crash reports of our binaries written during this job (not uploaded, not printed: they hold machine details)
    local report n=0
    if [ -f "$STATE_DIR/job-start" ] && [ -d "$HOME/Library/Logs/DiagnosticReports" ]; then
        while IFS= read -r report; do
            # .ips files are JSON, which writes "/" as "\/"
            if grep -qF -e "$GITHUB_WORKSPACE/" -e "$RUNNER_TEMP/" -e "$(sed 's#/#\\/#g' <<< "$GITHUB_WORKSPACE/")" -e "$(sed 's#/#\\/#g' <<< "$RUNNER_TEMP/")" "$report" 2>/dev/null; then
                rm -f "$report" && n=$((n + 1))
            fi
        done < <(find "$HOME/Library/Logs/DiagnosticReports" -type f -newer "$STATE_DIR/job-start" 2>/dev/null)
    fi
    echo "cleanup: $n crash report(s) of test binaries deleted"

    # 5. Launch Services: forget app bundles registered from the checkout or $RUNNER_TEMP (started apps register themselves)
    local lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister path n=0
    if [ -x "$lsregister" ]; then
        while IFS= read -r path; do
            if ourPath "$path"; then "$lsregister" -u "$path" >/dev/null 2>&1 && n=$((n + 1)) || true; fi
        done < <("$lsregister" -dump 2>/dev/null | sed -nE 's/^path: +(.*\.app)( \(0x[0-9a-f]+\))?$/\1/p' | sort -u)
    fi
    echo "cleanup: $n app bundle(s) unregistered from Launch Services"

    # 6. the apps' preferences and saved window state as setup found them
    local id saved
    for id in $BUNDLE_IDS; do
        if [ -f "$STATE_DIR/$id.plist" ]; then
            defaults import "$id" "$STATE_DIR/$id.plist" 2>/dev/null || echo "::warning::cleanup: could not restore the preferences of $id"
        elif [ -d "$STATE_DIR" ]; then
            defaults delete "$id" >/dev/null 2>&1 || true
        fi
        saved="$HOME/Library/Saved Application State/$id.savedState"
        if [ -d "$STATE_DIR/$id.savedState" ]; then
            rm -rf "$saved"; ditto "$STATE_DIR/$id.savedState" "$saved"
        elif [ -d "$STATE_DIR" ]; then
            rm -rf "$saved"
        fi
    done
    echo "cleanup: preferences and saved state of the apps as found"

    # 7. build output (the next checkout would remove it too, but the disk of this Mac is the owner's); the local cache keeps
    #    the libraries. Folders in $RUNNER_TEMP whose name starts with "_" belong to the runner.
    rm -rf "$GITHUB_WORKSPACE/.build" "$GITHUB_WORKSPACE/FreeFileSync/Build"
    find "$RUNNER_TEMP" -mindepth 1 -maxdepth 1 ! -name '_*' -exec rm -rf {} + 2>/dev/null || true

    # 8. cache entries unused for 14 days
    if [ -d "$CACHE_ROOT" ]; then
        find "$CACHE_ROOT" -mindepth 2 -maxdepth 2 -name .complete -mtime +14 2>/dev/null | while IFS= read -r e; do rm -rf "$(dirname "$e")"; done
        rm -rf "$CACHE_ROOT"/.tmp.*
        echo "cleanup: local cache $(du -sh "$CACHE_ROOT" | cut -f1)"
    fi
    echo "cleanup: $(df -h "$GITHUB_WORKSPACE" | awk 'NR == 2 { print $4 }') free"
}

cmd_scrub()
{
    [ "$selfHosted" = yes ] || return 0
    local f name sedArgs
    sedArgs=(-e "s#$(sed 's/[][\\.*^$#]/\\&/g' <<< "$HOME")#~#g")
    while IFS= read -r name; do sedArgs+=(-e "s#$(sed 's/[][\\.*^$#]/\\&/g' <<< "$name")#<private>#g"); done < <(privateNames)
    for f in "$@"; do
        [ -f "$f" ] || continue
        LC_ALL=C sed -i '' "${sedArgs[@]}" "$f"
    done
}

cmd_check_binaries()
{
    [ "$selfHosted" = yes ] || return 0
    local f strs hits
    for f in "$@"; do
        [ -f "$f" ] || continue
        strs="$(strings -a "$f")" # captured first: grep on a pipe could end strings with SIGPIPE
        hits="$(grep -F "$HOME/" <<< "$strs" | sed "s#$HOME#~#g" | sort -u || true)"
        if [ -n "$hits" ]; then
            echo "::warning::$(basename "$f") contains $(wc -l <<< "$hits" | tr -d ' ') path(s) in the home folder of the self-hosted Mac (shown with ~):"
            sed -n 1,20p <<< "$hits" | sed 's/^/    /'
        else
            echo "$(basename "$f"): no home folder paths"
        fi
    done
}

case "${1:-}" in
    setup)          shift; cmd_setup "$@" ;;
    require-gui)    cmd_require_gui ;;
    cache)          shift; cmd_cache "$@" ;;
    cleanup)        cmd_cleanup ;;
    scrub)          shift; cmd_scrub "$@" ;;
    check-binaries) shift; cmd_check_binaries "$@" ;;
    *) echo "usage: mac-runner.sh setup <need>... | require-gui | cache ... | cleanup | scrub <file>... | check-binaries <file>..." >&2; exit 2 ;;
esac
