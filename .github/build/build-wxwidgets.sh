#!/bin/bash
# Build a static GTK 3 wxWidgets with wxUSE_EXCEPTIONS=0 (FreeFileSync's application.cpp
# refuses wx's uncaught exception handling), with the Bugs.txt fixes applied. The dark mode fix adds the
# wxColorHook API that wx+/darkmode.cpp needs, so FreeFileSync doesn't compile without it.
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
make -j "$JOBS"
make install
mark_installed "$NAME"
