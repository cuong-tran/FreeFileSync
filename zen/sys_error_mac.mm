// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS: error formatting for CoreFoundation/Cocoa APIs (declared in sys_error.h)
#include "sys_error.h"
#import <Foundation/Foundation.h>

using namespace zen;


namespace
{
std::wstring toWString(NSString* str)
{
    if (!str)
        return {};
    const char* utf8 = [str UTF8String]; //owned by the autorelease pool
    return utf8 ? utfTo<std::wstring>(utf8) : std::wstring();
}
}


//"<domain> <code>: <description> [functionName]", e.g. "NSCocoaErrorDomain 4: “x” couldn’t be removed. [trashItemAtURL]"
std::wstring zen::formatCFError(const std::string& functionName, ::__CFError* error)
{
    @autoreleasepool
    {
        if (!error)
            return formatSystemError(functionName, L"", _("Error description not available.") + L" null CFError");

        NSError* nsError = (__bridge NSError*)error; //toll-free bridged
        std::wstring errorCode = toWString(nsError.domain) + L' ' + numberTo<std::wstring>(nsError.code);

        //POSIX errors: show the familiar errno name, e.g. "ENOENT" instead of "NSPOSIXErrorDomain 2"
        NSError* posixError = [nsError.domain isEqualToString:NSPOSIXErrorDomain] ? nsError : nsError.userInfo[NSUnderlyingErrorKey];
        if (posixError && [posixError.domain isEqualToString:NSPOSIXErrorDomain])
            errorCode += L" (" + trimCpy(beforeFirst(formatSystemError("", static_cast<ErrorCode>(posixError.code)), L':', IfNotFoundReturn::all)) + L')';

        std::wstring errorMsg = toWString(nsError.localizedDescription);
        if (NSString* reason = nsError.localizedFailureReason)
            if (![nsError.localizedDescription containsString:reason])
                errorMsg += L' ' + toWString(reason);

        return formatSystemError(functionName, errorCode, errorMsg);
    }
}


std::wstring zen::formatOSStatus(const std::string& functionName, int status)
{
    @autoreleasepool
    {
        NSError* nsError = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
        return formatSystemError(functionName, L"OSStatus " + numberTo<std::wstring>(status), toWString(nsError.localizedDescription));
    }
}
