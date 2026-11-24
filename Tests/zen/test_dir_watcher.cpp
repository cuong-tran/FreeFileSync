// zen/dir_watcher.h: change events for create, modify and delete within a watched folder

#include <thread>
#include <zen/dir_watcher.h>
#include <zen/file_access.h>
#include <zen/process_exec.h>
#include <zen/scope_guard.h>
#include "test.h"

using namespace zen;
using namespace zentest;


namespace
{
const auto eventTimeout = std::chrono::seconds(10); //generous for slow CI machines; the loop ends as soon as the event arrived

//poll fetchChanges() until a change accepted by "pred" arrives or the timeout is reached
//changes fetched but not matched stay in "backlog" for the next call: one fetch may return the events of several file operations
template <class Predicate>
bool waitForChange(DirWatcher& watcher, Predicate pred, std::vector<DirWatcher::Change>& backlog, //throw FileError
                   std::vector<DirWatcher::Change>* history = nullptr) //optional: every change fetched
{
    const auto stopTime = std::chrono::steady_clock::now() + eventTimeout;
    for (;;)
    {
        if (auto it = std::find_if(backlog.begin(), backlog.end(), pred);
            it != backlog.end())
        {
            backlog.erase(it);
            return true;
        }
        if (std::chrono::steady_clock::now() > stopTime)
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (const DirWatcher::Change& change : watcher.fetchChanges([] {}, std::chrono::milliseconds(50))) //throw FileError
        {
            backlog.push_back(change);
            if (history)
                history->push_back(change);
        }
    }
}


void dumpChanges(const std::vector<DirWatcher::Change>& changes)
{
    for (const DirWatcher::Change& change : changes)
        std::cerr << "    change type " << static_cast<int>(change.type) << ": " << toPrintable(change.itemPath) << '\n';
}
}


TEST_CASE(dirWatcher)
{
    const Zstring watchedPath = appendPath(testDir, "watched");
    const Zstring subPath = appendPath(watchedPath, "sub");
    createDirectoryIfMissingRecursion(subPath);

    std::unique_ptr<DirWatcher> watcher;
    CHECK_NO_THROW(watcher = std::make_unique<DirWatcher>(watchedPath));
    if (!watcher)
        return;

    std::vector<DirWatcher::Change> backlog;
    std::vector<DirWatcher::Change> history;
    auto expectChange = [&](const char* what, DirWatcher::ChangeType type, const Zstring& itemPath, bool anyType) //throw FileError
    {
        const bool found = waitForChange(*watcher, [&](const DirWatcher::Change& c)
        {
            return c.itemPath == itemPath && (anyType || c.type == type);
        }, backlog, &history);
        if (!found)
        {
            reportFailure(__FILE__, __LINE__, std::string("no ") + what + " event for " + toPrintable(itemPath));
            dumpChanges(backlog);
        }
        ++checkCount;
    };

    //create
    const Zstring filePath = appendPath(watchedPath, "file.txt");
    writeFile(filePath, "first");
    expectChange("create", DirWatcher::ChangeType::create, filePath, false);

    //modify: "update" is "informal: use for debugging/logging only", so any event for the path counts
    //drop the remaining notifications of the create step, so they can't count as modification
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    (void)watcher->fetchChanges([] {}, std::chrono::milliseconds(50));
    backlog.clear();
    writeFile(filePath, "second, longer content");
    expectChange("modify", DirWatcher::ChangeType::update, filePath, true);

    //delete
    removeFilePlain(filePath);
    expectChange("remove", DirWatcher::ChangeType::remove, filePath, false);
#ifdef ZEN_MAC
    //FSEvents accumulates flags per path ("created" stays set on later modifications): still one "create" per lifetime of the item
    CHECK_EQUAL(std::count_if(history.begin(), history.end(), [&](const DirWatcher::Change& c)
    {
        return c.itemPath == filePath && c.type == DirWatcher::ChangeType::create;
    }), static_cast<std::ptrdiff_t>(1));
#endif

    //subfolders existing when the watch started are watched, too
    const Zstring subFilePath = appendPath(subPath, "inner.txt");
    writeFile(subFilePath, "inner");
    expectChange("create (subfolder)", DirWatcher::ChangeType::create, subFilePath, false);
    removeFilePlain(subFilePath);
    expectChange("remove (subfolder)", DirWatcher::ChangeType::remove, subFilePath, false);

    //new folder: reported, but "newly added subdirectories are reported but not automatically added for watching" (Linux)
    const Zstring newDirPath = appendPath(watchedPath, "new folder");
    createDirectory(newDirPath);
    expectChange("create (folder)", DirWatcher::ChangeType::create, newDirPath, false);

    //renaming: old name removed, new name created
    const Zstring renamedPath = appendPath(watchedPath, "renamed");
    moveAndRenameItem(newDirPath, renamedPath, false);
    expectChange("remove (rename source)", DirWatcher::ChangeType::remove, newDirPath, false);
    expectChange("create (rename target)", DirWatcher::ChangeType::create, renamedPath, false);

#ifdef ZEN_MAC
    //FSEvents: folders created after the watch started are watched, too
    const Zstring renamedFilePath = appendPath(renamedPath, "in new folder.txt");
    writeFile(renamedFilePath, "x");
    expectChange("create (new subfolder)", DirWatcher::ChangeType::create, renamedFilePath, false);

    //paths are reported as passed to DirWatcher, also when it is a symlink (FSEvents reports resolved paths)
    {
        const Zstring linkPath = appendPath(testDir, "link to watched");
        std::filesystem::create_directory_symlink(toPath(watchedPath), toPath(linkPath));
        DirWatcher linkWatcher(linkPath); //throw FileError
        std::vector<DirWatcher::Change> linkBacklog;
        writeFile(appendPath(watchedPath, "via link.txt"), "x");
        CHECK(waitForChange(linkWatcher, [&](const DirWatcher::Change& c)
        {
            return c.type == DirWatcher::ChangeType::create && c.itemPath == appendPath(linkPath, "via link.txt");
        }, linkBacklog));
    }

    //paths are reported as passed to DirWatcher, also when that spelling differs from the one on disk in Unicode normalization or
    //case (APFS compares names case- and normalization-insensitively; NFC and NFD spellings differ in length)
    for (const auto& [onDisk, watchedAs] :
         {
             std::pair<Zstring, Zstring>{"caf" + Zstring("e\xCC\x81"), "caf" + Zstring("\xC3\xA9")}, //NFD on disk, watched as NFC
             std::pair<Zstring, Zstring>{"r" + Zstring("\xC3\xA9") + "sum" + Zstring("\xC3\xA9"), "r" + Zstring("e\xCC\x81") + "sum" + Zstring("e\xCC\x81")}, //NFC on disk, watched as NFD
             std::pair<Zstring, Zstring>{"MixedCase", "mIXEDcASE"},
         })
    {
        const Zstring dirOnDisk = appendPath(testDir, onDisk);
        createDirectory(dirOnDisk);
        const Zstring watchedPath2 = appendPath(testDir, watchedAs);
        DirWatcher watcher2(watchedPath2); //throw FileError
        std::vector<DirWatcher::Change> backlog2;
        writeFile(appendPath(dirOnDisk, "spelling.txt"), "x");
        if (!waitForChange(watcher2, [&](const DirWatcher::Change& c)
    {
        return c.type == DirWatcher::ChangeType::create && c.itemPath == appendPath(watchedPath2, "spelling.txt");
        }, backlog2))
        {
            reportFailure(__FILE__, __LINE__, "no create event when watched as " + toPrintable(watchedPath2));
            dumpChanges(backlog2);
        }
        ++checkCount;
    }

    //removal of the watched folder itself: like an unavailable base folder ("check existence of top watched directory externally")
    removeDirectoryPlainRecursion(watchedPath);
    expectChange("baseFolderUnavailable (folder removed)", DirWatcher::ChangeType::baseFolderUnavailable, watchedPath, false);
#endif

    //errors
    CHECK_THROWS(FileError, DirWatcher(appendPath(testDir, "missing")));
    writeFile(appendPath(testDir, "file"), "x");
    CHECK_THROWS(FileError, DirWatcher(appendPath(testDir, "file"))); //not a folder
}


#ifdef ZEN_MAC
namespace
{
void runShell(const Zstring& cmdLine) //throw SysError
{
    if (const auto [exitCode, output] = consoleExecute(cmdLine, std::nullopt); //throw SysError, SysErrorTimeOut
        exitCode != 0)
        throw SysError(L"exit code " + numberTo<std::wstring>(exitCode) + L": " + utfTo<std::wstring>(cmdLine) + L"\n" + utfTo<std::wstring>(output));
}
}


//macOS: unmounting a volume below the watched folder is a change of that folder, unmounting the watched folder's own volume
//makes the base folder unavailable (disk images, attached without admin rights)
TEST_CASE(dirWatcherUnmount)
{
    const Zstring imagePath = appendPath(testDir, "volume.dmg");
    runShell("hdiutil create -quiet -size 16m -fs APFS -volname ZENWATCH " + escapeCommandArg(imagePath)); //throw SysError

    auto waitAny = [](DirWatcher& watcher, std::vector<DirWatcher::Change>& changes, std::chrono::milliseconds duration) //collect all changes
    {
        const auto stopTime = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < stopTime)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            for (const DirWatcher::Change& change : watcher.fetchChanges([] {}, std::chrono::milliseconds(50))) //throw FileError
                changes.push_back(change);
        }
    };

    //1. volume mounted at a folder inside the watched folder
    {
        const Zstring watchedPath = appendPath(testDir, "watched");
        const Zstring mountPath = appendPath(watchedPath, "mnt");
        createDirectory(watchedPath);
        createDirectory(mountPath);
        runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
        bool attached = true;
        ZEN_ON_SCOPE_EXIT(if (attached) try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); } catch (SysError&) {});

        DirWatcher watcher(watchedPath); //throw FileError
        runShell("hdiutil detach -quiet " + escapeCommandArg(mountPath));
        attached = false;

        std::vector<DirWatcher::Change> changes;
        waitAny(watcher, changes, std::chrono::seconds(2));
        for (const DirWatcher::Change& change : changes)
            if (change.type == DirWatcher::ChangeType::baseFolderUnavailable)
                reportFailure(__FILE__, __LINE__, "baseFolderUnavailable after unmounting a volume below the watched folder");
        std::cout << "   unmount below the watched folder: " << changes.size() << " change(s)" << std::endl;
        dumpChanges(changes);

        //the watch still works
        std::vector<DirWatcher::Change> backlog;
        writeFile(appendPath(watchedPath, "after unmount.txt"), "x");
        CHECK(waitForChange(watcher, [&](const DirWatcher::Change& c)
        {
            return c.type == DirWatcher::ChangeType::create && c.itemPath == appendPath(watchedPath, "after unmount.txt");
        }, backlog));
    }

    //2. the watched folder is on the volume
    {
        const Zstring mountPath = appendPath(testDir, "mnt2");
        createDirectory(mountPath);
        runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
        bool attached = true;
        ZEN_ON_SCOPE_EXIT(if (attached) try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); } catch (SysError&) {});

        const Zstring watchedPath = appendPath(mountPath, "watched on volume");
        createDirectory(watchedPath);
        DirWatcher watcher(watchedPath); //throw FileError
        runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath));
        attached = false;

        std::vector<DirWatcher::Change> backlog;
        if (!waitForChange(watcher, [&](const DirWatcher::Change& c)
    {
        return c.type == DirWatcher::ChangeType::baseFolderUnavailable && c.itemPath == watchedPath;
        }, backlog))
        {
            reportFailure(__FILE__, __LINE__, "no baseFolderUnavailable after unmounting the watched folder's volume");
            dumpChanges(backlog);
        }
        ++checkCount;
    }
}
#endif
