// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "legacy_compiler.h"
#include <cassert>
#include <charconv>
/*  1. including <charconv> in header file blows up VC++:
       - string_tools.h: "An internal error has occurred in the compiler. (compiler file 'd:\agent\_work\1\s\src\vctools\Compiler\Utc\src\p2\p2symtab.c', line 2618)"
       - PCH: "fatal error C1076: compiler limit: internal heap limit reached"
        => include in separate compilation unit
    2. Disable "C/C++ -> Code Generation -> Smaller Type Check" (and PCH usage!), at least for this compilation unit: https://github.com/microsoft/STL/pull/171   */
#include <cmath> //NAN
#ifdef ZEN_FROM_CHARS_FALLBACK
    #include <cerrno>
    #include <clocale>
    #include <cstdlib>
    #include <string>
    #if __has_include(<xlocale.h>)
        #include <xlocale.h> //strtod_l (macOS, BSD)
    #endif
#endif


#ifdef ZEN_FROM_CHARS_FALLBACK
namespace
{
/*  std::from_chars(first, last, double) with std::chars_format::general, emulated with strtod_l() in the "C" locale:
    - no leading white space, no '+' (strtod accepts both): fail
    - no hexadecimal floats (strtod parses "0x1p3" as 8): "0x..." is the number 0 followed by other characters
    - overflow, and underflow to zero: fail (result_out_of_range); subnormal results are fine
    - characters after the number are ignored (like fromChars() does with the end pointer of from_chars())           */
bool fromCharsFallback(const char* first, const char* last, double& num)
{
    static const locale_t cLocale = ::newlocale(LC_ALL_MASK, "C", nullptr); //never freed: used until process exit
    if (!cLocale || first == last)
        return false;

    const char* it = first;
    if (*it == '-')
        ++it;
    if (it == last || !(('0' <= *it && *it <= '9') || *it == '.' || *it == 'i' || *it == 'I' || *it == 'n' || *it == 'N'))
        return false; //also: white space, '+', second sign

    if (*it == '0' && last - it >= 2 && (it[1] == 'x' || it[1] == 'X'))
    {
        num = *first == '-' ? -0.0 : 0.0;
        return true;
    }

    const std::string buf(first, last); //strtod needs a null-terminated string
    char* endPtr = nullptr;
    errno = 0;
    const double val = ::strtod_l(buf.c_str(), &endPtr, cLocale);
    if (endPtr == buf.c_str())
        return false;
    if (errno == ERANGE && (val == 0 || std::isinf(val)))
        return false;

    num = val;
    return true;
}
}
#endif


double zen::fromChars(const char* first, const char* last)
{
    double num = 0;
#ifdef ZEN_FROM_CHARS_FALLBACK
    return fromCharsFallback(first, last, num) ? num : NAN;
#else
    [[maybe_unused]] const std::from_chars_result rv = std::from_chars(first, last, num);
    return rv.ec == std::errc{} ? num : NAN;
#endif
}


char* zen::toChars(char* first, char* last, double num)
{
    const std::to_chars_result rv = std::to_chars(first, last, num);
    assert(rv.ec != std::errc::value_too_large);
    return rv.ec == std::errc{} ? rv.ptr : first;
}

