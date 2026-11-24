// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//AppKit and ImageIO side of the macOS icon loader (icon_loader_mac.h): Objective-C++ with ARC, compiled as C++23 (see the header)
//- file and folder icons: NSWorkspace; thumbnails: ImageIO (image files only, like gdk_pixbuf_new_from_file() on Linux)

#include "icon_loader_mac.h"
#include <algorithm>

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

using namespace fff::macos;


namespace
{
int divRound(int num, int den) { return (num + den / 2) / den; } //num, den > 0

//draw into an RGBA bitmap of at most maxSize x maxSize, keeping the aspect ratio; small images are not enlarged (as on Linux)
RenderResult renderCGImage(CGImageRef cgImg, int maxSize)
{
    const int srcWidth  = static_cast<int>(::CGImageGetWidth (cgImg));
    const int srcHeight = static_cast<int>(::CGImageGetHeight(cgImg));
    if (srcWidth <= 0 || srcHeight <= 0 || maxSize <= 0)
        return RenderError{"CGImageGetWidth", "Empty image."};

    int targetWidth  = srcWidth;
    int targetHeight = srcHeight;

    const int maxExtent = std::max(targetWidth, targetHeight);
    if (maxSize < maxExtent)
    {
        targetWidth  = std::max(1, divRound(targetWidth  * maxSize, maxExtent));
        targetHeight = std::max(1, divRound(targetHeight * maxSize, maxExtent));
    }

    CGColorSpaceRef colorSpace = ::CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace)
        return RenderError{"CGColorSpaceCreateWithName", "sRGB color space not available."};

    RenderedImage img{targetWidth, targetHeight, std::vector<unsigned char>(static_cast<size_t>(targetWidth) * targetHeight * 4)};
    //premultiplied alpha: the only RGBA layout CGBitmapContext supports; R, G, B, A in memory order
    CGContextRef ctx = ::CGBitmapContextCreate(img.rgba.data(), targetWidth, targetHeight, 8 /*bitsPerComponent*/, targetWidth * 4 /*bytesPerRow*/,
                                               colorSpace, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    ::CGColorSpaceRelease(colorSpace);
    if (!ctx)
        return RenderError{"CGBitmapContextCreate", "Cannot create bitmap context."};

    ::CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    ::CGContextDrawImage(ctx, CGRectMake(0, 0, targetWidth, targetHeight), cgImg); //first row in memory = top of the image
    ::CGContextRelease(ctx);

    for (size_t i = 0; i < img.rgba.size(); i += 4) //undo the premultiplication
        if (const int a = img.rgba[i + 3]; a != 0 && a != 255)
            for (size_t c = i; c < i + 3; ++c)
                img.rgba[c] = static_cast<unsigned char>(std::min(255, divRound(img.rgba[c] * 255, a)));
    return img;
}


RenderResult renderNSImage(NSImage* image, int maxSize, const char* functionName)
{
    if (!image)
        return RenderError{functionName, "Icon not available."};

    //picks the best representation for the size: Retina representations are larger and get scaled down
    NSRect rect = NSMakeRect(0, 0, maxSize, maxSize);
    CGImageRef cgImg = [image CGImageForProposedRect:&rect context:nil hints:nil]; //not owned
    if (!cgImg)
        return RenderError{"CGImageForProposedRect", "Icon has no bitmap representation."};

    return renderCGImage(cgImg, maxSize);
}


NSString* toNSPath(const std::string& path)
{
    return [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path.c_str() length:path.size()];
}
}


RenderResult fff::macos::renderIcon(IconKind kind, const std::string& arg, int maxSize)
{
    @autoreleasepool
    {
        NSWorkspace* ws = [NSWorkspace sharedWorkspace];
        switch (kind)
        {
            case IconKind::ofFile:
                return renderNSImage([ws iconForFile:toNSPath(arg)], maxSize, "iconForFile");

            case IconKind::ofExtension:
            {
                NSString* ext = arg.empty() ? nil : [NSString stringWithUTF8String:arg.c_str()]; //nil for broken UTF-8
                UTType* type = ext ? [UTType typeWithFilenameExtension:ext] : nil;
                return renderNSImage([ws iconForContentType:type ? type : UTTypeData], maxSize, "iconForContentType");
            }
            case IconKind::genericFile:
                return renderNSImage([ws iconForContentType:UTTypeData], maxSize, "iconForContentType(UTTypeData)");

            case IconKind::genericFolder:
                return renderNSImage([ws iconForContentType:UTTypeFolder], maxSize, "iconForContentType(UTTypeFolder)");

            case IconKind::trash:
                return renderNSImage([NSImage imageNamed:NSImageNameTrashFull], maxSize, "imageNamed(NSImageNameTrashFull)");

            case IconKind::fileManager:
            {
                NSURL* finderUrl = [ws URLForApplicationWithBundleIdentifier:@"com.apple.finder"];
                return renderNSImage([ws iconForFile:finderUrl ? finderUrl.path : @"/System/Library/CoreServices/Finder.app"], maxSize, "iconForFile(Finder)");
            }
        }
        return RenderError{"renderIcon", "Unknown icon kind."};
    }
}


RenderResult fff::macos::renderThumbnail(const std::string& filePath, int maxSize)
{
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath:toNSPath(filePath) isDirectory:NO];
        CGImageSourceRef src = ::CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!src)
            return RenderError{"CGImageSourceCreateWithURL", "Cannot open image file."};

        NSDictionary* options = @{(__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                                  (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform:  @YES, //apply the EXIF orientation
                                  (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize:         @(maxSize)};
        CGImageRef thumb = ::CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef)options);
        const int status = static_cast<int>(::CGImageSourceGetStatus(src));
        ::CFRelease(src);
        if (!thumb)
            return RenderError{"CGImageSourceCreateThumbnailAtIndex", "Not a supported image file (status " + std::to_string(status) + ")."};

        RenderResult result = renderCGImage(thumb, maxSize);
        ::CGImageRelease(thumb);
        return result;
    }
}
