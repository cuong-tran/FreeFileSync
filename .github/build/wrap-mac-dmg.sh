#!/usr/bin/env bash
# Wrap FreeFileSync.app and RealTimeSync.app into one drag-to-Applications disk image (CI job "DMG" in macos-build.yml; also
# runs locally on a Mac). Modelled on Musikku's scripts/wrap-mac-dmg.sh.
#   wrap-mac-dmg.sh <folder with FreeFileSync.app and RealTimeSync.app> <output.dmg>
# - The apps are copied into a staging folder; AppleDouble sidecars ("._*", left behind by copies through non-Apple file systems
#   or archive tools) and Finder detritus (com.apple.FinderInfo, resource forks) are removed first: codesign --strict rejects a
#   bundle that contains them, so they would break the seal for the user.
# - Layout: both apps and a link to /Applications, volume name "FreeFileSync <version>" (version from the app's Info.plist),
#   volume icon from FreeFileSync.icns; no background picture.
# - create-dmg (brew install create-dmg) lays out the Finder window. Where Finder cannot be scripted (CI runners sometimes
#   can't), or without create-dmg, the image is made with hdiutil alone: no window layout, but the volume icon
#   (.VolumeIcon.icns and the custom icon flag of the volume root, set on a read-write copy that is mounted inside the work
#   folder). The path taken is printed ("[wrap-mac-dmg] made with ...").
#   WRAP_DMG_NO_FINDER=1 skips create-dmg (straight to hdiutil): on a person's Mac, scripting Finder asks them for the
#   Automation permission in a dialog that would hold up the job, and create-dmg mounts its read-write image under /Volumes,
#   outside the job's folders (the self-hosted CI runner sets it). The hdiutil path mounts nothing outside the work folder.
# - Afterwards the image is mounted read-only: its content is listed and both apps must pass codesign --verify --strict.
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <folder with FreeFileSync.app and RealTimeSync.app> <output.dmg>" >&2; exit 2; }
src="$1" out="$2"
for app in FreeFileSync RealTimeSync; do
    [ -d "$src/$app.app" ] || { echo "no $app.app in $src" >&2; exit 1; }
done
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$src/FreeFileSync.app/Contents/Info.plist")"
volName="FreeFileSync $version"
volIcon="$src/FreeFileSync.app/Contents/Resources/FreeFileSync.icns"

work="$(mktemp -d "$(t="${TMPDIR:-/tmp}"; echo "${t%/}")/wrap-mac-dmg.XXXXXX")" # explicit template (TMPDIR without its trailing slash): macOS 27's mktemp -d ignores TMPDIR
mountPoint=""
cleanup()
{
    if [ -n "$mountPoint" ]; then hdiutil detach -force "$mountPoint" >/dev/null 2>&1 || true; fi
    rm -rf "$work"
}
trap cleanup EXIT

staging="$work/staging"
mkdir -p "$staging"
for app in FreeFileSync RealTimeSync; do
    ditto "$src/$app.app" "$staging/$app.app" # keeps symlinks, permissions and extended attributes
done
find "$staging" -name '._*' -print -delete
xattr -cr "$staging" # FinderInfo/resource forks: "resource fork, Finder information, or similar detritus not allowed"
for app in FreeFileSync RealTimeSync; do
    codesign --verify --strict --verbose=2 "$staging/$app.app"
done

mkdir -p "$(dirname "$out")"
rm -f "$out"
# volumes named like the image: one already mounted (e.g. an earlier image the user has open) would make ours mount as "<name> 1";
# refuse rather than detach it. Between attempts, what is mounted under that name was mounted by this run (by create-dmg).
mountedLikeImage()
{
    local v
    for v in "/Volumes/$volName" "/Volumes/$volName "*; do
        if [ -e "$v" ]; then echo "$v"; fi
    done
}
if [ -n "$(mountedLikeImage)" ]; then
    # not ours to detach: e.g. a FreeFileSync image of the same version the user has open in Finder
    msg="a volume named like the image (\"$volName\") is already mounted: $(mountedLikeImage | tr '\n' ' ')- it is left alone; eject it in Finder, then run this again"
    if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error::$msg"; else echo "$msg" >&2; fi
    exit 1
fi
detachOurs()
{
    local v
    while IFS= read -r v; do hdiutil detach -force "$v" >/dev/null 2>&1 || true; done < <(mountedLikeImage)
}

createDmg() # extra create-dmg options...
{
    create-dmg --volname "$volName" --volicon "$volIcon" \
        --window-pos 200 120 --window-size 660 400 --icon-size 100 --text-size 13 \
        --icon FreeFileSync.app 150 170 --icon RealTimeSync.app 330 170 --app-drop-link 510 170 \
        --hdiutil-retries 10 "$@" "$out" "$staging"
}
# without the Finder window layout: hdiutil alone, with the volume icon; the read-write copy is mounted inside the work folder
hdiutilDmg()
{
    local rw="$work/rw.dmg" rwMount="$work/rw-mnt"
    ln -s /Applications "$staging/Applications"
    cp -X "$volIcon" "$staging/.VolumeIcon.icns"
    hdiutil create -quiet -volname "$volName" -srcfolder "$staging" -fs HFS+ -format UDRW -ov "$rw"
    mkdir -p "$rwMount"
    mountPoint="$rwMount" # detached by cleanup() if a step below fails
    hdiutil attach -quiet -readwrite -nobrowse -noautoopen -noverify -mountpoint "$rwMount" "$rw"
    # kHasCustomIcon (0x0400) in the Finder flags of the volume root: bytes 8-9 of its 32-byte FinderInfo
    xattr -wx com.apple.FinderInfo 0000000000000000040000000000000000000000000000000000000000000000 "$rwMount"
    hdiutil detach -quiet "$rwMount"
    mountPoint=""
    hdiutil convert -quiet "$rw" -format UDZO -ov -o "$out"
    rm -f "$rw"
}
method=""
if [ "${WRAP_DMG_NO_FINDER:-0}" = 1 ]; then
    echo "[wrap-mac-dmg] WRAP_DMG_NO_FINDER=1: without create-dmg and the Finder window layout" >&2
elif ! command -v create-dmg >/dev/null; then
    echo "[wrap-mac-dmg] create-dmg not installed (brew install create-dmg): without the Finder window layout" >&2
elif createDmg; then
    method="create-dmg"
else
    echo "[wrap-mac-dmg] create-dmg failed (Finder not scriptable?): without the Finder window layout" >&2
    rm -f "$out"; detachOurs
fi
if [ -z "$method" ]; then
    hdiutilDmg
    method="hdiutil (volume icon, no window layout)"
fi
echo "[wrap-mac-dmg] made with $method: $out"

#check: content and signatures as the user gets them
hdiutil verify "$out"
mountPoint="$work/mnt"
mkdir -p "$mountPoint"
hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "$mountPoint" "$out" >/dev/null
echo "[wrap-mac-dmg] content of $(basename "$out") ($volName):"
ls -la "$mountPoint"
[ -L "$mountPoint/Applications" ] || { echo "no Applications link in the image" >&2; exit 1; }
if leftovers="$(find "$mountPoint" -name '._*' | grep .)"; then echo "AppleDouble files in the image: $leftovers" >&2; exit 1; fi
for app in FreeFileSync RealTimeSync; do
    codesign --verify --strict --verbose=2 "$mountPoint/$app.app"
    echo "[wrap-mac-dmg] $app.app: $(lipo -archs "$mountPoint/$app.app/Contents/MacOS/$app"), signature valid"
done
hdiutil detach "$mountPoint" >/dev/null
mountPoint=""
ls -lh "$out"
