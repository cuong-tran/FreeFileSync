// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//AppKit side of the Dock progress (dock_tile_mac.h): Objective-C++ with ARC, compiled as C++23 (see the header).
//The Dock tile gets a content view that draws the application icon and a progress bar at its bottom.

#include "dock_tile_mac.h"
#include <algorithm>
#include <cmath>

#import <AppKit/AppKit.h>

using namespace zen::macos;


@interface ZenDockProgressView : NSView
@property double fraction;
@property (strong) NSColor* barColor;
@end

@implementation ZenDockProgressView
- (void)drawRect:(NSRect)dirtyRect
{
    const NSRect bounds = [self bounds];
    [[NSApp applicationIconImage] drawInRect:bounds];

    const CGFloat barHeight = std::round(bounds.size.height * 0.12);
    const CGFloat margin    = std::round(bounds.size.width  * 0.10);
    const NSRect barRect = NSMakeRect(bounds.origin.x + margin, bounds.origin.y + std::round(bounds.size.height * 0.08),
                                      bounds.size.width - 2 * margin, barHeight);

    NSBezierPath* back = [NSBezierPath bezierPathWithRoundedRect:barRect xRadius:barHeight / 2 yRadius:barHeight / 2];
    [[NSColor colorWithCalibratedWhite:0.15 alpha:0.75] setFill];
    [back fill];

    NSRect fillRect = NSInsetRect(barRect, 1, 1);
    fillRect.size.width = std::max<CGFloat>(fillRect.size.height, fillRect.size.width * std::clamp(self.fraction, 0.0, 1.0));
    NSBezierPath* fill = [NSBezierPath bezierPathWithRoundedRect:fillRect xRadius:fillRect.size.height / 2 yRadius:fillRect.size.height / 2];
    [self.barColor setFill];
    [fill fill];
}
@end


namespace
{
NSColor* toNSColor(DockProgressColor color)
{
    switch (color)
    {
        case DockProgressColor::normal:        return [NSColor systemBlueColor];
        case DockProgressColor::indeterminate: return [NSColor systemGrayColor];
        case DockProgressColor::warning:       return [NSColor systemYellowColor];
        case DockProgressColor::error:         return [NSColor systemRedColor];
        case DockProgressColor::paused:        return [NSColor systemOrangeColor];
    }
    return [NSColor systemBlueColor];
}
}


void zen::macos::setDockProgress(double fraction, DockProgressColor color)
{
    NSDockTile* tile = [NSApp dockTile];
    ZenDockProgressView* view = nil;
    if ([[tile contentView] isKindOfClass:[ZenDockProgressView class]])
        view = (ZenDockProgressView*)[tile contentView];
    else
    {
        view = [[ZenDockProgressView alloc] initWithFrame:NSMakeRect(0, 0, [tile size].width, [tile size].height)];
        [tile setContentView:view];
    }
    view.fraction = color == DockProgressColor::indeterminate ? 1.0 : fraction;
    view.barColor = toNSColor(color);
    [tile display];
}


void zen::macos::clearDockProgress()
{
    NSDockTile* tile = [NSApp dockTile];
    [tile setContentView:nil];
    [tile display];
}
