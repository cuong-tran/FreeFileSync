// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS implementation of taskbar.h: progress on the application's Dock icon (dock_tile_mac.mm). There is one Dock icon per process,
//so the Taskbar created last shows its progress, and the icon is plain again once it is destroyed.

#include "taskbar.h"
#include "dock_tile_mac.h"

using namespace zen;


class Taskbar::Impl
{
public:
    Impl() { owner() = this; }
    ~Impl()
    {
        if (owner() == this)
        {
            owner() = nullptr;
            macos::clearDockProgress();
        }
    }

    void setStatus(Status status)
    {
        switch (status)
        {
            case Status::normal:        color_ = macos::DockProgressColor::normal;        break;
            case Status::indeterminate: color_ = macos::DockProgressColor::indeterminate; break;
            case Status::warning:       color_ = macos::DockProgressColor::warning;       break;
            case Status::error:         color_ = macos::DockProgressColor::error;         break;
            case Status::paused:        color_ = macos::DockProgressColor::paused;        break;
        }
        update();
    }

    void setProgress(double fraction)
    {
        fraction_ = fraction;
        update();
    }

private:
    void update()
    {
        if (owner() == this)
            macos::setDockProgress(fraction_, color_);
    }

    static Impl*& owner()
    {
        static Impl* inst = nullptr; //main thread only
        return inst;
    }

    double fraction_ = 0;
    macos::DockProgressColor color_ = macos::DockProgressColor::normal;
};


Taskbar::Taskbar(wxWindow* window) : pimpl_(std::make_unique<Impl>()) {}
Taskbar::~Taskbar() {}

void Taskbar::setStatus(Status status) { pimpl_->setStatus(status); }
void Taskbar::setProgress(double fraction) { pimpl_->setProgress(fraction); }
