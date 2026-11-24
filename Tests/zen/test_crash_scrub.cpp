// zen/crash_scrub.h: removing paths, user and host names from crash report text

#include <chrono>
#include <zen/crash_scrub.h>
#include "test.h"

using namespace zen;
using namespace zentest;


TEST_CASE(crashScrubPosixPaths)
{
    CHECK_EQUAL(scrubPaths("Cannot open file \"/home/john/Documents/report.pdf\"."), std::string("Cannot open file \"<path>.pdf\"."));
    CHECK_EQUAL(scrubPaths("Cannot open /home/john/a b.txt"), std::string("Cannot open <path>.txt")); //unquoted: on over a word with an extension
    CHECK_EQUAL(scrubPaths("/usr/lib/libc.so.6 crashed"), std::string("<path>.6 crashed"));
    CHECK_EQUAL(scrubPaths("folder /tmp/x, then /var/log."), std::string("folder <path>, then <path>."));
    CHECK_EQUAL(scrubPaths("(/home/john/dir)"), std::string("(<path>)"));
    CHECK_EQUAL(scrubPaths("path=/srv/data"), std::string("path=<path>"));
    CHECK_EQUAL(scrubPaths("~/Sync/file.ffs_gui is missing"), std::string("<path>.ffs_gui is missing"));
    CHECK_EQUAL(scrubPaths("network //server/share"), std::string("network <path>"));
    //macOS
    CHECK_EQUAL(scrubPaths("\"/Users/john/Library/Application Support/FreeFileSync/GlobalSettings.xml\""), std::string("\"<path>.xml\""));
    CHECK_EQUAL(scrubPaths("/Volumes/USB Stick/x"), std::string("<path>")); //unquoted, with a space: on while the next word has a separator
}


TEST_CASE(crashScrubWindowsPaths)
{
    CHECK_EQUAL(scrubPaths("Cannot read C:\\Users\\john\\file.txt"), std::string("Cannot read <path>.txt"));
    CHECK_EQUAL(scrubPaths("D:/Backup/x.zip done"), std::string("<path>.zip done"));
    CHECK_EQUAL(scrubPaths("\"C:\\My Files\\Tax 2024.xlsx\""), std::string("\"<path>.xlsx\""));
    //unquoted Windows path with spaces: continues while the next word contains a backslash
    CHECK_EQUAL(scrubPaths("in C:\\Program Files\\FreeFileSync\\FreeFileSync.exe at"), std::string("in <path>.exe at"));
    //UNC and long paths
    CHECK_EQUAL(scrubPaths("\\\\fileserver\\share\\data.db failed"), std::string("<path>.db failed"));
    CHECK_EQUAL(scrubPaths("\\\\?\\C:\\very\\long\\path"), std::string("<path>"));
    CHECK_EQUAL(scrubPaths("\\\\?\\UNC\\server\\share"), std::string("<path>"));
}


TEST_CASE(crashScrubPhrasesAndUrls)
{
    CHECK_EQUAL(scrubPaths("sftp://john:secret@example.com:22/home/john/x.txt"), std::string("<path>.txt"));
    CHECK_EQUAL(scrubPaths("server ftp://anonymous@ftp.example.org/pub"), std::string("server <path>"));
    CHECK_EQUAL(scrubPaths("https://user:pa55@host.example/api?key=1 failed"), std::string("<path> failed"));
    CHECK_EQUAL(scrubPaths("gdrive:/john@gmail.com:Shared/folder/file.doc"), std::string("<path>.doc"));
    CHECK_EQUAL(scrubPaths("mtp:\\Phone\\DCIM\\IMG_1.jpg"), std::string("<path>.jpg"));
    CHECK_EQUAL(scrubPaths("zip:/home/john/a.zip|folder"), std::string("<path>"));
    CHECK_EQUAL(scrubPaths("file:///etc/passwd"), std::string("<path>"));
    CHECK_EQUAL(scrubPaths("\xE2\x80\x9C" "sftp://host/My Folder" "\xE2\x80\x9D"), std::string("\xE2\x80\x9C" "<path>" "\xE2\x80\x9D")); //typographic quotes
}


TEST_CASE(crashScrubSensitiveWords)
{
    const std::vector<std::string> words{"john", "build-host-7"};
    CHECK_EQUAL(scrubPaths("user john on build-host-7", words), std::string("user <redacted> on <redacted>"));
    CHECK_EQUAL(scrubPaths("JOHN said", words), std::string("<redacted> said"));
    CHECK_EQUAL(scrubPaths("johnny and ajohn", words), std::string("johnny and ajohn")); //whole words only
    CHECK_EQUAL(scrubPaths("pc and me", {"pc", "me", ""}), std::string("pc and me"));   //too short to replace
    CHECK_EQUAL(scrubPaths("/home/john/x and john", words), std::string("<path> and <redacted>"));
}


TEST_CASE(crashScrubTextWithoutPaths)
{
    for (const std::string text :
         {
             "",
             "Internal error: unexpected state 42",
             "and/or 1/2 of the items, 3 / 4 of the rest",
             "ratio 50%/s, a:b, std::vector<int>",
             "2024/05/06 12:30:00",
             "It's done: \"quoted text\" and 'single'",
             "URL-like but no scheme: example.com/path",
             "\xC3\xA9t\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D", //UTF-8
             "lone / slash and ~ tilde",
         })
        CHECK_EQUAL(scrubPaths(text), text);
}


TEST_CASE(crashScrubFileName)
{
    CHECK_EQUAL(std::string(getPathFileName("/usr/lib/libc.so.6")), std::string("libc.so.6"));
    CHECK_EQUAL(std::string(getPathFileName("C:\\Program Files\\FreeFileSync\\FreeFileSync.exe")), std::string("FreeFileSync.exe"));
    CHECK_EQUAL(std::string(getPathFileName("/opt/FreeFileSync/Bin/")), std::string("Bin"));
    CHECK_EQUAL(std::string(getPathFileName("name")), std::string("name"));
    CHECK_EQUAL(std::string(getPathFileName("")), std::string(""));
}


TEST_CASE(crashScrubAdversarial)
{
    //unterminated quotes: the rest of the line is scrubbed
    CHECK_EQUAL(scrubPaths("Cannot open \"/home/john/secret file"), std::string("Cannot open \"<path>"));
    CHECK_EQUAL(scrubPaths("open '/home/john/secret file\nnext line"), std::string("open '<path>\nnext line"));
    CHECK_EQUAL(scrubPaths("\xE2\x80\x9C" "C:\\Users\\john\\a b"), std::string("\xE2\x80\x9C" "<path>"));
    //apostrophes inside names, and as quotes
    CHECK_EQUAL(scrubPaths("/home/john/John's Files/tax.txt"), std::string("<path>.txt"));
    CHECK_EQUAL(scrubPaths("Cannot open '/home/john/x y.txt'."), std::string("Cannot open '<path>.txt'."));
    CHECK_EQUAL(scrubPaths("C:\\Users\\O'Brien\\My Documents\\a.doc failed"), std::string("<path>.doc failed"));
    //quotes, colons and brackets are boundaries
    CHECK_EQUAL(scrubPaths("path:/home/john/x"), std::string("path:<path>"));
    CHECK_EQUAL(scrubPaths("x\"/home/john/x\""), std::string("x\"<path>\""));
    CHECK_EQUAL(scrubPaths("(see \"/a b/c.txt\")"), std::string("(see \"<path>.txt\")"));
    CHECK_EQUAL(scrubPaths("(C:\\x\\y.doc)."), std::string("(<path>.doc)."));
    CHECK_EQUAL(scrubPaths("(/srv/a) and [/srv/b]"), std::string("(<path>) and [<path>]"));
    CHECK_EQUAL(scrubPaths("[/srv/a],{/srv/b}"), std::string("[<path>")); //one token: "]" and "}" may be part of a name, so all of it goes
    //trailing punctuation, start and end of text
    CHECK_EQUAL(scrubPaths("/tmp/a.txt!"), std::string("<path>.txt!"));
    CHECK_EQUAL(scrubPaths("\"/tmp/a.txt\"."), std::string("\"<path>.txt\"."));
    CHECK_EQUAL(scrubPaths("/a/b"), std::string("<path>"));
    CHECK_EQUAL(scrubPaths("x /a/b"), std::string("x <path>"));
    CHECK_EQUAL(scrubPaths("\""), std::string("\""));
    CHECK_EQUAL(scrubPaths("/"), std::string("/"));
    CHECK_EQUAL(scrubPaths("'"), std::string("'"));
    //non-ASCII names
    CHECK_EQUAL(scrubPaths("/home/j\xC3\xB6hn/\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt"), std::string("<path>.txt"));
    CHECK_EQUAL(scrubPaths("C:\\Users\\J\xC3\xBCrgen\\Docs ok"), std::string("<path> ok"));
    //a path per line
    CHECK_EQUAL(scrubPaths("/a/b\n/c/d e"), std::string("<path>\n<path> e"));
}


TEST_CASE(crashScrubLongInput)
{
    //linear time: inputs that would make a naive scan quadratic finish quickly, and a long path is one placeholder
    const auto start = std::chrono::steady_clock::now();

    CHECK_EQUAL(scrubPaths(std::string(1'000'000, '"')), std::string(1'000'000, '"'));
    std::string openQuotes;
    for (int i = 0; i < 200'000; ++i)
        openQuotes += "\xE2\x80\x9C" "x";
    CHECK_EQUAL(scrubPaths(openQuotes), openQuotes);
    std::string words;
    for (int i = 0; i < 200'000; ++i)
        words += "(a ";
    CHECK_EQUAL(scrubPaths(words), words);
    CHECK_EQUAL(scrubPaths("/" + std::string(1'000'000, 'a')), std::string("<path>"));
    std::string longPath = "\"";
    for (int i = 0; i < 100'000; ++i)
        longPath += "/dir with space";
    CHECK_EQUAL(scrubPaths(longPath), std::string("\"<path>"));
    CHECK_EQUAL(scrubPaths(std::string(500'000, 'x'), {"xxx"}), std::string(500'000, 'x')); //no whole word

    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 5);
}
