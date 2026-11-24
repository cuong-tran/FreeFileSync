#!/bin/bash
# Package the FreeFileSync and RealTimeSync built by build-free-file-sync.sh into $FFS_BUILD_DIR/dist as a portable
# tar.gz and a .deb. The shared libraries they load from $FFS_DEPS_PREFIX (OpenSSL, libssh2, libcurl) are
# copied next to the binaries, and the binaries and those libraries get an $ORIGIN rpath, so the packages
# run without the build machine's prefix. wxWidgets is linked statically and needs nothing.
#
# Environment:
#   DEB_VERSION_SUFFIX  appended to the FreeFileSync version for the .deb version (CI: "+ci<run number>")
#   SOURCE_SHA          commit the build is from, named in the .deb description (default: git HEAD)
#   GITHUB_REPOSITORY   owner/name for the .deb maintainer and homepage (set by GitHub Actions; default: the
#                       GitHub repository of the "origin" remote)
#   FFS_SENTRY          1 for a build with crash reporting (build-free-file-sync.sh with FFS_SENTRY=1): ships the
#                       crashpad_handler from the Bin folder next to the binaries, and moves the binaries' debug info into
#                       $FFS_BUILD_DIR/debug-files (<exe>.debug plus a copy of the shipped <exe>, same build ID) for
#                       "sentry-cli debug-files upload"; the shipped binaries keep their symbol table, as without Sentry
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

ARCH="$(arch)"
# binaries of a toolchain other than g++ + libstdc++ carry its suffix (build-free-file-sync.sh, the makefiles), and so
# do the package file names; the default g++ build's names are unchanged
TOOLCHAIN_SUFFIX="$(toolchain_suffix)"
FFS_EXE="FreeFileSync_$ARCH$TOOLCHAIN_SUFFIX"
RTS_EXE="RealTimeSync_$ARCH$TOOLCHAIN_SUFFIX"
BIN_DIR="$REPO_ROOT/FreeFileSync/Build/Bin"
RES_DIR="$REPO_ROOT/FreeFileSync/Build/Resources"
# under the build work area: ignored by git, owned by these scripts, and a real path (BUILD_DIR is resolved)
DIST_DIR="$BUILD_DIR/dist"
for bin in "$BIN_DIR/$FFS_EXE" "$BIN_DIR/$RTS_EXE"; do
    [ -x "$bin" ] || die "$bin not found, run build-free-file-sync.sh first"
done

ffsVersion="$(sed -n 's/^const char ffsVersion\[\] = "\([^"]*\)".*/\1/p' "$REPO_ROOT/FreeFileSync/Source/version/version.h")"
[ -n "$ffsVersion" ] || die "ffsVersion not found in version.h"
debArch="$(dpkg --print-architecture)"
debVersion="$ffsVersion${DEB_VERSION_SUFFIX:-}"
sourceSha="${SOURCE_SHA:-$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}"
ghRepo="${GITHUB_REPOSITORY:-$({ git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || true; } |
    sed -nE 's#^(https://|git@|ssh://git@)github\.com[:/]([^/]+/[^/]+)$#\2#p' | sed -E 's#(\.git)?/?$##')}"
[ -n "$ghRepo" ] || die "set GITHUB_REPOSITORY=<owner>/<name> for the .deb homepage (no GitHub \"origin\" remote)"

rm -rf "$DIST_DIR"
stage="$DIST_DIR/FreeFileSync"
mkdir -p "$stage/Bin"
# same layout as upstream's Linux package: FreeFileSync looks for Resources next to the Bin folder of the
# executable, and RealTimeSync starts batch jobs with "<install dir>/FreeFileSync" (getFreeFileSyncLauncherPath())
cp -a "$BIN_DIR/$FFS_EXE" "$BIN_DIR/$RTS_EXE" "$stage/Bin/"
# crash reporting: the apps look for crashpad_handler next to themselves (FreeFileSync/Source/crash_report_init.h); it loads
# libcurl with dlopen("libcurl.so.4"), found through the $ORIGIN rpath set below
if [ "${FFS_SENTRY:-}" = 1 ]; then
    [ -x "$BIN_DIR/crashpad_handler" ] || die "FFS_SENTRY=1, but $BIN_DIR/crashpad_handler is missing"
    cp -a "$BIN_DIR/crashpad_handler" "$stage/Bin/"
fi
cp -a "$RES_DIR" "$stage/"

# bundle every library the binaries resolve from the prefix (ldd lists indirect dependencies too), under its soname
log "Bundling the libraries from $INSTALL_DIR/lib"
while read -r soname path; do
    cp -L "$path" "$stage/Bin/$soname"
    echo "$soname <= $path"
done < <(for bin in "$stage/Bin/$FFS_EXE" "$stage/Bin/$RTS_EXE"; do ldd "$bin"; done |
             awk -v prefix="$INSTALL_DIR/lib/" '$2 == "=>" && index($3, prefix) == 1 {print $1, $3}' | sort -u)

# $ORIGIN for the libraries as well: a RUNPATH only applies to an object's own dependencies, so libcurl
# needs one of its own to find the bundled libssh2 and OpenSSL
for obj in "$stage"/Bin/*; do
    [ -L "$obj" ] && continue
    if readelf -d "$obj" | grep -E '\((RPATH|RUNPATH)\)' >/dev/null || [[ "$obj" == *.so* ]]; then
        # shellcheck disable=SC2016 # a literal $ORIGIN for the dynamic loader
        patchelf --set-rpath '$ORIGIN' "$obj"
    fi
done

# every binary must start without the prefix: nothing unresolved and nothing loaded from $INSTALL_DIR.
# No "grep -q" in these pipelines: with pipefail, grep exiting at the first match can kill ldd with SIGPIPE.
for obj in "$stage"/Bin/*; do
    deps="$(LD_LIBRARY_PATH='' ldd "$obj")"
    if grep 'not found' <<<"$deps"; then die "$obj has unresolved shared libraries"; fi
    if grep -F "$INSTALL_DIR/" <<<"$deps"; then die "$obj still loads libraries from $INSTALL_DIR"; fi
done
LD_LIBRARY_PATH='' ldd "$stage/Bin/$FFS_EXE" | grep -F " => $stage/Bin/" ||
    die "$FFS_EXE does not load any bundled library"

# crash reporting: debug info out of the shipped binaries into separate files with the same build ID, after patchelf (the
# files must describe the binaries as shipped)
if [ "${FFS_SENTRY:-}" = 1 ]; then
    DEBUG_DIR="$BUILD_DIR/debug-files"
    rm -rf "$DEBUG_DIR"
    mkdir -p "$DEBUG_DIR"
    buildId() { readelf -n "$1" | sed -n 's/^ *Build ID: *//p'; }
    for exe in "$FFS_EXE" "$RTS_EXE"; do
        bin="$stage/Bin/$exe"
        readelf -S "$bin" | grep -F .debug_info >/dev/null || die "$exe has no debug info (FFS_SENTRY=1 builds with -g)"
        id="$(buildId "$bin")"
        [ -n "$id" ] || die "$exe has no build ID"
        objcopy --only-keep-debug "$bin" "$DEBUG_DIR/$exe.debug"
        objcopy --strip-debug --add-gnu-debuglink="$DEBUG_DIR/$exe.debug" "$bin"
        cp -a "$bin" "$DEBUG_DIR/$exe" # the executable as shipped: its unwind tables help the symbolication too
        [ "$(buildId "$DEBUG_DIR/$exe.debug")" = "$id" ] && [ "$(buildId "$bin")" = "$id" ] ||
            die "build IDs of $exe and its debug file differ"
        if readelf -S "$bin" | grep -F .debug_info; then die "$exe still has debug info"; fi
        log "$exe: build ID $id, debug file $(du -h "$DEBUG_DIR/$exe.debug" | cut -f1), shipped $(du -h "$bin" | cut -f1)"
    done
fi

ln -s "Bin/$FFS_EXE" "$stage/FreeFileSync"
ln -s "Bin/$RTS_EXE" "$stage/RealTimeSync"

log "Creating the portable archive"
tar -C "$DIST_DIR" -czf "$DIST_DIR/FreeFileSync_${ffsVersion}_Linux_$ARCH$TOOLCHAIN_SUFFIX.tar.gz" FreeFileSync

log "Creating the .deb"
pkg="$WORK_DIR/deb"
rm -rf "$pkg"
mkdir -p "$pkg/DEBIAN" "$pkg/opt" "$pkg/usr/bin" "$pkg/usr/share/applications" "$pkg/usr/share/pixmaps"
cp -a "$stage" "$pkg/opt/FreeFileSync"
ln -s /opt/FreeFileSync/FreeFileSync "$pkg/usr/bin/freefilesync"
ln -s /opt/FreeFileSync/RealTimeSync "$pkg/usr/bin/realtimesync"
unzip -p "$RES_DIR/Icons.zip" FreeFileSync.png >"$pkg/usr/share/pixmaps/freefilesync.png"
unzip -p "$RES_DIR/Icons.zip" RealTimeSync.png >"$pkg/usr/share/pixmaps/realtimesync.png"

printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=FreeFileSync' \
    'Comment=Folder Comparison and Synchronization' 'Exec=/usr/bin/freefilesync %F' \
    'Icon=freefilesync' 'Terminal=false' 'Categories=Utility;FileTools;' \
    >"$pkg/usr/share/applications/freefilesync.desktop"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=RealTimeSync' \
    'Comment=Automated Synchronization' 'Exec=/usr/bin/realtimesync %F' \
    'Icon=realtimesync' 'Terminal=false' 'Categories=Utility;FileTools;' \
    >"$pkg/usr/share/applications/realtimesync.desktop"

# let dpkg-shlibdeps work out the system library dependencies; the bundled libraries have no package info,
# so their own dependencies come from scanning them
shlibsDir="$WORK_DIR/shlibs"
rm -rf "$shlibsDir"
mkdir -p "$shlibsDir/debian"
echo 'Source: freefilesync' >"$shlibsDir/debian/control"
depends="$(cd "$shlibsDir" && dpkg-shlibdeps -O --ignore-missing-info "$pkg"/opt/FreeFileSync/Bin/* | sed -n 's/^shlibs:Depends=//p')"
[ -n "$depends" ] || die "dpkg-shlibdeps found no dependencies"

cat >"$pkg/DEBIAN/control" <<EOF
Package: freefilesync
Version: $debVersion
Section: utils
Priority: optional
Architecture: $debArch
Depends: $depends
Maintainer: ${ghRepo%%/*} <noreply@github.com>
Homepage: https://github.com/$ghRepo
Description: Folder comparison and synchronization (unofficial build)
 FreeFileSync and RealTimeSync built from https://github.com/$ghRepo
 at commit $sourceSha, with the Bugs.txt patches applied to the bundled libraries.
EOF
cat "$pkg/DEBIAN/control"

dpkg-deb --build --root-owner-group "$pkg" "$DIST_DIR/freefilesync_${debVersion}_$debArch$TOOLCHAIN_SUFFIX.deb"
rm -rf "$stage"
ls -l "$DIST_DIR"
