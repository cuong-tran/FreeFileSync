// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

//macOS only: a progress bar on the application's Dock icon (NSDockTile), for taskbar_mac.cpp. Standard types only: the Objective-C++
//side (dock_tile_mac.mm) is compiled as C++23, because the macOS 26 SDK's CoreGraphics headers are no valid C++26 (see
//FreeFileSync/Source/base/icon_loader_mac.h). Main thread only.
namespace zen::macos
{
enum class DockProgressColor
{
    normal,
    indeterminate,
    warning,
    error,
    paused,
};

void setDockProgress(double fraction /*[0, 1]*/, DockProgressColor color); //shows the bar
void clearDockProgress(); //the plain application icon
}
