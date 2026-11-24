// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <zen/crash_report.h>
#include <zen/file_access.h>
#include <zen/file_path.h>
#include <zen/sys_info.h>
#include <zen/utf.h>
#include <zen/zstring.h>
#include "ffs_paths.h"
#include "version/version.h"


namespace fff
{
/*  Crash reporting for FreeFileSync and RealTimeSync (zen/crash_report.h). Does nothing in a build without crash reporting
    (the default). Header only, so the default build gets no extra object file.

    1. startCrashReporting() as early as possible in OnInit(): crashes from then on are caught; until step 2 the consent the
       last process stored applies (FreeFileSync reads its settings only once the event loop runs)
    2. applyCrashReportSetting() once GlobalSettings.xml is read: the consent follows the user's answer to the first-start
       notice (ui/small_dlgs.cpp); until there is one, nothing is sent, and reports are kept until the answer
    3. zen::shutdownCrashReporting() in OnExit(), before teardownAfs(); exits through zen::terminateProcess() flush too
       (std::at_quick_exit in crash_report_sentry.cpp)

    libcurl: sentry-native's transport calls curl_global_init(CURL_GLOBAL_ALL) in step 1, before FreeFileSync's own
    curl_global_init(CURL_GLOBAL_NOTHING) in initAfs(). libcurl counts these calls (only the first one initializes, the last
    curl_global_cleanup() cleans up), and CURL_GLOBAL_SSL has had no effect since libcurl 7.57, so the only difference is
    CURL_GLOBAL_WIN32 (WinSock init) on Windows; FreeFileSync's curl setup is unchanged otherwise.

    Off without asking: FFS_NO_CRASH_REPORT=1 in the environment, a missing crash handler, or any other init failure
    (the app starts normally; a note goes to stderr).

    - release:  freefilesync@<ffsVersion>+<short commit hash> ("+dev" if the build passed none), the same for both apps
    - database: <config folder>/CrashReports/<app name>
    - handler:  CrashReportBuildInfo::handlerFileName (crashpad_handler) next to the executable                  */
inline void startCrashReporting(const char* appName)
{
    using namespace zen;

    if (!isCrashReportingAvailable())
        return;

    if (const char* optOut = ::getenv("FFS_NO_CRASH_REPORT");
        optOut && std::string_view(optOut) != "" && std::string_view(optOut) != "0")
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
            cfg.handlerPath = appendPath(*exeFolderPath, utfTo<Zstring>(buildInfo.handlerFileName));
        }
        catch (const FileError& e) { throw SysError(e.toString()); }
        cfg.caCertPath = appendPath(getResourceDirPath(), Zstr("cacert.pem"));

        //removed from report texts in addition to paths (best effort: a name that can't be determined isn't needed)
        try { cfg.privateWords.push_back(utfTo<std::string>(getLoginUser())); } /*throw FileError*/ catch (FileError&) {}
        try { cfg.privateWords.push_back(utfTo<std::string>(getHostName ())); } /*throw FileError*/ catch (FileError&) {}

        initCrashReporting(cfg); //throw SysError
    }
    catch (const SysError& e)
    {
        //not an error the user has to act on: no popup, no "Error:" prefix
        std::cerr << '[' << appName << "] Crash reporting is off: " << utfTo<std::string>(e.toString()) << '\n';
    }
}


//the consent for a stored answer (GlobalConfig::crashReports); nullopt: not asked yet (e.g. batch runs before the first
//start of the GUI) => withheld, reports are kept
inline zen::CrashReportConsent getCrashReportConsent(const std::optional<bool>& setting)
{
    if (!setting)
        return zen::CrashReportConsent::unknown;
    return *setting ? zen::CrashReportConsent::given : zen::CrashReportConsent::revoked;
}


inline void applyCrashReportSetting(const char* appName, const std::optional<bool>& setting)
{
    using namespace zen;

    if (!isCrashReportingActive())
        return;

    setCrashReportingConsent(getCrashReportConsent(setting));

    //CI (Tests/sentry/run.sh): FFS_TEST_CRASH=segv|abort|internal-error fails on purpose once the consent is set; never in
    //a build for release (SENTRY_ENVIRONMENT=release)
    if (const char* testCrash = ::getenv("FFS_TEST_CRASH");
        testCrash && getCrashReportBuildInfo().environment != "release")
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
