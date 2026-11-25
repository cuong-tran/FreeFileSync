// A ZIP archive ("zip:<archive>", afs/zip.cpp) as a read-only sync source. The archive and the folder it was made from are
// fixtures from run.sh (SYNC_TEST_ZIP, SYNC_TEST_ZIP_REF).

#include <cstdlib>
#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;


TEST_CASE(syncZipSource)
{
    const char* archive = std::getenv("SYNC_TEST_ZIP");
    const char* refPath = std::getenv("SYNC_TEST_ZIP_REF");
    if (!archive || !refPath)
    {
        std::cout << "   SKIPPED: needs the ZIP fixture from Tests/sync/run.sh (SYNC_TEST_ZIP, SYNC_TEST_ZIP_REF)" << std::endl;
        return;
    }
    const std::string archiveBefore = readFile(archive);
    const Zstring right = appendPath(testDir, "right");
    makeFile(right, "not in the archive.txt", "deleted by mirror", timeBase());

    const SyncCfg cfg{.left = Zstr("zip:") + Zstring(archive), .right = right};
    const RunResult res = runSync(cfg);
    CHECK_NO_ERRORS(res);
    //names, folders and content as in the folder the archive was made from; ZIP stores times in 2-second steps
    CHECK_TREES_EQUAL(readTree(right), readTree(refPath), 2);
    CHECK(compareOnly(cfg).allEqual);

    //read-only: syncing towards the archive fails with an error for each item, and changes nothing
    makeFile(right, "new right.txt", "cannot go into the archive", timeBase());
    const RunResult twoWay = runSync({.left = cfg.left, .right = right, .syncVar = SyncVariant::twoWay});
    CHECK(!twoWay.errors.empty());
    CHECK(readFile(archive) == archiveBefore);
    CHECK(pathExists(itemPath(right, "new right.txt")));
}
