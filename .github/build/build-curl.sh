#!/bin/bash
# Build libcurl against the OpenSSL and libssh2 in $FFS_DEPS_PREFIX, with the Bugs.txt fixes applied.
# Usage: build-curl.sh [version]   (or set CURL_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

CURL_VERSION="${1:-${CURL_VERSION:-8.21.0}}"
NAME="curl-$CURL_VERSION"

if is_installed "$NAME"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

fetch_source "$NAME" "$NAME.tar.xz" "https://curl.se/download/$NAME.tar.xz"

cd "$(fresh_build_dir curl)"
"$SOURCE_DIR/$NAME/configure" \
    --prefix="$INSTALL_DIR" \
    --with-openssl="$INSTALL_DIR" \
    --with-libssh2="$INSTALL_DIR" \
    --with-libidn2 \
    --without-libpsl \
    --disable-ldap \
    --disable-static \
    --disable-docs \
    --disable-manual \
    --enable-versioned-symbols
make -j "$JOBS"
make install
mark_installed "$NAME"
