#!/usr/bin/env bash
# Build sentry-native (crash reporting for "make FFS_SENTRY=1") as static libraries with the Crashpad backend and the curl
# transport, for the current FFS_TARGET, into $FFS_SENTRY_PREFIX (see common.sh), plus the crashpad_handler executable that
# ships next to the apps. The C++ parts (Crashpad) are built with the same compiler and C++ library as the apps: CXX, and
# libc++ with STDLIB=libc++.
#
# The prefix also gets lib/pkgconfig/sentry-native.pc, which the makefiles use with FFS_SENTRY=1.
# Linux: against the libcurl pkg-config finds (the one in $FFS_DEPS_PREFIX after build.sh; the distribution's otherwise),
# which crashpad_handler also uses for its uploads. macOS: static, against the libcurl.a in $FFS_DEPS_PREFIX (only the
# sentry library uses it; crashpad_handler uploads with the system's NSURLSession).
# Usage: build-sentry-native.sh [version]   (or set SENTRY_NATIVE_VERSION)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

SENTRY_NATIVE_VERSION="${1:-${SENTRY_NATIVE_VERSION:-0.17.6}}"
NAME="sentry-$SENTRY_NATIVE_VERSION" # source folder and stamp; check_name allows one dash only
check_name "$NAME"
ARCHIVE="sentry-native-$SENTRY_NATIVE_VERSION.zip"

# own prefix and stamps (cached separately from the other libraries), locked like the deps prefix in common.sh;
# the dependency stamps (curl) are read from the deps prefix
mkdir -p "$SENTRY_PREFIX"
exec 7>"$SENTRY_PREFIX/.lock"
flock -n 7 || { log "Waiting for another build using $SENTRY_PREFIX"; flock 7; }
DEP_STAMP_DIR="$STAMP_DIR"
STAMP_DIR="$SENTRY_PREFIX/.stamps"

case "${STDLIB:-}" in
    "")     ;;
    libc++) export CXXFLAGS="-stdlib=libc++${CXXFLAGS:+ $CXXFLAGS}" LDFLAGS="-stdlib=libc++ $LDFLAGS" ;;
    *)      die "STDLIB must be empty or \"libc++\", not \"$STDLIB\"" ;;
esac

if [ "$FFS_TARGET" = linux ]; then
    pkg-config --exists libcurl || die "libcurl not found by pkg-config: run build.sh first (or install the distribution's libcurl dev package)"
    CURL_INCLUDE="$(pkg-config --variable=includedir libcurl)"
    CURL_LIB="$(pkg-config --variable=libdir libcurl)/libcurl.so"
    PC_REQUIRES="Requires: libcurl" # the apps link the same libcurl anyway
else
    # the static libcurl from build-curl.sh; not through pkg-config: its libcurl.pc requires a zlib.pc, but zlib is the SDK's
    CURL_INCLUDE="$INSTALL_DIR/include"
    CURL_LIB="$INSTALL_DIR/lib/libcurl.a"
    PC_REQUIRES="" # the app link (M3) adds libcurl and its dependencies itself
fi
[ -f "$CURL_LIB" ] || die "$CURL_LIB not found: run build.sh first"

# rebuilt when the libcurl changes: the stamp of the curl from build.sh is part of the install id, and .curl records which
# libcurl was used (the one in the deps prefix or the distribution's)
if is_installed "$NAME" curl && [ "$(cat "$SENTRY_PREFIX/.curl" 2>/dev/null)" = "$CURL_LIB" ]; then
    log "$NAME already installed in $SENTRY_PREFIX"
    exit 0
fi
rm -f "$STAMP_DIR/sentry"-*

# the release asset bundles the submodules (crashpad, mini_chromium, ...) that GitHub's source archives lack; it has no
# top-level folder, so it is unpacked into one
download_archive "$ARCHIVE" "https://github.com/getsentry/sentry-native/releases/download/$SENTRY_NATIVE_VERSION/sentry-native.zip"
rm -rf "${SOURCE_DIR:?}/$NAME"
mkdir -p "$SOURCE_DIR/$NAME"
unzip -q "$ARCHIVE_DIR/$ARCHIVE" -d "$SOURCE_DIR/$NAME"
[ -f "$SOURCE_DIR/$NAME/CMakeLists.txt" ] || die "$ARCHIVE did not unpack to $SOURCE_DIR/$NAME"

BUILD="$(fresh_build_dir sentry-native)"
TARGET_OPTIONS=()
if [ "$FFS_TARGET" != linux ]; then
    # one architecture and the deployment target from common.sh (crashpad's mig step reads OSX_ARCHITECTURES as well)
    TARGET_OPTIONS=(-DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET")
fi
rm -rf "${SENTRY_PREFIX:?}/include" "${SENTRY_PREFIX:?}/lib" "${SENTRY_PREFIX:?}/bin"
cmake -S "$SOURCE_DIR/$NAME" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-cc}" \
    -DCMAKE_CXX_COMPILER="${CXX:-c++}" \
    -DCMAKE_INSTALL_PREFIX="$SENTRY_PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DSENTRY_BACKEND=crashpad \
    -DSENTRY_TRANSPORT=curl \
    -DSENTRY_BUILD_SHARED_LIBS=OFF \
    -DSENTRY_BUILD_TESTS=OFF \
    -DSENTRY_BUILD_EXAMPLES=OFF \
    -DSENTRY_ENABLE_INSTALL=ON \
    -DCRASHPAD_ZLIB_SYSTEM=ON \
    -DCURL_INCLUDE_DIR="$CURL_INCLUDE" \
    -DCURL_LIBRARY="$CURL_LIB" \
    "${TARGET_OPTIONS[@]}"
cmake --build "$BUILD" -j "$JOBS"
cmake --install "$BUILD"

HANDLER="$SENTRY_PREFIX/bin/crashpad_handler"
[ -x "$HANDLER" ] || die "crashpad_handler was not installed into $SENTRY_PREFIX/bin"

# link order of the static libraries (from the exported CMake targets in lib/cmake/sentry): sentry, its unwinder (Linux),
# then crashpad's client and what that needs. The required ones must exist; unwind and compat only on some platforms.
LIBS=()
REQUIRED=()
for lib in sentry unwind crashpad_client crashpad_util crashpad_mpack crashpad_compat mini_chromium; do
    if [ -f "$SENTRY_PREFIX/lib/lib$lib.a" ]; then
        LIBS+=("-l$lib")
        REQUIRED+=("$SENTRY_PREFIX/lib/lib$lib.a")
    elif [[ "$lib" != unwind && "$lib" != crashpad_compat ]]; then
        die "lib$lib.a was not installed into $SENTRY_PREFIX/lib"
    fi
done
verify_target_arch "$HANDLER" "${REQUIRED[@]}"

if [ "$FFS_TARGET" = linux ]; then
    SYS_LIBS="-lz -ldl -lrt -lpthread"
else
    # for the app link in M3 (not linked against yet)
    SYS_LIBS="-lz -framework Foundation -framework Security -framework IOKit -framework CoreFoundation -lbsm"
fi
mkdir -p "$SENTRY_PREFIX/lib/pkgconfig"
cat >"$SENTRY_PREFIX/lib/pkgconfig/sentry-native.pc" <<EOF
prefix=$SENTRY_PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
handler=\${prefix}/bin/crashpad_handler

Name: sentry-native
Description: sentry-native $SENTRY_NATIVE_VERSION, static, Crashpad backend, curl transport (built by .github/build/build-sentry-native.sh)
Version: $SENTRY_NATIVE_VERSION
$PC_REQUIRES
Cflags: -I\${includedir} -DSENTRY_BUILD_STATIC
Libs: -L\${libdir} ${LIBS[*]} $SYS_LIBS
EOF
cat "$SENTRY_PREFIX/lib/pkgconfig/sentry-native.pc"

echo "$CURL_LIB" >"$SENTRY_PREFIX/.curl"
mark_installed "$NAME" curl
