#!/bin/bash
# Build libssh2 against the OpenSSL in $FFS_DEPS_PREFIX, with the Bugs.txt fix applied.
# Usage: build-libssh2.sh [version]   (or set LIBSSH2_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

LIBSSH2_VERSION="${1:-${LIBSSH2_VERSION:-1.11.1}}"
NAME="libssh2-$LIBSSH2_VERSION"

if is_installed "$NAME"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

fetch_source "$NAME" "$NAME.tar.gz" "https://github.com/libssh2/libssh2/releases/download/$NAME/$NAME.tar.gz"

cd "$(fresh_build_dir libssh2)"
"$SOURCE_DIR/$NAME/configure" \
    --prefix="$INSTALL_DIR" \
    --with-crypto=openssl \
    --with-libssl-prefix="$INSTALL_DIR" \
    --with-libz \
    --disable-static \
    --disable-examples-build \
    --disable-docker-tests \
    --disable-sshd-tests
make -j "$JOBS"
make install
mark_installed "$NAME"
