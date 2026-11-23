// zen/file_access.h, zen/symlink_target.h

#include <zen/file_access.h>
#include <zen/file_traverser.h>
#include <zen/symlink_target.h>
#include "test.h"

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


namespace
{
//2024-02-29 13:37:42 UTC
const time_t testTime = 1709213862;

std::optional<time_t> getSymlinkModTime(const Zstring& linkPath) //through zen's traverser: std::filesystem has no lstat-like time
{
    std::optional<time_t> modTime;
    traverseFolder(*getParentFolderPath(linkPath), nullptr, nullptr, [&](const SymlinkInfo& si)
    {
        if (si.itemName == getItemName(linkPath))
            modTime = si.modTime;
    }); //throw FileError
    return modTime;
}
}


TEST_CASE(fileAccessCreateRemove)
{
    const Zstring dirPath  = appendPath(testDir, "dir");
    const Zstring filePath = appendPath(testDir, "file.txt");

    //directory
    CHECK_NO_THROW(createDirectory(dirPath));
    CHECK(fs::is_directory(toPath(dirPath)));
    CHECK_THROWS(ErrorTargetExisting, createDirectory(dirPath));
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, createDirectory(appendPath(testDir, nativeRelPath("missing/sub")))); //parent missing
    CHECK_THROWS(FileError, createDirectory(appendPath(testDir, "..."))); //documented in createDirectory(): "irregular" all-dots names are rejected
    CHECK(!pathExists(appendPath(testDir, "...")));

    //file
    writeFile(filePath, "content");
    CHECK_THROWS(ErrorTargetExisting, createDirectory(filePath)); //name used by a file
    CHECK_NO_THROW(removeFilePlain(filePath));
    CHECK(!pathExists(filePath));
    CHECK_THROWS(FileError, removeFilePlain(filePath)); //"ERROR if not existing"

    //non-empty directory
    writeFile(appendPath(dirPath, "child.txt"), "x");
    CHECK_THROWS(FileError, removeDirectoryPlain(dirPath));
    CHECK(pathExists(appendPath(dirPath, "child.txt")));
    removeFilePlain(appendPath(dirPath, "child.txt"));
    CHECK_NO_THROW(removeDirectoryPlain(dirPath));
    CHECK(!pathExists(dirPath));
    CHECK_THROWS(FileError, removeDirectoryPlain(dirPath)); //"ERROR if not existing"
}


TEST_CASE(fileAccessCreateRecursive)
{
    const Zstring deepPath = appendPath(testDir, nativeRelPath("a/b/c/d"));
    CHECK_NO_THROW(createDirectoryIfMissingRecursion(deepPath));
    CHECK(fs::is_directory(toPath(deepPath)));
    CHECK_NO_THROW(createDirectoryIfMissingRecursion(deepPath)); //already existing: no error
    CHECK_NO_THROW(createDirectoryIfMissingRecursion(testDir));

    //a path component is a file
    const Zstring filePath = appendPath(testDir, "file");
    writeFile(filePath, "x");
    CHECK_THROWS(FileError, createDirectoryIfMissingRecursion(appendPath(filePath, nativeRelPath("sub/sub2"))));
    CHECK_THROWS(FileError, createDirectoryIfMissingRecursion(filePath));
    CHECK_EQUAL(readFile(filePath), std::string("x"));
}


TEST_CASE(fileAccessRemoveRecursive)
{
    //tree with a symlink to a folder outside of it: removal must delete the link, not follow it
    const Zstring outsidePath = appendPath(testDir, "outside");
    createDirectory(outsidePath);
    writeFile(appendPath(outsidePath, "keep.txt"), "keep");

    const Zstring treePath = appendPath(testDir, "tree");
    createDirectoryIfMissingRecursion(appendPath(treePath, nativeRelPath("sub1/sub2/sub3")));
    createDirectory(appendPath(treePath, "empty"));
    writeFile(appendPath(treePath, "f1.txt"), "1");
    writeFile(appendPath(treePath, nativeRelPath("sub1/f2.txt")), "22");
    writeFile(appendPath(treePath, nativeRelPath("sub1/sub2/sub3/f3.txt")), "333");
    fs::create_directory_symlink(toPath(outsidePath), toPath(appendPath(treePath, nativeRelPath("sub1/linkToOutside"))));
    fs::create_symlink("missing-target", toPath(appendPath(treePath, "brokenLink")));

    CHECK_NO_THROW(removeDirectoryPlainRecursion(treePath));
    CHECK(!pathExists(treePath));
    CHECK(pathExists(appendPath(outsidePath, "keep.txt")));
    CHECK_THROWS(FileError, removeDirectoryPlainRecursion(treePath)); //"ERROR if not existing"

    //called on a symlink to a folder: removes the link only
    const Zstring linkPath = appendPath(testDir, "linkToOutside");
    fs::create_directory_symlink(toPath(outsidePath), toPath(linkPath));
    CHECK_NO_THROW(removeDirectoryPlainRecursion(linkPath));
    CHECK(!pathExists(linkPath));
    CHECK(pathExists(appendPath(outsidePath, "keep.txt")));

    //removeSymlinkPlain(): link only, also for a broken link
    fs::create_directory_symlink(toPath(outsidePath), toPath(linkPath));
    CHECK_NO_THROW(removeSymlinkPlain(linkPath));
    CHECK(!pathExists(linkPath));
    CHECK(pathExists(appendPath(outsidePath, "keep.txt")));
    const Zstring brokenPath = appendPath(testDir, "broken");
    fs::create_symlink("missing-target", toPath(brokenPath));
    CHECK_NO_THROW(removeSymlinkPlain(brokenPath));
    CHECK(!pathExists(brokenPath));
    CHECK_THROWS(FileError, removeSymlinkPlain(brokenPath)); //"ERROR if not existing"
}


TEST_CASE(fileAccessMove)
{
    const Zstring dir1 = appendPath(testDir, "dir1");
    const Zstring dir2 = appendPath(testDir, "dir2");
    createDirectory(dir1);
    createDirectory(dir2);

    //rename in the same folder
    const Zstring a = appendPath(dir1, "a.txt");
    const Zstring b = appendPath(dir1, "b.txt");
    writeFile(a, "content a");
    CHECK_NO_THROW(moveAndRenameItem(a, b, false /*replaceExisting*/));
    CHECK(!pathExists(a));
    CHECK_EQUAL(readFile(b), std::string("content a"));

    //move across folders
    const Zstring c = appendPath(dir2, "c.txt");
    CHECK_NO_THROW(moveAndRenameItem(b, c, false));
    CHECK(!pathExists(b));
    CHECK_EQUAL(readFile(c), std::string("content a"));

    //onto an existing target
    const Zstring d = appendPath(dir2, "d.txt");
    writeFile(d, "content d");
    CHECK_THROWS(ErrorTargetExisting, moveAndRenameItem(c, d, false));
    CHECK_EQUAL(readFile(c), std::string("content a")); //both unchanged
    CHECK_EQUAL(readFile(d), std::string("content d"));
    CHECK_NO_THROW(moveAndRenameItem(c, d, true /*replaceExisting*/));
    CHECK(!pathExists(c));
    CHECK_EQUAL(readFile(d), std::string("content a"));

    //case-only rename: the "existing target" is the source itself on case-insensitive file systems (macOS, Windows) => must still work
    const Zstring dUpper = appendPath(dir2, "D.txt");
    CHECK_NO_THROW(moveAndRenameItem(d, dUpper, false));
    CHECK_EQUAL(readFile(dUpper), std::string("content a"));
    std::vector<Zstring> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(toPath(dir2)))
        names.push_back(toZ(entry.path().filename()));
    CHECK(names.size() == 1 && names[0] == "D.txt");

    //folder with content
    writeFile(appendPath(dir1, "inner.txt"), "inner");
    const Zstring dir3 = appendPath(dir2, "dir3");
    CHECK_NO_THROW(moveAndRenameItem(dir1, dir3, false));
    CHECK(!pathExists(dir1));
    CHECK_EQUAL(readFile(appendPath(dir3, "inner.txt")), std::string("inner"));

    //folder onto an existing folder
    const Zstring dir4 = appendPath(testDir, "dir4");
    createDirectory(dir4);
    CHECK_THROWS(ErrorTargetExisting, moveAndRenameItem(dir3, dir4, false));
    CHECK(pathExists(dir3));

    //missing source
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, moveAndRenameItem(appendPath(testDir, "missing"), appendPath(testDir, "target"), false));
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, moveAndRenameItem(appendPath(testDir, "missing"), appendPath(testDir, "target"), true));
    CHECK(!pathExists(appendPath(testDir, "target")));

    //target folder missing
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, moveAndRenameItem(dUpper, appendPath(testDir, nativeRelPath("missing/x.txt")), false));
    CHECK(pathExists(dUpper));
    //ErrorMoveUnsupported (moving across volumes) is not tested: needs two file systems
}


TEST_CASE(fileAccessCopyFile)
{
    std::string content;
    for (int i = 0; i < 300'000; ++i)
        content += static_cast<char>(i * 7 + i / 251); //not a multiple of the block size
    const Zstring source = appendPath(testDir, "source.bin");
    const Zstring target = appendPath(testDir, "target.bin");
    writeFile(source, content);
    fs::last_write_time(toPath(source), fs::file_time_type(std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(testTime))));

    int64_t bytesNotified = 0;
    FileCopyResult result;
    CHECK_NO_THROW(result = copyNewFile(source, target, [&](int64_t bytesDelta) { bytesNotified += bytesDelta; }));
    CHECK_EQUAL(readFile(target), content);
    CHECK_EQUAL(result.fileSize, static_cast<uint64_t>(content.size()));
    CHECK_EQUAL(nativeFileTimeToTimeT(result.sourceModTime), testTime);
    CHECK(result.sourceFileIdx != result.targetFileIdx);
    CHECK(!result.errorModTime);
    CHECK_EQUAL(getModTime(target), testTime);
#if defined ZEN_LINUX || defined ZEN_MAC
    CHECK_EQUAL(bytesNotified, static_cast<int64_t>(content.size())); //plain file: (bytes read + bytes written) / 2
#else
    CHECK(bytesNotified > 0); //"accummulated delta != file size! consider ADS, sparse, compressed files"
#endif

    //empty file
    const Zstring emptySource = appendPath(testDir, "empty");
    writeFile(emptySource, "");
    CHECK_NO_THROW(result = copyNewFile(emptySource, appendPath(testDir, "empty copy"), nullptr));
    CHECK_EQUAL(result.fileSize, static_cast<uint64_t>(0));
    CHECK_EQUAL(readFile(appendPath(testDir, "empty copy")), std::string());

    //errors
    writeFile(target, "existing");
    CHECK_THROWS(ErrorTargetExisting, (void)copyNewFile(source, target, nullptr));
    CHECK_EQUAL(readFile(target), std::string("existing")); //not overwritten
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, (void)copyNewFile(appendPath(testDir, "missing"), appendPath(testDir, "new"), nullptr));
    CHECK(!pathExists(appendPath(testDir, "new")));
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, (void)copyNewFile(source, appendPath(testDir, nativeRelPath("missing/new")), nullptr));
    CHECK_THROWS(FileError, (void)copyNewFile(testDir, appendPath(testDir, "folder copy"), nullptr)); //source is a folder

    //callback exceptions are passed through
    CHECK_THROWS(std::runtime_error, (void)copyNewFile(source, appendPath(testDir, "aborted"), [](int64_t) { throw std::runtime_error("abort"); }));
}


TEST_CASE(fileAccessTimeAndSize)
{
    const Zstring filePath = appendPath(testDir, "file.txt");
    writeFile(filePath, std::string(12345, 'x'));

    CHECK_NO_THROW(setFileTime(filePath, testTime, ProcSymlink::follow));
    CHECK_EQUAL(getModTime(filePath), testTime);
    CHECK_NO_THROW(setFileTime(filePath, 0, ProcSymlink::follow)); //"modTime == 0 is correctly applied (no NOOP!)"
    CHECK_EQUAL(getModTime(filePath), static_cast<time_t>(0));
    CHECK_NO_THROW(setFileTime(filePath, testTime + 1, ProcSymlink::asLink)); //not a symlink: same as follow
    CHECK_EQUAL(getModTime(filePath), testTime + 1);

    //folder
    const Zstring dirPath = appendPath(testDir, "dir");
    createDirectory(dirPath);
    CHECK_NO_THROW(setFileTime(dirPath, testTime, ProcSymlink::follow));
    CHECK_EQUAL(getModTime(dirPath), testTime);

    //symlink: asLink changes the link, follow the target
    const Zstring linkPath = appendPath(testDir, "link");
    fs::create_symlink("file.txt", toPath(linkPath));
    CHECK_NO_THROW(setFileTime(linkPath, testTime - 100, ProcSymlink::asLink));
    CHECK_EQUAL(getSymlinkModTime(linkPath), std::optional<time_t>(testTime - 100));
    CHECK_EQUAL(getModTime(filePath), testTime + 1);
    CHECK_NO_THROW(setFileTime(linkPath, testTime + 200, ProcSymlink::follow));
    CHECK_EQUAL(getModTime(filePath), testTime + 200);
    CHECK_EQUAL(getSymlinkModTime(linkPath), std::optional<time_t>(testTime - 100));

    CHECK_THROWS(FileError, setFileTime(appendPath(testDir, "missing"), testTime, ProcSymlink::follow));

    //size
    CHECK_EQUAL(getFileSize(filePath), static_cast<uint64_t>(12345));
    CHECK_EQUAL(getFileSize(linkPath), static_cast<uint64_t>(12345)); //"symlink handling: follow"
    writeFile(filePath, "");
    CHECK_EQUAL(getFileSize(filePath), static_cast<uint64_t>(0));
    CHECK_THROWS(FileError, (void)getFileSize(appendPath(testDir, "missing")));
}


TEST_CASE(fileAccessItemType)
{
    const Zstring filePath = appendPath(testDir, "file");
    const Zstring dirPath  = appendPath(testDir, "dir");
    const Zstring fileLink = appendPath(testDir, "fileLink");
    const Zstring dirLink  = appendPath(testDir, "dirLink");
    const Zstring broken   = appendPath(testDir, "broken");
    const Zstring missing  = appendPath(testDir, "missing");
    writeFile(filePath, "x");
    createDirectory(dirPath);
    fs::create_symlink("file", toPath(fileLink));
    fs::create_directory_symlink("dir", toPath(dirLink));
    fs::create_symlink("missing", toPath(broken));

    CHECK_EQUAL(getItemType(filePath), ItemType::file);
    CHECK_EQUAL(getItemType(dirPath),  ItemType::folder);
    CHECK_EQUAL(getItemType(fileLink), ItemType::symlink);
    CHECK_EQUAL(getItemType(dirLink),  ItemType::symlink);
    CHECK_EQUAL(getItemType(broken),   ItemType::symlink);
    CHECK_THROWS(FileError, (void)getItemType(missing)); //"does not distinguish between error/not existing"

    CHECK_EQUAL(getItemTypeIfExists(filePath), std::optional<ItemType>(ItemType::file));
    CHECK_EQUAL(getItemTypeIfExists(dirPath),  std::optional<ItemType>(ItemType::folder));
    CHECK_EQUAL(getItemTypeIfExists(broken),   std::optional<ItemType>(ItemType::symlink));
    CHECK_EQUAL(getItemTypeIfExists(missing),  std::optional<ItemType>());
    CHECK_EQUAL(getItemTypeIfExists(appendPath(missing, nativeRelPath("sub/sub"))), std::optional<ItemType>()); //missing parents
    CHECK_THROWS(FileError, (void)getItemTypeIfExists(appendPath(filePath, "sub"))); //parent is a file: "The name is already used by another item"

    CHECK(itemExists(filePath));
    CHECK(itemExists(dirLink));
    CHECK(!itemExists(missing));
}


TEST_CASE(fileAccessSymlink)
{
    const Zstring targetPath = appendPath(testDir, "target.txt");
    const Zstring linkPath   = appendPath(testDir, "link");
    writeFile(targetPath, "target");
    fs::create_symlink("target.txt", toPath(linkPath)); //relative target

    CHECK_EQUAL(getSymlinkRawContent(linkPath).targetPath, Zstring("target.txt"));
    CHECK_EQUAL(getSymlinkResolvedPath(linkPath), targetPath);
    CHECK_THROWS(FileError, (void)getSymlinkRawContent(targetPath)); //not a symlink
    CHECK_THROWS(FileError, (void)getSymlinkRawContent(appendPath(testDir, "missing")));

    //absolute target with non-ASCII characters
    const Zstring dirPath = appendPath(testDir, "d\xC3\xA9j\xC3\xA0 vu"); //"déjà vu"
    createDirectory(dirPath);
    const Zstring absLink = appendPath(testDir, "absLink");
    fs::create_directory_symlink(toPath(dirPath), toPath(absLink));
    CHECK_EQUAL(getSymlinkRawContent(absLink).targetPath, dirPath);
    CHECK_EQUAL(getSymlinkResolvedPath(absLink), dirPath);

    //broken link: raw content is readable, the resolved path is not
    const Zstring brokenPath = appendPath(testDir, "broken");
    fs::create_symlink(toPath(nativeRelPath("missing/file")), toPath(brokenPath));
    CHECK_EQUAL(getSymlinkRawContent(brokenPath).targetPath, nativeRelPath("missing/file"));
    CHECK_THROWS(FileError, (void)getSymlinkResolvedPath(brokenPath));

    //copySymlink(): same raw content and modification time ("essential for syncing a symlink"), broken links included
    setFileTime(linkPath, testTime, ProcSymlink::asLink);
    const Zstring linkCopy = appendPath(testDir, "link copy");
    CHECK_NO_THROW(copySymlink(linkPath, linkCopy));
    CHECK_EQUAL(getItemType(linkCopy), ItemType::symlink);
    CHECK_EQUAL(getSymlinkRawContent(linkCopy).targetPath, Zstring("target.txt"));
    CHECK_EQUAL(getSymlinkModTime(linkCopy), std::optional<time_t>(testTime));
    CHECK_EQUAL(readFile(linkCopy), std::string("target"));

    const Zstring brokenCopy = appendPath(testDir, "broken copy");
    CHECK_NO_THROW(copySymlink(brokenPath, brokenCopy));
    CHECK_EQUAL(getSymlinkRawContent(brokenCopy).targetPath, nativeRelPath("missing/file"));

    CHECK_THROWS(FileError, copySymlink(linkPath, linkCopy)); //target existing
    CHECK_EQUAL(getSymlinkRawContent(linkCopy).targetPath, Zstring("target.txt"));
    CHECK_THROWS(FileError, copySymlink(targetPath, appendPath(testDir, "not a link copy"))); //source is not a symlink
    CHECK(!pathExists(appendPath(testDir, "not a link copy")));
}


TEST_CASE(fileAccessPermissions)
{
    bool permSupported = false;
    CHECK_NO_THROW(permSupported = supportsPermissions(testDir));
#if defined ZEN_LINUX || defined ZEN_MAC
    CHECK(permSupported); //POSIX permissions on every local file system
#endif
    if (!permSupported)
        return;

    using fs::perms;
    auto getPerms = [](const Zstring& itemPath) { return fs::status(toPath(itemPath)).permissions() & perms::mask; };

    const Zstring source = appendPath(testDir, "source");
    const Zstring target = appendPath(testDir, "target");
    writeFile(source, "s");
    writeFile(target, "t");

#if defined ZEN_LINUX || defined ZEN_MAC
    const perms permsSource = perms::owner_read | perms::owner_write | perms::group_read | perms::others_read | perms::others_exec; //0645
    const perms permsTarget = perms::owner_read | perms::owner_write;                                                              //0600
    fs::permissions(toPath(source), permsSource);
    fs::permissions(toPath(target), permsTarget);
    //chown() to the own user/group needs no admin rights
    CHECK_NO_THROW(copyItemPermissions(source, target, ProcSymlink::follow));
    CHECK(getPerms(target) == permsSource);

    //folders
    const Zstring sourceDir = appendPath(testDir, "sourceDir");
    const Zstring targetDir = appendPath(testDir, "targetDir");
    createDirectory(sourceDir);
    createDirectory(targetDir);
    const perms permsDir = perms::owner_all | perms::group_read | perms::group_exec; //0750
    fs::permissions(toPath(sourceDir), permsDir);
    CHECK_NO_THROW(copyItemPermissions(sourceDir, targetDir, ProcSymlink::follow));
    CHECK(getPerms(targetDir) == permsDir);
    copyDirectoryAttributes(sourceDir, targetDir); //throw FileError

    //symlinks with ProcSymlink::asLink: the files the links point to keep their permissions
    const Zstring target2 = appendPath(testDir, "target2");
    writeFile(target2, "t2");
    fs::permissions(toPath(target2), permsTarget);
    fs::create_symlink("source",  toPath(appendPath(testDir, "sourceLink")));
    fs::create_symlink("target2", toPath(appendPath(testDir, "targetLink")));
    CHECK_NO_THROW(copyItemPermissions(appendPath(testDir, "sourceLink"), appendPath(testDir, "targetLink"), ProcSymlink::asLink));
    CHECK(getPerms(target2) == permsTarget);
    CHECK(getPerms(source) == permsSource);
#else
    //Windows: ACLs, checked once the Windows implementation exists
    CHECK_NO_THROW(copyItemPermissions(source, target, ProcSymlink::follow));
#endif

    CHECK_THROWS(FileError, copyItemPermissions(appendPath(testDir, "missing"), target, ProcSymlink::follow));
    CHECK_THROWS(FileError, copyItemPermissions(source, appendPath(testDir, "missing"), ProcSymlink::follow));
}


TEST_CASE(fileAccessFreeSpaceAndTemp)
{
    int64_t freeSpace = 0;
    CHECK_NO_THROW(freeSpace = getFreeDiskSpace(testDir));
    if (freeSpace >= 0) //"returns < 0 if not available"
    {
        const auto available = static_cast<int64_t>(fs::space(toPath(testDir)).available);
        const int64_t tolerance = available / 20 + 100'000'000; //other processes may write meanwhile
        CHECK(freeSpace > 0);
        CHECK(std::abs(freeSpace - available) <= tolerance);

        //"folderPath does not need to exist (yet)"
        int64_t freeSpaceMissing = 0;
        CHECK_NO_THROW(freeSpaceMissing = getFreeDiskSpace(appendPath(testDir, nativeRelPath("not/yet/existing"))));
        CHECK(std::abs(freeSpaceMissing - freeSpace) <= tolerance);
    }

    Zstring tempPath;
    CHECK_NO_THROW(tempPath = getTempFolderPath());
    CHECK(!tempPath.empty());
    CHECK(fs::is_directory(toPath(tempPath)));
}
