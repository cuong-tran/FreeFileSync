// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <zen/crash_report.h>
#include <zen/file_access.h>
#include <zen/file_path.h>
#include <zen/sys_info.h>
#include <zen/thread.h>
#include <zen/utf.h>
#include <zen/zstring.h>
#include <zenxml/xml.h>
#include "ffs_paths.h"
#include "version/version.h"


namespace fff
{
/*  Crash reporting for FreeFileSync and RealTimeSync (zen/crash_report.h). Does nothing in a build without crash reporting
    (the default). Header only, so the default build gets no extra object file.

    1. startCrashReporting() as early as possible in OnInit(): crashes from then on are caught; until step 2 the consent the
       last process stored applies (FreeFileSync reads its settings only once the event loop runs)
    2. applyCrashReportSetting() once GlobalSettings.xml is read: the consent follows the user's answer to the first-start
       notice (ui/small_dlgs.cpp); until there is one, nothing is sent, and reports are kept until the answer. Then
       startCrashReportSettingWatch(): the answer can change in another process (FreeFileSync writes it at once,
       config.cpp writeCrashReportSetting()), and long-running processes such as RealTimeSync follow it without a restart
    3. stopCrashReportSettingWatch() and zen::shutdownCrashReporting() in OnExit(), before teardownAfs(); exits through
       zen::terminateProcess() flush too (std::at_quick_exit in crash_report_sentry.cpp)

    The setting records the user's consent, not a preference: it changes only through the notice or the options checkbox.

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
//start of the GUI, or another GlobalSettings.xml) => this process sends nothing and keeps the shared consent state as it is
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


//the stored answer only: <CrashReports Enabled="..."/> of GlobalSettings.xml (nullopt: no file, or no answer in it);
//without wxWidgets, so it can run on any thread
inline std::optional<bool> readCrashReportSetting(const Zstring& globalCfgFilePath) //throw FileError
{
    using namespace zen;
    if (!itemExists(globalCfgFilePath)) //throw FileError
        return std::nullopt;
    const XmlDoc doc = loadXml(globalCfgFilePath); //throw FileError
    if (const XmlElement* elem = doc.root().getChild("CrashReports"))
        if (bool enabled = true;
            elem->getAttribute("Enabled", enabled))
            return enabled;
    return std::nullopt;
}


/*  Rule: a process follows the stored answer when it changes. A background thread checks the file's modification time and
    size every few seconds (FFS_CRASH_REPORT_WATCH_MS overrides the interval, for Tests/sentry), reads the answer when they
    change, and applies a changed answer. A file without an answer changes nothing (see getCrashReportConsent()).     */
namespace impl
{
inline std::unique_ptr<zen::InterruptibleThread>& crashReportWatchThread()
{
    static std::unique_ptr<zen::InterruptibleThread> thread; //only touched by the main thread: start/stop
    return thread;
}
}

inline void startCrashReportSettingWatch(const Zstring& globalCfgFilePath, const std::optional<bool>& appliedSetting)
{
    using namespace zen;
    if (!isCrashReportingActive() || impl::crashReportWatchThread())
        return;

    std::chrono::milliseconds interval(5000);
    if (const char* ms = ::getenv("FFS_CRASH_REPORT_WATCH_MS"); ms && getCrashReportBuildInfo().environment != "release")
        interval = std::chrono::milliseconds(std::max(100, std::atoi(ms)));

    impl::crashReportWatchThread() = std::make_unique<InterruptibleThread>([globalCfgFilePath, appliedSetting, interval]
    {
        setCurrentThreadName(Zstr("Crash report setting"));
        const std::filesystem::path path(utfTo<std::string>(globalCfgFilePath));
        auto fileStamp = [&]
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(path, ec);
            const auto size = ec ? 0 : std::filesystem::file_size(path, ec);
            //(libc++'s file_time_type counts in a 128-bit type)
            return ec ? std::string() : std::to_string(static_cast<long long>(time.time_since_epoch().count())) + '/' + std::to_string(size);
        };
        std::string stamp = fileStamp();
        std::optional<bool> applied = appliedSetting;
        for (;;)
        {
            interruptibleSleep(interval); //throw ThreadStopRequest
            if (const std::string newStamp = fileStamp();
                newStamp != stamp)
            {
                stamp = newStamp;
                try
                {
                    if (const std::optional<bool> setting = readCrashReportSetting(globalCfgFilePath); //throw FileError
                        setting && setting != applied)
                    {
                        setCrashReportingConsent(getCrashReportConsent(setting));
                        applied = setting;
                    }
                }
                catch (FileError&) {} //e.g. caught while being written: the next change of the stamp tries again
            }
        }
    });
}

inline void stopCrashReportSettingWatch() { impl::crashReportWatchThread().reset(); } //interrupts and joins
}
