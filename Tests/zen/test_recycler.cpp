// zen/recycler.h
//
// Linux: run.sh points HOME and XDG_DATA_HOME into its temporary folder, so a successful move ends up in a trash folder there
// instead of the user's real recycle bin.

#include <zen/recycler.h>
#include "test.h"

using namespace zen;
using namespace zentest;


#ifdef ZEN_MAC
NOT_PORTED_YET_TEST_CASE(recycler, "zen/recycler (NSFileManager trashItemAtURL), M3 step 2")
#else
SANDBOX_TEST_CASE(recycler)
{
    const Zstring filePath = appendPath(testDir, "to recycle.txt");
    writeFile(filePath, "recycle me");

    const Zstring dirPath = appendPath(testDir, "folder to recycle");
    std::filesystem::create_directories(toPath(appendPath(dirPath, "sub")));
    writeFile(appendPath(dirPath, nativeRelPath("sub/file")), "x");

    for (const Zstring& itemPath : {filePath, dirPath})
        try
        {
            moveToRecycleBin(itemPath); //throw FileError, RecycleBinUnavailable
            CHECK(!pathExists(itemPath)); //moved: gone from its folder
        }
        catch (const RecycleBinUnavailable& e)
        {
            //no recycle bin (e.g. some CI machines): reported as RecycleBinUnavailable, the item stays where it is
            std::cout << "   recycle bin not available: " << utfTo<std::string>(e.toString()) << std::endl;
            CHECK(pathExists(itemPath));
            CHECK(!e.toString().empty());
        }
        catch (const FileError& e) //any other error is a failure
        {
            reportFailure(__FILE__, __LINE__, "moveToRecycleBin: " + utfTo<std::string>(e.toString()));
        }

    //"fails if item is not existing (anymore)"
    CHECK_THROWS_NOT(FileError, RecycleBinUnavailable, moveToRecycleBin(appendPath(testDir, "missing")));
}
#endif
