#!/bin/bash
# Build FreeFileSync and RealTimeSync with the upstream makefiles against the libraries
# installed by build.sh. Binaries go to FreeFileSync/Build/Bin.
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

command -v wx-config >/dev/null && [ "$(command -v wx-config)" = "$INSTALL_DIR/bin/wx-config" ] ||
    die "wx-config not found in $INSTALL_DIR/bin, run build.sh first"

# glib >= 2.78 makes g_free() a function-like macro when sized free is available, which breaks the
# "::g_free(ptr)" calls in afs/ftp.cpp, base/icon_loader.cpp and zen/zstring.cpp. Capping the glib API
# level at 2.76 keeps it a plain function.
export CXXFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76${CXXFLAGS:+ $CXXFLAGS}"

# The makefiles append to CXXFLAGS/LDFLAGS from the environment, which carries the rpath to $INSTALL_DIR/lib.
make -C "$REPO_ROOT/FreeFileSync/Source" -j "$JOBS"
make -C "$REPO_ROOT/FreeFileSync/Source/RealTimeSync" -j "$JOBS"
