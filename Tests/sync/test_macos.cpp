// macOS only: extended attributes are synchronized on APFS; a FAT volume (disk image, attached without admin rights) gets no
// AppleDouble "._name" files, which the next comparison would see as extra items, and compares equal after the sync;
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


TEST_CASE(syncMacFatVolume)
{
    const time_t t0 = timeBase();
    const Zstring left = appendPath(testDir, "left");
    makeTree(left,
    {
        {"tagged.txt",       {Entry::Type::file, "with xattrs", t0}},
        {"Mixed Case",       {Entry::Type::folder}},
        {"Mixed Case/b.txt", {Entry::Type::file, "b", t0 + 1}}, //odd second: FAT stores times in 2-second steps
    });
    runShell("xattr -w com.freefilesync.synctest 'file value' " + escapeCommandArg(itemPath(left, "tagged.txt"))); //throw SysError
    runShell("xattr -w com.apple.metadata:kMDItemWhereFroms 'somewhere' " + escapeCommandArg(itemPath(left, "Mixed Case")));

    const Zstring imagePath = appendPath(testDir, "fat.dmg");
    const Zstring mountPath = appendPath(testDir, "mnt");
    makeFolder(mountPath, "");
    runShell("hdiutil create -quiet -size 32m -fs MS-DOS -volname SYNCTEST " + escapeCommandArg(imagePath));
    runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
    ZEN_ON_SCOPE_EXIT(try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); }
                      catch (const SysError& e) { reportFailure(__FILE__, __LINE__, "hdiutil detach: " + utfTo<std::string>(e.toString())); });

    const Zstring right = appendPath(mountPath, "target");
    makeFolder(right, "");
    const SyncCfg cfg{.left = left, .right = right};
    const RunResult res = runSync(cfg);
    CHECK_NO_ERRORS(res); //attributes FAT cannot store are skipped silently, the data copy stands

    //no "._tagged.txt" etc.: exactly the left items, times within FAT's 2 seconds
    CHECK_TREES_EQUAL(readTree(right), readTree(left), 2);
    const RunResult again = compareOnly(cfg);
    CHECK_NO_ERRORS(again);
    CHECK(again.allEqual);
    if (!again.allEqual)
        std::cerr << again.toString();

    //a name that is no valid UTF-8, if this file system takes one (APFS doesn't): loading its icon or moving it to the trash fails
    //with an error instead of ending the process with an Objective-C exception
    {
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

    //FAT is case-insensitive: a case-only rename on the left becomes a rename on the volume
    std::filesystem::rename(toPath(itemPath(left, "Mixed Case")), toPath(itemPath(left, "MIXED case")));
    const RunResult renamed = runSync(cfg);
    CHECK_NO_ERRORS(renamed);
    CHECK_EQUAL(renamed.opCount(SO_RENAME_RIGHT), 1);
    CHECK_TREES_EQUAL(readTree(right), readTree(left), 2);
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
