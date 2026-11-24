// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <iostream>
#include <zen/crash_report.h>
#include <zen/file_access.h>
#include <zen/sys_info.h>
#include "ffs_paths.h"
#include "version/version.h"


namespace fff
{
/*  Crash reporting for FreeFileSync and RealTimeSync (zen/crash_report.h): call startCrashReporting() as early as possible in
    OnInit() and zen::shutdownCrashReporting() in OnExit(). Does nothing in a build without crash reporting (the default).
    A missing crash handler or any other init failure leaves reporting off; the app starts normally.

    - release:  freefilesync@<ffsVersion>+<short commit hash> ("+dev" if the build passed none), the same for both apps
    - database: <config folder>/CrashReports/<app name>
    - handler:  crashpad_handler next to the executable                                                                 */
inline void startCrashReporting(const char* appName)
{
    using namespace zen;

    if (!isCrashReportingAvailable())
        return;

    const CrashReportBuildInfo& buildInfo = getCrashReportBuildInfo();
    try
    {
        CrashReportConfig cfg;
        cfg.enabled      = true;
        cfg.dsn          = buildInfo.dsn;
        cfg.release      = std::string("freefilesync@") + ffsVersion + '+' + (buildInfo.gitSha.empty() ? std::string("dev") : buildInfo.gitSha);
        cfg.environment  = buildInfo.environment;
        cfg.appName      = appName;
        cfg.databasePath = appendPath(appendPath(getConfigDirPath(), Zstr("CrashReports")), utfTo<Zstring>(appName));
        try
        {
            const std::optional<Zstring> exeFolderPath = getParentFolderPath(getProcessPath()); //throw FileError
            if (!exeFolderPath)
                throw SysError(L"Executable has no parent folder.");
            cfg.handlerPath = appendPath(*exeFolderPath, Zstr("crashpad_handler"));
        }
        catch (const FileError& e) { throw SysError(e.toString()); }
        cfg.caCertPath = appendPath(getResourceDirPath(), Zstr("cacert.pem"));

        initCrashReporting(cfg); //throw SysError
        setCrashReportingConsent(true);
    }
    catch (const SysError& e)
    {
        //not an error the user has to act on: no popup, no "Error:" prefix
        std::cerr << '[' << appName << "] Crash reporting is off: " << utfTo<std::string>(e.toString()) << '\n';
    }

    //CI (Tests/sentry/run.sh): FFS_TEST_CRASH=segv|abort|internal-error fails on purpose right after init; Sentry builds only
    if (const char* testCrash = ::getenv("FFS_TEST_CRASH"))
    {
        const std::string_view kind = testCrash;
        if (kind == "segv")
            triggerTestCrash(TestCrash::segv);
        else if (kind == "abort")
            triggerTestCrash(TestCrash::abort);
        else if (kind == "internal-error")
            triggerTestCrash(TestCrash::internalError);
        else
            std::cerr << '[' << appName << "] Unknown FFS_TEST_CRASH value: " << kind << '\n';
    }
}
}
