#!/usr/bin/env bash
# Shared settings and helpers for the build-*.sh scripts. Source it, don't run it.
#
# Environment overrides:
#   FFS_TARGET        what to build for: "linux" (the Linux host; default on Linux), "macos-arm64" or "macos-x86_64"
#                     (default on macOS: the host architecture; the other one is cross-compiled with -arch)
#   FFS_BUILD_DIR     work area for downloads, sources and build trees (default: <repo>/.build)
#   FFS_DEPS_PREFIX   install prefix for the libraries (default: $FFS_BUILD_DIR/deps on Linux, $FFS_BUILD_DIR/deps/<target> on macOS)
#   FFS_SKIP_PATCHES  set to 1 to build a library version that has no patch in patches/
#   FFS_UNPATCHED     set to 1 to build every library without its patch, even if patches/ has one (CI uses it
#                     to check that FreeFileSync still compiles against stock libraries)
#   JOBS              parallel make jobs (default: nproc)

# associative arrays below; macOS ships bash 3.2 as /bin/bash, so the scripts start with "#!/usr/bin/env bash"
if (( BASH_VERSINFO[0] < 4 )); then
    printf 'error: bash 4 or newer needed (this is %s); on macOS: brew install bash\n' "$BASH_VERSION" >&2
    exit 1
fi

# macOS: the GNU tools these scripts use (sha256sum, realpath -m, nproc, flock) come from Homebrew
if [ "$(uname -s)" = Darwin ]; then
    command -v brew >/dev/null && [ -d "$(brew --prefix)/opt/coreutils/libexec/gnubin" ] && PATH="$(brew --prefix)/opt/coreutils/libexec/gnubin:$PATH"
    for tool in sha256sum realpath nproc flock; do
        command -v "$tool" >/dev/null || { printf 'error: %s not found: brew install coreutils flock\n' "$tool" >&2; exit 1; }
    done
fi

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." &>/dev/null && pwd)"
# the build-<lib>.sh that sourced this file, resolved now because the scripts cd before mark_installed
CALLER_SCRIPT="$(realpath -- "$0")"
COMMON_SCRIPT="$(realpath -- "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$REPO_ROOT/.github/build"
PATCH_DIR="$SCRIPT_DIR/patches"

# Target: Linux builds for its own host as before; macOS builds one architecture per prefix
case "$(uname -s)" in
    Linux)  FFS_TARGET="${FFS_TARGET:-linux}"
            [ "$FFS_TARGET" = linux ] || { printf 'error: FFS_TARGET=%s cannot be built on Linux\n' "$FFS_TARGET" >&2; exit 1; } ;;
    Darwin) FFS_TARGET="${FFS_TARGET:-macos-$(uname -m)}"
            [[ "$FFS_TARGET" =~ ^macos-(arm64|x86_64)$ ]] || { printf 'error: FFS_TARGET=%s cannot be built on macOS\n' "$FFS_TARGET" >&2; exit 1; } ;;
    *)      printf 'error: unsupported build host %s\n' "$(uname -s)" >&2; exit 1 ;;
esac

# absolute, because the scripts change directories and the prefix ends up in rpaths and .pc files
BUILD_DIR="$(realpath -m -- "${FFS_BUILD_DIR:-$REPO_ROOT/.build}")"
ARCHIVE_DIR="$BUILD_DIR/archives"
SOURCE_DIR="$BUILD_DIR/src"
if [ "$FFS_TARGET" = linux ]; then
    WORK_DIR="$BUILD_DIR/tmp"
    INSTALL_DIR="$(realpath -m -- "${FFS_DEPS_PREFIX:-$BUILD_DIR/deps}")"
else
    WORK_DIR="$BUILD_DIR/tmp/$FFS_TARGET"
    INSTALL_DIR="$(realpath -m -- "${FFS_DEPS_PREFIX:-$BUILD_DIR/deps/$FFS_TARGET}")"
fi
STAMP_DIR="$INSTALL_DIR/.stamps"
JOBS="${JOBS:-$(nproc)}"

# One build at a time per work area and per prefix: the scripts delete and recreate trees in src/ and
# tmp/ and install into the prefix. Locks are always taken in this order, and released when the script exits.
mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
exec 8>"$BUILD_DIR/.lock" 9>"$INSTALL_DIR/.lock"
for fd in 8 9; do
    flock -n "$fd" || { printf '==> Waiting for another build using %s\n' "$BUILD_DIR or $INSTALL_DIR" >&2; flock "$fd"; }
done

# Make the installed libraries win over the system ones, at build time (wx-config, pkg-config)
# and at run time (rpath; the libraries are installed as shared objects, except wxWidgets).
export PATH="$INSTALL_DIR/bin:$PATH"
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPPFLAGS="-I$INSTALL_DIR/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$INSTALL_DIR/lib -Wl,-rpath,$INSTALL_DIR/lib${LDFLAGS:+ $LDFLAGS}"

# macOS: static libraries for one architecture, built with the toolchain the apps will use (CC/CXX, default: Xcode's
# clang) for the deployment target below. The architecture goes into the compiler flags, so x86_64 is cross-compiled
# on an arm64 host (and vice versa); setting CFLAGS/CXXFLAGS replaces the configure scripts' default "-g -O2", so -O2 is
# added here. pkg-config looks only into the prefix: Homebrew's .pc files describe libraries for the host
# architecture only, and nothing from Homebrew may end up in the app.
FFS_MACOS_DEPLOYMENT_TARGET=13.3 # oldest macOS the apps support; see .github/build/README.md for why
if [ "$FFS_TARGET" != linux ]; then
    TARGET_ARCH="${FFS_TARGET#macos-}"
    # shellcheck disable=SC2034 # AUTOCONF_HOST, OPENSSL_CONFIG: used by the build-<lib>.sh scripts that source this file
    case "$TARGET_ARCH" in
        arm64)  AUTOCONF_HOST=aarch64-apple-darwin; OPENSSL_CONFIG=darwin64-arm64-cc ;;
        x86_64) AUTOCONF_HOST=x86_64-apple-darwin;  OPENSSL_CONFIG=darwin64-x86_64-cc ;;
    esac
    export MACOSX_DEPLOYMENT_TARGET="$FFS_MACOS_DEPLOYMENT_TARGET"
    export CC="${CC:-clang}" CXX="${CXX:-clang++}"
    export CFLAGS="-O2 -arch $TARGET_ARCH${CFLAGS:+ $CFLAGS}"
    export CXXFLAGS="-O2 -arch $TARGET_ARCH${CXXFLAGS:+ $CXXFLAGS}"
    export LDFLAGS="-arch $TARGET_ARCH $LDFLAGS"
    export PKG_CONFIG_LIBDIR="$INSTALL_DIR/lib/pkgconfig"
fi

# SHA-256 of the archives for the default versions. Other versions are downloaded unverified.
declare -A KNOWN_SHA256=(
    [openssl-3.5.9.tar.gz]=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
    [libssh2-83284a05a1add34722c1fa36fa706fe7aced87b6.tar.gz]=03e70390ceafcde9ceb98654d9e55f05c705c2e62c05863d5284e4e11f51b8a7
    [curl-8.21.0.tar.xz]=aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
    [wxWidgets-3.3.4.tar.bz2]=815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98
    # macOS only (Linux uses the distribution's libidn2); same value as Homebrew's libidn2 formula
    [libidn2-2.3.8.tar.gz]=f557911bf6171621e1f72ff35f5b1825bb35b52ed45325dcdee931e5d3c0787a
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
# "openssl" matches the stamp of the openssl-<version> in the prefix; fetch_source keeps only
# one stamp per library), so changing any of them rebuilds it.
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
    if [ -f "$PATCH_DIR/$name.patch" ] && [ "${FFS_UNPATCHED:-0}" != 1 ]; then sha256sum "$PATCH_DIR/$name.patch" | cut -d' ' -f1; else echo unpatched; fi
    sha256sum "$CALLER_SCRIPT" | cut -d' ' -f1 # the calling build-<lib>.sh, for its configure options
    sha256sum "$COMMON_SCRIPT" | cut -d' ' -f1 # this file, for the shared settings above
    # the compilers and flags the library is built with
    echo "CC=${CC:-cc}: $("${CC:-cc}" --version 2>/dev/null | head -n 1)"
    echo "CXX=${CXX:-c++}: $("${CXX:-c++}" --version 2>/dev/null | head -n 1)"
    echo "CFLAGS=${CFLAGS:-} CXXFLAGS=${CXXFLAGS:-}"
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
    # this install replaces whatever version of the library is in the prefix: drop the stamps of all
    # its versions now, so neither switching back to an older version nor a failed build reuses one
    rm -f "$STAMP_DIR/${name%-*}"-*
    local archive="$ARCHIVE_DIR/$file"
    local patch="$PATCH_DIR/$name.patch"
    [ "${FFS_UNPATCHED:-0}" = 1 ] && patchMode=nopatch

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

# verify_target_arch <file>...: on macOS, each library or binary must contain exactly the target architecture and
# be built for the deployment target (catches a forgotten -arch, a Homebrew library or a wrong minimum version); no-op on Linux
verify_target_arch() {
    [ "$FFS_TARGET" = linux ] && return 0
    local file archs minos
    for file in "$@"; do
        [ -f "$file" ] || die "verify_target_arch: $file does not exist"
        archs="$(lipo -archs "$file" 2>/dev/null)" || die "verify_target_arch: lipo cannot read $file"
        [ "$archs" = "$TARGET_ARCH" ] || die "$file is built for '$archs', not $TARGET_ARCH"
        # the minimum macOS version of the first object file (LC_BUILD_VERSION) must be the deployment target
        minos="$(otool -l "$file" | awk '/LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
        [ "$minos" = "$MACOSX_DEPLOYMENT_TARGET" ] || die "$file is built for macOS ${minos:-?}, not $MACOSX_DEPLOYMENT_TARGET"
        log "$(basename "$file"): $archs, macOS $minos"
    done
}
