// macOS only: extended attributes are synchronized on APFS; on FAT and exFAT volumes (disk images, attached without admin rights)
// the sync writes no attributes, AppleDouble "._name" companions that macOS itself adds there (com.apple.provenance, for a
// provenance-tracked process) are not reported as items, and the volume compares equal after mirror, versioning, deletion and
// two-way runs; a "._X" of the user's own (no AppleDouble file) is listed and synchronized, real companions stay hidden;
// the icon loader (base/icon_loader_mac.*) renders file icons and thumbnails off the main thread

#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#include <zen/base64.h>
#include <zen/scope_guard.h>
#include <zen/recycler.h>
#include <FreeFileSync/Source/base/icon_loader.h>
#include "sync_test.h"
#if defined ZEN_MAC
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/xattr.h>
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


namespace
{
const char appleDoubleHeader[] = {0x00, 0x05, 0x16, 0x07, 0x00, 0x02, 0x00, 0x00}; //magic, version 2

bool hasAppleDoubleHeader(const std::filesystem::path& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    char header[sizeof(appleDoubleHeader)] = {};
    in.read(header, sizeof(header));
    return in.gcount() == static_cast<std::streamsize>(sizeof(header)) && std::memcmp(header, appleDoubleHeader, sizeof(header)) == 0;
}

std::string describeOnDisk(const Zstring& filePath) //what a "._" file holds now, for the log
{
    if (!pathExists(filePath))
        return "not there";
    if (hasAppleDoubleHeader(toPath(filePath)))
        return "an AppleDouble file (" + std::to_string(std::filesystem::file_size(toPath(filePath))) + " bytes)";
    try { return "the content \"" + readFile(filePath) + '"'; }
    catch (const std::exception& e) { return e.what(); }
}

//what the native AFS has to list on a volume without native extended attributes (afs/native.cpp, hideAppleDoubleCompanions()):
//everything on disk, except "._X" next to an X that is a regular file with the AppleDouble header. Relative, '/'-separated
std::set<std::string> expectedListing(const Zstring& folder)
{
    namespace fs = std::filesystem;
    std::set<std::string> out;
    for (auto it = fs::recursive_directory_iterator(toPath(folder)); it != fs::recursive_directory_iterator(); ++it) //does not follow symlinks
    {
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        std::error_code ec;
        if (name.size() > 2 && name.starts_with("._") && fs::is_regular_file(fs::symlink_status(p)) &&
            fs::exists(fs::symlink_status(p.parent_path() / name.substr(2), ec)) && hasAppleDoubleHeader(p))
            continue;
        out.insert(fs::relative(p, toPath(folder)).generic_string());
    }
    return out;
}

//what the native AFS lists: the right side of a comparison with an empty folder
std::set<std::string> engineListing(const Zstring& folder, const Zstring& emptyFolder)
{
    const RunResult res = compareOnly({.left = emptyFolder, .right = folder});
    CHECK_NO_ERRORS(res);
    std::set<std::string> out;
    for (const std::string& row : res.plan)
        out.insert(row.substr(0, row.rfind(": ")));
    return out;
}

void checkListing(const char* file, int line, const Zstring& folder, const Zstring& emptyFolder)
{
    const std::set<std::string> listed   = engineListing(folder, emptyFolder);
    const std::set<std::string> expected = expectedListing(folder);
    ++checkCount;
    if (listed != expected)
    {
        std::string msg = "native AFS listing of " + utfTo<std::string>(folder) + ":";
        for (const std::string& p : expected) if (!listed  .contains(p)) msg += "\n    missing: " + p;
        for (const std::string& p : listed)   if (!expected.contains(p)) msg += "\n    not expected: " + p;
        reportFailure(file, line, msg);
    }
}

std::string errnoText(int ec) { return std::string(::strerror(ec)) + " (errno " + std::to_string(ec) + ')'; }
}


TEST_CASE(syncMacFatAppleDoubleNames) //FAT and exFAT
{
    //hideAppleDoubleCompanions() hides only real AppleDouble companions: a user's own "._X" next to X is an item like any other
    const bool tracked = provenanceTracked(testDir);
    std::cout << "   process provenance-tracked: " << (tracked ? "yes" : "no") << std::endl;

    for (const std::string fsType : {"MS-DOS", "ExFAT"})
    {
        std::cout << "   " << fsType << std::endl;
        const Zstring base  = appendPath(testDir, utfTo<Zstring>(fsType));
        const Zstring empty = appendPath(base, "empty");
        const Zstring left  = appendPath(base, "left");
        makeFolder(empty, "");

        const Zstring imagePath = appendPath(base, "volume.dmg");
        const Zstring mountPath = appendPath(base, "mnt");
        makeFolder(mountPath, "");
        runShell("hdiutil create -quiet -size 128m -fs " + utfTo<Zstring>(fsType) + " -volname ADNAMES " + escapeCommandArg(imagePath)); //throw SysError
        runShell("hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint " + escapeCommandArg(mountPath) + ' ' + escapeCommandArg(imagePath));
        ZEN_ON_SCOPE_EXIT(try { runShell("hdiutil detach -quiet -force " + escapeCommandArg(mountPath)); }
                          catch (const SysError& e) { reportFailure(__FILE__, __LINE__, "hdiutil detach: " + utfTo<std::string>(e.toString())); });

        //1. what the kernel does when this process writes "._X" next to an X it created (a companion of X exists exactly when
        //   the process is tracked: com.apple.provenance)
        const Zstring names = appendPath(mountPath, "names");
        makeFolder(names, "");
        writeFile(itemPath(names, "user.txt"), "item");
        const Zstring userAd = itemPath(names, "._user.txt");
        const bool hasCompanion = pathExists(userAd);
        std::cout << "      X written by this process: companion \"._X\" " << (hasCompanion ? "made by macOS: " + describeOnDisk(userAd) : std::string("none")) << std::endl;
        const std::string userData = "the user's own file named like a companion";
        int fd = ::open(userAd.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        const int createError = fd == -1 ? errno : 0;
        if (fd != -1)
        {
            CHECK(::write(fd, userData.data(), userData.size()) == static_cast<ssize_t>(userData.size()));
            ::close(fd);
        }
        std::cout << "      open(\"._X\", O_CREAT | O_EXCL): " << (fd != -1 ? std::string("created") : errnoText(createError)) << std::endl;
        if (hasCompanion)
        {
            CHECK_EQUAL(createError, EEXIST); //the companion is that file
            //overwrite the companion with ordinary content: then it is no AppleDouble file, and must be listed
            fd = ::open(userAd.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
            const int truncError = fd == -1 ? errno : 0;
            if (fd != -1)
            {
                const ssize_t written = ::write(fd, userData.data(), userData.size());
                std::cout << "      open(\"._X\", O_TRUNC) and write(): " << (written == static_cast<ssize_t>(userData.size()) ? std::string("written") : errnoText(errno)) << std::endl;
                ::close(fd);
            }
            else
                std::cout << "      open(\"._X\", O_TRUNC): " << errnoText(truncError) << std::endl;
            const ssize_t attrSize = ::listxattr(itemPath(names, "user.txt").c_str(), nullptr, 0, XATTR_NOFOLLOW);
            std::cout << "      afterwards \"._X\" holds " << describeOnDisk(userAd) << "; listxattr(X): " << (attrSize >= 0 ? std::to_string(attrSize) + " bytes" : errnoText(errno)) << std::endl;
        }
        else
            CHECK_EQUAL(createError, 0); //no companion: an ordinary name
        checkListing(__FILE__, __LINE__, names, empty);
        if (pathExists(userAd) && !hasAppleDoubleHeader(toPath(userAd)))
            CHECK(engineListing(names, empty).contains("._user.txt"));

        //2. a real companion (an attribute set explicitly: macOS writes it as "._real.txt") stays hidden, tracked or not; an
        //   AppleDouble file without its item and a folder named like a companion are listed
        writeFile(itemPath(names, "real.txt"), "with an attribute");
        CHECK(::setxattr(itemPath(names, "real.txt").c_str(), "com.freefilesync.synctest", "value", 5, 0, XATTR_NOFOLLOW) == 0);
        CHECK(hasAppleDoubleHeader(toPath(itemPath(names, "._real.txt"))));
        writeFile(itemPath(names, "._orphan.txt"), readFile(itemPath(names, "._real.txt"))); //AppleDouble content, no "orphan.txt"
        makeFolder(names, "dir");
        std::error_code ec;
        std::filesystem::create_directory(toPath(itemPath(names, "._dir")), ec);
        std::cout << "      folder \"._dir\" next to \"dir\": " << (ec ? "refused: " + ec.message() : std::string("created")) << std::endl;
        checkListing(__FILE__, __LINE__, names, empty);
        const std::set<std::string> listed = engineListing(names, empty);
        CHECK(!listed.contains("._real.txt"));
        CHECK(listed.contains("real.txt"));
        CHECK(listed.contains("._orphan.txt"));
        if (!ec)
            CHECK(listed.contains("._dir"));

        //3. a mirror of an APFS folder with "data.txt" and a "._data.txt" of its own (no AppleDouble file) to the volume
        const time_t t0 = timeBase();
        makeTree(left,
        {
            {"data.txt",   {Entry::Type::file, "data", t0}},
            {"._data.txt", {Entry::Type::file, userData, t0}},
        });
        const Zstring right = appendPath(mountPath, "target");
        makeFolder(right, "");
        const SyncCfg cfg{.left = left, .right = right};
        const RunResult res = runSync(cfg);
        const RunResult again = compareOnly(cfg);
        std::cout << "      mirror of X and a user's \"._X\": " << res.errors.size() << " error(s); on the volume \"._X\" holds "
                  << describeOnDisk(itemPath(right, "._data.txt")) << "; next comparison: " << (again.allEqual ? "equal" : "differences") << std::endl;
        if (!tracked)
        {
            //without companions the name is an ordinary one: the file syncs like any other
            CHECK_NO_ERRORS(res);
            CHECK_NO_ERRORS(again);
            CHECK(again.allEqual);
            CHECK_EQUAL(readFile(itemPath(right, "._data.txt")), userData);
        }
        else if (!again.allEqual || !res.errors.empty()) //the kernel keeps X's provenance in that name (measured, see the log)
            std::cout << res.toString() << again.toString();
        checkListing(__FILE__, __LINE__, right, empty);

        //4. cost of the header check: n files with a companion each (an attribute set: the companion is there whether the process
        //   is tracked or not). The listing opens every companion (a candidate); the same calls as isAppleDoubleFile() on their
        //   own show what that part costs
        const int n = 2000;
        const Zstring many = appendPath(mountPath, "many");
        makeFolder(many, "");
        for (int i = 0; i < n; ++i)
        {
            const Zstring filePath = itemPath(many, "file " + std::to_string(i) + ".txt");
            writeFile(filePath, "x");
            if (::setxattr(filePath.c_str(), "com.freefilesync.synctest", "v", 1, 0, XATTR_NOFOLLOW) != 0)
            {
                reportFailure(__FILE__, __LINE__, "setxattr: " + errnoText(errno));
                break;
            }
        }
        using Clock = std::chrono::steady_clock;
        const auto msSince = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
        double listingFirst = 0, listingBest = 1e9, checkFirst = 0, checkBest = 1e9;
        for (int run = 0; run < 3; ++run)
        {
            Clock::time_point start = Clock::now();
            CHECK_EQUAL(engineListing(many, empty).size(), static_cast<size_t>(n)); //the companions hidden
            const double listing = msSince(start);

            start = Clock::now();
            const int dirFd = ::open(many.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            CHECK(dirFd != -1);
            int adFiles = 0;
            for (int i = 0; i < n && dirFd != -1; ++i)
            {
                const std::string name = "._file " + std::to_string(i) + ".txt";
                struct stat st = {};
                if (::fstatat(dirFd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode))
                    continue;
                const int fd2 = ::openat(dirFd, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
                if (fd2 == -1)
                    continue;
                char header[sizeof(appleDoubleHeader)] = {};
                if (::fstat(fd2, &st) == 0 && ::pread(fd2, header, sizeof(header), 0) == static_cast<ssize_t>(sizeof(header)) &&
                    std::memcmp(header, appleDoubleHeader, sizeof(header)) == 0)
                    ++adFiles;
                ::close(fd2);
            }
            if (dirFd != -1)
                ::close(dirFd);
            const double check = msSince(start);
            CHECK_EQUAL(adFiles, n);

            if (run == 0) { listingFirst = listing; checkFirst = check; }
            listingBest = std::min(listingBest, listing);
            checkBest   = std::min(checkBest,   check);
        }
        std::cout << "      " << n << " files with a companion each: comparison " << static_cast<int>(listingFirst) << " ms first, "
                  << static_cast<int>(listingBest) << " ms best of 3; header checks of the " << n << " companions "
                  << static_cast<int>(checkFirst) << " ms first, " << static_cast<int>(checkBest) << " ms best (about "
                  << static_cast<int>(checkBest * 1000 / n) << " µs per candidate)" << std::endl;
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
