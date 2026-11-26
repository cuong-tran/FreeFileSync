#!/usr/bin/env bash
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
if [ "$FFS_TARGET" = linux ]; then
    "$SOURCE_DIR/$NAME/Configure" --prefix="$INSTALL_DIR" --openssldir="$INSTALL_DIR/ssl" --libdir=lib shared no-tests no-docs
    make -j "$JOBS"
else
    # macOS: static libraries for one architecture (the target name sets it, CFLAGS from common.sh repeat it). No path of the
    # build machine may end up in libcrypto, and so in the apps: OpenSSL keeps its compiler flags as text in the library
    # (OpenSSL_version(OPENSSL_CFLAGS)), so it gets neither common.sh's path map nor the prefix's -I (it needs no other library);
    # its sources are compiled by relative path anyway. Its built-in directories (OPENSSLDIR for openssl.cnf, ENGINESDIR,
    # MODULESDIR) are OpenSSL's defaults under /usr/local instead of the prefix: the apps load no configuration, engines or
    # modules from where the libraries were built. install_sw installs no OPENSSLDIR content, so nothing is written there.
    CFLAGS="${CFLAGS/" $PATH_MAP_FLAG"/}" CPPFLAGS="" \
        "$SOURCE_DIR/$NAME/Configure" "$OPENSSL_CONFIG" --prefix="$INSTALL_DIR" --openssldir=/usr/local/ssl --libdir=lib no-shared no-tests no-docs
    make -j "$JOBS" ENGINESDIR=/usr/local/lib/engines-3 MODULESDIR=/usr/local/lib/ossl-modules
fi
make install_sw
[ "$FFS_TARGET" = linux ] || verify_target_arch "$INSTALL_DIR/lib/libssl.a" "$INSTALL_DIR/lib/libcrypto.a"
mark_installed "$NAME"
