// Sync variants: mirror, update, two-way (with sync.ffs_db over three runs: changes, deletions and moves on each side, conflicts)

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;


namespace
{
const time_t t0 = timeBase();

//left and right of a typical first run: items on one side only, newer on either side, equal items, empty folders, Unicode names
Tree leftTree()
{
    return
    {
        {"equal.txt",                {Entry::Type::file, "same on both sides", t0}},
        {"left only.txt",            {Entry::Type::file, "only left", t0}},
        {"left newer.txt",           {Entry::Type::file, "left version, newer", t0 + TIME_STEP}},
        {"right newer.txt",          {Entry::Type::file, "left version, older", t0}},
        {"folder",                   {Entry::Type::folder}},
        {"folder/nested",            {Entry::Type::folder}},
        {"folder/nested/deep.bin",   {Entry::Type::file, std::string(300000, 'x') + "end", t0}},
        {"empty folder left",        {Entry::Type::folder}},
        {"Ünïcödé ✓",                {Entry::Type::folder}},
        {"Ünïcödé ✓/名前.txt",        {Entry::Type::file, "unicode", t0}},
        {"empty file",               {Entry::Type::file, "", t0}},
    };
}

Tree rightTree()
{
    return
    {
        {"equal.txt",                {Entry::Type::file, "same on both sides", t0}},
        {"right only.txt",           {Entry::Type::file, "only right", t0}},
        {"left newer.txt",           {Entry::Type::file, "right version, older", t0}},
        {"right newer.txt",          {Entry::Type::file, "right version, newer", t0 + TIME_STEP}},
        {"folder",                   {Entry::Type::folder}},
        {"folder/right only sub",    {Entry::Type::folder}},
        {"folder/right only sub/x",  {Entry::Type::file, "x", t0}},
        {"empty folder right",       {Entry::Type::folder}},
    };
}
}


TEST_CASE(syncMirror)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,  leftTree());
    makeTree(right, rightTree());

    const SyncCfg cfg{.left = left, .right = right, .syncVar = SyncVariant::mirror};
    const RunResult res = runSync(cfg);
    CHECK_NO_ERRORS(res);
    CHECK(res.opCount(SO_CREATE_RIGHT) > 0);
    CHECK_EQUAL(res.opCount(SO_OVERWRITE_RIGHT), 2); //left newer and right newer: mirror overwrites both
    CHECK(res.opCount(SO_DELETE_RIGHT) >= 3);        //right only.txt, empty folder right, folder/right only sub (with its file)
    CHECK_EQUAL(res.opCount(SO_OVERWRITE_LEFT) + res.opCount(SO_CREATE_LEFT) + res.opCount(SO_DELETE_LEFT), 0);

    CHECK_TREES_EQUAL(readTree(right), leftTree()); //names, types, content, sizes, mtimes
    CHECK_TREES_EQUAL(readTree(left),  leftTree()); //source untouched

    const RunResult again = compareOnly(cfg);
    CHECK_NO_ERRORS(again);
    CHECK(again.allEqual);
}


TEST_CASE(syncUpdate)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,  leftTree());
    makeTree(right, rightTree());

    const RunResult res = runSync({.left = left, .right = right, .syncVar = SyncVariant::update});
    CHECK_NO_ERRORS(res);

    //update: new and newer items go left to right; nothing is deleted, items newer on the right stay
    Tree expected = rightTree();
    for (const auto& [relPath, e] : leftTree())
        if (relPath != "right newer.txt")
            expected[relPath] = e;
    CHECK_TREES_EQUAL(readTree(right), expected);
    CHECK_TREES_EQUAL(readTree(left), leftTree());
    CHECK_EQUAL(res.opCount(SO_DELETE_RIGHT), 0);
}


//two-way: run 1 without a database (newer wins, items on one side are copied), then runs that use sync.ffs_db to tell changes from
//deletions and to detect moves, then a run with conflicts
TEST_CASE(syncTwoWayDatabase)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,  leftTree());
    makeTree(right, rightTree());
    const SyncCfg cfg{.left = left, .right = right, .syncVar = SyncVariant::twoWay};

    //---- run 1: no database yet
    {
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);

        Tree expected = leftTree();
        for (const auto& [relPath, e] : rightTree())
            if (relPath != "left newer.txt")
                expected[relPath] = e; //right only items, and the newer right version
        CHECK_TREES_EQUAL(readTree(left),  expected);
        CHECK_TREES_EQUAL(readTree(right), expected);

        //the database is written on both sides (".sync.ffs_db", hidden on Linux and macOS)
        CHECK(pathExists(appendPath(left,  ".sync.ffs_db")));
        CHECK(pathExists(appendPath(right, ".sync.ffs_db")));
        CHECK(!pathExists(appendPath(left, "sync.ffs_lock"))); //directory locks are released
        CHECK(compareOnly(cfg).allEqual);
    }

    //---- run 2: a change, a deletion, a move and a new item on each side
    const time_t t1 = t0 + 5 * TIME_STEP;
    makeFile(left, "equal.txt", "changed on the left", t1);                   //change
    std::filesystem::remove(toPath(itemPath(left, "left only.txt")));          //deletion
    std::filesystem::rename(toPath(itemPath(left, "folder/nested/deep.bin")),  //move
                            toPath(itemPath(left, "deep moved.bin")));
    makeFile(left, "new left.txt", "new on the left", t1);                    //creation

    makeFile(right, "right newer.txt", "changed on the right", t1);
    std::filesystem::remove(toPath(itemPath(right, "right only.txt")));
    std::filesystem::rename(toPath(itemPath(right, "Ünïcödé ✓/名前.txt")),      //move to another folder
                            toPath(itemPath(right, "folder/名前 moved.txt")));
    makeFile(right, "new right.txt", "new on the right", t1);
    {
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);
        CHECK_EQUAL(res.conflicts, 0);
        //moves are carried out as moves, not as copy + delete (file ids from the database)
        CHECK_EQUAL(res.opCount(SO_MOVE_RIGHT_TO), 1);
        CHECK_EQUAL(res.opCount(SO_MOVE_LEFT_TO),  1);
        CHECK_EQUAL(res.opCount(SO_DELETE_RIGHT), 1); //left only.txt
        CHECK_EQUAL(res.opCount(SO_DELETE_LEFT),  1); //right only.txt
        if (res.conflicts != 0 || res.opCount(SO_MOVE_RIGHT_TO) != 1)
            std::cerr << res.toString();

        const Tree treeL = readTree(left);
        CHECK_TREES_EQUAL(readTree(right), treeL);
        CHECK(treeL.contains("deep moved.bin") && !treeL.contains("folder/nested/deep.bin"));
        CHECK(treeL.contains("folder/名前 moved.txt") && !treeL.contains("Ünïcödé ✓/名前.txt"));
        CHECK(!treeL.contains("left only.txt") && !treeL.contains("right only.txt"));
        CHECK(treeL.contains("new left.txt") && treeL.contains("new right.txt"));
        CHECK(treeL.contains("equal.txt") && treeL.at("equal.txt").content == "changed on the left");
        CHECK(treeL.contains("right newer.txt") && treeL.at("right newer.txt").content == "changed on the right");
        CHECK(compareOnly(cfg).allEqual);
    }

    //---- run 3: conflicts: the same file changed on both sides; changed on one side and deleted on the other
    const time_t t2 = t1 + 5 * TIME_STEP;
    makeFile(left,  "equal.txt", "left edit", t2);
    makeFile(right, "equal.txt", "right edit, a different size", t2 + TIME_STEP);
    makeFile(left, "new left.txt", "changed left", t2);
    std::filesystem::remove(toPath(itemPath(right, "new left.txt")));
    std::filesystem::remove(toPath(itemPath(right, "empty file"))); //no conflict: a plain deletion, still carried out
    {
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);
        CHECK_EQUAL(res.conflicts, 2);
        CHECK_EQUAL(res.opCount(SO_UNRESOLVED_CONFLICT), 2);
        if (res.conflicts != 2)
            std::cerr << res.toString();

        //conflicts are left alone on both sides
        CHECK_EQUAL(readFile(itemPath(left,  "equal.txt")), std::string("left edit"));
        CHECK_EQUAL(readFile(itemPath(right, "equal.txt")), std::string("right edit, a different size"));
        CHECK(pathExists(itemPath(left, "new left.txt")) && !pathExists(itemPath(right, "new left.txt")));
        //the rest is synchronized
        CHECK(!pathExists(itemPath(left, "empty file")));

        //still conflicts on the next comparison: the database does not hide them
        const RunResult again = compareOnly(cfg);
        CHECK_EQUAL(again.conflicts, 2);
    }
}
