// zen/legacy_compiler.h: fromChars()/toChars() (floating-point std::from_chars/to_chars, or the strtod_l() fallback where the
// C++ library lacks from_chars for double, e.g. Apple's libc++ before macOS 26)
//
// The fallback is tested on every platform: run.sh also builds this test with zen/legacy_compiler.cpp and -DZEN_TEST_FROM_CHARS_FALLBACK
// into a second binary (zen_test_fallback) and runs it; the expected values are those of std::from_chars (libstdc++ and libc++).

#include <clocale>
#include <cmath>
#include <zen/legacy_compiler.h>
#include <zen/string_tools.h>
#include "test.h"

using namespace zen;
using namespace zentest;


namespace
{
double fromCharsStr(const std::string& str) { return fromChars(str.data(), str.data() + str.size()); }

bool sameDouble(double a, double b) { return (std::isnan(a) && std::isnan(b)) || (a == b && std::signbit(a) == std::signbit(b)); }

void checkFromChars(const char* file, int line)
{
    struct Case { const char* input; double expected; };
    const Case cases[] =
    {
        {"0", 0}, {"-0", -0.0}, {"1", 1}, {"1.5", 1.5}, {"-1.5", -1.5}, {".5", 0.5}, {"5.", 5}, {"-.5", -0.5},
        {"1e5", 1e5}, {"1E5", 1e5}, {"1e-5", 1e-5}, {"00012", 12}, {"0.1", 0.1}, {"3.14159265358979323846", 3.14159265358979323846},
        {"1.7976931348623157e308", 1.7976931348623157e308}, {"2.2250738585072014e-308", 2.2250738585072014e-308},
        {"4.9e-324", 4.9e-324}, //smallest subnormal: in range
        {"123456789012345678901234567890", 123456789012345678901234567890.0},
        //trailing characters end the number (fromChars() ignores the end pointer)
        {"1 ", 1}, {"1.5x", 1.5}, {"1,5", 1}, {"1e", 1}, {"1e+", 1}, {"1.2.3", 1.2}, {"infx", INFINITY},
        //no hexadecimal floats with chars_format::general: "0" followed by "x..."
        {"0x10", 0}, {"-0x1p3", -0.0}, {"0X", 0},
        {"inf", INFINITY}, {"-inf", -INFINITY}, {"INF", INFINITY}, {"infinity", INFINITY}, {"Infinity", INFINITY},
        {"nan", NAN}, {"NaN", NAN}, {"nan(123)", NAN}, {"-nan", NAN},
        //errors => NAN
        {"", NAN}, {".", NAN}, {"+1", NAN}, {" 1", NAN}, {"--1", NAN}, {"-", NAN}, {"e5", NAN}, {"in", NAN}, {"n", NAN},
        {"1e400", NAN}, {"-1e400", NAN}, {"1.7976931348623159e308", NAN}, {"1e-400", NAN}, //out of range
    };
    for (const Case& c : cases)
    {
        ++checkCount;
        const double result = fromCharsStr(c.input);
        if (!sameDouble(result, c.expected))
            reportFailure(file, line, std::string("fromChars(\"") + c.input + "\") = " + numberTo<std::string>(result) +
                          ", expected " + numberTo<std::string>(c.expected));
    }
    CHECK_EQUAL(stringTo<double>("2.5"), 2.5); //string_tools.h goes through fromChars()
    CHECK_EQUAL(stringTo<double>(std::wstring(L"-0.25")), -0.25);

    //round trip with toChars()
    for (const double val : {0.1, 1.0 / 3, -123.456, 1e300, 5e-324, 6.02214076e23})
    {
        char buf[64] = {};
        const char* end = toChars(std::begin(buf), std::end(buf), val);
        CHECK(end != buf);
        CHECK_EQUAL(fromChars(buf, end), val);
    }
}
}


TEST_CASE(legacyCompilerFromChars)
{
#ifdef ZEN_FROM_CHARS_FALLBACK
    std::cout << "   using the strtod_l() fallback" << std::endl;
#endif
    checkFromChars(__FILE__, __LINE__);

    //locale-independent: a locale with ',' as decimal separator changes neither parsing nor formatting
    for (const char* localeName : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"})
        if (const char* oldLocale = std::setlocale(LC_ALL, nullptr))
        {
            const std::string oldLocaleCopy = oldLocale;
            if (!std::setlocale(LC_ALL, localeName))
                continue;
            checkFromChars(__FILE__, __LINE__);
            std::setlocale(LC_ALL, oldLocaleCopy.c_str());
            break;
        }
}
