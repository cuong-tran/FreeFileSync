// zen/dir_watcher.h: change events for create, modify and delete within a watched folder

#include <thread>
#include <zen/dir_watcher.h>
#include <zen/file_access.h>
#include "test.h"

using namespace zen;
using namespace zentest;


namespace
{
const auto eventTimeout = std::chrono::seconds(10); //generous for slow CI machines; the loop ends as soon as the event arrived

//poll fetchChanges() until a change accepted by "pred" arrives or the timeout is reached
//changes fetched but not matched stay in "backlog" for the next call: one fetch may return the events of several file operations
template <class Predicate>
bool waitForChange(DirWatcher& watcher, Predicate pred, std::vector<DirWatcher::Change>& backlog)
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
            backlog.push_back(change);
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
    auto expectChange = [&](const char* what, DirWatcher::ChangeType type, const Zstring& itemPath, bool anyType)
    {
        const bool found = waitForChange(*watcher, [&](const DirWatcher::Change& c)
        {
            return c.itemPath == itemPath && (anyType || c.type == type);
        }, backlog);
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

    //errors
    CHECK_THROWS(FileError, DirWatcher(appendPath(testDir, "missing")));
}
