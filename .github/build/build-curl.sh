#!/usr/bin/env bash
# Build libcurl against the OpenSSL and libssh2 in $FFS_DEPS_PREFIX, with the Bugs.txt fixes applied.
# Usage: build-curl.sh [version]   (or set CURL_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

CURL_VERSION="${1:-${CURL_VERSION:-8.21.0}}"
NAME="curl-$CURL_VERSION"

DEPS=(openssl libssh2)
[ "$FFS_TARGET" = linux ] || DEPS+=(libidn2) # macOS: libidn2 from the prefix (build-libidn2.sh), Linux: the distribution's

if is_installed "$NAME" "${DEPS[@]}"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

fetch_source "$NAME" "$NAME.tar.xz" "https://curl.se/download/$NAME.tar.xz"

cd "$(fresh_build_dir curl)"
if [ "$FFS_TARGET" = linux ]; then
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
else
    # macOS: a static library for one architecture. Only what FreeFileSync uses (FTP, HTTPS for Google Drive and the update
    # check, SFTP through libssh2) plus the SDK's zlib; everything optional is switched off explicitly, so nothing is picked
    # up from Homebrew. OpenSSL verifies server certificates with the macOS trust store (--with-apple-sectrust), so no CA
    # bundle has to ship with the app.
    "$SOURCE_DIR/$NAME/configure" \
        --host="$AUTOCONF_HOST" \
        --prefix="$INSTALL_DIR" \
        --with-openssl="$INSTALL_DIR" \
        --with-apple-sectrust \
        --with-libssh2="$INSTALL_DIR" \
        --with-libidn2="$INSTALL_DIR" \
        --with-zlib \
        --without-libpsl \
        --without-brotli \
        --without-zstd \
        --without-nghttp2 \
        --without-nghttp3 \
        --without-ngtcp2 \
        --without-librtmp \
        --without-libgsasl \
        --disable-ldap \
        --disable-shared \
        --enable-static \
        --disable-docs \
        --disable-manual
fi
make -j "$JOBS"
make install
[ "$FFS_TARGET" = linux ] || verify_target_arch "$INSTALL_DIR/lib/libcurl.a"
mark_installed "$NAME" "${DEPS[@]}"
