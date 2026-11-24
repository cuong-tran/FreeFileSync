// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS implementation of icon_loader.h (the Linux one, with GTK/GIO, is icon_loader.cpp). Objective-C++ with ARC.
//- file and folder icons: NSWorkspace; thumbnails: ImageIO (image files only, like gdk_pixbuf_new_from_file() on Linux)
//- unlike GTK, AppKit can render icons on worker threads, so FileIconHolder carries the finished image (wx+/image_holder.h)
//  and the main thread only moves it into a wxImage

#include "icon_loader.h"
#include <algorithm>
#include <cstring> //memset
#include <vector>
#include <sys/stat.h>
#include <zen/basic_math.h>
#include <zen/file_path.h> //getFileExtension
#include <zen/scope_guard.h>
#include <zen/sys_error.h>
#include <zen/thread.h> //runningOnMainThread

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

using namespace zen;
using namespace fff;


namespace
{
//draw into an RGBA bitmap of at most maxSize x maxSize, keeping the aspect ratio; small images are not enlarged (as on Linux)
ImageHolder imageHolderFromCGImage(CGImageRef cgImg, int maxSize) //throw SysError
{
    const int srcWidth  = static_cast<int>(::CGImageGetWidth (cgImg));
    const int srcHeight = static_cast<int>(::CGImageGetHeight(cgImg));
    if (srcWidth <= 0 || srcHeight <= 0 || maxSize <= 0)
        throw SysError(formatSystemError("CGImageGetWidth", L"", L"Empty image."));

    int targetWidth  = srcWidth;
    int targetHeight = srcHeight;

    const int maxExtent = std::max(targetWidth, targetHeight);
    if (maxSize < maxExtent)
    {
        targetWidth  = std::max(1, numeric::intDivRound(targetWidth  * maxSize, maxExtent));
        targetHeight = std::max(1, numeric::intDivRound(targetHeight * maxSize, maxExtent));
    }

    CGColorSpaceRef colorSpace = ::CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace)
        throw SysError(formatSystemError("CGColorSpaceCreateWithName", L"", L"sRGB color space not available."));
    ZEN_ON_SCOPE_EXIT(::CGColorSpaceRelease(colorSpace));

    std::vector<unsigned char> rgba(static_cast<size_t>(targetWidth) * targetHeight * 4); //premultiplied alpha: the only RGBA layout CGBitmapContext supports
    CGContextRef ctx = ::CGBitmapContextCreate(rgba.data(), targetWidth, targetHeight, 8 /*bitsPerComponent*/, targetWidth * 4 /*bytesPerRow*/,
                                               colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big); //R, G, B, A in memory order
    if (!ctx)
        throw SysError(formatSystemError("CGBitmapContextCreate", L"", L"Cannot create bitmap context."));
    ZEN_ON_SCOPE_EXIT(::CGContextRelease(ctx));

    ::CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    ::CGContextDrawImage(ctx, CGRectMake(0, 0, targetWidth, targetHeight), cgImg); //first row in memory = top of the image

    ImageHolder imgOut(targetWidth, targetHeight, true /*withAlpha*/);
    unsigned char* rgbOut   = imgOut.getRgb();
    unsigned char* alphaOut = imgOut.getAlpha();

    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        const int a = rgba[i + 3];
        *alphaOut++ = static_cast<unsigned char>(a);
        for (int c = 0; c < 3; ++c) //undo the premultiplication
            *rgbOut++ = a == 0 ? 0 : static_cast<unsigned char>(std::min(255, numeric::intDivRound(rgba[i + c] * 255, a)));
    }
    return imgOut;
}


ImageHolder imageHolderFromNSImage(NSImage* img, int maxSize, const char* functionName) //throw SysError
{
    if (!img)
        throw SysError(formatSystemError(functionName, L"", L"Icon not available."));

    @autoreleasepool
    {
        //picks the best representation for the size: Retina representations are larger and get scaled down
        NSRect rect = NSMakeRect(0, 0, maxSize, maxSize);
        CGImageRef cgImg = [img CGImageForProposedRect:&rect context:nil hints:nil]; //not owned
        if (!cgImg)
            throw SysError(formatSystemError("CGImageForProposedRect", L"", L"Icon has no bitmap representation."));

        return imageHolderFromCGImage(cgImg, maxSize); //throw SysError
    }
}


NSString* toNSPath(const Zstring& path)
{
    return [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path.c_str() length:path.size()];
}


FileIconHolder iconForContentType(UTType* type, int maxSize, const char* functionName) //throw SysError
{
    if (!type)
        throw SysError(formatSystemError(functionName, L"", L"Unknown content type."));

    @autoreleasepool
    {
        return FileIconHolder(imageHolderFromNSImage([[NSWorkspace sharedWorkspace] iconForContentType:type], maxSize, functionName), maxSize); //throw SysError
    }
}
}


FileIconHolder fff::getIconByTemplatePath(const Zstring& templatePath, int maxSize) //throw SysError
{
    @autoreleasepool
    {
        const Zstring ext = getFileExtension(templatePath);
        UTType* type = ext.empty() ? UTTypeData : [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:ext.c_str()]];
        return iconForContentType(type ? type : UTTypeData, maxSize, "iconForContentType"); //throw SysError
    }
}


FileIconHolder fff::genericFileIcon(int maxSize) //throw SysError
{
    return iconForContentType(UTTypeData, maxSize, "iconForContentType(UTTypeData)"); //throw SysError
}


FileIconHolder fff::genericDirIcon(int maxSize) //throw SysError
{
    return iconForContentType(UTTypeFolder, maxSize, "iconForContentType(UTTypeFolder)"); //throw SysError
}


FileIconHolder fff::getTrashIcon(int maxSize) //throw SysError
{
    @autoreleasepool
    {
        return FileIconHolder(imageHolderFromNSImage([NSImage imageNamed:NSImageNameTrashFull], maxSize, "imageNamed(NSImageNameTrashFull)"), maxSize); //throw SysError
    }
}


FileIconHolder fff::getFileManagerIcon(int maxSize) //throw SysError
{
    @autoreleasepool
    {
        NSURL* finderUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.finder"];
        NSString* finderPath = finderUrl ? finderUrl.path : @"/System/Library/CoreServices/Finder.app";
        return FileIconHolder(imageHolderFromNSImage([[NSWorkspace sharedWorkspace] iconForFile:finderPath], maxSize, "iconForFile(Finder)"), maxSize); //throw SysError
    }
}


FileIconHolder fff::getFileIcon(const Zstring& filePath, int maxSize) //throw SysError
{
    //iconForFile: returns a generic icon for a missing file: fail like g_file_query_info() on Linux
    struct stat fileInfo = {};
    if (::lstat(filePath.c_str(), &fileInfo) != 0)
        THROW_LAST_SYS_ERROR("lstat");

    @autoreleasepool
    {
        return FileIconHolder(imageHolderFromNSImage([[NSWorkspace sharedWorkspace] iconForFile:toNSPath(filePath)], maxSize, "iconForFile"), maxSize); //throw SysError
    }
}


ImageHolder fff::getThumbnailImage(const Zstring& filePath, int maxSize) //throw SysError
{
    struct stat fileInfo = {};
    if (::stat(filePath.c_str(), &fileInfo) != 0)
        THROW_LAST_SYS_ERROR("stat");

    if (!S_ISREG(fileInfo.st_mode)) //skip blocking file types, e.g. named pipes, see file_io.cpp
        throw SysError(_("Unsupported item type.") + L" [" + printNumber<std::wstring>(L"0%06o", fileInfo.st_mode & S_IFMT) + L']');

    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath:toNSPath(filePath) isDirectory:NO];
        CGImageSourceRef src = ::CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!src)
            throw SysError(formatSystemError("CGImageSourceCreateWithURL", L"", L"Cannot open image file."));
        ZEN_ON_SCOPE_EXIT(::CFRelease(src));

        NSDictionary* options = @{(__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                                  (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform:  @YES, //apply the EXIF orientation
                                  (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize:         @(maxSize)};
        CGImageRef thumb = ::CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef)options);
        if (!thumb)
            throw SysError(formatSystemError("CGImageSourceCreateThumbnailAtIndex", L"", L"Not a supported image file (status " +
                                             numberTo<std::wstring>(static_cast<int>(::CGImageSourceGetStatus(src))) + L")."));
        ZEN_ON_SCOPE_EXIT(::CGImageRelease(thumb));

        return imageHolderFromCGImage(thumb, maxSize); //throw SysError
    }
}


wxImage fff::extractWxImage(ImageHolder&& ih)
{
    assert(runningOnMainThread());
    if (!ih.getRgb())
        return wxNullImage;

    wxImage img(ih.getWidth(), ih.getHeight(), ih.releaseRgb(), false /*static_data*/); //pass ownership
    if (ih.getAlpha())
        img.SetAlpha(ih.releaseAlpha(), false /*static_data*/);
    else
    {
        assert(false);
        img.SetAlpha();
        ::memset(img.GetAlpha(), wxIMAGE_ALPHA_OPAQUE, ih.getWidth() * ih.getHeight());
    }
    return img;
}


wxImage fff::extractWxImage(zen::FileIconHolder&& fih)
{
    assert(runningOnMainThread());
    wxImage img = extractWxImage(std::move(fih.image)); //already rendered on the worker thread
    fih = {};
    return img;
}
