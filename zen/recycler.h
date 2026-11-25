// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <vector>
#include <functional>
#include "file_error.h"
#include "build_info.h"

namespace zen
{
/* --------------------
   |Recycle Bin Access|
   --------------------

    Windows: -> Recycler API (IFileOperation) always available
             -> COM needs to be initialized before calling any of these functions! CoInitializeEx/CoUninitialize

    Linux: Compiler flags: `pkg-config --cflags gio-2.0`
           Linker   flags: `pkg-config --libs gio-2.0`

           Already included in package "gtk+-2.0"!                  */


//fails if item is not existing (anymore)
void moveToRecycleBin(const Zstring& itemPath); //throw FileError, RecycleBinUnavailable

#if defined ZEN_MAC
namespace impl
{
//macOS: the trash can't be redirected for tests (unlike $XDG_DATA_HOME on Linux): moveToRecycleBin() that returns the
//item's new path in the trash, so a test can delete what it put there
//the result is empty if the item was moved, but the trash didn't tell where to
Zstring moveToRecycleBinGetTrashPath(const Zstring& itemPath); //throw FileError, RecycleBinUnavailable
}
#endif
}
