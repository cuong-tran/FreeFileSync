#!/usr/bin/env bash
# Assemble and ad-hoc sign a macOS app bundle (called by the makefiles for TARGET_OS=mac; see CONTRIBUTING.md):
#   mac_bundle.sh <bundle.app> <executable> <app name> <bundle id> <icon png> <minimum macOS> <document extensions> [<resources folder> [<helper>...]]
#     <bundle.app>           output folder, replaced
#     <executable>           the linked binary: becomes Contents/MacOS/<app name>
#     <icon png>             square PNG (e.g. 128 px from Resources/Icons.zip, extracted by the makefile): Contents/Resources/<app name>.icns
#     <document extensions>  space-separated, e.g. "ffs_gui ffs_batch": the app opens them from Finder (wxApp::MacOpenFiles); the app
#                            owns them (LSHandlerRank Owner) unless an extension ends in ":Alternate", e.g. "ffs_batch:Alternate"
#     <resources folder>     copied into Contents/Resources (FreeFileSync/Build/Resources), plus Animal.dat from .github/build/resources;
#                            "" for none
#     <helper>               executables copied next to the main one, one per argument, e.g. crashpad_handler of a Sentry build
# The version comes from FreeFileSync/Source/version/version.h. Signing: ad-hoc ("-"), inner code first, then the bundle (no --deep),
# without hardened runtime; not notarized.
set -euo pipefail
[ $# -ge 7 ] || { echo "usage: $0 <bundle.app> <executable> <app name> <bundle id> <icon png> <minimum macOS> <document extensions> [<resources folder>]" >&2; exit 2; }
bundle="$1" exe="$2" appName="$3" bundleId="$4" iconPng="$5" minOs="$6" docExts="$7" resources="${8:-}"
shift $(($# < 8 ? $# : 8)) # the rest: helpers
srcDir="$(cd "$(dirname "$0")" && pwd)"
repoRoot="$(cd "$srcDir/../.." && pwd)"

version="$(sed -nE 's/.*ffsVersion\[\] = "([0-9.]+)".*/\1/p' "$srcDir/version/version.h")"
[ -n "$version" ] || { echo "no ffsVersion in version/version.h" >&2; exit 1; }

rm -rf "$bundle"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$exe" "$bundle/Contents/MacOS/$appName"
for helper in "$@"; do
    cp "$helper" "$bundle/Contents/MacOS/"
done

if [ -n "$resources" ]; then
    cp -R "$resources/." "$bundle/Contents/Resources/"
    [ -f "$bundle/Contents/Resources/Animal.dat" ] || cp "$repoRoot/.github/build/resources/Animal.dat" "$bundle/Contents/Resources/"
fi

# icon: an .iconset from the PNG in the sizes iconutil wants (larger ones scaled up from the source)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
iconset="$work/$appName.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$iconPng" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    sips -z $((size * 2)) $((size * 2)) "$iconPng" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$bundle/Contents/Resources/$appName.icns"

docTypes=""
for ext in $docExts; do
    rank=Owner
    case "$ext" in *:Alternate) ext="${ext%:Alternate}" rank=Alternate ;; esac
    docTypes+="
        <dict>
            <key>CFBundleTypeName</key><string>$appName configuration (.$ext)</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>CFBundleTypeExtensions</key><array><string>$ext</string></array>
            <key>CFBundleTypeIconFile</key><string>$appName.icns</string>
            <key>LSHandlerRank</key><string>$rank</string>
        </dict>"
done

cat > "$bundle/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>$appName</string>
    <key>CFBundleIconFile</key><string>$appName.icns</string>
    <key>CFBundleIdentifier</key><string>$bundleId</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>$appName</string>
    <key>CFBundleDisplayName</key><string>$appName</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>$minOs</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>$docTypes
    </array>
    <key>NSAppleEventsUsageDescription</key><string>$appName asks System Events to shut down or put the computer to sleep when a synchronization finishes, if you choose that.</string>
    <key>NSDesktopFolderUsageDescription</key><string>$appName needs access to the folders you synchronize.</string>
    <key>NSDocumentsFolderUsageDescription</key><string>$appName needs access to the folders you synchronize.</string>
    <key>NSDownloadsFolderUsageDescription</key><string>$appName needs access to the folders you synchronize.</string>
    <key>NSRemovableVolumesUsageDescription</key><string>$appName needs access to the removable volumes you synchronize.</string>
    <key>NSNetworkVolumesUsageDescription</key><string>$appName needs access to the network volumes you synchronize.</string>
</dict>
</plist>
PLIST
plutil -lint "$bundle/Contents/Info.plist" >/dev/null
printf 'APPL????' > "$bundle/Contents/PkgInfo"

# ad-hoc signature: helper executables first (e.g. crashpad_handler of a Sentry build), then the bundle with its main executable
for helper in "$bundle"/Contents/MacOS/*; do
    [ "$(basename "$helper")" = "$appName" ] || codesign --force --sign - --timestamp=none "$helper"
done
codesign --force --sign - --timestamp=none "$bundle"
codesign --verify --strict "$bundle"
echo "built $bundle ($appName $version, $bundleId, macOS $minOs+)"
