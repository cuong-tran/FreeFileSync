// zen/zstring.h: comparison, case conversion, natural sort, Unicode normalization

#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#include <zen/zstring.h>
#include "test.h"

using namespace zen;
using namespace zentest;


namespace
{
const Zstring eAcuteNfc = "\xC3\xA9";   //é precomposed U+00E9
const Zstring eAcuteNfd = "e\xCC\x81";  //é decomposed: e + U+0301
const Zstring EAcuteNfc = "\xC3\x89";   //É U+00C9
const Zstring angstromNfc = "\xC3\x85"; //Å U+00C5
const Zstring angstromNfd = "A\xCC\x8A";
const Zstring nonBmp = "\xF0\x9F\x98\x80"; //U+1F600
}


TEST_CASE(zstringNormalization)
{
    CHECK_EQUAL(getUnicodeNormalForm(eAcuteNfd, UnicodeNormalForm::nfc), eAcuteNfc);
    CHECK_EQUAL(getUnicodeNormalForm(eAcuteNfc, UnicodeNormalForm::nfd), eAcuteNfd);
    CHECK_EQUAL(getUnicodeNormalForm(eAcuteNfc, UnicodeNormalForm::nfc), eAcuteNfc);
    CHECK_EQUAL(getUnicodeNormalForm(eAcuteNfd, UnicodeNormalForm::nfd), eAcuteNfd);
    CHECK_EQUAL(getUnicodeNormalForm(eAcuteNfd), eAcuteNfc); //native == nfc
    CHECK_EQUAL(getUnicodeNormalForm("caf" + eAcuteNfd + " " + angstromNfd, UnicodeNormalForm::nfc), "caf" + eAcuteNfc + " " + angstromNfc);
    CHECK_EQUAL(getUnicodeNormalForm("plain ascii"), Zstring("plain ascii"));
    CHECK_EQUAL(getUnicodeNormalForm(Zstring()), Zstring());
    CHECK_EQUAL(getUnicodeNormalForm(nonBmp, UnicodeNormalForm::nfd), nonBmp);

    //broken UTF-8 and Unicode non-characters are replaced by U+FFFD, not rejected
    const Zstring replacementChar = "\xEF\xBF\xBD";
    CHECK_EQUAL(getUnicodeNormalForm("a\xFF" "b"), "a" + replacementChar + "b");
    CHECK_EQUAL(getUnicodeNormalForm("a\xEF\xB7\x90" "b"), "a" + replacementChar + "b"); //U+FDD0 non-character

    //ZstringNorm: equal keys for canonically equivalent strings
    CHECK(ZstringNorm(eAcuteNfc) == ZstringNorm(eAcuteNfd));
    CHECK(ZstringNorm(Zstring("a")) != ZstringNorm(Zstring("A")));
    CHECK_EQUAL(std::hash<ZstringNorm>()(eAcuteNfc), std::hash<ZstringNorm>()(eAcuteNfd));
    std::set<ZstringNorm> normSet{ZstringNorm(eAcuteNfc), ZstringNorm(eAcuteNfd), ZstringNorm(Zstring("x"))};
    CHECK_EQUAL(normSet.size(), static_cast<size_t>(2));
}


TEST_CASE(zstringCase)
{
    CHECK_EQUAL(getUpperCase("abc XYZ 123 _-."), Zstring("ABC XYZ 123 _-."));
    CHECK_EQUAL(getUpperCase(eAcuteNfc), EAcuteNfc);
    CHECK_EQUAL(getUpperCase(eAcuteNfd), EAcuteNfc); //"output is Unicode-normalized"
    CHECK_EQUAL(getUpperCase("\xC3\xA4\xC3\xB6\xC3\xBC"), Zstring("\xC3\x84\xC3\x96\xC3\x9C")); //äöü => ÄÖÜ
    CHECK_EQUAL(getUpperCase("\xCF\x83\xCF\x82"), Zstring("\xCE\xA3\xCE\xA3")); //σς => ΣΣ
    CHECK_EQUAL(getUpperCase("\xD0\xB4\xD0\xB0"), Zstring("\xD0\x94\xD0\x90")); //Cyrillic да => ДА
    CHECK_EQUAL(getUpperCase(nonBmp), nonBmp);
    CHECK_EQUAL(getUpperCase(Zstring()), Zstring());
    //simple (1:1) case mapping per code point on all platforms (Linux: GLib, macOS: table from CoreFoundation)
    CHECK_EQUAL(getUpperCase("\xC3\x9F"), Zstring("\xC3\x9F")); //ß has no 1:1 upper case ("SS" is a full mapping)
    CHECK_EQUAL(getUpperCase("\xEF\xAC\x80"), Zstring("\xEF\xAC\x80")); //ligature ﬀ: likewise
    CHECK_EQUAL(getUpperCase("\xC7\x86"), Zstring("\xC7\x84")); //ǆ => Ǆ
    CHECK_EQUAL(getUpperCase("\xC3\xBF"), Zstring("\xC5\xB8")); //ÿ => Ÿ (outside Latin-1)
    CHECK_EQUAL(getUpperCase("\xF0\x90\x90\xA8"), Zstring("\xF0\x90\x90\x80")); //Deseret U+10428 => U+10400 (outside the BMP)
    CHECK_EQUAL(getUpperCase("\xE5\x86\x99\xE7\x9C\x9F"), Zstring("\xE5\x86\x99\xE7\x9C\x9F")); //CJK: no case
    CHECK(!equalNoCase("\xC3\x9F", "SS"));

    CHECK(equalNoCase("Hello World", "hELLO wORLD"));
    CHECK(!equalNoCase("Hello", "Hello "));
    CHECK(equalNoCase("caf" + eAcuteNfc, "CAF" + EAcuteNfc));
    CHECK(equalNoCase("caf" + eAcuteNfd, "CAF" + EAcuteNfc)); //normalization is ignored, too
    CHECK(!equalNoCase("cafe", "caf" + eAcuteNfc));
    CHECK(equalNoCase("\xCF\x83", "\xCF\x82")); //σ, ς: same upper case
    CHECK(equalNoCase(Zstring(), Zstring()));

    CHECK(compareNoCase("abc", "ABC") == std::weak_ordering::equivalent);
    CHECK(compareNoCase("abc", "ABD") < 0);
    CHECK(compareNoCase("B", "a") > 0);
    CHECK(compareNoCase("ab", "ABC") < 0);
    CHECK(compareNoCase(eAcuteNfd, EAcuteNfc) == std::weak_ordering::equivalent);
    //consistency of the ASCII fast path with the Unicode path: '_' sorts the same way in both
    CHECK((compareNoCase("a_", "aB") < 0) == (compareNoCase("a_" + eAcuteNfc, "aB" + eAcuteNfc) < 0));

    //compareNoCase()/equalNoCase() (macOS: allocation-free fast path for normalized text) agree with comparing getUpperCase()
    {
        const Zstring samples[] =
        {
            "", "a", "A", "ab", "aB_", "\xC3\x9F", "SS", eAcuteNfc, eAcuteNfd, EAcuteNfc, "caf" + eAcuteNfd, "CAF" + EAcuteNfc,
            "\xCF\x83", "\xCF\x82", "\xCE\xA3", "\xC7\x86", "\xC7\x84", "\xD0\xB4\xD0\xB0", "\xD0\x94\xD0\x90",
            "\xE5\x86\x99", nonBmp, "\xF0\x90\x90\xA8", "\xF0\x90\x90\x80", "x\xFFy", "x\xEF\xBF\xBDy", "\xEF\xB7\x90",
            angstromNfc, angstromNfd, "\xC4\xB1", "I", "i", "\xC4\xB0", //ı I i İ
        };
        for (const Zstring& lhs : samples)
            for (const Zstring& rhs : samples)
            {
                ++checkCount;
                const std::weak_ordering expected = getUpperCase(lhs) <=> getUpperCase(rhs);
                if (compareNoCase(lhs, rhs) != expected || equalNoCase(lhs, rhs) != (expected == std::weak_ordering::equivalent))
                    reportFailure(__FILE__, __LINE__, "compareNoCase()/equalNoCase() differ from getUpperCase() for " + toPrintable(lhs) + ", " + toPrintable(rhs));
            }
    }

    CHECK(ZstringNoCase(Zstring("Readme.TXT")) == ZstringNoCase(Zstring("README.txt")));
    CHECK_EQUAL(std::hash<ZstringNoCase>()(Zstring("abc")), std::hash<ZstringNoCase>()(Zstring("ABC")));
}


TEST_CASE(zstringNaturalSort)
{
    CHECK(compareNatural("file2", "file10") < 0);
    CHECK(compareNatural("file10", "file2") > 0);
    CHECK(compareNatural("file10", "file10") == std::weak_ordering::equivalent);
    CHECK(compareNatural("File", "file") == std::weak_ordering::equivalent); //case-insensitive
    CHECK(compareNatural("a", "B") < 0);
    CHECK(compareNatural("x01", "x1") == std::weak_ordering::equivalent); //leading zeros ignored
    CHECK(compareNatural("x 1", "x   1") == std::weak_ordering::equivalent); //whitespace blocks condensed
    CHECK(compareNatural("x 1", "x1") < 0);   //whitespace before non-whitespace
    CHECK(compareNatural("1abc", "abc") < 0); //numbers before text
    CHECK(compareNatural("", "a") < 0);       //"nothing" before "something"
    CHECK(compareNatural("caf" + eAcuteNfd, "CAF" + EAcuteNfc) == std::weak_ordering::equivalent);
    CHECK(compareNatural("99999999999999999999", "100000000000000000000") < 0); //longer than any integer type

    std::vector<Zstring> names{"img12.png", "img10.png", "IMG2.png", "img1.png", "img 3.png", "Img02a.png"};
    std::sort(names.begin(), names.end(), LessNaturalSort());
    const std::vector<Zstring> expected{"img 3.png", "img1.png", "IMG2.png", "Img02a.png", "img10.png", "img12.png"};
    CHECK(names == expected);
    if (names != expected)
        for (const Zstring& name : names)
            std::cerr << "    " << toPrintable(name) << '\n';
}


//sorting many non-ASCII names must stay fast (comparisons run millions of times in the file grid): fails only on gross regressions
TEST_CASE(zstringSortPerformance)
{
    const Zstring stems[] =
    {
        "file", "Report", "zebra",
        "\xC3\x84rger", "A\xCC\x88rger",         //Ärger NFC, NFD
        "caf\xC3\xA9", "cafe\xCC\x81",           //café NFC, NFD
        "\xD0\x94\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5", //Данные
        "\xE5\x86\x99\xE7\x9C\x9F",             //写真
        "\xF0\x9F\x98\x80 smile",                 //emoji
    };
    std::mt19937 rng(42); //deterministic
    std::vector<Zstring> names;
    for (int i = 0; i < 100'000; ++i)
        names.push_back(stems[rng() % std::size(stems)] + Zstr(' ') + numberTo<Zstring>(rng() % 10'000) + (rng() % 2 ? ".txt" : ".JPG"));

    auto timeSort = [&](const char* what, auto less)
    {
        std::vector<Zstring> sorted = names;
        const auto startTime = std::chrono::steady_clock::now();
        std::sort(sorted.begin(), sorted.end(), less);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
        std::cout << "   sort 100k mixed names, " << what << ": " << ms << " ms" << std::endl;
        CHECK(std::is_sorted(sorted.begin(), sorted.end(), less));
        CHECK(ms < 60'000); //gross regressions only: a few hundred ms are expected on Linux and macOS
    };
    timeSort("LessNaturalSort", LessNaturalSort());
    timeSort("LessNativePath", LessNativePath());
    timeSort("compareNoCase", [](const Zstring& lhs, const Zstring& rhs) { return compareNoCase(lhs, rhs) < 0; });
}
