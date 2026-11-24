// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "crash_report.h"

//empty implementation: the default build contains no crash reporting code (see crash_report.h)
using namespace zen;


const CrashReportBuildInfo& zen::getCrashReportBuildInfo()
{
    static const CrashReportBuildInfo info;
    return info;
}


bool zen::isCrashReportingAvailable() { return false; }

void zen::initCrashReporting(const CrashReportConfig& cfg) {} //throw SysError

bool zen::isCrashReportingActive() { return false; }

void zen::setCrashReportingConsent(bool consent) {}

void zen::shutdownCrashReporting() {}

void zen::addCrashBreadcrumb(const std::string& category, const std::wstring& message) {}

void zen::reportInternalError(const std::wstring& message) {}

void zen::triggerTestCrash(TestCrash kind) {}
