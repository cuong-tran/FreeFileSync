#!/usr/bin/env bash
# Build libssh2 against the OpenSSL in $FFS_DEPS_PREFIX, with the Bugs.txt fix applied.
# FreeFileSync 14.13 needs libssh2 1.11.2_DEV (e.g. LIBSSH2_ERROR_STORE_OVERFLOW is not in
# the 1.11.1 release), so this builds a pinned commit of libssh2's master branch.
# Usage: build-libssh2.sh [commit]   (or set LIBSSH2_COMMIT; a full commit hash)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

# master as of 2026-10-14, the last commit before the FreeFileSync 14.13 release
LIBSSH2_COMMIT="${1:-${LIBSSH2_COMMIT:-83284a05a1add34722c1fa36fa706fe7aced87b6}}"
NAME="libssh2-$LIBSSH2_COMMIT"

if is_installed "$NAME" openssl; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

# GitHub's commit archive unpacks to libssh2-<commit>
fetch_source "$NAME" "$NAME.tar.gz" "https://github.com/libssh2/libssh2/archive/$LIBSSH2_COMMIT.tar.gz"

# A source snapshot has no configure script, so use libssh2's CMake build.
BUILD="$(fresh_build_dir libssh2)"
if [ "$FFS_TARGET" = linux ]; then
    TARGET_OPTIONS=(-DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF)
else
    # macOS: a static library for one architecture, against the static OpenSSL in the prefix and the SDK's zlib
    TARGET_OPTIONS=(-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DOPENSSL_USE_STATIC_LIBS=ON
                    -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET")
fi
cmake -S "$SOURCE_DIR/$NAME" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_RPATH="$INSTALL_DIR/lib" \
    -DCRYPTO_BACKEND=OpenSSL \
    -DOPENSSL_ROOT_DIR="$INSTALL_DIR" \
    -DENABLE_ZLIB_COMPRESSION=ON \
    "${TARGET_OPTIONS[@]}" \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -DLIBSSH2_BUILD_DOCS=OFF
cmake --build "$BUILD" -j "$JOBS"
cmake --install "$BUILD"
[ "$FFS_TARGET" = linux ] || verify_target_arch "$INSTALL_DIR/lib/libssh2.a"
mark_installed "$NAME" openssl
