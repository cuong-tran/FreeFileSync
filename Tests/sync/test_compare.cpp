// Comparison: by time and size vs. by content; names that differ in case only or in Unicode normalization; copying permissions

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;
namespace fs = std::filesystem;


namespace
{
const time_t t0 = timeBase();
}


TEST_CASE(syncCompareTimeSizeVsContent)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"same time and size.txt", {Entry::Type::file, "AAAA", t0}},
        {"same content.txt",       {Entry::Type::file, "same", t0}},
        {"different size.txt",     {Entry::Type::file, "short", t0}},
    });
    makeTree(right,
    {
        {"same time and size.txt", {Entry::Type::file, "BBBB", t0}},             //different content: only a content comparison sees it
        {"same content.txt",       {Entry::Type::file, "same", t0 + TIME_STEP}}, //different time: only time and size sees it
        {"different size.txt",     {Entry::Type::file, "longer", t0}},
    });

    const RunResult byTime = compareOnly({.left = left, .right = right, .compareVar = CompareVariant::timeSize});
    CHECK_NO_ERRORS(byTime);
    //same content.txt: right newer; different size.txt: same time, but a different size (a conflict in the comparison, which
    //mirror resolves by overwriting the right side); same time and size.txt: equal for this comparison, although the content differs
    CHECK_EQUAL(byTime.opCount(SO_OVERWRITE_RIGHT), 2);
    CHECK_EQUAL(byTime.plan.size(), 2u);
    CHECK_EQUAL(byTime.conflicts, 0);
    if (byTime.plan.size() != 2)
        std::cerr << byTime.toString();

    const SyncCfg byContentCfg{.left = left, .right = right, .compareVar = CompareVariant::content};
    const RunResult byContent = runSync(byContentCfg);
    CHECK_NO_ERRORS(byContent);
    CHECK_EQUAL(byContent.opCount(SO_OVERWRITE_RIGHT), 2); //same time and size.txt, different size.txt
    CHECK_EQUAL(byContent.conflicts, 0);
    if (byContent.opCount(SO_OVERWRITE_RIGHT) != 2)
        std::cerr << byContent.toString();

    const Tree treeR = readTree(right);
    CHECK(treeR.at("same time and size.txt").content == "AAAA");
    CHECK(treeR.at("different size.txt").content == "short");
    CHECK(treeR.at("same content.txt").modTime == t0 + TIME_STEP); //equal by content: not touched
    CHECK(compareOnly(byContentCfg).allEqual);
}


//"File.txt" on the left, "file.txt" on the right, same content and time
TEST_CASE(syncCaseOnlyRename)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,  {{"File.txt", {Entry::Type::file, "same", t0}}, {"Folder", {Entry::Type::folder}}, {"Folder/a", {Entry::Type::file, "a", t0}}});
    makeTree(right, {{"file.txt", {Entry::Type::file, "same", t0}}, {"folder", {Entry::Type::folder}}, {"folder/a", {Entry::Type::file, "a", t0}}});

    const RunResult res = runSync({.left = left, .right = right});
    CHECK_NO_ERRORS(res);
    //the engine matches names ignoring case on every platform (like the Windows and macOS file systems do): the pair is equal
    //except for the name, and mirror renames the right items to the left spelling, on a case-sensitive Linux file system too
    CHECK_EQUAL(res.opCount(SO_RENAME_RIGHT), 2);
    CHECK_EQUAL(res.opCount(SO_CREATE_RIGHT) + res.opCount(SO_DELETE_RIGHT), 0);
    if (res.opCount(SO_RENAME_RIGHT) != 2)
        std::cerr << res.toString();

    CHECK_TREES_EQUAL(readTree(right), readTree(left)); //exact spelling: readTree() returns the names as stored
    CHECK(compareOnly({.left = left, .right = right}).allEqual);
}


//the same name precomposed (NFC) on the left and decomposed (NFD) on the right: canonically equivalent Unicode
TEST_CASE(syncUnicodeNormalization)
{
    const std::string nfc = "caf\xc3\xa9.txt";   //"café.txt", U+00E9
    const std::string nfd = "cafe\xcc\x81.txt";  //"café.txt", e + U+0301
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,  {{nfc, {Entry::Type::file, "same", t0}}});
    makeTree(right, {{nfd, {Entry::Type::file, "same", t0}}});

    const Tree before = readTree(right);
    CHECK(before.size() == 1 && before.begin()->first == nfd); //APFS and HFS+ keep the spelling they were given, as Linux does

    const RunResult res = runSync({.left = left, .right = right});
    CHECK_NO_ERRORS(res);
    //the engine compares names in one Unicode normal form (zen/zstring.h): canonically equivalent names are the same name, so the
    //pair is equal, not even a rename (unlike a case change, see syncCaseOnlyRename), and each side keeps its spelling, on Linux too
    CHECK(res.allEqual);
    CHECK(res.plan.empty());
    if (!res.plan.empty())
        std::cerr << res.toString();
    CHECK_TREES_EQUAL(readTree(right), before);
    CHECK(compareOnly({.left = left, .right = right}).allEqual);
}


TEST_CASE(syncCopyPermissions)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"group readable.txt", {Entry::Type::file, "640", t0}},
        {"private",            {Entry::Type::folder}},
        {"private/script.sh",  {Entry::Type::file, "#!/bin/sh\n", t0}},
    });
    makeFolder(right, "");
    const auto setPerms = [&](const std::string& relPath, fs::perms p) { fs::permissions(toPath(itemPath(left, relPath)), p, fs::perm_options::replace); };
    const auto getPerms = [&](const Zstring& root, const std::string& relPath) { return fs::status(toPath(itemPath(root, relPath))).permissions() & fs::perms::mask; };

    using enum fs::perms;
    setPerms("group readable.txt", owner_read | owner_write | group_read);                          //0640
    setPerms("private/script.sh",  owner_all | group_read | group_exec);                             //0750
    setPerms("private",            owner_all | group_read | group_exec);                             //0750

    const RunResult res = runSync({.left = left, .right = right, .copyPermissions = true});
    CHECK_NO_ERRORS(res);
    CHECK_TREES_EQUAL(readTree(right), readTree(left));
    for (const std::string relPath : {"group readable.txt", "private/script.sh", "private"})
        CHECK_EQUAL(static_cast<int>(getPerms(right, relPath)), static_cast<int>(getPerms(left, relPath)));
}
