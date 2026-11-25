// zen/recycler.h
//
// Linux: run.sh points HOME and XDG_DATA_HOME into its temporary folder, so a successful move ends up in a trash folder there
// instead of the user's real recycle bin.
// macOS: the trash can't be redirected (NSFileManager uses the user's ~/.Trash, or <volume>/.Trashes/<uid>), so the test
// moves its items through impl::moveToRecycleBinGetTrashPath() (what moveToRecycleBin() calls) and deletes them from the trash
// by the path it returns; nothing else in the trash is touched.

#include <zen/recycler.h>
#include <zen/file_access.h>
#include "test.h"

using namespace zen;
using namespace zentest;


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
#ifdef ZEN_MAC
            const Zstring trashPath = impl::moveToRecycleBinGetTrashPath(itemPath); //throw FileError, RecycleBinUnavailable
            CHECK(!pathExists(itemPath)); //moved: gone from its folder
            CHECK(!trashPath.empty() && pathExists(trashPath));
            if (!trashPath.empty() && pathExists(trashPath))
            {
                if (itemPath == filePath)
                {
                    CHECK_EQUAL(readFile(trashPath), std::string("recycle me")); //the same item
                    removeFilePlain(trashPath); //throw FileError
                }
                else
                {
                    CHECK(pathExists(appendPath(trashPath, nativeRelPath("sub/file"))));
                    removeDirectoryPlainRecursion(trashPath); //throw FileError
                }
                CHECK(!pathExists(trashPath));
            }
#else
            moveToRecycleBin(itemPath); //throw FileError, RecycleBinUnavailable
            CHECK(!pathExists(itemPath)); //moved: gone from its folder
#endif
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
