// Error paths: items the sync cannot read are reported through the callback, and the sync goes on with the rest without
// deleting or damaging what it could not read. Skipped as root, which reads everything.

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;
namespace fs = std::filesystem;


TEST_CASE(syncUnreadableItems)
{
    if (runningAsRoot())
    {
        std::cout << "   SKIPPED: running as root (permissions do not stop root from reading)" << std::endl;
        return;
    }
    const time_t t0 = timeBase();
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"readable.txt",            {Entry::Type::file, "fine", t0}},
        {"unreadable.txt",          {Entry::Type::file, "new version nobody can read", t0 + TIME_STEP}},
        {"unreadable folder",       {Entry::Type::folder}},
        {"unreadable folder/x.txt", {Entry::Type::file, "hidden", t0}},
        {"sub/after.txt",           {Entry::Type::file, "synced after the errors", t0}},
    });
    makeTree(right,
    {
        {"unreadable.txt",          {Entry::Type::file, "old version", t0}},
        {"unreadable folder",       {Entry::Type::folder}},
        {"unreadable folder/y.txt", {Entry::Type::file, "must not be deleted", t0}},
    });
    fs::permissions(toPath(itemPath(left, "unreadable.txt")),    fs::perms::none, fs::perm_options::replace);
    fs::permissions(toPath(itemPath(left, "unreadable folder")), fs::perms::none, fs::perm_options::replace);

    const RunResult res = runSync({.left = left, .right = right});

    //both reported: the folder while scanning, the file while copying
    CHECK_EQUAL(res.errors.size(), 2u);
    CHECK(std::any_of(res.errors.begin(), res.errors.end(), [](const std::string& e) { return contains(e, "unreadable folder"); }));
    CHECK(std::any_of(res.errors.begin(), res.errors.end(), [](const std::string& e) { return contains(e, "unreadable.txt"); }));
    if (res.errors.size() != 2)
        std::cerr << res.toString();

    fs::permissions(toPath(itemPath(left, "unreadable.txt")),    fs::perms::owner_all, fs::perm_options::replace);
    fs::permissions(toPath(itemPath(left, "unreadable folder")), fs::perms::owner_all, fs::perm_options::replace);

    CHECK_TREES_EQUAL(readTree(right), (Tree
    {
        {"readable.txt",            {Entry::Type::file, "fine", t0}},
        {"unreadable.txt",          {Entry::Type::file, "old version", t0}},           //failed copy: the old version is intact (fail-safe copy)
        {"unreadable folder",       {Entry::Type::folder}},
        {"unreadable folder/y.txt", {Entry::Type::file, "must not be deleted", t0}},   //not scanned on the left: nothing deleted below it
        {"sub",                     {Entry::Type::folder}},
        {"sub/after.txt",           {Entry::Type::file, "synced after the errors", t0}},
    }));
}
