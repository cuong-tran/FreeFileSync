// zen/zstring.h: comparison, case conversion, natural sort, Unicode normalization

#include <algorithm>
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
