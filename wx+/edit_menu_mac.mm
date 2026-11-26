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


void zen::macos::giveTextFieldsEditShortcuts()
{
    /*  wxOSX gives a button with a mnemonic in its label ("&Versioning") the key equivalent Cmd+<letter> (src/osx/cocoa/button.mm,
        SetAcceleratorFromLabel()). AppKit offers a key equivalent to the key window's views before the menu bar, so in a dialog with
        such a button Cmd+V pressed in a text field clicked the button instead of pasting (docs/MAC_TEST_REPORT.md, F6). A local
        monitor sees the key first: with a text field (its field editor, an NSText) focused, the menu bar gets the edit shortcuts
        before the window; outside text fields nothing changes.  */
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event)
    {
        const NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask &
                                          (NSEventModifierFlagCommand | NSEventModifierFlagShift | NSEventModifierFlagOption | NSEventModifierFlagControl);
        //with Command held, "characters" is the Latin letter also on non-Latin layouts (Russian, Greek, ...), as macOS matches menu
        //shortcuts there; "charactersIgnoringModifiers" would be that layout's own letter
        NSString* key = event.characters.length > 0 ? event.characters.lowercaseString : event.charactersIgnoringModifiers.lowercaseString;
        const bool editShortcut = (mods == NSEventModifierFlagCommand && [@[@"x", @"c", @"v", @"a", @"z"] containsObject:key]) ||
                                  (mods == (NSEventModifierFlagCommand | NSEventModifierFlagShift) && [key isEqualToString:@"z"]);
        if (editShortcut && [NSApp.keyWindow.firstResponder isKindOfClass:NSText.class])
        {
            [NSApp.mainMenu performKeyEquivalent:event];
            return nil; //handled by the Edit menu, or the item is disabled (nothing to paste or undo): never on to a button's mnemonic
        }
        return event;
    }];
}
