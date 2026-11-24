#!/usr/bin/env bash
# Wrap FreeFileSync.app and RealTimeSync.app into one drag-to-Applications disk image (CI job "DMG" in macos-build.yml; also
# runs locally on a Mac). Modelled on Musikku's scripts/wrap-mac-dmg.sh.
#   wrap-mac-dmg.sh <folder with FreeFileSync.app and RealTimeSync.app> <output.dmg>
# - The apps are copied into a staging folder; AppleDouble sidecars ("._*", left behind by copies through non-Apple file systems
#   or archive tools) and Finder detritus (com.apple.FinderInfo, resource forks) are removed first: codesign --strict rejects a
#   bundle that contains them, so they would break the seal for the user.
# - Layout: both apps and a link to /Applications, volume name "FreeFileSync <version>" (version from the app's Info.plist),
#   volume icon from FreeFileSync.icns; no background picture.
# - create-dmg (brew install create-dmg) lays out the Finder window. On a machine where Finder cannot be scripted (CI runners
#   sometimes can't), it runs again with --skip-jenkins (no window layout), and without create-dmg at all, plain
#   "hdiutil create -format UDZO" makes the image. The path taken is printed ("[wrap-mac-dmg] made with ...").
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

work="$(mktemp -d)"
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
detachStale() # an earlier run's volume of the same name would make the new one mount as "<name> 1"
{
    local v
    for v in "/Volumes/$volName" "/Volumes/$volName "*; do
        if [ -e "$v" ]; then hdiutil detach -force "$v" >/dev/null 2>&1 || true; fi
    done
}
detachStale

createDmg() # extra create-dmg options...
{
    create-dmg --volname "$volName" --volicon "$volIcon" \
        --window-pos 200 120 --window-size 660 400 --icon-size 100 --text-size 13 \
        --icon FreeFileSync.app 150 170 --icon RealTimeSync.app 330 170 --app-drop-link 510 170 \
        --hdiutil-retries 10 "$@" "$out" "$staging"
}
method=""
if command -v create-dmg >/dev/null; then
    if createDmg; then
        method="create-dmg"
    else
        echo "[wrap-mac-dmg] create-dmg failed (Finder not scriptable?): again without the window layout (--skip-jenkins)" >&2
        rm -f "$out"; detachStale
        if createDmg --skip-jenkins; then method="create-dmg --skip-jenkins"; fi
    fi
else
    echo "[wrap-mac-dmg] create-dmg not installed (brew install create-dmg)" >&2
fi
if [ -z "$method" ]; then
    rm -f "$out"; detachStale
    ln -s /Applications "$staging/Applications"
    hdiutil create -volname "$volName" -srcfolder "$staging" -fs HFS+ -format UDZO -ov "$out"
    method="hdiutil create -format UDZO (no window layout, no volume icon)"
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
