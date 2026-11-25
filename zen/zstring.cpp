// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "zstring.h"
    #include "sys_error.h"
#if defined ZEN_MAC
    #include <cstdlib> //malloc, free
    #include <CoreFoundation/CoreFoundation.h>
#endif

using namespace zen;


#if defined ZEN_MAC
namespace
{
/*  macOS: the three GLib functions this file uses, implemented with CoreFoundation, so the code below is the same as on Linux:
    - g_unichar_toupper(): simple (1:1) upper-case mapping per code point, like GLib (e.g. "ß" stays "ß"); a table for the BMP
      is built once from CFStringUppercase(), other planes ask CoreFoundation per character (rare)
    - g_utf8_normalize(): CFStringNormalize(), skipped for text that is normalized already (quick check below)              */
using gchar    = char;
using gunichar = impl::CodePoint;
enum GNormalizeMode { G_NORMALIZE_NFD, G_NORMALIZE_NFC };

void g_free(void* ptr) { ::free(ptr); }


//code points with NFC_QC = Yes and canonical combining class 0 (UAX #15): a string of only these is in NFC/NFD;
//covers ASCII, Latin-1, Cyrillic, kana, CJK, Hangul syllables (NFC), emoji; everything else goes through CoreFoundation
bool isNormalizedCodePoint(impl::CodePoint cp, GNormalizeMode mode)
{
    if (mode == G_NORMALIZE_NFC)
        return cp < 0x300 ||
               (0x400 <= cp && cp < 0x483) || //Cyrillic letters (no combining marks)
               (0x3041 <= cp && cp < 0x3097) || //Hiragana (without U+3099/309A combining marks)
               (0x30A1 <= cp && cp < 0x30FB) || //Katakana
               (0x4E00 <= cp && cp < 0xA000) || //CJK Unified Ideographs
               (0xAC00 <= cp && cp < 0xD7A4) || //Hangul syllables (precomposed)
               (0x1F300 <= cp && cp < 0x1FB00);  //pictographs, emoji
    else //NFD: no precomposed characters
        return cp < 0xC0 ||
               (0x4E00 <= cp && cp < 0xA000) ||
               (0x1F300 <= cp && cp < 0x1FB00);
}


gchar* g_utf8_normalize(const gchar* str, ssize_t len, GNormalizeMode mode) //returns nullptr on error; caller must g_free()
{
    assert(len >= 0);
    const std::string_view strView(str, len);

    bool normalized = true; //valid UTF-8 expected (getValidUtf())
    {
        UtfDecoder<char> decoder(strView.data(), strView.size());
        while (const std::optional<impl::CodePoint> cp = decoder.getNext())
            if (!isNormalizedCodePoint(*cp, mode))
            {
                normalized = false;
                break;
            }
    }

    std::string_view output = strView;
    std::vector<UInt8> buf;
    if (!normalized)
    {
        CFStringRef cfStr = ::CFStringCreateWithBytesNoCopy(nullptr, reinterpret_cast<const UInt8*>(strView.data()), static_cast<CFIndex>(strView.size()),
                                                            kCFStringEncodingUTF8, false /*isExternalRepresentation*/, kCFAllocatorNull /*don't free bytes*/);
        if (!cfStr)
            return nullptr;
        ZEN_ON_SCOPE_EXIT(::CFRelease(cfStr));

        CFMutableStringRef cfStrNorm = ::CFStringCreateMutableCopy(nullptr, 0 /*maxLength: unlimited*/, cfStr);
        if (!cfStrNorm)
            return nullptr;
        ZEN_ON_SCOPE_EXIT(::CFRelease(cfStrNorm));

        ::CFStringNormalize(cfStrNorm, mode == G_NORMALIZE_NFC ? kCFStringNormalizationFormC : kCFStringNormalizationFormD);

        const CFRange range = CFRangeMake(0, ::CFStringGetLength(cfStrNorm));
        CFIndex bufSize = 0;
        ::CFStringGetBytes(cfStrNorm, range, kCFStringEncodingUTF8, 0 /*lossByte: fail*/, false, nullptr, 0, &bufSize);
        buf.resize(bufSize);
        CFIndex bytesWritten = 0;
        if (::CFStringGetBytes(cfStrNorm, range, kCFStringEncodingUTF8, 0, false, buf.data(), bufSize, &bytesWritten) != range.length ||
            bytesWritten != bufSize)
            return nullptr;
        output = std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    gchar* outStr = static_cast<gchar*>(::malloc(output.size() + 1));
    if (!outStr)
        return nullptr;
    std::copy(output.begin(), output.end(), outStr);
    outStr[output.size()] = 0;
    return outStr;
}


gunichar toUpperCf(gunichar cp) //code points without a 1:1 upper-case mapping are returned unchanged
{
    UniChar utf16[2] = {};
    CFIndex utf16Len = 0;
    codePointToUtf<UniChar>(cp, [&](UniChar c) { if (utf16Len < 2) utf16[utf16Len++] = c; });

    CFMutableStringRef cfStr = ::CFStringCreateMutable(nullptr, 0);
    if (!cfStr)
        return cp;
    ZEN_ON_SCOPE_EXIT(::CFRelease(cfStr));
    ::CFStringAppendCharacters(cfStr, utf16, utf16Len);
    ::CFStringUppercase(cfStr, nullptr /*locale: none => locale-independent mapping*/);

    UniChar upper[3] = {};
    const CFIndex upperLen = ::CFStringGetLength(cfStr);
    if (upperLen > 2)
        return cp;
    ::CFStringGetCharacters(cfStr, CFRangeMake(0, upperLen), upper);

    std::optional<impl::CodePoint> upperCp;
    UtfDecoder<UniChar> decoder(upper, upperLen);
    if (std::optional<impl::CodePoint> cp1 = decoder.getNext())
        if (!decoder.getNext()) //exactly one code point
            upperCp = cp1;
    return upperCp ? *upperCp : cp;
}


gunichar g_unichar_toupper(gunichar cp)
{
    if (cp < 0x80)
        return asciiToUpper(static_cast<char>(cp));

    if (cp >= 0x10000)
        return toUpperCf(cp);

    static const std::vector<uint16_t> upperBmp = [] //thread-safe init
    {
        std::vector<uint16_t> table(0x10000);
        for (size_t i = 0; i < table.size(); ++i)
            table[i] = static_cast<uint16_t>(i);

        //one CFStringUppercase() for all of the BMP (except surrogates): characters separated by U+0000, which maps to itself
        std::vector<UniChar> chars;
        chars.reserve(2 * 0x10000);
        for (UniChar c = 0x80; c != 0; ++c)
            if (c < 0xD800 || c > 0xDFFF)
            {
                chars.push_back(c);
                chars.push_back(0);
            }

        CFMutableStringRef cfStr = ::CFStringCreateMutable(nullptr, 0);
        if (!cfStr)
            return table; //identity => compares case-sensitively for non-ASCII; never seen
        ZEN_ON_SCOPE_EXIT(::CFRelease(cfStr));
        ::CFStringAppendCharacters(cfStr, chars.data(), static_cast<CFIndex>(chars.size()));
        ::CFStringUppercase(cfStr, nullptr /*locale: none*/);

        std::vector<UniChar> upper(::CFStringGetLength(cfStr));
        ::CFStringGetCharacters(cfStr, CFRangeMake(0, upper.size()), upper.data());

        //"chars" and "upper" have the same number of separators: segment i of "upper" is the upper case of character i
        auto itUpper = upper.begin();
        for (auto itChar = chars.begin(); itChar != chars.end() && itUpper != upper.end(); itChar += 2)
        {
            const auto itSep = std::find(itUpper, upper.end(), 0);
            if (itSep - itUpper == 1 && (*itUpper < 0xD800 || *itUpper > 0xDFFF)) //1:1 mappings within the BMP only
                table[*itChar] = *itUpper;
            itUpper = itSep == upper.end() ? itSep : itSep + 1;
        }
        return table;
    }();
    return upperBmp[cp];
}


/*  fast path of compareNoCase()/equalNoCase() (e.g. compareNativePath()): if both strings are NFC already (quick check) and contain
    nothing getValidUtf() replaces, compare their upper case code point by code point, without any allocation. Same result as
    comparing getUpperCaseNonAscii() of both: memcmp() order of UTF-8 == code point order.                                       */
std::optional<std::weak_ordering> compareNoCaseNormalized(const Zstring& lhs, const Zstring& rhs)
{
    auto isNormalizedValidUtf = [](const Zstring& str)
    {
        UtfDecoder<char> decoder(str.c_str(), str.size());
        while (const std::optional<impl::CodePoint> cp = decoder.getNext())
            if (*cp == impl::REPLACEMENT_CHAR || //broken UTF-8 (or a literal U+FFFD: the slow path handles both)
                (0xfdd0 <= *cp && *cp <= 0xfdef) || *cp % 0x10'000 >= 0xfffe || //Unicode non-characters (see getValidUtf())
                !isNormalizedCodePoint(*cp, G_NORMALIZE_NFC))
                return false;
        return true;
    };
    if (!isNormalizedValidUtf(lhs) || !isNormalizedValidUtf(rhs))
        return std::nullopt;

    UtfDecoder<char> decL(lhs.c_str(), lhs.size());
    UtfDecoder<char> decR(rhs.c_str(), rhs.size());
    for (;;)
    {
        const std::optional<impl::CodePoint> cpL = decL.getNext();
        const std::optional<impl::CodePoint> cpR = decR.getNext();
        if (!cpL || !cpR)
            return !cpR <=> !cpL;

        if (const gunichar upL = g_unichar_toupper(*cpL), upR = g_unichar_toupper(*cpR);
            upL != upR)
            return upL <=> upR;
    }
}
}
#endif


namespace
{

Zstring getUnicodeNormalForm_NonAsciiValidUtf(const Zstring& str, UnicodeNormalForm form)
{
    //Example: const char* decomposed  = "\x6f\xcc\x81"; //ó
    //         const char* precomposed = "\xc3\xb3"; //ó
    assert(!isAsciiString(str)); //includes "not-empty" check
    assert(!contains(str, Zchar('\0'))); //don't expect embedded nulls!

    try
    {
        gchar* strNorm = ::g_utf8_normalize(str.c_str(), str.size(), form == UnicodeNormalForm::nfc ? G_NORMALIZE_NFC : G_NORMALIZE_NFD);
        if (!strNorm)
            throw SysError(formatSystemError("g_utf8_normalize", L"", L"Conversion failed."));
        ZEN_ON_SCOPE_EXIT(::g_free(strNorm));

        const std::string_view strNormView(strNorm, strSize(strNorm));

        if (equalString(str, strNormView)) //avoid extra memory allocation
            return str;

        return Zstring(strNormView);

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error normalizing string:" + '\n' +
                                 utfTo<std::string>(str)  + "\n\n" + utfTo<std::string>(e.toString()));
    }
}


Zstring getValidUtf(const Zstring& str)
{
    /*  1. do NOT fail on broken UTF encoding, instead normalize using REPLACEMENT_CHAR!
        2. NormalizeString() haateeez them Unicode non-characters: ERROR_NO_UNICODE_TRANSLATION! http://www.unicode.org/faq/private_use.html#nonchar1
         - No such issue on Linux/macOS with g_utf8_normalize(), and CFStringGetFileSystemRepresentation()
            -> still, probably good idea to "normalize" Unicode non-characters cross-platform
         - consistency for compareNoCase(): let's *unconditionally* check before other normalization operations, not just in error case!   */
    using impl::CodePoint;
    auto isUnicodeNonCharacter = [](CodePoint cp) { assert(cp <= impl::CODE_POINT_MAX); return (0xfdd0 <= cp && cp <= 0xfdef) || cp % 0x10'000 >= 0xfffe; };

    const bool invalidUtf = [&] //pre-check: avoid memory allocation if valid UTF
    {
        UtfDecoder<Zchar> decoder(str.c_str(), str.size());
        while (const std::optional<CodePoint> cp = decoder.getNext())
            if (*cp == impl::REPLACEMENT_CHAR || //marks broken UTF encoding
                isUnicodeNonCharacter(*cp))
                return true;
        return false;
    }();

    if (invalidUtf) //band-aid broken UTF encoding with REPLACEMENT_CHAR
    {
        Zstring validStr; //don't want extra memory allocations in the standard case (valid UTF)
        UtfDecoder<Zchar> decoder(str.c_str(), str.size());
        while (std::optional<CodePoint> cp = decoder.getNext())
        {
            if (isUnicodeNonCharacter(*cp))   //
                *cp = impl::REPLACEMENT_CHAR; //"normalize" Unicode non-characters

            codePointToUtf<Zchar>(*cp, [&](Zchar ch) { validStr += ch; });
        }
        return validStr;
    }
    else
        return str;
}


Zstring getUpperCaseNonAscii(const Zstring& str)
{
    assert(!isAsciiString(str));

    const Zstring& strValidUtf = getValidUtf(str);
    try
    {
        const Zstring strNorm = getUnicodeNormalForm_NonAsciiValidUtf(strValidUtf, UnicodeNormalForm::native);

        Zstring output;
        output.reserve(strNorm.size());

        UtfDecoder<char> decoder(strNorm.c_str(), strNorm.size());
        while (const std::optional<impl::CodePoint> cp = decoder.getNext())
            codePointToUtf<char>(::g_unichar_toupper(*cp), [&](const char c) { output += c; }); //don't use std::towupper: *incomplete* and locale-dependent!

        static_assert(sizeof(impl::CodePoint) == sizeof(gunichar));
        return output;

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error converting string to upper case:" + '\n' +
                                 utfTo<std::string>(str)  + "\n\n" + utfTo<std::string>(e.toString()));
    }
}
}


Zstring getUnicodeNormalForm(const Zstring& str, UnicodeNormalForm form)
{
    static_assert(std::is_same_v<decltype(str), const Zbase<Zchar>&>, "god bless our ref-counting! => save needless memory allocation!");

    if (isAsciiString(str)) //fast path: in the range of 3.5ns
        return str;

    return getUnicodeNormalForm_NonAsciiValidUtf(getValidUtf(str), form); //slow path
}


Zstring getUpperCase(const Zstring& str)
{
    return isAsciiString(str) ? //fast path: in the range of 3.5ns
           getAsciiUpperCase(str) :
           getUpperCaseNonAscii(str); //slow path
}


namespace
{
std::weak_ordering compareNoCaseUtf8(const char* lhs, size_t lhsLen, const char* rhs, size_t rhsLen)
{
    //expect Unicode normalized strings!
    assert(Zstring(lhs, lhsLen) == getUnicodeNormalForm(Zstring(lhs, lhsLen), UnicodeNormalForm::nfd));
    assert(Zstring(rhs, rhsLen) == getUnicodeNormalForm(Zstring(rhs, rhsLen), UnicodeNormalForm::nfd));

    //- strncasecmp implements ASCII CI-comparsion only! => signature is broken for UTF8-input; toupper() similarly doesn't support Unicode
    //- wcsncasecmp: https://opensource.apple.com/source/Libc/Libc-763.12/string/wcsncasecmp-fbsd.c
    // => re-implement comparison based on g_unichar_tolower() to avoid memory allocations

    UtfDecoder<char> decL(lhs, lhsLen);
    UtfDecoder<char> decR(rhs, rhsLen);
    for (;;)
    {
        const std::optional<impl::CodePoint> cpL = decL.getNext();
        const std::optional<impl::CodePoint> cpR = decR.getNext();
        if (!cpL || !cpR)
            return !cpR <=> !cpL;

        static_assert(sizeof(gunichar) == sizeof(impl::CodePoint));
        static_assert(std::is_unsigned_v<gunichar>, "unsigned char-comparison is the convention!");

        //ordering: "to lower" converts to higher code points than "to upper"
        const gunichar charL = ::g_unichar_toupper(*cpL); //note: tolower can be ambiguous, so don't use:
        const gunichar charR = ::g_unichar_toupper(*cpR); //e.g. "Σ" (upper case) can be lower-case "ς" in the end of the word or "σ" in the middle.
        if (charL != charR)
            return charL <=> charR;
    }
}
}


std::weak_ordering compareNatural(const Zstring& lhs, const Zstring& rhs)
{
    try
    {
        /* Unicode Normalization Forms:
              Windows: CompareString() ignores NFD/NFC differences and converts to NFD
              Linux:  g_unichar_toupper() can't ignore differences
              macOS:  CFStringCompare() considers differences */
        const Zstring& lhsNorm = getUnicodeNormalForm(lhs, UnicodeNormalForm::nfd); //normalize: - broken UTF encoding
        const Zstring& rhsNorm = getUnicodeNormalForm(rhs, UnicodeNormalForm::nfd); //           - Unicode non-characters

        const char* strL = lhsNorm.c_str();
        const char* strR = rhsNorm.c_str();

        const char* const strEndL = strL + lhsNorm.size();
        const char* const strEndR = strR + rhsNorm.size();
        /*  - compare strings after conceptually creating blocks of whitespace/numbers/text
            - implement strict weak ordering!
            - don't follow broken "strnatcasecmp": https://github.com/php/php-src/blob/master/ext/standard/strnatcmp.c
                    1. incorrect non-ASCII CI-comparison
                    2. incorrect bounds checks
                    3. incorrect trimming of *all* whitespace
                    4. arbitrary handling of leading 0 only at string begin
                    5. incorrect handling of whitespace following a number
                    6. code is a mess                                          */
        for (;;)
        {
            if (strL == strEndL || strR == strEndR)
                return (strL != strEndL) <=> (strR != strEndR); //"nothing" before "something"
            //note: "something" never would have been condensed to "nothing" further below => can finish evaluation here

            const bool wsL = isWhiteSpace(*strL);
            const bool wsR = isWhiteSpace(*strR);
            if (wsL != wsR)
                return !wsL <=> !wsR; //whitespace before non-ws!
            if (wsL)
            {
                ++strL, ++strR;
                while (strL != strEndL && isWhiteSpace(*strL)) ++strL;
                while (strR != strEndR && isWhiteSpace(*strR)) ++strR;
                continue;
            }

            const bool digitL = isDigit(*strL);
            const bool digitR = isDigit(*strR);
            if (digitL != digitR)
                return !digitL <=> !digitR; //numbers before chars!
            if (digitL)
            {
                while (strL != strEndL && *strL == '0') ++strL;
                while (strR != strEndR && *strR == '0') ++strR;

                int rv = 0;
                for (;; ++strL, ++strR)
                {
                    const bool endL = strL == strEndL || !isDigit(*strL);
                    const bool endR = strR == strEndR || !isDigit(*strR);
                    if (endL != endR)
                        return !endL <=> !endR; //more digits means bigger number
                    if (endL)
                        break; //same number of digits

                    if (rv == 0 && *strL != *strR)
                        rv = *strL - *strR; //found first digit difference comparing from left
                }
                if (rv != 0)
                    return rv <=> 0;
                continue;
            }

            //compare full junks of text: consider Unicode encoding!
            const char* textBeginL = strL++;
            const char* textBeginR = strR++; //current char is neither white space nor digit at this point!
            while (strL != strEndL && !isWhiteSpace(*strL) && !isDigit(*strL)) ++strL;
            while (strR != strEndR && !isWhiteSpace(*strR) && !isDigit(*strR)) ++strR;

            if (const std::weak_ordering cmp = compareNoCaseUtf8(textBeginL, strL - textBeginL, textBeginR, strR - textBeginR);
                cmp != std::weak_ordering::equivalent)
                return cmp;
        }

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error comparing strings:" + '\n' +
                                 utfTo<std::string>(lhs) + '\n' + utfTo<std::string>(rhs) + "\n\n" + utfTo<std::string>(e.toString()));
    }
}


std::weak_ordering compareNoCase(const Zstring& lhs, const Zstring& rhs)
{
    const bool isAsciiL = isAsciiString(lhs);
    const bool isAsciiR = isAsciiString(rhs);

    //fast path: no memory allocations => ~ 6x speedup
    if (isAsciiL && isAsciiR)
    {
        const size_t minSize = std::min(lhs.size(), rhs.size());
        for (size_t i = 0; i < minSize; ++i)
        {
            //ordering: do NOT call compareAsciiNoCase(), which uses asciiToLower()!
            const Zchar lUp = asciiToUpper(lhs[i]); //
            const Zchar rUp = asciiToUpper(rhs[i]); //no surprises: emulate getUpperCase() [verified!]
            if (lUp != rUp)                         //
                return lUp <=> rUp;                 //
        }
        return lhs.size() <=> rhs.size();
    }
    //--------------------------------------

    //can't we instead skip isAsciiString() and compare chars as long as isAsciiChar()?
    // => NOPE! e.g. decomposed Unicode! A seemingly single isAsciiChar() might be followed by a combining character!!!
#if defined ZEN_MAC
    if (const std::optional<std::weak_ordering> cmp = compareNoCaseNormalized(lhs, rhs)) //no allocations for most text
        return *cmp;
#endif

    return (isAsciiL ? getAsciiUpperCase(lhs) : getUpperCaseNonAscii(lhs)) <=>
           (isAsciiR ? getAsciiUpperCase(rhs) : getUpperCaseNonAscii(rhs));
}


bool equalNoCase(const Zstring& lhs, const Zstring& rhs)
{
    const bool isAsciiL = isAsciiString(lhs); //need *full* check first, see above comment regarding "decomposed Unicode"
    const bool isAsciiR = isAsciiString(rhs); //

    //fast-path: no extra memory allocations
    if (isAsciiL && isAsciiR)
    {
        if (lhs.size() != rhs.size())
            return false;

        for (size_t i = 0; i < lhs.size(); ++i)
            if (asciiToUpper(lhs[i]) !=
                asciiToUpper(rhs[i]))
                return false;
        return true;
    }

    //caveat: ASCII-char and non-ASCII Unicode *can* compare case-insensitive equal!!! e.g. i and ı https://freefilesync.org/forum/viewtopic.php?t=9718
#if defined ZEN_MAC
    if (const std::optional<std::weak_ordering> cmp = compareNoCaseNormalized(lhs, rhs)) //no allocations for most text
        return *cmp == std::weak_ordering::equivalent;
#endif
    return (isAsciiL ? getAsciiUpperCase(lhs) : getUpperCaseNonAscii(lhs)) ==
           (isAsciiR ? getAsciiUpperCase(rhs) : getUpperCaseNonAscii(rhs));
}
