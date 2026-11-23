// zen/file_traverser.h

#include <map>
#include <zen/file_traverser.h>
#include <zen/file_access.h>
#include "test.h"

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


namespace
{
enum class Kind { file, folder, symlink };

struct Entry
{
    Kind kind;
    uint64_t fileSize = 0;
    time_t modTime = 0;

    bool operator==(const Entry&) const = default;
};

//names as returned by the file system
Zstring normalizeName(const Zstring& name)
{
#ifdef ZEN_MAC
    //macOS file systems may change the Unicode normalization of names (HFS+: NFD); compare normalized
    return getUnicodeNormalForm(name);
#else
    return name; //Linux, Windows: names are returned byte-for-byte (code unit for code unit) as created
#endif
}

//recursive traversal of "dirPath", keyed by relative path with '/' as separator
void traverseRecursive(const Zstring& dirPath, const Zstring& relPath, std::map<Zstring, Entry>& entries) //throw FileError
{
    auto rel = [&](const Zstring& itemName) { return (relPath.empty() ? Zstring() : relPath + Zstr('/')) + normalizeName(itemName); };
    std::vector<std::pair<Zstring, Zstring>> subFolders;

    traverseFolder(dirPath,
    [&](const FileInfo& fi)
    {
        CHECK_EQUAL(fi.fullPath, appendPath(dirPath, fi.itemName));
        CHECK(entries.emplace(rel(fi.itemName), Entry{Kind::file, fi.fileSize, fi.modTime}).second);
    },
    [&](const FolderInfo& fi)
    {
        CHECK_EQUAL(fi.fullPath, appendPath(dirPath, fi.itemName));
        CHECK(entries.emplace(rel(fi.itemName), Entry{Kind::folder}).second);
        subFolders.emplace_back(fi.fullPath, rel(fi.itemName));
    },
    [&](const SymlinkInfo& si)
    {
        CHECK_EQUAL(si.fullPath, appendPath(dirPath, si.itemName));
        CHECK(entries.emplace(rel(si.itemName), Entry{Kind::symlink, 0, si.modTime}).second);
    }); //throw FileError

    for (const auto& [subPath, subRelPath] : subFolders)
        traverseRecursive(subPath, subRelPath, entries);
}
}


TEST_CASE(fileTraverser)
{
    const time_t time1 = 1709213862; //2024-02-29 13:37:42 UTC
    const time_t time2 = 946684800;  //2000-01-01 00:00:00 UTC

    const Zstring nameCombining = "e\xCC\x81t\xC3\xA9"; //"été": first "é" decomposed (e + U+0301 COMBINING ACUTE ACCENT), second precomposed
    const Zstring nameNonBmp    = "smile \xF0\x9F\x98\x80.txt"; //U+1F600 GRINNING FACE: a surrogate pair in UTF-16
    const Zstring nameCjk       = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"; //"日本語"
    const Zstring nameLong      = Zstring(251, 'L') + ".txt"; //255 characters: the usual maximum name length
    const Zstring nameSpaces    = " leading and trailing space ";
    const Zstring nameDot       = ".hidden";

    std::map<Zstring, Entry> expected;
    auto addFile = [&](const Zstring& relPath, size_t size, time_t modTime) //throw FileError
    {
        const Zstring filePath = appendPath(testDir, nativeRelPath(relPath));
        writeFile(filePath, std::string(size, 'x'));
        setFileTime(filePath, modTime, ProcSymlink::follow); //throw FileError
        expected[normalizeName(relPath)] = {Kind::file, size, modTime};
    };
    auto addFolder = [&](const Zstring& relPath) //throw FileError
    {
        createDirectory(appendPath(testDir, nativeRelPath(relPath))); //throw FileError
        expected[normalizeName(relPath)] = {Kind::folder};
    };
    auto addSymlink = [&](const Zstring& relPath, const Zstring& target, bool isDir) //throw FileError
    {
        const Zstring linkPath = appendPath(testDir, nativeRelPath(relPath));
        if (isDir)
            fs::create_directory_symlink(toPath(target), toPath(linkPath));
        else
            fs::create_symlink(toPath(target), toPath(linkPath));
        setFileTime(linkPath, time2, ProcSymlink::asLink); //throw FileError
        expected[normalizeName(relPath)] = {Kind::symlink, 0, time2};
    };

    addFile("plain.txt", 10, time1);
    addFile("empty", 0, time2);
    addFile(nameDot, 3, time1);
    addFile(nameLong, 255, time1);
    addFile(nameSpaces, 1, time1);
    addFolder("sub");
    addFile("sub/" + nameNonBmp, 123'456, time2);
    addFolder("sub/" + nameCombining);
    addFile("sub/" + nameCombining + '/' + nameCjk, 7, time1);
    addFolder("sub/" + nameCombining + "/empty folder");
    addFolder(nameCjk);
    addSymlink("fileLink", "plain.txt", false);
    addSymlink("sub/folderLink", toZ(fs::path("..") / "sub"), true); //not followed: would recurse forever
    addSymlink("brokenLink", "does not exist", false);

    std::map<Zstring, Entry> found;
    CHECK_NO_THROW(traverseRecursive(testDir, Zstring(), found));

    CHECK_EQUAL(found.size(), expected.size());
    for (const auto& [relPath, entry] : expected)
    {
        const auto it = found.find(relPath);
        if (it == found.end())
            reportFailure(__FILE__, __LINE__, "not reported: " + toPrintable(relPath));
        else if (!(it->second == entry))
            reportFailure(__FILE__, __LINE__, "wrong details for " + toPrintable(relPath) + ": kind " + toPrintable(it->second.kind) +
                          ", size " + std::to_string(it->second.fileSize) + ", time " + std::to_string(it->second.modTime));
    }
    for (const auto& [relPath, entry] : found)
        if (!expected.contains(relPath))
            reportFailure(__FILE__, __LINE__, "unexpected item: " + toPrintable(relPath));

#ifndef ZEN_MAC
    //the combining sequence is not normalized away: both forms can exist side by side
    const Zstring nameNfc = getUnicodeNormalForm(nameCombining, UnicodeNormalForm::nfc);
    CHECK(nameNfc != nameCombining);
    CHECK_NO_THROW(createDirectory(appendPath(testDir, nativeRelPath("sub/" + nameNfc))));
    std::vector<Zstring> subNames;
    traverseFolder(appendPath(testDir, "sub"), nullptr, [&](const FolderInfo& fi) { subNames.push_back(fi.itemName); }, nullptr);
    CHECK_EQUAL(subNames.size(), static_cast<size_t>(2));
#endif

    //optional callbacks, errors
    CHECK_NO_THROW(traverseFolder(testDir, nullptr, nullptr, nullptr));
    CHECK_THROWS(FileError, traverseFolder(appendPath(testDir, "missing"), nullptr, nullptr, nullptr));
    CHECK_THROWS(FileError, traverseFolder(appendPath(testDir, "plain.txt"), nullptr, nullptr, nullptr));

    //an empty folder reports nothing
    int count = 0;
    traverseFolder(appendPath(testDir, nativeRelPath("sub/" + nameCombining + "/empty folder")),
    [&](const FileInfo&) { ++count; }, [&](const FolderInfo&) { ++count; }, [&](const SymlinkInfo&) { ++count; });
    CHECK_EQUAL(count, 0);
}
