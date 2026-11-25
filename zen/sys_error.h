// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include "build_info.h"  //ZEN_LINUX, ZEN_MAC
#include "scope_guard.h" //
#include "i18n.h"        //not used by this header, but the "rest of the world" needs it!
#include "zstring.h"     //
#include "extra_log.h"   //

#if defined ZEN_LINUX
    #include <glib.h>
#elif defined ZEN_MAC
    struct __CFError; //CFErrorRef = __CFError*, see formatCFError()
#endif
    #include <cerrno>

namespace zen
{
//evaluate GetLastError()/errno and assemble specific error message
    using ErrorCode = int;

ErrorCode getLastError();

std::wstring formatSystemError(const std::string& functionName, const std::wstring& errorCode, const std::wstring& errorMsg);
std::wstring formatSystemError(const std::string& functionName, ErrorCode ec);
#if defined ZEN_LINUX
    std::wstring formatGlibError(const std::string& functionName, GError* error);
#elif defined ZEN_MAC
    //CoreFoundation/Cocoa errors (implementation: sys_error_mac.mm); NSError* is toll-free bridged: pass (__bridge CFErrorRef)nsError
    std::wstring formatCFError(const std::string& functionName, ::__CFError* error); //error may be nullptr
    std::wstring formatOSStatus(const std::string& functionName, int /*OSStatus*/ status);
#endif


//A low-level exception class giving (non-translated) detail information only - same conceptional level like "GetLastError()"!
class SysError
{
public:
    explicit SysError(const std::wstring& msg) : msg_(msg) {}
    const std::wstring& toString() const { return msg_; }

private:
    std::wstring msg_;
};

#define DEFINE_NEW_SYS_ERROR(X) struct X : public zen::SysError { X(const std::wstring& msg) : SysError(msg) {} };



//better leave it as a macro (see comment in file_error.h)
#define THROW_LAST_SYS_ERROR(functionName)                           \
    do { const ErrorCode ecInternal = getLastError(); throw zen::SysError(formatSystemError(functionName, ecInternal)); } while (false)


/* Example: ASSERT_SYSERROR(expr);

    Equivalent to:
        if (!expr)
            throw zen::SysError(L"Assertion failed: \"expr\"");            */
#define ASSERT_SYSERROR(expr) ASSERT_SYSERROR_IMPL(expr, #expr) //throw SysError



//######################## implementation ########################
inline
ErrorCode getLastError()
{
    return errno; //don't use "::" prefix, errno is a macro!
}


std::wstring getSystemErrorDescription(ErrorCode ec); //return empty string on error
//intentional overload ambiguity to catch usage errors with HRESULT:
std::wstring getSystemErrorDescription(long long) = delete;




namespace impl
{
inline bool validateBool(bool  b) { return b; }
inline bool validateBool(void* b) { return b; }
bool validateBool(int) = delete; //catch unintended bool conversions, e.g. HRESULT
}
#define ASSERT_SYSERROR_IMPL(expr, exprStr) \
    { if (!zen::impl::validateBool(expr))        \
            throw zen::SysError(L"Assertion failed: \"" L ## exprStr L"\""); }
}
