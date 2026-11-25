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
#                                      TMPDIR into $RUNNER_TEMP; sets the trash log the tests write (FFS_TEST_TRASH_LOG); saves
#                                      what "cleanup" restores.
#                                      Needs: bash (bash 4.4+), build (coreutils, flock, pkg-config, GNU make, cmake), timeout
#                                      (gtimeout), python3, ccache (optional), create-dmg (optional).
#   mac-runner.sh require-gui          fails on the self-hosted runner if its jobs have no window server session (apps cannot start)
#   mac-runner.sh cache restore <key> <path>       self-hosted: a persistent cache folder next to the checkout
#   mac-runner.sh cache save <kind> <key> <path>   ($RUNNER_WORKSPACE/ffs-ci-cache); "restore" copies an entry to <path> (APFS
#                                      clones: instant, no extra space) and outputs hit=true/false; "save" stores <path> under <key>
#                                      and keeps only the two most recently used entries of <kind> (entries unused for 14 days go
#                                      as well)
#   mac-runner.sh cleanup              last step of every macOS job (if: always(), so also after a failure or a cancellation).
#                                      Self-hosted: ends processes whose executable is in the checkout or $RUNNER_TEMP (resolved
#                                      paths, from lsof), detaches disk images that live there, deletes what the tests moved to the
#                                      trash (from the trash log, each path checked by trashItemOk), deletes
#                                      crash reports of binaries from the checkout or $RUNNER_TEMP, unregisters test app bundles
#                                      from Launch Services, restores the apps' preferences and saved window state as found,
#                                      removes the build output, evicts old cache entries
#   mac-runner.sh scrub <file>...      self-hosted: replaces the home folder, user, host and computer names in files about to be
#                                      uploaded as artifacts (build logs); a no-op on GitHub's runners
#   mac-runner.sh check-binaries <file>...   fails if a built binary contains a path in the home folder (any runner: on GitHub's
#                                      it is /Users/runner, on the self-hosted Mac the owner's), listing them with ~
#   mac-runner.sh self-test            tests the path checks, the trash validator (hostile inputs), the cache trimming (a path
#                                      with blanks) and the disk image list parsing in temporary folders; touches nothing else
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
    # (unwrapped first: the bridged NULL pointer itself is a truthy object)
    r="$(osascript -l JavaScript -e "ObjC.import('CoreGraphics');
        ObjC.deepUnwrap(ObjC.castRefToObject(\$.CGSessionCopyCurrentDictionary())) ? 'yes' : 'no'" 2>/dev/null || echo no)"
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

cacheRestore() # <key> <path>: prints true/false
{
    local key="$1" path="$2"
    local entry="$CACHE_ROOT/$key"
    if [ -f "$entry/.complete" ]; then
        rm -rf "$path"
        mkdir -p "$(dirname "$path")"
        cp -Rc "$entry/data" "$path" 2>/dev/null || { rm -rf "$path"; cp -R "$entry/data" "$path"; } # -c: APFS clones
        touch "$entry/.complete"
        echo "local cache hit: $key" >&2
        echo true
    else
        echo "local cache miss: $key" >&2
        echo false
    fi
}

cacheSave() # <kind> <key> <path>
{
    local kind="$1" key="$2" path="$3"
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
    # the two most recently used entries of this kind stay (line by line: the work folder path may contain blanks)
    local e n=0
    while IFS= read -r e; do
        e="${e%/.complete}"
        [ "$(cat "$e/.kind" 2>/dev/null)" = "$kind" ] || continue
        n=$((n + 1))
        if [ "$n" -gt 2 ]; then echo "local cache: evicting $(basename "$e")"; rm -rf "$e"; fi
    done < <(ls -td "$CACHE_ROOT"/*/.complete 2>/dev/null || true)
}

cmd_cache()
{
    [ "$selfHosted" = yes ] || { echo "mac-runner.sh cache: self-hosted runner only" >&2; exit 2; }
    case "${1:-}" in
        restore) output hit "$(cacheRestore "$2" "$3")" ;;
        save)    cacheSave "$2" "$3" "$4" ;;
        *) echo "usage: mac-runner.sh cache restore <key> <path> | save <kind> <key> <path>" >&2; exit 2 ;;
    esac
}

# the folders of this job: the checkout and $RUNNER_TEMP, each as given and resolved (pwd -P: /private/..., a symlinked work
# folder), one per line, without a trailing slash; ROOTS holds them during cleanup
ourRoots()
{
    local r
    for r in "$@"; do
        [ -n "$r" ] || continue
        r="${r%/}"
        echo "$r"
        (cd -P -- "$r" 2>/dev/null && pwd -P) || true
    done | awk 'NF && !seen[$0]++'
}
ROOTS=""

ourPath() # <path>: true if it is inside one of $ROOTS
{
    local r
    while IFS= read -r r; do
        [ -n "$r" ] || continue
        case "$1" in ("$r"/*) return 0 ;; esac
    done <<< "$ROOTS"
    return 1
}

# pids of this user's processes whose executable or command path is inside $ROOTS: the executable is the first "txt" file lsof
# lists for a process (resolved, whatever path the program was started by; later "txt" entries are libraries and other mapped
# files, which don't count: e.g. Spotlight's indexer may map a file of the checkout), the command path is what ps shows (the
# path as started: absolute, as the workflows start the apps). Never this script or its parent.
ourPids()
{
    {
        local line pid="" first=no p exe
        { lsof -a -u "$(id -u)" -d txt -Fpn 2>/dev/null || true; } | while IFS= read -r line; do
            case "$line" in
                (p*) pid="${line#p}" first=yes ;;
                (n*) if [ "$first" = yes ] && ourPath "${line#n}"; then echo "$pid"; fi; first=no ;;
            esac
        done
        ps -x -o pid=,comm= | while read -r p exe; do if ourPath "$exe"; then echo "$p"; fi; done
    } | awk -v self=$$ -v parent=$PPID 'NF && $1 != self && $1 != parent && !seen[$1]++'
}

# trashItemOk <path>: true only for <home>/.Trash/<name> or <mount point>/.Trashes/<uid>/<name>, where <name> is one path component
# starting with "zen_test " or "sync_test " (the tests' prefixes), the path has no "." or ".." components or "//", the trash
# folders are no symlinks, <home> resolves to this user's home folder ($FFS_CI_TRASH_HOME for the self-test) and <mount point> is
# one. Strict on purpose: cleanup runs rm -rf on what passes, on a person's machine. (No cd into the trash: without Full Disk
# Access that may be refused.)
trashItemOk()
{
    local p="$1" name parent top home uid mount
    case "$p" in (/*) ;; (*) return 1 ;; esac
    case "$p/" in (*/./*|*/../*|*//*) return 1 ;; esac
    case "$p" in (*"
"*) return 1 ;; esac
    name="${p##*/}" parent="${p%/*}"
    case "$name" in ("zen_test "?*|"sync_test "?*) ;; (*) return 1 ;; esac
    uid="$(id -u)"
    home="$(cd -P -- "${FFS_CI_TRASH_HOME:-$HOME}" 2>/dev/null && pwd -P)" || return 1
    case "$parent" in
        (*/.Trash)
            top="${parent%/.Trash}"
            [ -n "$top" ] && [ -d "$parent" ] && [ ! -L "$parent" ] || return 1
            [ "$(cd -P -- "$top" 2>/dev/null && pwd -P)" = "$home" ] || return 1 ;;
        (*/.Trashes/"$uid")
            top="${parent%/.Trashes/$uid}"
            [ -n "$top" ] || top=/
            [ -d "$parent" ] && [ ! -L "$parent" ] && [ ! -L "${parent%/*}" ] || return 1
            top="$(cd -P -- "$top" 2>/dev/null && pwd -P)" || return 1
            # the mount point of the volume the folder is on: the last field(s) of "df -P" (they may contain blanks)
            mount="$(df -P "$top" 2>/dev/null | awk 'NR == 2 { for (i = 6; i <= NF; ++i) printf "%s%s", $i, (i < NF ? " " : "") }')"
            [ -n "$mount" ] && [ "$mount" = "$top" ] || return 1 ;;
        (*) return 1 ;;
    esac
    return 0
}

# attachedImages <hdiutil info -plist file>: the whole-disk device (e.g. /dev/disk5) of each attached image whose image file is
# inside $ROOTS; fails if the file can't be read as a property list (read with PlistBuddy: always there, unlike python3)
attachedImages()
{
    local f="$1" buddy=/usr/libexec/PlistBuddy i=0 j path dev best
    "$buddy" -c 'Print :' "$f" >/dev/null 2>&1 || return 1
    while "$buddy" -c "Print :images:$i" "$f" >/dev/null 2>&1; do
        path="$("$buddy" -c "Print :images:$i:image-path" "$f" 2>/dev/null || true)"
        if [ -n "$path" ] && ourPath "$path"; then
            j=0 best=""
            while dev="$("$buddy" -c "Print :images:$i:system-entities:$j:dev-entry" "$f" 2>/dev/null)"; do
                if [ -z "$best" ] || [ "${#dev}" -lt "${#best}" ]; then best="$dev"; fi # /dev/disk5, not /dev/disk5s1
                j=$((j + 1))
            done
            [ -z "$best" ] || echo "$best"
        fi
        i=$((i + 1))
    done
}

cmd_cleanup()
{
    [ "$selfHosted" = yes ] || return 0
    [ -n "${GITHUB_WORKSPACE:-}" ] && [ -n "${RUNNER_TEMP:-}" ] || { echo "cleanup: GITHUB_WORKSPACE/RUNNER_TEMP not set" >&2; exit 2; }

    ROOTS="$(ourRoots "$GITHUB_WORKSPACE" "$RUNNER_TEMP")"

    # 1. processes from the checkout or $RUNNER_TEMP (apps, crashpad_handler, test binaries) that are still running
    local pid pids n=0
    pids="$(ourPids)"
    for pid in $pids; do kill -TERM "$pid" 2>/dev/null && n=$((n + 1)) || true; done
    if [ "$n" -gt 0 ]; then
        sleep 2
        for pid in $(ourPids); do kill -KILL "$pid" 2>/dev/null || true; done
    fi
    echo "cleanup: $n leftover process(es) ended"

    # 2. disk images attached from the checkout or $RUNNER_TEMP (the tests' FAT/APFS images, the DMG checks); a list that can't
    #    be read is a warning, not "0 detached"
    local dev n=0 info devs
    info="$(mktemp)"
    if ! hdiutil info -plist > "$info" 2>/dev/null; then
        echo "::warning::cleanup: hdiutil info failed: disk images of this job not checked"
    elif ! devs="$(attachedImages "$info")"; then
        echo "::warning::cleanup: hdiutil's image list could not be read: disk images of this job not checked"
    else
        while IFS= read -r dev; do
            [ -n "$dev" ] || continue
            hdiutil detach -force "$dev" >/dev/null 2>&1 && n=$((n + 1)) || echo "::warning::cleanup: could not detach $dev"
        done <<< "$devs"
        echo "cleanup: $n disk image(s) detached"
    fi
    rm -f "$info"

    # 3. what the tests moved to the trash and did not delete themselves (cancelled run): only listed paths that trashItemOk
    #    accepts; nothing else in the trash is looked at
    local item n=0 refused=0
    if [ -f "$STATE_DIR/trash.log" ]; then
        while IFS= read -r item; do
            [ -n "$item" ] || continue
            if ! trashItemOk "$item"; then refused=$((refused + 1)); continue; fi
            if [ -e "$item" ] || [ -L "$item" ]; then
                rm -rf -- "$item" && n=$((n + 1)) || echo "::warning::cleanup: could not delete a test item from the trash"
            fi
        done < "$STATE_DIR/trash.log"
    fi
    echo "cleanup: $n test item(s) deleted from the trash"
    [ "$refused" -eq 0 ] || echo "::warning::cleanup: $refused path(s) in the trash log refused (not a test item directly in a trash folder)"

    # 4. crash reports of our binaries written during this job (not uploaded, not printed: they hold machine details)
    local report n=0 r patterns
    patterns=()
    while IFS= read -r r; do # each root as given and resolved, plain and as JSON writes it ("/" as "\/" in .ips files)
        patterns+=(-e "$r/" -e "$(sed 's#/#\\/#g' <<< "$r/")")
    done <<< "$ROOTS"
    if [ -f "$STATE_DIR/job-start" ] && [ -d "$HOME/Library/Logs/DiagnosticReports" ]; then
        while IFS= read -r report; do
            if grep -qF "${patterns[@]}" "$report" 2>/dev/null; then rm -f "$report" && n=$((n + 1)); fi
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
    local f strs hits home failed=0 roots
    home="$(cd -P -- "$HOME" && pwd -P)"
    roots="$(printf '%s\n' "${HOME%/}" "$home" | awk '!seen[$0]++')"
    for f in "$@"; do
        [ -f "$f" ] || { echo "::error::check-binaries: $f does not exist"; failed=1; continue; }
        strs="$(strings -a "$f")" # captured first: grep on a pipe could end strings with SIGPIPE
        hits="$(while IFS= read -r r; do grep -F "$r/" <<< "$strs" | sed "s#$r#~#g" || true; done <<< "$roots" | sort -u)"
        if [ -n "$hits" ]; then
            echo "::error::$(basename "$f") contains $(wc -l <<< "$hits" | tr -d ' ') path(s) in the home folder of the build machine (shown with ~):"
            sed -n 1,30p <<< "$hits" | sed 's/^/    /'
            failed=1
        else
            echo "$(basename "$f"): no home folder paths"
        fi
    done
    return "$failed"
}

# tests of the parts above that decide what cleanup deletes, in temporary folders only (a hosted job runs it, and any Mac can)
cmd_self_test()
{
    local fails=0 base uid
    base="$(cd -P -- "$(mktemp -d)" && pwd -P)"
    SELF_TEST_DIR="$base"
    trap 'rm -rf "$SELF_TEST_DIR"' EXIT
    uid="$(id -u)"
    check() # <expected 0/1> <description> <command...>
    {
        local want="$1" what="$2" got=0
        shift 2
        "$@" || got=1
        if [ "$got" = "$want" ]; then echo "ok: $what"; else echo "FAILED: $what (expected $([ "$want" = 0 ] && echo accept || echo refuse))"; fails=$((fails + 1)); fi
    }

    # trash validator: one accepted case, hostile ones refused
    local h="$base/home" other="$base/other"
    mkdir -p "$h/.Trash/zen_test 1 dir/sub" "$other/.Trash" "$base/linkhome" "$base/notmount/.Trashes/$uid"
    ln -s "$other/.Trash" "$base/linkhome/.Trash"
    export FFS_CI_TRASH_HOME="$h"
    check 0 "item in the home trash"               trashItemOk "$h/.Trash/zen_test 1 to recycle.txt"
    check 0 "folder item in the home trash"        trashItemOk "$h/.Trash/zen_test 1 dir"
    check 0 "sync test item in the home trash"     trashItemOk "$h/.Trash/sync_test 7 gone.txt"
    check 1 "relative path"                        trashItemOk ".Trash/zen_test 1 x"
    check 1 "other name prefix"                    trashItemOk "$h/.Trash/Important.docx"
    check 1 "bare prefix"                          trashItemOk "$h/.Trash/zen_test "
    check 1 "the trash itself"                     trashItemOk "$h/.Trash"
    check 1 "two components below the trash"       trashItemOk "$h/.Trash/zen_test 1 dir/sub"
    check 1 "dot-dot after the item"               trashItemOk "$h/.Trash/zen_test 1 dir/../../.."
    check 1 "dot-dot as the item"                  trashItemOk "$h/.Trash/.."
    check 1 "dot-dot inside the path"              trashItemOk "$h/.Trash/../.Trash/zen_test 1 x"
    check 1 "dot-dot escape with a prefix name"    trashItemOk "$h/x/../.Trash/zen_test 1 x"
    check 1 "dot component"                        trashItemOk "$h/./.Trash/zen_test 1 x"
    check 1 "trailing dot-dot"                     trashItemOk "$h/.Trash/zen_test 1/.."
    check 1 "double slash"                         trashItemOk "$h//.Trash/zen_test 1 x"
    check 1 "trailing slash"                       trashItemOk "$h/.Trash/zen_test 1 x/"
    check 1 "another folder's .Trash"              trashItemOk "$other/.Trash/zen_test 1 x"
    check 1 "symlinked .Trash"                     env FFS_CI_TRASH_HOME="$base/linkhome" "$0" __trash-ok "$base/linkhome/.Trash/zen_test 1 x"
    check 1 ".Trashes on a folder that is no mount point" trashItemOk "$base/notmount/.Trashes/$uid/zen_test 1 x"
    check 1 ".Trashes of another user"             trashItemOk "/.Trashes/$((uid + 1))/zen_test 1 x"
    check 1 "root folder"                          trashItemOk "/"
    check 1 "empty path"                           trashItemOk ""
    check 1 "newline in the path"                  trashItemOk "$h/.Trash/zen_test 1
x"
    unset FFS_CI_TRASH_HOME

    # paths of the job, as given and resolved
    mkdir -p "$base/real dir" && ln -s "$base/real dir" "$base/link dir"
    ROOTS="$(ourRoots "$base/link dir/")"
    check 0 "path below a root as given"          ourPath "$base/link dir/a/b"
    check 0 "path below a root resolved"          ourPath "$base/real dir/a"
    check 1 "the root itself"                     ourPath "$base/real dir"
    check 1 "a sibling with the root as prefix"   ourPath "$base/real dir2/a"

    # cache: a cache folder with blanks in its path, the two most recently used entries of a kind stay
    CACHE_ROOT="$base/cache with blanks"
    mkdir -p "$base/data"
    echo x > "$base/data/f"
    local k
    for k in a b c; do cacheSave kind1 "key $k" "$base/data" > /dev/null; sleep 1; done
    cacheSave kind2 "other" "$base/data" > /dev/null
    check 0 "cache: newest entries of the kind kept" test -f "$CACHE_ROOT/key c/.complete" -a -f "$CACHE_ROOT/key b/.complete"
    check 1 "cache: oldest entry of the kind evicted" test -e "$CACHE_ROOT/key a"
    check 0 "cache: other kinds untouched"         test -f "$CACHE_ROOT/other/.complete"
    check 0 "cache: restore"                       test "$(cacheRestore "key b" "$base/restored" 2>/dev/null)" = true -a -f "$base/restored/f"

    # disk image list (macOS: PlistBuddy)
    if [ -x /usr/libexec/PlistBuddy ]; then
        ROOTS="$(ourRoots "$base/ws")"
        cat > "$base/info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>images</key><array>
<dict><key>image-path</key><string>/Users/someone/Downloads/Other.dmg</string><key>system-entities</key><array>
  <dict><key>dev-entry</key><string>/dev/disk4s1</string></dict><dict><key>dev-entry</key><string>/dev/disk4</string></dict></array></dict>
<dict><key>image-path</key><string>$base/ws/tmp/tmp.x/test image.dmg</string><key>system-entities</key><array>
  <dict><key>dev-entry</key><string>/dev/disk7s1</string></dict><dict><key>dev-entry</key><string>/dev/disk7</string></dict></array></dict>
</array></dict></plist>
EOF
        check 0 "image list: only this job's image, whole disk" test "$(attachedImages "$base/info.plist")" = /dev/disk7
        echo "not a plist" > "$base/bad.plist"
        check 1 "image list: unreadable list is an error" attachedImages "$base/bad.plist"
    else
        echo "skipped: disk image list (no PlistBuddy: not macOS)"
    fi

    [ "$fails" -eq 0 ] || { echo "== mac-runner.sh self-test: $fails failed"; return 1; }
    echo "== mac-runner.sh self-test: all passed"
}

case "${1:-}" in
    setup)          shift; cmd_setup "$@" ;;
    require-gui)    cmd_require_gui ;;
    cache)          shift; cmd_cache "$@" ;;
    cleanup)        cmd_cleanup ;;
    scrub)          shift; cmd_scrub "$@" ;;
    check-binaries) shift; cmd_check_binaries "$@" ;;
    self-test)      cmd_self_test ;;
    __trash-ok)     trashItemOk "$2" ;; # for the self-test (a fresh process for another FFS_CI_TRASH_HOME)
    *) echo "usage: mac-runner.sh setup <need>... | require-gui | cache ... | cleanup | scrub <file>... | check-binaries <file>..." >&2; exit 2 ;;
esac
