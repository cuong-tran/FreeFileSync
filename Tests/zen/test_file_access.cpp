// zen/file_access.h, zen/symlink_target.h

#include <zen/file_access.h>
#include <zen/file_traverser.h>
#include <zen/symlink_target.h>
#include <zen/process_exec.h>
#include <zen/extra_log.h>
#include "test.h"

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


namespace
{
//errors zen reports through logExtraError() (e.g. extended attributes it couldn't copy); tests fetch them with fetchExtraLog(),
//anything left is printed at exit (and the log must be initialized before it is destroyed)
const bool extraLogInit = []
{
    initExtraLog([](const ErrorLog& log)
    {
        for (const LogEntry& entry : log)
            std::cerr << "unfetched extra log entry: " << std::string(entry.message.begin(), entry.message.end()) << '\n';
    });
    return true;
}();

//2024-02-29 13:37:42 UTC
const time_t testTime = 1709213862;

std::optional<time_t> getSymlinkModTime(const Zstring& linkPath) //throw FileError
{
    //through zen's traverser: std::filesystem has no lstat-like time
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
#ifdef ZEN_MAC
    //macOS: symlinks have permissions of their own (lchmod)
    auto getLinkPerms = [](const Zstring& itemPath) { return fs::symlink_status(toPath(itemPath)).permissions() & perms::mask; };
    fs::permissions(toPath(appendPath(testDir, "sourceLink")), perms::owner_all, fs::perm_options::replace | fs::perm_options::nofollow);
    CHECK_NO_THROW(copyItemPermissions(appendPath(testDir, "sourceLink"), appendPath(testDir, "targetLink"), ProcSymlink::asLink));
    CHECK(getLinkPerms(appendPath(testDir, "targetLink")) == perms::owner_all);
    CHECK(getPerms(target2) == permsTarget);
#endif
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


#ifdef ZEN_MAC
namespace
{
std::string runShell(const Zstring& cmdLine) //throw SysError: output of a command that must succeed
{
    const auto [exitCode, output] = consoleExecute(cmdLine, std::nullopt); //throw SysError, SysErrorTimeOut
    if (exitCode != 0)
        throw SysError(L"exit code " + numberTo<std::wstring>(exitCode) + L": " + utfTo<std::wstring>(cmdLine) + L"\n" + utfTo<std::wstring>(output));
    return utfTo<std::string>(output);
}
}


//macOS: extended attributes and ACLs, checked with the system tools (xattr, chmod +a, ls -le)
TEST_CASE(fileAccessMacMetadata)
{
    const Zstring source = appendPath(testDir, "source");
    const Zstring target = appendPath(testDir, "target");
    writeFile(source, "content");

    //copyNewFile(): extended attributes come along; the byte count is the file content only
    runShell("xattr -w com.freefilesync.zentest 'zen value' " + escapeCommandArg(source)); //throw SysError
    int64_t bytesNotified = 0;
    CHECK_NO_THROW((void)copyNewFile(source, target, [&](int64_t bytesDelta) { bytesNotified += bytesDelta; }));
    CHECK_EQUAL(bytesNotified, static_cast<int64_t>(7));
    CHECK_EQUAL(trimCpy(runShell("xattr -p com.freefilesync.zentest " + escapeCommandArg(target))), std::string("zen value"));

    //copyDirectoryAttributes(): extended attributes of folders
    const Zstring sourceDir = appendPath(testDir, "sourceDir");
    const Zstring targetDir = appendPath(testDir, "targetDir");
    createDirectory(sourceDir);
    createDirectory(targetDir);
    runShell("xattr -w com.freefilesync.zentest 'folder value' " + escapeCommandArg(sourceDir));
    CHECK_NO_THROW(copyDirectoryAttributes(sourceDir, targetDir));
    CHECK_EQUAL(trimCpy(runShell("xattr -p com.freefilesync.zentest " + escapeCommandArg(targetDir))), std::string("folder value"));

    //copyItemPermissions(): ACL, owner and mode; "allow" entries only, so the test folder stays deletable
    auto getAcl = [](const Zstring& itemPath) //throw SysError: the ACL lines of "ls -led", e.g. " 0: group:everyone allow read"
    {
        std::vector<std::string> aclLines;
        const std::string output = runShell("ls -led " + escapeCommandArg(itemPath));
        split(output, '\n', [&](std::string_view line)
        {
            line = trimCpy(line);
            if (!line.empty() && isDigit(line[0]) && contains(line, ": "))
                aclLines.emplace_back(line);
        });
        return aclLines;
    };
    auto getMode = [](const Zstring& itemPath) { return fs::symlink_status(toPath(itemPath)).permissions() & fs::perms::mask; };
    using fs::perms;

    //read-only file: the ACL is copied before the mode, so the target doesn't need to be writable and stays 0444
    const Zstring aclSource = appendPath(testDir, "aclSource");
    const Zstring aclTarget = appendPath(testDir, "aclTarget");
    writeFile(aclSource, "s");
    writeFile(aclTarget, "t");
    runShell("chmod +a 'everyone allow read' " + escapeCommandArg(aclSource));
    fs::permissions(toPath(aclSource), perms::owner_read | perms::group_read | perms::others_read); //0444
    fs::permissions(toPath(aclTarget), perms::owner_read | perms::owner_write);                     //0600
    CHECK(getAcl(aclTarget).empty());
    CHECK_NO_THROW(copyItemPermissions(aclSource, aclTarget, ProcSymlink::follow));
    CHECK(getMode(aclTarget) == (perms::owner_read | perms::group_read | perms::others_read));
    CHECK(!getAcl(aclSource).empty());
    CHECK(getAcl(aclTarget) == getAcl(aclSource));

    //read-only folder (0555)
    const Zstring aclSourceDir = appendPath(testDir, "aclSourceDir");
    const Zstring aclTargetDir = appendPath(testDir, "aclTargetDir");
    createDirectory(aclSourceDir);
    createDirectory(aclTargetDir);
    runShell("chmod +a 'everyone allow list,search' " + escapeCommandArg(aclSourceDir));
    const perms perms0555 = perms::owner_read | perms::owner_exec | perms::group_read | perms::group_exec | perms::others_read | perms::others_exec;
    fs::permissions(toPath(aclSourceDir), perms0555);
    CHECK_NO_THROW(copyItemPermissions(aclSourceDir, aclTargetDir, ProcSymlink::follow));
    CHECK(getMode(aclTargetDir) == perms0555);
    CHECK(!getAcl(aclSourceDir).empty() && getAcl(aclTargetDir) == getAcl(aclSourceDir));

    //no ACL on the source: the target's ACL is removed
    CHECK_NO_THROW(copyItemPermissions(appendPath(testDir, "source"), aclTarget, ProcSymlink::follow));
    CHECK(getAcl(aclTarget).empty());

    //inherited entries (from an inheritable entry of the target's parent folder) are kept: the target's ACL becomes the
    //source's explicit entries followed by its own inherited ones
    const Zstring inheritDir = appendPath(testDir, "inheritDir");
    createDirectory(inheritDir);
    runShell("chmod +a 'everyone allow readattr,file_inherit,directory_inherit' " + escapeCommandArg(inheritDir));
    const Zstring inheritTarget  = appendPath(inheritDir, "target");
    const Zstring inheritTarget2 = appendPath(inheritDir, "target2");
    writeFile(inheritTarget,  "t");
    writeFile(inheritTarget2, "t");
    auto isInherited = [](const std::string& line) { return contains(line, " inherited "); };
    const std::vector<std::string> aclInherited = getAcl(inheritTarget);
    CHECK(aclInherited.size() == 1 && isInherited(aclInherited[0])); //the kernel added it on create
    std::cout << "   inherited ACL: " << (aclInherited.empty() ? std::string("none") : aclInherited[0]) << std::endl;

    CHECK_NO_THROW(copyItemPermissions(aclSource, inheritTarget, ProcSymlink::follow)); //explicit "everyone allow read"
    const std::vector<std::string> aclMerged = getAcl(inheritTarget);
    CHECK(aclMerged.size() == 2 && aclMerged.size() == aclInherited.size() + getAcl(aclSource).size());
    if (aclMerged.size() == 2)
    {
        CHECK(!isInherited(aclMerged[0]) && contains(aclMerged[0], "allow read")); //explicit entries first
        CHECK(isInherited(aclMerged[1]));
    }
    //source without explicit entries: a target with only inherited entries is left as it is, explicit ones are removed
    CHECK_NO_THROW(copyItemPermissions(appendPath(testDir, "source"), inheritTarget2, ProcSymlink::follow));
    CHECK(getAcl(inheritTarget2) == aclInherited);
    CHECK_NO_THROW(copyItemPermissions(appendPath(testDir, "source"), inheritTarget, ProcSymlink::follow));
    CHECK(getAcl(inheritTarget) == aclInherited);

    //symlinks (ProcSymlink::asLink): mode and ACL of the link itself, the link targets are untouched
    const Zstring linkSource = appendPath(testDir, "linkSource");
    const Zstring linkTarget = appendPath(testDir, "linkTarget");
    fs::create_symlink("aclSource", toPath(linkSource));
    fs::create_symlink("target", toPath(linkTarget));
    fs::permissions(toPath(linkSource), perms::owner_read | perms::owner_exec, fs::perm_options::replace | fs::perm_options::nofollow);
    bool linkAcl = true;
    try { runShell("chmod -h +a 'everyone allow read' " + escapeCommandArg(linkSource)); }
    catch (const SysError& e) { linkAcl = false; std::cout << "   no ACL on symlinks: " << utfTo<std::string>(e.toString()) << std::endl; }
    const auto targetModeBefore = getMode(target);
    CHECK_NO_THROW(copyItemPermissions(linkSource, linkTarget, ProcSymlink::asLink));
    CHECK(getMode(linkTarget) == (perms::owner_read | perms::owner_exec));
    if (linkAcl)
        CHECK(!getAcl(linkSource).empty() && getAcl(linkTarget) == getAcl(linkSource));
    CHECK(getMode(target) == targetModeBefore);
    CHECK(getMode(aclSource) == (perms::owner_read | perms::group_read | perms::others_read));
}


//macOS: a second volume (FAT disk image, attached without admin rights): moves across volumes, a case-insensitive file system
//without ACLs, extended attributes it can't fully take
TEST_CASE(fileAccessMacSecondVolume)
{
    const Zstring imagePath = appendPath(testDir, "fat.dmg");
    const Zstring mountPath = appendPath(testDir, "mnt");
    createDirectory(mountPath);
    runShell("hdiutil create -quiet -size 32m -fs MS-DOS -volname ZENTEST " + escapeCommandArg(imagePath)); //throw SysError
    runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
    ZEN_ON_SCOPE_EXIT(try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); }
                      catch (const SysError& e) { reportFailure(__FILE__, __LINE__, "hdiutil detach: " + utfTo<std::string>(e.toString())); });
    (void)fetchExtraLog(); //start empty

    int64_t freeSpace = 0;
    CHECK_NO_THROW(freeSpace = getFreeDiskSpace(mountPath));
    CHECK(0 < freeSpace && freeSpace <= 32 * 1024 * 1024);

    //extended attributes the target can't fully take never fail the copy: the file is kept
    const Zstring source = appendPath(testDir, "source.txt");
    writeFile(source, "content");
    runShell("xattr -w com.freefilesync.small 'zen value' " + escapeCommandArg(source));
    runShell("xattr -w com.freefilesync.big \"$(head -c 200000 /dev/zero | tr '\\0' a)\" " + escapeCommandArg(source)); //200 KB
    const Zstring target = appendPath(mountPath, "copy.txt");
    CHECK_NO_THROW((void)copyNewFile(source, target, nullptr));
    CHECK(pathExists(target) && readFile(target) == "content");
    //...and are not copied at all to a volume without native extended attributes: macOS would emulate them with AppleDouble
    //"._<name>" files, which the next comparison sees as target-only items
    const Zstring sourceDir = appendPath(testDir, "sourceDir");
    createDirectory(sourceDir);
    runShell("xattr -w com.freefilesync.small 'folder value' " + escapeCommandArg(sourceDir));
    createDirectory(appendPath(mountPath, "dir"));
    CHECK_NO_THROW(copyDirectoryAttributes(sourceDir, appendPath(mountPath, "dir")));
    CHECK_NO_THROW((void)copyNewFile(source, appendPath(mountPath, nativeRelPath("dir/copy2.txt")), nullptr));

    for (const Zstring& item : {target, appendPath(mountPath, "dir")})
    {
        const std::string xattrNames = trimCpy(runShell("xattr " + escapeCommandArg(item)));
        CHECK(xattrNames.empty());
        if (!xattrNames.empty()) //which ones tells who wrote them: the copy (com.freefilesync.*) or the system
            std::cerr << "   extended attributes on " << toPrintable(item) << ": " << xattrNames << '\n';
    }
    for (const Zstring& folder : {mountPath, appendPath(mountPath, "dir")})
        traverseFolder(folder,
        [&](const FileInfo& fi) { CHECK(!startsWith(fi.itemName, "._")); if (startsWith(fi.itemName, "._")) std::cerr << "   AppleDouble file: " << toPrintable(fi.fullPath) << '\n'; },
        [&](const FolderInfo& fi) { CHECK(!startsWith(fi.itemName, "._")); }, nullptr);
    for (const LogEntry& entry : fetchExtraLog())
        reportFailure(__FILE__, __LINE__, "logged: " + std::string(entry.message.begin(), entry.message.end()));

    //no ACLs on FAT (owner and mode depend on the mount: just report)
    try
    {
        copyItemPermissions(source, target, ProcSymlink::follow); //throw FileError
        std::cout << "   copyItemPermissions() to FAT: ok" << std::endl;
    }
    catch (const FileError& e) { std::cout << "   copyItemPermissions() to FAT: " << utfTo<std::string>(e.toString()) << std::endl; }

    //moving across volumes
    CHECK_THROWS(ErrorMoveUnsupported, moveAndRenameItem(source, appendPath(mountPath, "moved.txt"), false));
    CHECK(pathExists(source));

    //FAT is case-insensitive: the existing-target check sees the same item, case-only renames work
    writeFile(appendPath(mountPath, "Case.txt"), "x");
    CHECK(pathExists(appendPath(mountPath, "CASE.TXT")));
    CHECK_NO_THROW(moveAndRenameItem(appendPath(mountPath, "Case.txt"), appendPath(mountPath, "CASE.txt"), false));
    std::vector<Zstring> names;
    traverseFolder(mountPath, [&](const FileInfo& fi) { names.push_back(fi.itemName); }, nullptr, nullptr);
    CHECK(std::find(names.begin(), names.end(), "CASE.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Case.txt") == names.end());
}
#endif
