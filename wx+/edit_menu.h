// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <zen/build_info.h>
#include <utility>
#include <wx/menu.h>
#include <wx/window.h>
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

    /*  wxOSX sends Cut/Copy/Paste/Select All to the first responder as the native actions; a text field handles them itself. Any other
        focused window (file grids, overview, configuration list, log, the rich text of message boxes) has no such action: AppKit takes
        the Cmd key for the menu before the window sees it, and wxOSX turns the action into wxEVT_MENU for this menu instead. Hand it to
        the focused window: first as that command (wxRichTextCtrl copies on wxID_COPY), else as the Cmd+<key> it handled before the
        Edit menu existed (the grids' Ctrl+C/Ctrl+A handlers; wxOSX: Cmd = ControlDown())  */
    for (const auto& [id, keyCode] : {std::pair{wxID_CUT, 'X'}, std::pair{wxID_COPY, 'C'}, std::pair{wxID_PASTE, 'V'}, std::pair{wxID_SELECTALL, 'A'}})
        menuEdit->Bind(wxEVT_MENU, [id, keyCode](wxCommandEvent& event)
        {
            wxWindow* focus = wxWindow::FindFocus();
            if (!focus)
                return;

            wxCommandEvent cmdEvent(wxEVT_MENU, id);
            cmdEvent.SetEventObject(focus);
            if (focus->GetEventHandler()->ProcessEvent(cmdEvent))
                return;

            wxKeyEvent keyEvent(wxEVT_KEY_DOWN);
            keyEvent.m_keyCode = keyCode;
            keyEvent.SetControlDown(true);
            keyEvent.SetEventObject(focus);
            keyEvent.SetId(focus->GetId());
            focus->GetEventHandler()->ProcessEvent(keyEvent);
        }, id);

    menuBar.Insert(pos, menuEdit, _("&Edit"));

    macos::setNativeUndoRedoActions(menuEdit->GetHMenu(), 0 /*posUndo*/, 1 /*posRedo*/);
    return *menuEdit;
}
#endif
}
