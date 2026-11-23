#!/usr/bin/env bash
# Build a static GTK 3 wxWidgets with wxUSE_EXCEPTIONS=0 (FreeFileSync's application.cpp
# refuses wx's uncaught exception handling), with the Bugs.txt fixes applied. The dark mode fix adds the
# wxColorHook API that wx+/darkmode.cpp uses for full dark mode support; without it the makefiles warn and
# build a fallback without the higher-contrast gray text.
# Usage: build-wxwidgets.sh [version]   (or set WXWIDGETS_VERSION)
# Not --enable-no_exceptions: that also puts -fno-exceptions into wx-config --cxxflags,
# and FreeFileSync itself uses C++ exceptions.
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

WXWIDGETS_VERSION="${1:-${WXWIDGETS_VERSION:-3.3.4}}"
NAME="wxWidgets-$WXWIDGETS_VERSION"

if is_installed "$NAME"; then
    log "$NAME already installed in $INSTALL_DIR"
    exit 0
fi

fetch_source "$NAME" "$NAME.tar.bz2" "https://github.com/wxWidgets/wxWidgets/releases/download/v$WXWIDGETS_VERSION/$NAME.tar.bz2"

cd "$(fresh_build_dir wxwidgets)"
if [ "$FFS_TARGET" = linux ]; then
    "$SOURCE_DIR/$NAME/configure" \
        --prefix="$INSTALL_DIR" \
        --with-gtk=3 \
        --disable-shared \
        --disable-exceptions \
        --disable-stc \
        --disable-webview \
        --disable-mediactrl \
        --disable-webrequest \
        --disable-tests
else
    # macOS: wxOSX/Cocoa with the same feature set, for one architecture and the deployment target from common.sh.
    # --disable-sys-libs: wx's bundled image/regex/expat libraries instead of whatever Homebrew has for the host
    # architecture; zlib is the SDK's. Of the Bugs.txt fixes, wx/settings.h (the color hook), src/aui and src/common
    # apply to wxOSX; the src/gtk and src/unix hunks are patched in too but not compiled.
    "$SOURCE_DIR/$NAME/configure" \
        --host="$AUTOCONF_HOST" \
        --prefix="$INSTALL_DIR" \
        --with-osx_cocoa \
        --with-macosx-version-min="$MACOSX_DEPLOYMENT_TARGET" \
        --disable-sys-libs \
        --with-zlib=sys \
        --disable-shared \
        --disable-exceptions \
        --disable-stc \
        --disable-webview \
        --disable-mediactrl \
        --disable-webrequest \
        --disable-tests
fi
make -j "$JOBS"
make install
[ "$FFS_TARGET" = linux ] || verify_target_arch "$(find "$INSTALL_DIR/lib" -maxdepth 1 -name 'libwx_*core*.a' | head -n 1)"
mark_installed "$NAME"
