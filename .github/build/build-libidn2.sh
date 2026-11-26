#!/usr/bin/env bash
# Build libidn2 for macOS and install it into $FFS_DEPS_PREFIX (see common.sh). Linux uses the distribution's libidn2.
# FreeFileSync calls libidn2 itself (zen/socket.h) and libcurl uses it for international domain names, so the macOS build
# keeps the same IDN implementation as Linux instead of curl's --with-apple-idn, which would cover curl only.
# A static library for one architecture, with the part of libunistring it needs built in and without gettext, so it has
# no dependency outside the SDK (Homebrew's libunistring and gettext exist for the host architecture only).
# Usage: build-libidn2.sh [version]   (or set LIBIDN2_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

[ "$FFS_TARGET" != linux ] || die "build-libidn2.sh is for macOS targets; Linux builds use the distribution's libidn2"

LIBIDN2_VERSION="${1:-${LIBIDN2_VERSION:-2.3.8}}"
NAME="libidn2-$LIBIDN2_VERSION"

if is_installed "$NAME"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

# not in Bugs.txt, so built unpatched
# ftpmirror.gnu.org redirects to a mirror nearby, which may be broken (one had an expired certificate): then GNU's own server
fetch_source "$NAME" "$NAME.tar.gz" "https://ftpmirror.gnu.org/libidn/$NAME.tar.gz https://ftp.gnu.org/gnu/libidn/$NAME.tar.gz" nopatch

cd "$(fresh_build_dir libidn2)"
"$SOURCE_DIR/$NAME/configure" \
    --host="$AUTOCONF_HOST" \
    --prefix="$INSTALL_DIR" \
    --disable-shared \
    --enable-static \
    --with-included-libunistring \
    --disable-nls \
    --disable-doc \
    --disable-valgrind-tests
make -j "$JOBS"
make install
verify_target_arch "$INSTALL_DIR/lib/libidn2.a"
mark_installed "$NAME"
