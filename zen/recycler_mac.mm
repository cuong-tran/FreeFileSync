// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS implementation of recycler.h (Linux: recycler.cpp)
#include "recycler.h"
#import <Foundation/Foundation.h>

using namespace zen;


Zstring zen::impl::moveToRecycleBinGetTrashPath(const Zstring& itemPath) //throw FileError, RecycleBinUnavailable
{
    @autoreleasepool
    {
        NSFileManager* fileManager = [NSFileManager defaultManager];
        NSString* itemPathNs = [fileManager stringWithFileSystemRepresentation:itemPath.c_str() length:itemPath.size()];
        if (!itemPathNs)
            throw FileError(replaceCpy(_("Unable to move %x to the recycle bin."), L"%x", fmtPath(itemPath)),
                            formatSystemError("stringWithFileSystemRepresentation", L"", L"Conversion failed."));

        NSError* error = nil;
        NSURL* trashUrl = nil;
        //boot volume: ~/.Trash; other volumes: <volume>/.Trashes/<uid>; works for files, folders and symlinks (not followed)
        if (![fileManager trashItemAtURL:[NSURL fileURLWithPath:itemPathNs] resultingItemURL:&trashUrl error:&error])
        {
            //e.g. network shares without a trash: "NSCocoaErrorDomain 3328: The operation couldn’t be completed. (Cocoa error 3328.)"
            if ([error.domain isEqualToString:NSCocoaErrorDomain] && error.code == NSFeatureUnsupportedError)
                throw RecycleBinUnavailable(replaceCpy(_("The recycle bin is not available for %x."), L"%x", fmtPath(itemPath)),
                                            formatCFError("trashItemAtURL", (__bridge CFErrorRef)error));

            throw FileError(replaceCpy(_("Unable to move %x to the recycle bin."), L"%x", fmtPath(itemPath)),
                            formatCFError("trashItemAtURL", (__bridge CFErrorRef)error));
        }

        if (const char* trashPath = trashUrl.fileSystemRepresentation) //"nil if [it] can't be represented"
            return trashPath;
        if (const char* trashPath = trashUrl.path.UTF8String) //fallback: the path as a plain string
            return trashPath;
        return Zstring(); //moved, but its new place is unknown

    }
}


void zen::moveToRecycleBin(const Zstring& itemPath) //throw FileError, RecycleBinUnavailable
{
    (void)impl::moveToRecycleBinGetTrashPath(itemPath); //throw FileError, RecycleBinUnavailable
}
