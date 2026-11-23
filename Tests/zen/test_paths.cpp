// zen/file_path.h, zen/resolve_path.h: pure path helpers
//
// expects these environment variables, set by run.sh (zen reads the environment once at startup, so the test can't set them itself):
//   ZEN_TEST_VAR=zen test value
//   ZEN_TEST_EMPTY=     (set, but empty)

#include <zen/file_path.h>
#include <zen/resolve_path.h>
#include <zen/time.h>
#include <zen/sys_info.h>
#include "test.h"

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


namespace
{
#ifdef ZEN_WIN
const Zstring rootPath = "C:\\";
#else
const Zstring rootPath = "/";
#endif

Zstring absPath(const Zstring& relPath) { return rootPath + nativeRelPath(relPath); }
}


TEST_CASE(filePathHelpers)
{
    //item name, extension
    CHECK_EQUAL(getItemName(absPath("dir/file.txt")), Zstring("file.txt"));
    CHECK_EQUAL(getItemName(absPath("dir/sub")), Zstring("sub"));
    CHECK_EQUAL(getItemName(Zstring("relative")), Zstring("relative"));
    CHECK_EQUAL(getFileExtension(absPath("dir/archive.tar.gz")), Zstring("gz"));
    CHECK_EQUAL(getFileExtension(absPath("dir.d/noext")), Zstring());
    CHECK_EQUAL(getFileExtension(Zstring(".hidden")), Zstring("hidden"));

    //parent
    CHECK_EQUAL(getParentFolderPath(absPath("dir/sub/file.txt")), std::optional<Zstring>(absPath("dir/sub")));
    CHECK_EQUAL(getParentFolderPath(absPath("dir")), std::optional<Zstring>(rootPath));
    CHECK_EQUAL(getParentFolderPath(rootPath), std::optional<Zstring>());
    CHECK_EQUAL(getParentFolderPath(Zstring()), std::optional<Zstring>());

    //components
    const std::optional<PathComponents> pc = parsePathComponents(absPath("dir/sub/file.txt"));
    CHECK(pc.has_value());
    if (pc)
    {
        CHECK_EQUAL(pc->rootPath, rootPath);
        CHECK_EQUAL(pc->relPath, nativeRelPath("dir/sub/file.txt"));
    }
    const std::optional<PathComponents> pcRoot = parsePathComponents(rootPath);
    CHECK(pcRoot && pcRoot->rootPath == rootPath && pcRoot->relPath.empty());
    CHECK(!parsePathComponents(Zstring("relative/path")));
    CHECK(!parsePathComponents(Zstring()));

#ifdef ZEN_LINUX
    //Linux: mount points of removable/network devices are treated as volume roots
    const std::optional<PathComponents> pcMnt = parsePathComponents("/mnt/usbstick/folder/file");
    CHECK(pcMnt && pcMnt->rootPath == "/mnt/usbstick" && pcMnt->relPath == "folder/file");
    CHECK_EQUAL(getParentFolderPath("/mnt/usbstick/folder"), std::optional<Zstring>("/mnt/usbstick"));
    CHECK_EQUAL(getParentFolderPath("/mnt/usbstick"), std::optional<Zstring>());
    const std::optional<PathComponents> pcGvfs = parsePathComponents("/run/user/1000/gvfs/smb-share:server=host,share=data/folder");
    CHECK(pcGvfs && pcGvfs->rootPath == "/run/user/1000/gvfs/smb-share:server=host,share=data" && pcGvfs->relPath == "folder");
    const std::optional<PathComponents> pcRunUser = parsePathComponents("/run/user/notanumber/gvfs/x");
    CHECK(pcRunUser && pcRunUser->rootPath == "/");
#endif

    //append
    CHECK_EQUAL(appendPath(absPath("dir"), Zstring("file")), absPath("dir/file"));
    CHECK_EQUAL(appendPath(rootPath, Zstring("file")), absPath("file")); //root already ends with a separator
    CHECK_EQUAL(appendPath(absPath("dir"), nativeRelPath("a/b")), absPath("dir/a/b"));
    CHECK_EQUAL(appendPath(absPath("dir"), Zstring()), absPath("dir"));
    CHECK_EQUAL(appendPath(Zstring(), Zstring("rel")), Zstring("rel"));
    CHECK_EQUAL(appendSeparator(absPath("dir")), absPath("dir") + FILE_NAME_SEPARATOR);
    CHECK_EQUAL(appendSeparator(rootPath), rootPath);

    //relative path checks
    CHECK(isValidRelPath(Zstring()));
    CHECK(isValidRelPath(Zstring("file")));
    CHECK(isValidRelPath(nativeRelPath("a/b/c")));
    CHECK(!isValidRelPath(Zstring() + FILE_NAME_SEPARATOR + "a"));
    CHECK(!isValidRelPath(Zstring("a") + FILE_NAME_SEPARATOR));
    CHECK(!isValidRelPath(Zstring("a") + FILE_NAME_SEPARATOR + FILE_NAME_SEPARATOR + "b"));
    CHECK(!isValidRelPath(FILE_NAME_SEPARATOR == '/' ? "a\\b" : "a/b")); //the other separator

    //native path comparison
#ifdef ZEN_LINUX
    //Linux: byte-wise comparison
    CHECK(!equalNativePath("/dir/File", "/dir/file"));
    CHECK(!equalNativePath("/dir/\xC3\xA9", "/dir/e\xCC\x81")); //NFC vs NFD "é"
    CHECK(compareNativePath("/dir/A", "/dir/a") < 0);
#else
    //macOS: case and Unicode normalization ignored; Windows: case ignored
    CHECK(equalNativePath(absPath("dir/File"), absPath("dir/file")));
#endif
    CHECK(equalNativePath(absPath("dir/file"), absPath("dir/file")));
    CHECK(compareNativePath(absPath("dir/a"), absPath("dir/b")) < 0);
    CHECK(LessNativePath()(absPath("a"), absPath("b")));

    //environment
    CHECK_EQUAL(getEnvironmentVar("ZEN_TEST_VAR"), std::optional<Zstring>("zen test value")); //set by run.sh
    CHECK_EQUAL(getEnvironmentVar("ZEN_TEST_EMPTY"), std::optional<Zstring>(Zstring()));      //
    CHECK_EQUAL(getEnvironmentVar("ZEN_TEST_NOT_SET_ANYWHERE"), std::optional<Zstring>());
}


TEST_CASE(resolvePath)
{
    //macros: environment variables
    CHECK_EQUAL(expandMacros("a %ZEN_TEST_VAR% b"), Zstring("a zen test value b"));
    CHECK_EQUAL(expandMacros("%zen_test_var%"), Zstring("%zen_test_var%")); //environment variable names are case-sensitive
    CHECK_EQUAL(expandMacros("[%ZEN_TEST_EMPTY%]"), Zstring("[]"));
    CHECK_EQUAL(expandMacros("%ZEN_TEST_NOT_SET_ANYWHERE%"), Zstring("%ZEN_TEST_NOT_SET_ANYWHERE%"));
    CHECK_EQUAL(expandMacros("100% sure"), Zstring("100% sure"));
    CHECK_EQUAL(expandMacros("50% of %ZEN_TEST_VAR%"), Zstring("50% of zen test value")); //"% of " is not a macro
    CHECK_EQUAL(expandMacros("%ZEN_TEST_VAR%%ZEN_TEST_VAR%"), Zstring("zen test valuezen test value"));
    CHECK_EQUAL(expandMacros("no macros"), Zstring("no macros"));
    CHECK_EQUAL(expandMacros(""), Zstring());

    //macros: time (internal macros, case-insensitive)
    const Zstring year = expandMacros("%Year%");
    CHECK_EQUAL(year.size(), static_cast<size_t>(4));
    CHECK(stringTo<int>(year) >= 2024);
    CHECK_EQUAL(expandMacros("%YEAR%").size(), static_cast<size_t>(4));
    const Zstring date = expandMacros("%Date%");
    CHECK(date.size() == 10 && date[4] == '-' && date[7] == '-' && startsWith(date, year));
    CHECK_EQUAL(expandMacros("%Time%").size(), static_cast<size_t>(6)); //HHMMSS
    CHECK_EQUAL(expandMacros("%TimeStamp%").size(), static_cast<size_t>(17)); //"2012-05-15 131513"
    const int weekDay = stringTo<int>(expandMacros("%WeekDay%"));
    CHECK(1 <= weekDay && weekDay <= 7);
    const int week = stringTo<int>(expandMacros("%Week%"));
    CHECK(1 <= week && week <= 53);

    //getResolvedFilePath(): macros, trimming, trailing separator, "." segments
    const Zstring dirPath = appendPath(testDir, "dir");
    CHECK_EQUAL(getResolvedFilePath(dirPath), dirPath);
    CHECK_EQUAL(getResolvedFilePath("  " + dirPath + "  "), dirPath);
    CHECK_EQUAL(getResolvedFilePath(appendSeparator(dirPath)), dirPath);
    CHECK_EQUAL(getResolvedFilePath(testDir + FILE_NAME_SEPARATOR + '.' + FILE_NAME_SEPARATOR + "dir"), dirPath);
    CHECK_EQUAL(getResolvedFilePath(appendPath(testDir, "%ZEN_TEST_VAR%")), appendPath(testDir, "zen test value"));
    CHECK_EQUAL(getResolvedFilePath(rootPath), rootPath); //volume root keeps its separator
    CHECK_EQUAL(getResolvedFilePath(""), Zstring());

    //relative paths: against the working directory
    const Zstring cwd = toZ(fs::current_path());
    CHECK_EQUAL(getResolvedFilePath("relative"), appendPath(cwd, "relative"));
    CHECK_EQUAL(getResolvedFilePath(nativeRelPath("a/b")), appendPath(cwd, nativeRelPath("a/b")));

#if defined ZEN_LINUX || defined ZEN_MAC
    //'~' expands to the home folder
    const Zstring userHome = getUserHome(); //throw FileError; ignores $HOME when running as root
    CHECK_EQUAL(getResolvedFilePath("~"), userHome);
    CHECK_EQUAL(getResolvedFilePath("~/docs"), appendPath(userHome, "docs"));
    CHECK_EQUAL(getResolvedFilePath("~user"), appendPath(toZ(fs::current_path()), "~user")); //only "~" and "~/" are expanded

    const Zstring home = *getEnvironmentVar("HOME");
    //path phrase aliases: $HOME => %HOME%
    const std::vector<Zstring> aliases = getPathPhraseAliases(appendPath(home, "docs"));
    CHECK(std::find(aliases.begin(), aliases.end(), appendPath(home, "docs")) != aliases.end());
    CHECK(std::find(aliases.begin(), aliases.end(), "%HOME%/docs") != aliases.end());
#endif

    //makePathPhrase(): a trailing space must survive trimming
    CHECK_EQUAL(makePathPhrase(absPath("dir")), absPath("dir"));
    CHECK_EQUAL(makePathPhrase(absPath("dir ")), absPath("dir ") + FILE_NAME_SEPARATOR);
    CHECK_EQUAL(getResolvedFilePath(makePathPhrase(absPath("dir "))), absPath("dir "));
}
