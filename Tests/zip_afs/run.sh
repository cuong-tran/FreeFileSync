#!/usr/bin/env bash
# Build and run the ZIP AbstractFileSystem test (needs a C++26 compiler, zlib, python3, zip; Linux: glib/gio dev packages,
# macOS: Xcode or its command line tools)
#   CXX:    compiler (default g++ on Linux, Xcode's clang++ on macOS; g++ 14 or newer, or clang 20 or newer)
#   STDLIB: "libc++" to build against LLVM's libc++ instead of the compiler's default C++ library (like the makefiles' STDLIB)
#   ARCH:   macOS only: arm64 or x86_64 (default: the host's); the other one is cross-compiled and runs under Rosetta 2
#   CXXFLAGS, LDFLAGS from the environment are appended to the compile/link commands
set -euo pipefail
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    echo "bash 4.4 or newer needed (this is $BASH_VERSION); on macOS: brew install bash" >&2 # empty arrays under set -u, negative indexes
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

#zen sources the ZIP AFS needs, plus the platform's own files and libraries
ZEN_SOURCES=("$ROOT"/zen/{zstring,file_path,resolve_path,file_access,file_io,file_traverser,sys_error,sys_info,sys_version,thread,format_unit,legacy_compiler,process_exec}.cpp)
PLATFORM_FLAGS=()
PLATFORM_LIBS=()
OBJC_SOURCES=()
case "$(uname -s)" in
    Linux)
        CXX="${CXX:-g++}"
        #glib >= 2.78 can make g_free() a function-like macro, which breaks "::g_free(ptr)" in zen/zstring.cpp:
        #cap the glib API at 2.76 like .github/build/build-free-file-sync.sh (older glib doesn't define GLIB_VERSION_2_76 and needs no cap)
        if pkg-config --atleast-version=2.76 glib-2.0; then
            PLATFORM_FLAGS+=(-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76)
        fi
        #zen/sys_error.cpp needs glib + gio (GError)
        read -r -a GLIB_CFLAGS <<< "$(pkg-config --cflags glib-2.0 gio-2.0 zlib)"
        read -r -a GLIB_LIBS   <<< "$(pkg-config --libs glib-2.0 gio-2.0 zlib)"
        PLATFORM_FLAGS+=("${GLIB_CFLAGS[@]}")
        PLATFORM_LIBS+=("${GLIB_LIBS[@]}")
        ;;
    Darwin)
        CXX="${CXX:-clang++}" #Xcode's clang
        ARCH="${ARCH:-$(uname -m)}"
        DEPLOYMENT_TARGET="$(sed -nE 's/^FFS_MACOS_DEPLOYMENT_TARGET=([0-9.]+).*/\1/p' "$ROOT/.github/build/common.sh")"
        [ -n "$DEPLOYMENT_TARGET" ] || { echo "no FFS_MACOS_DEPLOYMENT_TARGET in .github/build/common.sh" >&2; exit 2; }
        PLATFORM_FLAGS+=(-arch "$ARCH" -mmacosx-version-min="$DEPLOYMENT_TARGET")
        PLATFORM_LIBS+=(-arch "$ARCH" -mmacosx-version-min="$DEPLOYMENT_TARGET" -lz -framework CoreFoundation -framework Foundation)
        OBJC_SOURCES+=("$ROOT/zen/sys_error_mac.mm") #NSError/OSStatus texts (zen/sys_error.h)
        ;;
    *)
        echo "Tests/zip_afs/run.sh: no build setup for $(uname -s) yet" >&2
        exit 2
        ;;
esac
#compiler family (detected positively from the predefined macros, like the makefiles) and STDLIB: same flags as the makefiles
COMPILER_FLAGS=()
COMPILER_MACROS="$("$CXX" -dM -E -x c++ /dev/null 2>/dev/null || true)"
if grep -q __clang__ <<< "$COMPILER_MACROS"; then
    COMPILER_FLAGS=(-Wtautological-overlap-compare -Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-lambda-capture)
elif grep -q __GNUC__ <<< "$COMPILER_MACROS"; then
    COMPILER_FLAGS=(-Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op)
fi
case "${STDLIB:-}" in
    "")     ;;
    libc++) COMPILER_FLAGS+=(-stdlib=libc++) ;;
    *)      echo "STDLIB must be empty or \"libc++\", not \"$STDLIB\"" >&2; exit 2 ;;
esac
read -r -a CXXFLAGS_USER <<< "${CXXFLAGS:-}"
read -r -a LDFLAGS_USER  <<< "${LDFLAGS:-}"

echo "== build ($("$CXX" --version | sed -n 1p)${STDLIB:+, $STDLIB}${ARCH:+, $ARCH})"
#same language standard and warnings as FreeFileSync/Source/Makefile
CXXFLAGS_TEST=(-std=c++26 -O1 -g -pthread -DNDEBUG -DWXINTL_NO_GETTEXT_MACRO -I"$ROOT" -I"$ROOT/zenXml" -include "zen/i18n.h"
    -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion "${COMPILER_FLAGS[@]}" "${PLATFORM_FLAGS[@]}" "${CXXFLAGS_USER[@]}")
OBJC_OBJECTS=()
for src in "${OBJC_SOURCES[@]}"; do #Objective-C++ with automatic reference counting, compiled on its own (-fobjc-arc is for .mm files only)
    OBJC_OBJECTS+=("$WORK/$(basename "$src").o")
    "$CXX" "${CXXFLAGS_TEST[@]}" -fobjc-arc -c "$src" -o "${OBJC_OBJECTS[-1]}"
done
"$CXX" "${CXXFLAGS_TEST[@]}" \
    "$ROOT/Tests/zip_afs/zip_afs_test.cpp" \
    "$ROOT/FreeFileSync/Source/afs/zip.cpp" \
    "$ROOT/FreeFileSync/Source/afs/abstract.cpp" \
    "${ZEN_SOURCES[@]}" "${OBJC_OBJECTS[@]}" \
    "${PLATFORM_LIBS[@]}" "${LDFLAGS_USER[@]}" -o "$WORK/zip_afs_test"

if [ "${ZIP_TEST_COMPILE_ONLY:-0}" = 1 ]; then
    echo "== built (ZIP_TEST_COMPILE_ONLY=1: not run)"
    exit 0
fi

echo "== fixtures"
REF="$WORK/ref"
mkdir -p "$REF/sub dir/nested" "$REF/empty folder" "$REF/Données été"
echo "hello" > "$REF/a.txt"
head -c 300000 /dev/urandom > "$REF/sub dir/random.bin"                            # incompressible
{ yes "compressible line" || true; } | head -n 50000 > "$REF/sub dir/nested/big.txt"  # multiple inflate blocks; "|| true": yes gets SIGPIPE, fatal with pipefail
: > "$REF/sub dir/empty.txt"
echo "accents" > "$REF/Données été/fichier é.txt"
touch -t 202402291337.42 "$REF/a.txt" # -t: the same on GNU and BSD touch

cd "$REF"
python3 -I - "$WORK" <<'EOF'
import os, sys, zipfile
work = sys.argv[1]
def add_all(zf, compress_type):
    for dirpath, dirnames, filenames in os.walk("."):
        for d in dirnames:
            zf.write(os.path.join(dirpath, d), compress_type=compress_type)
        for f in filenames:
            zf.write(os.path.join(dirpath, f), compress_type=compress_type)
with zipfile.ZipFile(f"{work}/py_deflate.zip", "w") as zf: add_all(zf, zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(f"{work}/py_stored.zip",  "w") as zf: add_all(zf, zipfile.ZIP_STORED)

# stored archive with corrupted payload => CRC must be detected
with zipfile.ZipFile(f"{work}/corrupt.zip", "w") as zf:
    zf.writestr("corrupt.txt", "some payload that will be modified")
data = bytearray(open(f"{work}/corrupt.zip", "rb").read())
pos = data.find(b"some payload")
data[pos] ^= 0xFF
open(f"{work}/corrupt.zip", "wb").write(data)

# more than 65535 entries without Zip64 records, entry count stored modulo 65536 (as some tools do)
# => all entries must still be listed
import struct
with zipfile.ZipFile(f"{work}/many.zip", "w") as zf:
    for i in range(65540):
        zf.writestr(f"f{i}", "")
data = open(f"{work}/many.zip", "rb").read()
eocdPos  = data.rfind(b"PK\x05\x06")
zip64Pos = data.rfind(b"PK\x06\x06", 0, eocdPos)
assert zip64Pos > 0 # zipfile writes Zip64 records for > 65535 entries: strip them
eocd = bytearray(data[eocdPos:eocdPos + 22])
struct.pack_into("<HH", eocd, 8, 65540 & 0xffff, 65540 & 0xffff)
open(f"{work}/many.zip", "wb").write(data[:zip64Pos] + bytes(eocd))

# unsafe and clashing entry names => only "docs" (as folder), "docs/readme.txt" and "ok.txt" may be listed
# zipfile cuts names at NUL and may normalize "..", so write placeholders of the same length and patch the raw bytes
with zipfile.ZipFile(f"{work}/unsafe.zip", "w") as zf:
    zf.writestr("XX/escape.txt", "outside the target folder") # => "../escape.txt": must be skipped
    zf.writestr("aXNULb", "name cut short by c_str()")        # => "a\0NULb": must be skipped
    zf.writestr("docs", "file with the name of a folder")     # clashes with the folder below: folder wins
    zf.writestr("docs/readme.txt", "inside the folder")
    zf.writestr("ok.txt", "normal entry")
data = open(f"{work}/unsafe.zip", "rb").read()
assert data.count(b"XX/escape.txt") == 2 and data.count(b"aXNULb") == 2 # local header + central directory
data = data.replace(b"XX/escape.txt", b"../escape.txt").replace(b"aXNULb", b"a\0NULb")
open(f"{work}/unsafe.zip", "wb").write(data)

# archive comment containing a fake end-of-central-directory record ("PK\5\6" + 18 bytes, but not at the end of file)
# => the real record must be used, i.e. one entry listed
with zipfile.ZipFile(f"{work}/comment.zip", "w") as zf:
    zf.writestr("entry.txt", "listed only if the real EOCD record is found")
    zf.comment = b"note PK\x05\x06" + bytes(18) + b" end of note"

# Zip64 end records for a small archive: zipfile writes them only above its limits => lower the entry count limit
limit = zipfile.ZIP_FILECOUNT_LIMIT
zipfile.ZIP_FILECOUNT_LIMIT = 0
with zipfile.ZipFile(f"{work}/py_zip64.zip", "w") as zf: add_all(zf, zipfile.ZIP_DEFLATED)
zipfile.ZIP_FILECOUNT_LIMIT = limit
assert b"PK\x06\x06" in open(f"{work}/py_zip64.zip", "rb").read()

# self-extracting archives: executable stub prepended without adjusting the stored offsets (like "cat stub archive.zip")
stub = b"#!/bin/sh\necho self-extracting stub\nexit 0\n" + bytes(1000)
for name in ["py_deflate", "py_zip64"]:
    open(f"{work}/sfx_{name}.zip", "wb").write(stub + open(f"{work}/{name}.zip", "rb").read())

# names without UTF-8 flag (bit 11): bytes "C3 A9" are CP437 "├⌐" from a DOS/Windows host, but UTF-8 "é" from a Unix host (e.g. macOS Archive Utility)
# zipfile sets the flag for non-ASCII names => write ASCII placeholders of the same length and patch the raw bytes
with zipfile.ZipFile(f"{work}/unflagged.zip", "w") as zf:
    for placeholder, host in [("XX_dos.txt", 0), ("XX_unix.txt", 3)]:
        zi = zipfile.ZipInfo(placeholder)
        zi.create_system = host
        zf.writestr(zi, "name without UTF-8 flag")
data = open(f"{work}/unflagged.zip", "rb").read()
assert data.count(b"XX_") == 4 # local header + central directory
open(f"{work}/unflagged.zip", "wb").write(data.replace(b"XX_", b"\xc3\xa9_"))
EOF
zip -q -r "$WORK/infozip.zip" .                 # Info-ZIP: extended time stamps, UTF-8 names
zip -q -r -fz "$WORK/infozip64.zip" .           # force Zip64 extra fields
cd "$WORK"

echo "== test"
for archive in py_deflate py_stored py_zip64 sfx_py_deflate sfx_py_zip64 infozip infozip64; do
    "$WORK/zip_afs_test" "$WORK/$archive.zip" "$REF"
done
cp "$WORK/py_deflate.zip" "$WORK/pipe|name.zip"       # '|' in the archive name must not be taken as the inner path separator
"$WORK/zip_afs_test" "$WORK/pipe|name.zip" "$REF"
"$WORK/zip_afs_test" --expect-read-error "$WORK/corrupt.zip" corrupt.txt
"$WORK/zip_afs_test" --expect-item-count "$WORK/many.zip" 65540
"$WORK/zip_afs_test" --expect-item-count "$WORK/unsafe.zip" 3
"$WORK/zip_afs_test" --expect-item-count "$WORK/comment.zip" 1
"$WORK/zip_afs_test" --expect-item-count "$WORK/unflagged.zip" 2
"$WORK/zip_afs_test" --expect-item "$WORK/unflagged.zip" "├⌐_dos.txt"  # CP437
"$WORK/zip_afs_test" --expect-item "$WORK/unflagged.zip" "é_unix.txt"  # UTF-8

echo "== all ZIP tests passed"
