// Deletion handling: permanent, versioning (replace, time stamp folder, time stamp file), recycle bin
//
// Recycle bin: Linux: run.sh points HOME/XDG_DATA_HOME into its sandbox, so gio moves the items into a trash folder there.
// macOS: NSFileManager always uses the user's real trash (~/.Trash for the startup volume, where the test folders are), so the
// items get names unique to this run and the test deletes them from the trash again; nothing else in the trash is touched.

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;
namespace fs = std::filesystem;


namespace
{
const time_t t0 = timeBase();

//right has an outdated file, a file and a folder that the left does not have => mirror overwrites one and deletes two
Tree expectedRightTree(const std::string& changedFile = "new.txt")
{
    return
    {
        {"keep.txt",            {Entry::Type::file, "keep", t0}},
        {"sub",                 {Entry::Type::folder}},
        {"sub/" + changedFile,  {Entry::Type::file, "new version", t0 + TIME_STEP}},
    };
}

void makeDeletionTrees(const Zstring& left, const Zstring& right, const std::string& goneFile, const std::string& goneFolder,
                       const std::string& changedFile = "new.txt")
{
    makeTree(left, expectedRightTree(changedFile));
    makeTree(right,
    {
        {"keep.txt",                    {Entry::Type::file, "keep", t0}},
        {"sub",                         {Entry::Type::folder}},
        {"sub/" + changedFile,          {Entry::Type::file, "old version", t0}},
        {goneFile,                      {Entry::Type::file, "deleted file", t0}},
        {goneFolder,                    {Entry::Type::folder}},
        {goneFolder + "/inside.txt",    {Entry::Type::file, "inside deleted folder", t0}},
    });
}

const Tree expectedRight = expectedRightTree();
}


TEST_CASE(syncDeletePermanently)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeDeletionTrees(left, right, "gone.txt", "gone folder");

    const RunResult res = runSync({.left = left, .right = right, .deletion = DeletionVariant::permanent});
    CHECK_NO_ERRORS(res);
    CHECK_TREES_EQUAL(readTree(right), expectedRight);
    CHECK_EQUAL(fs::directory_iterator(toPath(testDir)) == fs::directory_iterator(), false); //sanity
    CHECK_EQUAL(static_cast<int>(std::distance(fs::directory_iterator(toPath(testDir)), fs::directory_iterator())), 2); //nothing kept anywhere else
}


TEST_CASE(syncVersioningReplace)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    const Zstring versions = appendPath(testDir, "versions");
    makeDeletionTrees(left, right, "gone.txt", "gone folder");

    const SyncCfg cfg{.left = left, .right = right, .deletion = DeletionVariant::versioning,
                      .versioningFolder = versions, .versioningStyle = VersioningStyle::replace};
    {
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);
        CHECK_TREES_EQUAL(readTree(right), expectedRight);
        //replace: the old versions under their relative paths, with their modification times
        CHECK_TREES_EQUAL(readTree(versions), (Tree
        {
            {"gone.txt",                {Entry::Type::file, "deleted file", t0}},
            {"gone folder",             {Entry::Type::folder}},
            {"gone folder/inside.txt",  {Entry::Type::file, "inside deleted folder", t0}},
            {"sub",                     {Entry::Type::folder}},
            {"sub/new.txt",             {Entry::Type::file, "old version", t0}},
        }));
    }
    //a second overwrite replaces the stored version
    makeFile(left, "sub/new.txt", "newest version", t0 + 2 * TIME_STEP);
    {
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);
        CHECK_EQUAL(readFile(itemPath(versions, "sub/new.txt")), std::string("new version"));
        CHECK_EQUAL(readFile(itemPath(right,    "sub/new.txt")), std::string("newest version"));
    }
}


TEST_CASE(syncVersioningTimestamp)
{
    for (const VersioningStyle style : {VersioningStyle::timestampFolder, VersioningStyle::timestampFile})
    {
        const Zstring caseDir = appendPath(testDir, style == VersioningStyle::timestampFolder ? "folder style" : "file style");
        const Zstring left  = appendPath(caseDir, "left");
        const Zstring right = appendPath(caseDir, "right");
        const Zstring versions = appendPath(caseDir, "versions");
        makeDeletionTrees(left, right, "gone.txt", "gone folder");

        const RunResult res = runSync({.left = left, .right = right, .deletion = DeletionVariant::versioning,
                                       .versioningFolder = versions, .versioningStyle = style});
        CHECK_NO_ERRORS(res);
        CHECK_TREES_EQUAL(readTree(right), expectedRight);

        const Tree tree = readTree(versions);
        std::vector<std::string> files;
        for (const auto& [relPath, e] : tree)
            if (e.type == Entry::Type::file)
                files.push_back(relPath + " = " + e.content);
        CHECK_EQUAL(files.size(), 3u);

        if (style == VersioningStyle::timestampFolder)
        {
            //<versions>/<YYYY-MM-DD hhmmss>/<relative path>: one folder for the whole sync
            std::vector<std::string> topLevel;
            for (const auto& [relPath, e] : tree)
                if (!contains(relPath, '/'))
                    topLevel.push_back(relPath);
            CHECK_EQUAL(topLevel.size(), 1u);
            if (topLevel.size() == 1)
            {
                const std::string& stamp = topLevel[0];
                CHECK(stamp.size() == 17 && stamp[4] == '-' && stamp[7] == '-' && stamp[10] == ' '); //e.g. "2026-11-24 153012"
                CHECK(tree.contains(stamp + "/gone.txt") && tree.at(stamp + "/gone.txt").content == "deleted file");
                CHECK(tree.contains(stamp + "/sub/new.txt") && tree.at(stamp + "/sub/new.txt").content == "old version");
                CHECK(tree.contains(stamp + "/gone folder/inside.txt"));
            }
        }
        else
        {
            //<versions>/<relative path> <YYYY-MM-DD hhmmss>.<ext>: the time stamp goes into each file name
            const auto hasVersion = [&](const std::string& prefix, const std::string& content)
            {
                return std::any_of(tree.begin(), tree.end(), [&](const auto& item)
                {
                    return startsWith(item.first, prefix + ' ') && endsWith(item.first, ".txt") && item.second.content == content;
                });
            };
            CHECK(hasVersion("gone.txt", "deleted file"));
            CHECK(hasVersion("sub/new.txt", "old version"));
            CHECK(hasVersion("gone folder/inside.txt", "inside deleted folder"));
        }
        if (files.size() != 3)
            for (const std::string& f : files)
                std::cerr << "   version: " << f << '\n';
    }
}


SANDBOX_TEST_CASE(syncRecycleBin)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    const std::string goneFile   = uniqueName("gone.txt");
    const std::string goneFolder = uniqueName("gone folder");
    const std::string changedFile = uniqueName("changed.txt"); //its old version is moved to the trash when overwritten
    makeDeletionTrees(left, right, goneFile, goneFolder, changedFile);

    const RunResult res = runSync({.left = left, .right = right, .deletion = DeletionVariant::recycler});
    CHECK_NO_ERRORS(res);
    CHECK_TREES_EQUAL(readTree(right), expectedRightTree(changedFile));

    //where the items went: the trash folder holds them under their names (or a variant of it if the name was taken)
#if defined ZEN_LINUX
    const Zstring trashFiles = appendPath(appendPath(getenv("XDG_DATA_HOME") ? Zstring(getenv("XDG_DATA_HOME")) : Zstring(), "Trash"), "files");
#elif defined ZEN_MAC
    const Zstring trashFiles = appendPath(realHomePath(), ".Trash");
#endif
    const Zstring trashedFile   = appendPath(trashFiles, utfTo<Zstring>(goneFile));
    const Zstring trashedFolder = appendPath(trashFiles, utfTo<Zstring>(goneFolder));
    const Zstring trashedOldVersion = appendPath(trashFiles, utfTo<Zstring>(changedFile));

    bool recycled = false;
    for (const std::string& w : res.warnings)
        if (contains(w, "recycle bin") || contains(w, "Recycle Bin"))
            std::cout << "   warning: " << w << std::endl;

    if (pathExists(trashedFile))
    {
        recycled = true;
        CHECK_EQUAL(readFile(trashedFile), std::string("deleted file"));
        CHECK(pathExists(appendPath(trashedFolder, "inside.txt")));
        CHECK(pathExists(trashedOldVersion) && readFile(trashedOldVersion) == "old version"); //the overwritten version, too
    }
    else
    {
        //no recycle bin on this machine: the engine warns (warnRecyclerMissing) and deletes permanently
        std::cout << "   recycle bin not used: " << utfTo<std::string>(trashedFile) << " does not exist" << std::endl;
        CHECK(!res.warnings.empty());
    }

#if defined ZEN_MAC //clean up what this test put into the user's trash
    if (recycled)
    {
        CHECK_NO_THROW(removeFilePlain(trashedFile)); //throw FileError
        CHECK_NO_THROW(removeDirectoryPlainRecursion(trashedFolder)); //throw FileError
        if (pathExists(trashedOldVersion))
            CHECK_NO_THROW(removeFilePlain(trashedOldVersion)); //throw FileError
    }
#else
    (void)recycled;
#endif
}
