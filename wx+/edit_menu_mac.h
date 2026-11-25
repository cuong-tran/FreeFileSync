// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

//macOS only, for edit_menu.h. Standard types only: the Objective-C++ side (edit_menu_mac.mm) is compiled as C++23, because the macOS 26
//SDK's CoreGraphics headers are no valid C++26 (see FreeFileSync/Source/base/icon_loader_mac.h). Main thread only.
namespace zen::macos
{
//let the items at these positions of an NSMenu (wxMenu::GetHMenu()) send undo:/redo: to the first responder, as in a native Edit menu
void setNativeUndoRedoActions(void* nsMenu, int posUndo, int posRedo);
}
