#!/bin/bash
# Build OpenSSL and install it into $FFS_DEPS_PREFIX (see common.sh).
# Usage: build-openssl.sh [version]   (or set OPENSSL_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

OPENSSL_VERSION="${1:-${OPENSSL_VERSION:-3.5.9}}"
NAME="openssl-$OPENSSL_VERSION"

if is_installed "$NAME"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

# OpenSSL isn't listed in Bugs.txt, so it is built unpatched.
fetch_source "$NAME" "$NAME.tar.gz" "https://github.com/openssl/openssl/releases/download/$NAME/$NAME.tar.gz" nopatch

cd "$(fresh_build_dir openssl)"
# --libdir=lib: keep everything in $INSTALL_DIR/lib instead of lib64 on x86_64
"$SOURCE_DIR/$NAME/Configure" --prefix="$INSTALL_DIR" --openssldir="$INSTALL_DIR/ssl" --libdir=lib shared no-tests no-docs
make -j "$JOBS"
make install_sw
mark_installed "$NAME"
