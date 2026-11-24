// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <vector>
#include <zen/zstring.h>
#include <zen/build_info.h>
#include <wx/app.h>

namespace rts
{
class Application : public wxApp
{
private:
    bool OnInit() override;
    int  OnRun () override;
    int  OnExit() override;
    wxLayoutDirection GetLayoutDirection() const override;

    void onEnterEventLoop();

#if defined ZEN_MAC
    void MacOpenFiles(const wxArrayString& fileNames) override; //Finder: open .ffs_real/.ffs_batch files (double-click, Dock icon drop)
    std::vector<Zstring> macOpenFilesAtLaunch_; //received before onEnterEventLoop()
    bool eventLoopEntered_ = false;
#endif
};
}
