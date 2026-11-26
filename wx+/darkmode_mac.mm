// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "darkmode_mac.h"
#import <AppKit/AppKit.h>


void zen::macos::followSystemAppearance()
{
    NSApp.appearance = nil; //windows then take the system's appearance and get wxEVT_SYS_COLOUR_CHANGED if it differs from the app's
}
