// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS implementation of dir_watcher.h: FSEvents with file-level events (Linux: dir_watcher.cpp)
#include "dir_watcher.h"
#include <mutex>
#include <sys/stat.h>
#include <stdlib.h> //realpath
#include <CoreServices/CoreServices.h> //FSEvents
#include <dispatch/dispatch.h>
#include "file_access.h"
#include "scope_guard.h"

using namespace zen;

/*  - FSEvents watches the whole tree, including subfolders created later
    - events of the same path are coalesced, and their flags accumulate (e.g. "created | modified | removed" for a file written
      and deleted shortly after): the item's existence when the event arrives decides between create and remove; renames
      come as two "renamed" events (old and new path), decided the same way
    - paths are reported resolved (no symlinks, e.g. /private/var instead of /var) and in their on-disk case: they are mapped
      back to the path the watcher was created with
    - removal, renaming and unmounting of the watched folder itself: ChangeType::baseFolderUnavailable (kFSEventStreamCreateFlagWatchRoot) */

namespace
{
struct WatchState //accessed by the FSEvents callback on the stream's dispatch queue
{
    Zstring baseDirPath;     //as passed by the caller
    Zstring baseDirPathReal; //realpath(): as reported by FSEvents
    std::mutex lockChanges;
    std::vector<DirWatcher::Change> changes; //protected by lockChanges
};


std::optional<Zstring> mapEventPath(const Zstring& eventPath, const WatchState& shared)
{
    //compare like the file system: case-insensitive and normalization-insensitive on default APFS volumes
    for (const Zstring* base : {&shared.baseDirPathReal, &shared.baseDirPath})
        if (eventPath.size() >= base->size() &&
            equalNativePath(Zstring(eventPath.begin(), eventPath.begin() + base->size()), *base))
        {
            if (eventPath.size() == base->size())
                return shared.baseDirPath;
            if (eventPath[base->size()] == FILE_NAME_SEPARATOR)
                return appendPath(shared.baseDirPath, Zstring(eventPath.begin() + base->size() + 1, eventPath.end()));
        }
    return std::nullopt; //e.g. a parent folder for kFSEventStreamEventFlagRootChanged
}


bool existsNoFollow(const Zstring& itemPath)
{
    struct stat info = {};
    return ::lstat(itemPath.c_str(), &info) == 0; //symlinks are items of their own
}


void eventCallback(ConstFSEventStreamRef /*streamRef*/,
                   void* clientCallBackInfo,
                   size_t numEvents,
                   void* eventPaths, //char** without kFSEventStreamCreateFlagUseCFTypes
                   const FSEventStreamEventFlags eventFlags[],
                   const FSEventStreamEventId /*eventIds*/[])
{
    auto& shared = *static_cast<WatchState*>(clientCallBackInfo);
    const char* const* paths = static_cast<const char* const*>(eventPaths);

    std::vector<DirWatcher::Change> changes;
    for (size_t i = 0; i < numEvents; ++i)
    {
        const FSEventStreamEventFlags flags = eventFlags[i];
        const Zstring eventPath = paths[i];

        if (flags & (kFSEventStreamEventFlagRootChanged | //watched folder removed, renamed, or (parent) moved
                     kFSEventStreamEventFlagUnmount))     //volume unmounted: path is the mount point
        {
            if ((flags & kFSEventStreamEventFlagUnmount) && !mapEventPath(eventPath, shared) &&
                !startsWith(appendSeparator(shared.baseDirPathReal), appendSeparator(eventPath))) //another volume
                continue;
            changes.push_back({DirWatcher::ChangeType::baseFolderUnavailable, shared.baseDirPath});
            continue;
        }

        const std::optional<Zstring> itemPath = mapEventPath(eventPath, shared);
        if (!itemPath)
            continue;

        if (flags & (kFSEventStreamEventFlagMustScanSubDirs | //events were dropped: something changed somewhere below itemPath
                     kFSEventStreamEventFlagUserDropped |
                     kFSEventStreamEventFlagKernelDropped))
            changes.push_back({DirWatcher::ChangeType::update, *itemPath});
        else if (flags & (kFSEventStreamEventFlagItemCreated |
                          kFSEventStreamEventFlagItemRemoved |
                          kFSEventStreamEventFlagItemRenamed))
            changes.push_back({existsNoFollow(*itemPath) ? DirWatcher::ChangeType::create : DirWatcher::ChangeType::remove, *itemPath});
        else if (flags & (kFSEventStreamEventFlagItemModified      |
                          kFSEventStreamEventFlagItemInodeMetaMod  |
                          kFSEventStreamEventFlagItemFinderInfoMod |
                          kFSEventStreamEventFlagItemChangeOwner   |
                          kFSEventStreamEventFlagItemXattrMod      |
                          kFSEventStreamEventFlagItemCloned))
            changes.push_back({DirWatcher::ChangeType::update, *itemPath});
        //else: kFSEventStreamEventFlagHistoryDone, kFSEventStreamEventFlagEventIdsWrapped, kFSEventStreamEventFlagMount, ...
    }

    if (!changes.empty())
    {
        std::lock_guard dummy(shared.lockChanges);
        shared.changes.insert(shared.changes.end(), changes.begin(), changes.end());
    }
}
}


struct DirWatcher::Impl
{
    WatchState shared;
    FSEventStreamRef stream = nullptr;
    dispatch_queue_t queue = nullptr;
};


DirWatcher::DirWatcher(const Zstring& dirPath) : //throw FileError
    baseDirPath_(dirPath),
    pimpl_(std::make_unique<Impl>())
{
    const std::wstring errorMsg = replaceCpy(_("Cannot monitor directory %x."), L"%x", fmtPath(baseDirPath_));

    //FSEventStreamCreate() doesn't check the path: fail like the Linux implementation for missing folders and files
    struct stat dirInfo = {};
    if (::stat(baseDirPath_.c_str(), &dirInfo) != 0)
        THROW_LAST_FILE_ERROR(errorMsg, "stat");
    if (!S_ISDIR(dirInfo.st_mode))
        throw FileError(errorMsg, formatSystemError("stat", ENOTDIR));

    char* dirPathReal = ::realpath(baseDirPath_.c_str(), nullptr);
    if (!dirPathReal)
        THROW_LAST_FILE_ERROR(errorMsg, "realpath");
    ZEN_ON_SCOPE_EXIT(::free(dirPathReal));

    pimpl_->shared.baseDirPath     = baseDirPath_;
    pimpl_->shared.baseDirPathReal = dirPathReal;

    CFStringRef dirPathCf = ::CFStringCreateWithCString(nullptr, dirPathReal, kCFStringEncodingUTF8);
    if (!dirPathCf)
        throw FileError(errorMsg, formatSystemError("CFStringCreateWithCString", L"", L"Conversion failed."));
    ZEN_ON_SCOPE_EXIT(::CFRelease(dirPathCf));

    const void* pathArray[] = {dirPathCf};
    CFArrayRef pathsToWatch = ::CFArrayCreate(nullptr, pathArray, 1, &kCFTypeArrayCallBacks);
    if (!pathsToWatch)
        throw FileError(errorMsg, formatSystemError("CFArrayCreate", L"", L"Out of memory."));
    ZEN_ON_SCOPE_EXIT(::CFRelease(pathsToWatch));

    FSEventStreamContext context = {.info = &pimpl_->shared}; //outlives the stream: see ~DirWatcher()

    pimpl_->stream = ::FSEventStreamCreate(nullptr,              //CFAllocatorRef allocator
                                           eventCallback,        //FSEventStreamCallback callback
                                           &context,             //FSEventStreamContext* context
                                           pathsToWatch,         //CFArrayRef pathsToWatch
                                           kFSEventStreamEventIdSinceNow, //FSEventStreamEventId sinceWhen
                                           0.05,                 //CFTimeInterval latency [s]: NoDefer delivers the first event at once
                                           kFSEventStreamCreateFlagFileEvents | //FSEventStreamCreateFlags flags
                                           kFSEventStreamCreateFlagNoDefer    |
                                           kFSEventStreamCreateFlagWatchRoot);
    if (!pimpl_->stream)
        throw FileError(errorMsg, formatSystemError("FSEventStreamCreate", L"", L"Failed to create event stream."));
    ZEN_ON_SCOPE_FAIL(::FSEventStreamRelease(pimpl_->stream));

    pimpl_->queue = ::dispatch_queue_create("zen.DirWatcher", DISPATCH_QUEUE_SERIAL);
    if (!pimpl_->queue)
        throw FileError(errorMsg, formatSystemError("dispatch_queue_create", L"", L"Failed to create dispatch queue."));
    ZEN_ON_SCOPE_FAIL(::dispatch_release(pimpl_->queue));

    ::FSEventStreamSetDispatchQueue(pimpl_->stream, pimpl_->queue);

    if (!::FSEventStreamStart(pimpl_->stream))
    {
        ::FSEventStreamInvalidate(pimpl_->stream);
        throw FileError(errorMsg, formatSystemError("FSEventStreamStart", L"", L"Failed to start event stream."));
    }
}


DirWatcher::~DirWatcher()
{
    ::FSEventStreamStop(pimpl_->stream);
    ::FSEventStreamInvalidate(pimpl_->stream); //no callbacks are scheduled after this...
    ::dispatch_sync_f(pimpl_->queue, nullptr, [](void*) {}); //...and one that is running has finished (serial queue): "shared" is unused now
    ::FSEventStreamRelease(pimpl_->stream);
    ::dispatch_release(pimpl_->queue);
}


std::vector<DirWatcher::Change> DirWatcher::fetchChanges(const std::function<void()>& requestUiUpdate, std::chrono::milliseconds cbInterval) //throw FileError
{
    std::vector<Change> output;
    {
        std::lock_guard dummy(pimpl_->shared.lockChanges);
        output.swap(pimpl_->shared.changes);
    }
    return output;
}
