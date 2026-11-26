// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "ffs_paths.h"
#include <zen/file_access.h>
#include <zen/sys_info.h>
#include <zen/build_info.h>
#if defined ZEN_MAC
    #include "ffs_paths_mac.h"
#endif

    #include <iostream> //std::cerr

using namespace zen;


namespace
{
Zstring getProcessParentFolderPath()
{
    //buffer getSymlinkResolvedPath()!
    //note: compiler generates magic-statics code => fine, we don't expect accesses during shutdown => don't need FunStatGlobal<>
    static const Zstring exeFolderParentPath = []
    {
        try
        {
            const Zstring& processPath = getProcessPath(); //throw FileError
            /*  no need for getSymlinkResolvedPath():
                => support file systems with buggy GetFinalPathNameByHandle() implementation, e.g. Dokany-based: https://freefilesync.org/forum/viewtopic.php?t=8828
                => we're already supporting calling FFS via symlink for launcher executable, which guarantees: */
            assert(getItemType(                     processPath)  != ItemType::symlink); //throw FileError
            assert(getItemType(*getParentFolderPath(processPath)) != ItemType::symlink); //throw FileError

            /*  Linux: <install>/Bin/FreeFileSync_<arch>                 => <install>, resources in <install>/Resources
                macOS: <bundle>.app/Contents/MacOS/<app>                 => <bundle>.app/Contents, resources in Contents/Resources (the bundle layout)
                       unbundled (tests, development): Build/Bin/<app>   => Build, as on Linux                                       */
            return *getParentFolderPath(*getParentFolderPath(processPath)); //no parent folder!!? => let it crash!
        }
        catch (const FileError& e)
        {
            throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Failed to get process parent folder. " + utfTo<std::string>(e.toString()));
        }
    }();
    return exeFolderParentPath;
}
}


Zstring fff::getInstallDirPath()
{
    return getProcessParentFolderPath();

}




Zstring fff::getResourceDirPath()
{
    return appendPath(getProcessParentFolderPath(), Zstr("Resources"));
}


Zstring fff::getConfigDirPath()
{
    //note: compiler generates magic-statics code => fine, we don't expect accesses during shutdown
    static const Zstring ffsConfigPath = []
    {
        /*  Windows:            %AppData%\FreeFileSync
            macOS:              ~/Library/Application Support/FreeFileSync
            Linux (XDG layout): ~/.config/FreeFileSync                        */
        const Zstring& configPath = []
        {
            try
            {
                return appendPath(getUserDataPath(), Zstr("FreeFileSync")); //throw FileError
            }
            catch (const FileError& e)
            {
                throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Failed to get config path. " + utfTo<std::string>(e.toString()));
            }
        }();

        try
        {
            createDirectoryIfMissingRecursion(configPath); //throw FileError
        }
        catch (const FileError& e) { logExtraError(e.toString()); }

        return configPath;
    }();
    return ffsConfigPath;
}


//this function is called by RealTimeSync!!!
Zstring fff::getFreeFileSyncLauncherPath() //throw FileError
{
#if defined ZEN_MAC //bundles side by side, as in the official Mac package: <folder>/RealTimeSync.app and <folder>/FreeFileSync.app
    const Zstring installDirPath = getInstallDirPath(); //bundle: <folder>/RealTimeSync.app/Contents
    if (endsWith(installDirPath, Zstr(".app/Contents")))
        if (const std::optional<Zstring> bundlePath = getParentFolderPath(installDirPath))
        {
            auto launcherNextTo = [](const Zstring& bundle) -> std::optional<Zstring>
            {
                if (const std::optional<Zstring> folderPath = getParentFolderPath(bundle))
                    return appendPath(*folderPath, Zstr("FreeFileSync.app/Contents/MacOS/FreeFileSync"));
                return std::nullopt;
            };
            auto launcherExists = [](const Zstring& launcherPath)
            {
                try { return itemExists(launcherPath); } /*throw FileError*/ catch (FileError&) { return false; }
            };

            const std::optional<Zstring> siblingPath = launcherNextTo(*bundlePath);
            if (siblingPath && launcherExists(*siblingPath))
                return *siblingPath;

            //App Translocation (quarantined bundle run from a read-only mount without its sibling): next to the original bundle
            if (const std::optional<Zstring> originalBundlePath = getUntranslocatedBundlePath(*bundlePath))
                if (const std::optional<Zstring> launcherPath = launcherNextTo(*originalBundlePath);
                    launcherPath && launcherExists(*launcherPath))
                    return *launcherPath;

            /*  the bundles were installed apart: FreeFileSync.app where LaunchServices knows it, installed copies first (/Applications,
                ~/Applications), then others, never one on a mounted volume (an old disk image, an external disk)  */
            const std::vector<Zstring> ffsBundlePaths = getAppBundlePathsById(freeFileSyncBundleId);
            const std::optional<Zstring> homePath = [] () -> std::optional<Zstring>
            {
                try { return getUserHome(); } /*throw FileError*/ catch (FileError&) { return std::nullopt; }
            }();
            auto isInstalled = [&](const Zstring& path)
            {
                return startsWith(path, Zstr("/Applications/")) ||
                       (homePath && startsWith(path, appendPath(*homePath, Zstr("Applications")) + Zstr('/')));
            };
            for (const bool installedPass : {true, false})
                for (const Zstring& ffsBundlePath : ffsBundlePaths)
                    if (isInstalled(ffsBundlePath) == installedPass && !startsWith(ffsBundlePath, Zstr("/Volumes/")))
                        if (const Zstring launcherPath = appendPath(ffsBundlePath, Zstr("Contents/MacOS/FreeFileSync"));
                            launcherExists(launcherPath))
                            return launcherPath;

            if (siblingPath)
                return *siblingPath; //not found anywhere: the expected place, for the error message when it is started
        }
    return appendPath(installDirPath, Zstr("FreeFileSync")); //unbundled: as on Linux
#else
    return appendPath(getInstallDirPath(), Zstr("FreeFileSync"));
#endif

}
