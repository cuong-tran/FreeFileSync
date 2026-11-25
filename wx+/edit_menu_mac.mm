// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "edit_menu_mac.h"
#import <AppKit/AppKit.h>


void zen::macos::setNativeUndoRedoActions(void* nsMenu, int posUndo, int posRedo)
{
    /*  wxOSX maps wxID_CUT/COPY/PASTE/SELECTALL to the native actions (cut:, copy:, ...) sent to the first responder, but not
        wxID_UNDO/REDO (src/osx/cocoa/menuitem.mm): those become wxEVT_MENU for the frame owning the menu bar, which doesn't reach the
        focused text field and is disabled while a modal dialog runs. The native actions reach the text field's undo manager
        (wxTextCtrl's own for multi-line fields, the window's for the field editor of single-line ones) in any window, modal or not,
        and AppKit enables the items from what that undo manager can do.  */
    NSMenu* menu = (__bridge NSMenu*)nsMenu;
    auto setAction = [menu](int pos, SEL action)
    {
        if (0 <= pos && pos < menu.numberOfItems)
        {
            NSMenuItem* item = [menu itemAtIndex:pos];
            item.target = nil; //first responder
            item.action = action;
        }
    };
    setAction(posUndo, @selector(undo:));
    setAction(posRedo, @selector(redo:));
}
