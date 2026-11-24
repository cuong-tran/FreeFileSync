#!/usr/bin/env bash
# Build FreeFileSync and RealTimeSync with the upstream makefiles against the libraries
# installed by build.sh. Binaries go to FreeFileSync/Build/Bin (debug FreeFileSync: Build/Debug/Bin),
# objects to $FFS_BUILD_DIR/obj.
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

command -v wx-config >/dev/null && [ "$(command -v wx-config)" = "$INSTALL_DIR/bin/wx-config" ] ||
    die "wx-config not found in $INSTALL_DIR/bin, run build.sh first"
# pkg-config falls back to the system's .pc files for anything missing from the prefix, which would
# link an unpatched system library without a word. FFS_SYSTEM_LIBS lists the ones that may come from the
# system on purpose (e.g. "openssl libcurl" to build against the distribution's packages). Those must not
# be in the prefix at all: its pkgconfig folder, -I and -L come first (common.sh), so a copy left there by
# an earlier build would still be used.
declare -A HEADERS=([openssl]=openssl [libssh2]=libssh2.h [libcurl]=curl)
for pc in openssl libssh2 libcurl; do
    if [[ " ${FFS_SYSTEM_LIBS:-} " == *" $pc "* ]]; then
        if [ -e "$INSTALL_DIR/lib/pkgconfig/$pc.pc" ] || [ -e "$INSTALL_DIR/include/${HEADERS[$pc]}" ] ||
           [ "$(pkg-config --variable=prefix "$pc" 2>/dev/null)" = "$INSTALL_DIR" ]; then
            die "FFS_SYSTEM_LIBS names $pc, but it is installed in $INSTALL_DIR and would be used instead of the system's: use another FFS_DEPS_PREFIX or remove it"
        fi
        pkg-config --exists "$pc" || die "FFS_SYSTEM_LIBS names $pc, but pkg-config finds no system $pc"
        continue
    fi
    [ "$(pkg-config --variable=prefix "$pc" 2>/dev/null)" = "$INSTALL_DIR" ] ||
        die "$pc is not installed in $INSTALL_DIR (pkg-config would use the system's), run build.sh first"
done

# glib >= 2.78 makes g_free() a function-like macro when sized free is available, which breaks the
# "::g_free(ptr)" calls in afs/ftp.cpp, base/icon_loader.cpp and zen/zstring.cpp. Capping the glib API
# level at 2.76 keeps it a plain function. Older glib doesn't define GLIB_VERSION_2_76 and needs no cap.
if pkg-config --atleast-version=2.76 glib-2.0; then
    export CXXFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76${CXXFLAGS:+ $CXXFLAGS}"
fi

# The makefiles keep their objects in FreeFileSync/Build/Obj, shared with the Docker and manual builds,
# and make tracks neither the compiler nor the libraries an object was built against. So use a separate
# object folder for builds against $INSTALL_DIR, and relink the binaries every time (a binary from
# another build can be newer than all of these objects). BUILD=debug in the environment is passed on.
ARCH="$(arch)"
BUILD_MODE="${BUILD:-release}"
OBJ_DIR="$BUILD_DIR/obj/$BUILD_MODE"
BIN_DIR="$REPO_ROOT/FreeFileSync/Build/Bin"
FFS_BIN_DIR="$BIN_DIR"
[ "$BUILD_MODE" = debug ] && FFS_BIN_DIR="$REPO_ROOT/FreeFileSync/Build/Debug/Bin"
# Toolchains other than g++ + libstdc++ get a suffix on the binary and object folder names, like in the makefiles
# (e.g. FreeFileSync_x86_64-clang-libc++ for CXX=clang++-20 STDLIB=libc++); empty for the default g++ build.
TOOLCHAIN_SUFFIX="$(toolchain_suffix)"
rm -f "$FFS_BIN_DIR/FreeFileSync_$ARCH$TOOLCHAIN_SUFFIX" "$BIN_DIR/RealTimeSync_$ARCH$TOOLCHAIN_SUFFIX"

# The makefiles append to CXXFLAGS/LDFLAGS from the environment, which carries the rpath to $INSTALL_DIR/lib.
make -C "$REPO_ROOT/FreeFileSync/Source" -j "$JOBS" objPath="$OBJ_DIR/FreeFileSync_$ARCH$TOOLCHAIN_SUFFIX"
make -C "$REPO_ROOT/FreeFileSync/Source/RealTimeSync" -j "$JOBS" objPath="$OBJ_DIR/RealTimeSync_$ARCH$TOOLCHAIN_SUFFIX"
