// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

//macOS implementation of icon_loader.h (the Linux one, with GTK/GIO, is icon_loader.cpp), on top of the AppKit rendering in
//icon_loader_mac.mm. Unlike GTK, AppKit renders icons on worker threads, so FileIconHolder carries the finished image
//(wx+/image_holder.h) and the main thread only moves it into a wxImage.

#include "icon_loader.h"
#include <algorithm>
#include <cstring> //memcpy, memset
#include <sys/stat.h>
#include <zen/file_path.h> //getFileExtension
#include <zen/sys_error.h>
#include <zen/thread.h> //runningOnMainThread
#include <zen/utf.h>
#include "icon_loader_mac.h"

using namespace zen;
using namespace fff;


namespace
{
ImageHolder toImageHolder(macos::RenderResult&& result) //throw SysError
{
    if (const macos::RenderError* e = std::get_if<macos::RenderError>(&result))
        throw SysError(formatSystemError(e->functionName, L"", utfTo<std::wstring>(e->message)));

    const macos::RenderedImage& img = std::get<macos::RenderedImage>(result);
    ImageHolder out(img.width, img.height, true /*withAlpha*/);
    unsigned char* rgb   = out.getRgb();
    unsigned char* alpha = out.getAlpha();
    for (size_t i = 0; i < img.rgba.size(); i += 4)
    {
        std::memcpy(rgb, &img.rgba[i], 3);
        rgb += 3;
        *alpha++ = img.rgba[i + 3];
    }
    return out;
}


FileIconHolder renderIcon(macos::IconKind kind, const Zstring& arg, int maxSize) //throw SysError
{
    return FileIconHolder(toImageHolder(macos::renderIcon(kind, utfTo<std::string>(arg), maxSize)), maxSize); //throw SysError
}
}


FileIconHolder fff::getIconByTemplatePath(const Zstring& templatePath, int maxSize) //throw SysError
{
    return renderIcon(macos::IconKind::ofExtension, getFileExtension(templatePath), maxSize); //throw SysError
}


FileIconHolder fff::genericFileIcon   (int maxSize) { return renderIcon(macos::IconKind::genericFile,   Zstring(), maxSize); } //throw SysError
FileIconHolder fff::genericDirIcon    (int maxSize) { return renderIcon(macos::IconKind::genericFolder, Zstring(), maxSize); } //throw SysError
FileIconHolder fff::getTrashIcon      (int maxSize) { return renderIcon(macos::IconKind::trash,         Zstring(), maxSize); } //throw SysError
FileIconHolder fff::getFileManagerIcon(int maxSize) { return renderIcon(macos::IconKind::fileManager,   Zstring(), maxSize); } //throw SysError


FileIconHolder fff::getFileIcon(const Zstring& filePath, int maxSize) //throw SysError
{
    //iconForFile: returns a generic icon for a missing file: fail like g_file_query_info() on Linux
    struct stat fileInfo = {};
    if (::lstat(filePath.c_str(), &fileInfo) != 0)
        THROW_LAST_SYS_ERROR("lstat");

    return renderIcon(macos::IconKind::ofFile, filePath, maxSize); //throw SysError
}


ImageHolder fff::getThumbnailImage(const Zstring& filePath, int maxSize) //throw SysError
{
    struct stat fileInfo = {};
    if (::stat(filePath.c_str(), &fileInfo) != 0)
        THROW_LAST_SYS_ERROR("stat");

    if (!S_ISREG(fileInfo.st_mode)) //skip blocking file types, e.g. named pipes, see file_io.cpp
        throw SysError(_("Unsupported item type.") + L" [" + printNumber<std::wstring>(L"0%06o", fileInfo.st_mode & S_IFMT) + L']');

    return toImageHolder(macos::renderThumbnail(filePath.c_str(), maxSize)); //throw SysError
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
        std::memset(img.GetAlpha(), wxIMAGE_ALPHA_OPAQUE, ih.getWidth() * ih.getHeight());
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
