// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <string>
#include <vector>
#include "sys_error.h"


/*  Crash reporting, chosen at build time (see the makefiles):
    - crash_report_sentry.cpp: sentry-native with its Crashpad backend ("make FFS_SENTRY=1 SENTRY_DSN=...")
    - crash_report_none.cpp:   empty implementation, the default: no reporting code at all

    Only crashes (minidumps) and reportInternalError() are sent; never sync logs, file names or FileError texts shown to users.
    Uploads need consent: nothing leaves the machine before setCrashReportingConsent(true).
    Texts (messages, breadcrumbs, exception values) are scrubbed of paths, user and host name (zen/crash_scrub.h); a minidump
    can't be scrubbed on the client: it contains the paths of the loaded modules and the threads' stack memory.               */
namespace zen
{
//values compiled in by the build (FFS_SENTRY=1), all empty in the empty implementation
struct CrashReportBuildInfo
{
    std::string dsn;
    std::string environment; //"ci", "release" or "dev"
    std::string gitSha;      //short commit hash, or empty if unknown
    std::string handlerFileName; //the crash handler executable that ships next to the app, e.g. "crashpad_handler"
};
const CrashReportBuildInfo& getCrashReportBuildInfo();


struct CrashReportConfig
{
    bool enabled = false;    //false: initCrashReporting() is a no-op
    std::string dsn;
    std::string release;     //e.g. "freefilesync@14.13+1a2b3c4"
    std::string environment;
    std::string appName;     //"FreeFileSync" or "RealTimeSync": sent as tag "app"
    Zstring databasePath;    //folder for pending reports and run state (created if missing)
    Zstring handlerPath;     //crash handler executable (crashpad_handler); reporting stays off if it is missing
    Zstring caCertPath;      //optional: CA bundle for the HTTPS upload (empty: the system's)
    std::vector<std::string> privateWords; //removed from report texts in addition to paths, e.g. user and host name (zen/crash_scrub.h)
};

//true if built with a crash reporting implementation; false in the empty build
bool isCrashReportingAvailable();

//call once, as early as possible; uploads stay paused until setCrashReportingConsent()
void initCrashReporting(const CrashReportConfig& cfg); //throw SysError

//true after a successful initCrashReporting() and before shutdownCrashReporting()
bool isCrashReportingActive();

//consent is not stored by the reporter: set it after every init, from the user's settings
void setCrashReportingConsent(bool consent);

//flushes pending events (bounded wait); safe to call when not initialized
void shutdownCrashReporting();

//context attached to the next crash or internal error report; must not contain sync data
void addCrashBreadcrumb(const std::string& category, const std::wstring& message);

//report an unexpected internal error (a bug, not a user-facing error such as FileError)
void reportInternalError(const std::wstring& message);


//deliberate failures for testing the reporting pipeline in CI (no-op in the empty build)
enum class TestCrash
{
    segv,
    abort,
    internalError,
};
void triggerTestCrash(TestCrash kind);
}
