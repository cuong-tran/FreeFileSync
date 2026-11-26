// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "ffs_paths_mac.h"
#include <climits> //PATH_MAX
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h> //LSCopyApplicationURLsForBundleIdentifier
#include <zen/scope_guard.h>
#include <zen/string_tools.h>

using namespace zen;


namespace
{
Zstring getPathFromUrl(CFURLRef url)
{
    char buffer[PATH_MAX] = {};
    if (!::CFURLGetFileSystemRepresentation(url, true /*resolveAgainstBase*/, reinterpret_cast<UInt8*>(buffer), sizeof(buffer)))
        return Zstring();

    Zstring path = buffer;
    while (path.size() > 1 && endsWith(path, Zstr('/'))) //directory URLs may end with a slash
        path.pop_back();
    return path;
}
}


std::optional<Zstring> fff::getUntranslocatedBundlePath(const Zstring& bundlePath)
{
    /*  Security.framework exports these since macOS 10.12, but the SDK has no header for them => looked up at run time, so a macOS
        without them simply has no translocation (and the app needs no link to Security.framework):
            Boolean  SecTranslocateIsTranslocatedURL(CFURLRef path, bool* isTranslocated, CFErrorRef* error);
            CFURLRef SecTranslocateCreateOriginalPathForURL(CFURLRef translocatedPath, CFErrorRef* error);  */
    using IsTranslocatedUrlFun     = Boolean (*)(CFURLRef path, bool* isTranslocated, CFErrorRef* error);
    using CreateOriginalPathForUrl = CFURLRef(*)(CFURLRef translocatedPath, CFErrorRef* error);

    static void* const libSecurity = ::dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL); //never closed
    if (!libSecurity)
        return std::nullopt;

    const auto isTranslocatedUrl  = reinterpret_cast<IsTranslocatedUrlFun    >(::dlsym(libSecurity, "SecTranslocateIsTranslocatedURL"));
    const auto createOriginalPath = reinterpret_cast<CreateOriginalPathForUrl>(::dlsym(libSecurity, "SecTranslocateCreateOriginalPathForURL"));
    if (!isTranslocatedUrl || !createOriginalPath)
        return std::nullopt;

    CFURLRef url = ::CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(bundlePath.c_str()),
                                                             bundlePath.size(), true /*isDirectory*/);
    if (!url)
        return std::nullopt;
    ZEN_ON_SCOPE_EXIT(::CFRelease(url));

    bool translocated = false;
    if (!isTranslocatedUrl(url, &translocated, nullptr /*error*/) || !translocated)
        return std::nullopt;

    CFURLRef originalUrl = createOriginalPath(url, nullptr /*error*/);
    if (!originalUrl)
        return std::nullopt;
    ZEN_ON_SCOPE_EXIT(::CFRelease(originalUrl));

    const Zstring originalPath = getPathFromUrl(originalUrl);
    if (originalPath.empty() || originalPath == bundlePath)
        return std::nullopt;
    return originalPath;
}


std::vector<Zstring> fff::getAppBundlePathsById(const char* bundleId)
{
    std::vector<Zstring> bundlePaths;

    CFStringRef bundleIdRef = ::CFStringCreateWithCString(nullptr, bundleId, kCFStringEncodingUTF8);
    if (!bundleIdRef)
        return bundlePaths;
    ZEN_ON_SCOPE_EXIT(::CFRelease(bundleIdRef));

    CFArrayRef urls = ::LSCopyApplicationURLsForBundleIdentifier(bundleIdRef, nullptr /*outError*/); //"best" one first; none: nullptr
    if (!urls)
        return bundlePaths;
    ZEN_ON_SCOPE_EXIT(::CFRelease(urls));

    for (CFIndex i = 0; i < ::CFArrayGetCount(urls); ++i)
        if (auto url = static_cast<CFURLRef>(::CFArrayGetValueAtIndex(urls, i)))
            if (const Zstring path = getPathFromUrl(url);
                !path.empty() && !contains(path, Zstr("/AppTranslocation/")))
                bundlePaths.push_back(path);

    return bundlePaths;
}
