// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

//macOS only, for darkmode.cpp. Standard types only: the Objective-C++ side (darkmode_mac.mm) is compiled as C++23, because the macOS 26
//SDK's CoreGraphics headers are no valid C++26 (see FreeFileSync/Source/base/icon_loader_mac.h). Main thread only.
namespace zen::macos
{
//let the app follow the system appearance again (NSApp.appearance = nil), also through later light/dark switches; wxOSX's
//wxApp::SetAppearance(System) instead fixes the app to the appearance it has at that moment
void followSystemAppearance();
}
