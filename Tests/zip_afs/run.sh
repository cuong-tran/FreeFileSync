#!/usr/bin/env bash
# Build and run the ZIP AbstractFileSystem test (Linux only, like the app: needs a C++26 compiler, zlib, glib/gio, python3, zip)
#   CXX:    compiler (default g++; g++ 14 or newer, or clang 20 or newer)
#   STDLIB: "libc++" to build against LLVM's libc++ instead of the compiler's default C++ library (like the makefiles' STDLIB)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
CXX="${CXX:-g++}"
# CXXFLAGS, LDFLAGS from the environment are appended to the compile/link command

#glib >= 2.78 can make g_free() a function-like macro, which breaks "::g_free(ptr)" in zen/zstring.cpp:
#cap the glib API at 2.76 like .github/build/build-free-file-sync.sh (older glib doesn't define GLIB_VERSION_2_76 and needs no cap)
GLIB_CAP=()
if pkg-config --atleast-version=2.76 glib-2.0; then
    GLIB_CAP=(-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76)
fi
#compiler family and STDLIB: same flags as the makefiles
if "$CXX" --version | grep -q clang; then
    COMPILER_FLAGS=(-Wtautological-overlap-compare -Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-lambda-capture)
else
    COMPILER_FLAGS=(-Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op)
fi
case "${STDLIB:-}" in
    "")     ;;
    libc++) COMPILER_FLAGS+=(-stdlib=libc++) ;;
    *)      echo "STDLIB must be empty or \"libc++\", not \"$STDLIB\"" >&2; exit 2 ;;
esac
read -r -a CXXFLAGS_USER <<< "${CXXFLAGS:-}"
read -r -a LDFLAGS_USER  <<< "${LDFLAGS:-}"

echo "== build ($("$CXX" --version | head -n 1)${STDLIB:+, $STDLIB})"
#same language standard and warnings as FreeFileSync/Source/Makefile; zen/sys_error.cpp needs glib + gio (GError)
"$CXX" -std=c++26 -O1 -g -pthread -DNDEBUG -DWXINTL_NO_GETTEXT_MACRO -I"$ROOT" -I"$ROOT/zenXml" -include "zen/i18n.h" \
    -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls \
    -Wno-unused-function -Wno-deprecated-enum-enum-conversion "${COMPILER_FLAGS[@]}" \
    "${GLIB_CAP[@]}" $(pkg-config --cflags glib-2.0 gio-2.0 zlib) "${CXXFLAGS_USER[@]}" \
    "$ROOT/Tests/zip_afs/zip_afs_test.cpp" \
    "$ROOT/FreeFileSync/Source/afs/zip.cpp" \
    "$ROOT/FreeFileSync/Source/afs/abstract.cpp" \
    "$ROOT"/zen/{zstring,file_path,resolve_path,file_access,file_io,file_traverser,sys_error,sys_info,sys_version,thread,format_unit,legacy_compiler,process_exec}.cpp \
    $(pkg-config --libs glib-2.0 gio-2.0 zlib) "${LDFLAGS_USER[@]}" -o "$WORK/zip_afs_test"

echo "== fixtures"
REF="$WORK/ref"
mkdir -p "$REF/sub dir/nested" "$REF/empty folder" "$REF/Données été"
echo "hello" > "$REF/a.txt"
head -c 300000 /dev/urandom > "$REF/sub dir/random.bin"                            # incompressible
{ yes "compressible line" || true; } | head -n 50000 > "$REF/sub dir/nested/big.txt"  # multiple inflate blocks; "|| true": yes gets SIGPIPE, fatal with pipefail
: > "$REF/sub dir/empty.txt"
echo "accents" > "$REF/Données été/fichier é.txt"
touch -d "2024-02-29 13:37:42" "$REF/a.txt"

cd "$REF"
python3 - "$WORK" <<'EOF'
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
