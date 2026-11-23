#!/bin/bash
# Shared settings and helpers for the build-*.sh scripts. Source it, don't run it.
#
# Environment overrides:
#   FFS_BUILD_DIR     work area for downloads, sources and build trees (default: <repo>/.build)
#   FFS_DEPS_PREFIX   install prefix for the libraries (default: $FFS_BUILD_DIR/deps)
#   FFS_SKIP_PATCHES  set to 1 to build a library version that has no patch in patches/
#   JOBS              parallel make jobs (default: nproc)

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." &>/dev/null && pwd)"
SCRIPT_DIR="$REPO_ROOT/.github/build"
PATCH_DIR="$SCRIPT_DIR/patches"

BUILD_DIR="${FFS_BUILD_DIR:-$REPO_ROOT/.build}"
ARCHIVE_DIR="$BUILD_DIR/archives"
SOURCE_DIR="$BUILD_DIR/src"
WORK_DIR="$BUILD_DIR/tmp"
INSTALL_DIR="${FFS_DEPS_PREFIX:-$BUILD_DIR/deps}"
STAMP_DIR="$INSTALL_DIR/.stamps"
JOBS="${JOBS:-$(nproc)}"

# Make the installed libraries win over the system ones, at build time (wx-config, pkg-config)
# and at run time (rpath; the libraries are installed as shared objects, except wxWidgets).
export PATH="$INSTALL_DIR/bin:$PATH"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPPFLAGS="-I$INSTALL_DIR/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$INSTALL_DIR/lib -Wl,-rpath,$INSTALL_DIR/lib${LDFLAGS:+ $LDFLAGS}"

# SHA-256 of the archives for the default versions. Other versions are downloaded unverified.
declare -A KNOWN_SHA256=(
    [openssl-3.5.9.tar.gz]=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
    [libssh2-83284a05a1add34722c1fa36fa706fe7aced87b6.tar.gz]=03e70390ceafcde9ceb98654d9e55f05c705c2e62c05863d5284e4e11f51b8a7
    [curl-8.21.0.tar.xz]=aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
    [wxWidgets-3.3.4.tar.bz2]=815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98
)

log() { printf '\n==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# check_name <name-version>: versions and commits come from arguments and environment variables and
# end up in file paths (rm -rf, tar, stamps), so allow only <letters/digits>-<letters/digits/dots/underscores>
check_name() {
    [[ "$1" =~ ^[A-Za-z0-9]+-[A-Za-z0-9._]+$ ]] || die "invalid library version or commit in '$1'"
}

# is_installed <name-version> [dependency name...]: true if this version was installed with the
# current patch and build script, and against the dependency versions installed now (e.g.
# "openssl" matches the stamp of whichever openssl-<version> was built), so changing any of
# them rebuilds it.
is_installed() {
    check_name "$1"
    [ -f "$STAMP_DIR/$1" ] && [ "$(cat "$STAMP_DIR/$1")" = "$(install_id "$@")" ]
}

# mark_installed <name-version> [dependency name...]: call after a successful install
mark_installed() {
    mkdir -p "$STAMP_DIR"
    install_id "$@" >"$STAMP_DIR/$1"
}

install_id() {
    local name="$1" dep stamp
    shift
    if [ -f "$PATCH_DIR/$name.patch" ]; then sha256sum "$PATCH_DIR/$name.patch" | cut -d' ' -f1; else echo unpatched; fi
    sha256sum "$0" | cut -d' ' -f1 # the calling build-<lib>.sh, for its configure options
    for dep in "$@"; do
        for stamp in "$STAMP_DIR/$dep"-*; do
            [ -f "$stamp" ] && echo "$(basename "$stamp") $(sha256sum <"$stamp" | cut -d' ' -f1)"
        done
    done
    return 0
}

# fetch_source <name-version> <archive file name> <url> [nopatch]
# Downloads and verifies the archive, unpacks a fresh copy to $SOURCE_DIR/<name-version>
# and applies patches/<name-version>.patch (the fixes listed in Bugs.txt).
fetch_source() {
    local name="$1" file="$2" url="$3" patchMode="${4:-}"
    check_name "$name"
    local archive="$ARCHIVE_DIR/$file"
    local patch="$PATCH_DIR/$name.patch"

    if [ "$patchMode" != nopatch ] && [ ! -f "$patch" ] && [ "${FFS_SKIP_PATCHES:-0}" != 1 ]; then
        die "no patch for $name in $PATCH_DIR: port the $name fixes from Bugs.txt first, or set FFS_SKIP_PATCHES=1 to build it unpatched"
    fi

    mkdir -p "$ARCHIVE_DIR" "$SOURCE_DIR"
    if [ ! -f "$archive" ]; then
        log "Downloading $url"
        curl -fL --retry 3 -o "$archive.part" "$url"
        mv "$archive.part" "$archive"
    fi

    local expected="${KNOWN_SHA256[$file]:-}"
    if [ -n "$expected" ]; then
        echo "$expected  $archive" | sha256sum -c - >/dev/null ||
            { rm -f "$archive"; die "checksum mismatch for $file (deleted, run again to re-download)"; }
    else
        log "No known checksum for $file, not verified"
    fi

    rm -rf "${SOURCE_DIR:?}/$name"
    tar xf "$archive" -C "$SOURCE_DIR"
    [ -d "$SOURCE_DIR/$name" ] || die "$file did not unpack to $SOURCE_DIR/$name"

    if [ "$patchMode" != nopatch ] && [ -f "$patch" ]; then
        log "Applying $(basename "$patch")"
        patch -d "$SOURCE_DIR/$name" -p1 --forward --batch <"$patch"
    fi
}

# fresh_build_dir <name>: prints an empty out-of-tree build directory
fresh_build_dir() {
    local dir="$WORK_DIR/$1"
    rm -rf "$dir"
    mkdir -p "$dir"
    echo "$dir"
}
