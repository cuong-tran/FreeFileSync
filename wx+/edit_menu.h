// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <zen/build_info.h>
#include <wx/menu.h>
#if defined ZEN_MAC
    #include "edit_menu_mac.h"
#endif


namespace zen
{
#if defined ZEN_MAC
/*  macOS: text fields get Undo, Cut, Copy, Paste and Select All (and their Cmd shortcuts) from the Edit menu of the menu bar: without
    one, Cmd+V and Cmd+Z do nothing in dialogs (docs/MAC_TEST_REPORT.md, F6). Added in code, not in the wxFormBuilder-generated files  */
inline
wxMenu& addEditMenu(wxMenuBar& menuBar, size_t pos) //returns the Edit menu, owned by the menu bar
{
    auto menuEdit = new wxMenu;
    menuEdit->Append(wxID_UNDO, _("&Undo") + L"\tCtrl+Z"); //wxOSX: Ctrl = Cmd
    menuEdit->Append(wxID_REDO, _("&Redo") + L"\tCtrl+Shift+Z");
    menuEdit->AppendSeparator();
    menuEdit->Append(wxID_CUT,       _("Cu&t")        + L"\tCtrl+X");
    menuEdit->Append(wxID_COPY,      _("&Copy")       + L"\tCtrl+C");
    menuEdit->Append(wxID_PASTE,     _("&Paste")      + L"\tCtrl+V");
    menuEdit->Append(wxID_SELECTALL, _("Select &all") + L"\tCtrl+A");

    menuBar.Insert(pos, menuEdit, _("&Edit"));

    macos::setNativeUndoRedoActions(menuEdit->GetHMenu(), 0 /*posUndo*/, 1 /*posRedo*/);
    return *menuEdit;
}
#endif
}
