// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <optional>
#include <vector>
#include <zen/zstring.h>

//macOS helpers of ffs_paths.cpp: Core Foundation and LaunchServices stay in ffs_paths_mac.cpp
namespace fff
{
//FreeFileSync.app's bundle id: must match the one FreeFileSync/Source/Makefile passes to mac_bundle.sh
constexpr const char* freeFileSyncBundleId = "io.github.cuong-tran.freefilesync";

/*  App Translocation: macOS runs a bundle that still has the quarantine flag (downloaded, then moved by Finder together with other
    files, e.g. copied out of a DMG) from a random read-only mount that holds that one bundle only:
        /private/var/folders/.../AppTranslocation/<uuid>/d/RealTimeSync.app
    => the bundle's original location (e.g. /Applications/RealTimeSync.app), or none if the bundle is not translocated  */
std::optional<Zstring> getUntranslocatedBundlePath(const Zstring& bundlePath);

//the app bundles LaunchServices knows for a bundle id, the one it would launch first; translocated copies left out
std::vector<Zstring> getAppBundlePathsById(const char* bundleId);
}
