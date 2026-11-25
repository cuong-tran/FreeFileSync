// macOS only: extended attributes are synchronized on APFS; on FAT and exFAT volumes (disk images, attached without admin rights)
// the sync writes no attributes, AppleDouble "._name" companions that macOS itself adds there (com.apple.provenance, for a
// provenance-tracked process) are not reported as items, and the volume compares equal after mirror, versioning, deletion and
// two-way runs;
// the icon loader (base/icon_loader_mac.*) renders file icons and thumbnails off the main thread

#include <thread>
#include <zen/base64.h>
#include <zen/scope_guard.h>
#include <zen/recycler.h>
#include <FreeFileSync/Source/base/icon_loader.h>
#include "sync_test.h"
#if defined ZEN_MAC
    #include <FreeFileSync/Source/base/icon_loader_mac.h>
#endif

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;

#if defined ZEN_MAC
namespace
{
std::string getXattr(const Zstring& itemPath, const std::string& name) //throw SysError
{
    return trimCpy(runShell("xattr -p " + utfTo<Zstring>(name) + ' ' + escapeCommandArg(itemPath)));
}
}


TEST_CASE(syncMacXattrApfs)
{
    const time_t t0 = timeBase();
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"tagged.txt",    {Entry::Type::file, "with xattrs", t0}},
        {"folder",        {Entry::Type::folder}},
        {"folder/a.txt",  {Entry::Type::file, "a", t0}},
    });
    makeFolder(right, "");
    runShell("xattr -w com.freefilesync.synctest 'file value' "   + escapeCommandArg(itemPath(left, "tagged.txt"))); //throw SysError
    runShell("xattr -w com.freefilesync.synctest 'folder value' " + escapeCommandArg(itemPath(left, "folder")));

    const SyncCfg cfg{.left = left, .right = right};
    const RunResult res = runSync(cfg);
    CHECK_NO_ERRORS(res);
    CHECK_TREES_EQUAL(readTree(right), readTree(left));
    CHECK_EQUAL(getXattr(itemPath(right, "tagged.txt"), "com.freefilesync.synctest"), std::string("file value"));
    CHECK_EQUAL(getXattr(itemPath(right, "folder"),     "com.freefilesync.synctest"), std::string("folder value"));

    //an update replaces the file: the new version's attributes come along
    makeFile(left, "tagged.txt", "changed", t0 + TIME_STEP);
    runShell("xattr -w com.freefilesync.synctest 'new value' " + escapeCommandArg(itemPath(left, "tagged.txt")));
    CHECK_NO_ERRORS(runSync(cfg));
    CHECK_EQUAL(getXattr(itemPath(right, "tagged.txt"), "com.freefilesync.synctest"), std::string("new value"));
    CHECK(compareOnly(cfg).allEqual);
}


namespace
{
//AppleDouble companions "._X" of existing items: what a volume without native extended attributes holds on disk for X (macOS
//gives every item a provenance-tracked process creates com.apple.provenance, which such a volume keeps only so). Returns the tree
//without them, and in hasCompanions whether there were any; "._X" without an X (an orphan) stays in the tree
Tree withoutCompanions(const Tree& tree, bool& hasCompanions)
{
    Tree out;
    hasCompanions = false;
    for (const auto& [relPath, entry] : tree)
    {
        const size_t pos = relPath.rfind('/');
        const std::string folder = pos == std::string::npos ? "" : relPath.substr(0, pos + 1);
        const std::string name   = pos == std::string::npos ? relPath : relPath.substr(pos + 1);
        if (name.size() > 2 && name.starts_with("._") && tree.contains(folder + name.substr(2)))
            hasCompanions = true;
        else
            out.emplace(relPath, entry);
    }
    return out;
}


//whether this process is provenance-tracked (see withoutCompanions()): a file it makes on the test folder's own volume shows it
bool provenanceTracked(const Zstring& testDir)
{
    const Zstring probe = appendPath(testDir, "tracked probe.txt");
    writeFile(probe, "x");
    const bool tracked = contains(runShell("xattr " + escapeCommandArg(probe)), "com.apple.provenance"); //throw SysError
    std::filesystem::remove(toPath(probe));
    return tracked;
}
}


TEST_CASE(syncMacFatVolume) //FAT and exFAT
{
    //a process macOS tracks gets AppleDouble companions for each item it creates on these volumes (com.apple.provenance, which it
    //can't remove); the native AFS doesn't report them, and they must never show up as items. Untracked, there are none at all
    const bool tracked = provenanceTracked(testDir);
    std::cout << "   process provenance-tracked: " << (tracked ? "yes" : "no") << std::endl;
    //the folder on the volume as the user sees it, and as it is on disk: exactly the expected items, plus companions only when tracked
    const auto checkVolumeTree = [&](const char* file, int line, const Zstring& root, const Tree& expected)
    {
        bool hasCompanions = false;
        //(the database and the lock are left out, as readTree() does, and their companions with them)
        const Tree onDisk = readTree(root, {".sync.ffs_db", "sync.ffs_lock", "._.sync.ffs_db", "._sync.ffs_lock"});
        checkTreesEqual(file, line, withoutCompanions(onDisk, hasCompanions), expected, 2 /*FAT: 2-second steps*/);
        ++checkCount;
        if (hasCompanions && !tracked)
            reportFailure(file, line, "AppleDouble files on the volume, but this process is not provenance-tracked: the sync wrote them");
    };

    for (const std::string fsType : {"MS-DOS", "ExFAT"})
    {
        std::cout << "   " << fsType << std::endl;
        const Zstring base = appendPath(testDir, utfTo<Zstring>(fsType));
        const time_t t0 = timeBase();
        const Zstring left = appendPath(base, "left");
        makeTree(left,
        {
            {"tagged.txt",                {Entry::Type::file, "with xattrs", t0}},
            {"Mixed Case",                {Entry::Type::folder}},
            {"Mixed Case/b.txt",          {Entry::Type::file, "b", t0 + 1}}, //odd second: FAT stores times in 2-second steps
            {"gone later.txt",            {Entry::Type::file, "deleted with versioning", t0}},
            {"gone folder",               {Entry::Type::folder}},
            {"gone folder/inner.txt",     {Entry::Type::file, "inner", t0}},
        });
        runShell("xattr -w com.freefilesync.synctest 'file value' " + escapeCommandArg(itemPath(left, "tagged.txt"))); //throw SysError
        runShell("xattr -w com.apple.metadata:kMDItemWhereFroms 'somewhere' " + escapeCommandArg(itemPath(left, "Mixed Case")));

        const Zstring imagePath = appendPath(base, "volume.dmg");
        const Zstring mountPath = appendPath(base, "mnt");
        makeFolder(mountPath, "");
        runShell("hdiutil create -quiet -size 32m -fs " + utfTo<Zstring>(fsType) + " -volname SYNCTEST " + escapeCommandArg(imagePath));
        runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
        ZEN_ON_SCOPE_EXIT(try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); }
                          catch (const SysError& e) { reportFailure(__FILE__, __LINE__, "hdiutil detach: " + utfTo<std::string>(e.toString())); });

        const Zstring right    = appendPath(mountPath, "target");
        const Zstring versions = appendPath(mountPath, "versions"); //on the same volume: deleted items are moved there
        makeFolder(right, "");
        SyncCfg cfg{.left = left, .right = right};

        //mirror: the attributes FAT can't store are skipped silently, the data copy stands; the second comparison finds nothing to do
        const RunResult res = runSync(cfg);
        CHECK_NO_ERRORS(res);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));
        const RunResult again = compareOnly(cfg);
        CHECK_NO_ERRORS(again);
        CHECK(again.allEqual);
        if (!again.allEqual)
            std::cerr << again.toString();

        //deletions with versioning on the same volume: items and their companions move together, no orphan stays behind
        std::filesystem::remove(toPath(itemPath(left, "gone later.txt")));
        std::filesystem::remove_all(toPath(itemPath(left, "gone folder")));
        cfg.deletion = DeletionVariant::versioning;
        cfg.versioningFolder = versions;
        const RunResult versioned = runSync(cfg);
        CHECK_NO_ERRORS(versioned);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));
        checkVolumeTree(__FILE__, __LINE__, versions,
        {
            {"gone later.txt",        {Entry::Type::file, "deleted with versioning", t0}},
            {"gone folder",           {Entry::Type::folder}},
            {"gone folder/inner.txt", {Entry::Type::file, "inner", t0}},
        });

        //permanent deletion of a folder with content: no "directory not empty" because of companions
        std::filesystem::remove_all(toPath(itemPath(left, "Mixed Case")));
        cfg.deletion = DeletionVariant::permanent;
        const RunResult removed = runSync(cfg);
        CHECK_NO_ERRORS(removed);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));

        //two-way with the database on the volume (its companion and the lock's are not items either), then an update
        cfg.syncVar = SyncVariant::twoWay;
        const RunResult twoWay = runSync(cfg);
        CHECK_NO_ERRORS(twoWay);
        makeFile(left, "tagged.txt", "changed on the left", t0 + 4 * TIME_STEP);
        const RunResult update = runSync(cfg);
        CHECK_NO_ERRORS(update);
        CHECK_EQUAL(update.opCount(SO_OVERWRITE_RIGHT), 1);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));
        const RunResult twoWayAgain = compareOnly(cfg);
        CHECK_NO_ERRORS(twoWayAgain);
        CHECK(twoWayAgain.allEqual);
        if (!twoWayAgain.allEqual)
            std::cerr << twoWayAgain.toString();
        cfg.syncVar = SyncVariant::mirror;

        //content comparison reads every file the volume lists: companions are no files to it either
        cfg.compareVar = CompareVariant::content;
        const RunResult byContent = compareOnly(cfg);
        CHECK_NO_ERRORS(byContent);
        CHECK(byContent.allEqual);
        cfg.compareVar = CompareVariant::timeSize;

        //an orphan "._X" (no X) is an ordinary item: listed, and deleted by a mirror like any other target-only file
        writeFile(appendPath(right, "._orphan"), "not a companion");
        const RunResult orphan = compareOnly(cfg);
        CHECK_NO_ERRORS(orphan);
        CHECK_EQUAL(orphan.opCount(SO_DELETE_RIGHT), 1);
        const RunResult orphanRemoved = runSync(cfg);
        CHECK_NO_ERRORS(orphanRemoved);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));

        if (fsType == "MS-DOS")
        {
            //a name that is no valid UTF-8, if this file system takes one (APFS doesn't): loading its icon or moving it to the trash
            //fails with an error instead of ending the process with an Objective-C exception
            const Zstring brokenPath = appendPath(mountPath, Zstring("broken \xff name.txt"));
            std::error_code ec;
            bool created = false;
            try { writeFile(brokenPath, "x"); created = pathExists(brokenPath); }
            catch (const std::exception&) {}
            std::cout << "   a name with broken UTF-8 on FAT: " << (created ? "created" : "rejected by the file system") << std::endl;
            if (created)
            {
                try { (void)getFileIcon(brokenPath, 32); } //throw SysError
                catch (const SysError& e) { std::cout << "   its icon: " << utfTo<std::string>(e.toString()) << std::endl; }
                std::filesystem::remove(toPath(brokenPath), ec);
            }
            CHECK_THROWS(FileError, moveToRecycleBin(Zstring("/private/tmp/broken \xff name, not existing"))); //nil NSString: FileError
        }

        //FAT and exFAT are case-insensitive: a case-only rename on the left becomes a rename on the volume
        makeTree(left, {{"Case Only", {Entry::Type::folder}}});
        CHECK_NO_ERRORS(runSync(cfg));
        std::filesystem::rename(toPath(itemPath(left, "Case Only")), toPath(itemPath(left, "CASE only")));
        const RunResult renamed = runSync(cfg);
        CHECK_NO_ERRORS(renamed);
        CHECK_EQUAL(renamed.opCount(SO_RENAME_RIGHT), 1);
        checkVolumeTree(__FILE__, __LINE__, right, readTree(left));
    }
}


TEST_CASE(syncMacIcons)
{
    makeFile(testDir, "doc.txt", "text", timeBase());
    const Zstring pngPath = itemPath(testDir, "pixel.png");
    writeFile(pngPath, stringDecodeBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==")); //1 x 1 pixel

    const auto checkIcon = [](const FileIconHolder& fih, int maxSize)
    {
        CHECK(static_cast<bool>(fih));
        CHECK(fih.image.getWidth() > 0 && fih.image.getWidth() <= maxSize && fih.image.getHeight() > 0 && fih.image.getHeight() <= maxSize);
        CHECK(fih.maxSize == maxSize);
    };
    //on a worker thread, as the GUI's icon buffer does
    std::thread([&]
    {
        try
        {
            checkIcon(getFileIcon(itemPath(testDir, "doc.txt"), 32), 32); //throw SysError
            checkIcon(getFileIcon(testDir, 48), 48);                       //a folder
            checkIcon(getIconByTemplatePath(Zstr("file.pdf"), 16), 16);
            checkIcon(getIconByTemplatePath(Zstr("no extension"), 16), 16);
            checkIcon(genericFileIcon(24), 24);
            checkIcon(genericDirIcon(24), 24);
            checkIcon(getTrashIcon(24), 24);
            checkIcon(getFileManagerIcon(24), 24);

            const ImageHolder thumb = getThumbnailImage(pngPath, 64); //throw SysError
            CHECK(thumb.getWidth() == 1 && thumb.getHeight() == 1); //not enlarged
        }
        catch (const SysError& e) { reportFailure(__FILE__, __LINE__, "icon loader: " + utfTo<std::string>(e.toString())); }

        CHECK_THROWS(SysError, getFileIcon(itemPath(testDir, "missing"), 32));
        CHECK_THROWS(SysError, getThumbnailImage(itemPath(testDir, "doc.txt"), 64)); //not an image

        //paths that are no valid UTF-8 (names APFS rejects, but other file systems may hold) have no NSString: an error, never an
        //Objective-C exception, which would end the process. Checked at the AppKit layer directly (icon_loader_mac.h): see
        //syncMacFatVolume for a real file with such a name where the file system allows it
        const std::string brokenPath = utfTo<std::string>(testDir) + "/broken \xff\xfe name.png";
        CHECK(std::holds_alternative<macos::RenderError>(macos::renderIcon     (macos::IconKind::ofFile, brokenPath, 32)));
        CHECK(std::holds_alternative<macos::RenderError>(macos::renderThumbnail(brokenPath, 32)));
        CHECK(std::holds_alternative<macos::RenderedImage>(macos::renderIcon(macos::IconKind::ofExtension, "\xff\xfe", 16))); //broken extension: generic icon
    }).join();
}
#endif
