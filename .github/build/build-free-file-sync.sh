#!/bin/bash
# Build FreeFileSync and RealTimeSync with the upstream makefiles against the libraries
# installed by build.sh. Binaries go to FreeFileSync/Build/Bin.
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

command -v wx-config >/dev/null && [ "$(command -v wx-config)" = "$INSTALL_DIR/bin/wx-config" ] ||
    die "wx-config not found in $INSTALL_DIR/bin, run build.sh first"

# The makefiles append to CXXFLAGS/LDFLAGS from the environment, which carries the rpath to $INSTALL_DIR/lib.
make -C "$REPO_ROOT/FreeFileSync/Source" -j "$JOBS"
make -C "$REPO_ROOT/FreeFileSync/Source/RealTimeSync" -j "$JOBS"
