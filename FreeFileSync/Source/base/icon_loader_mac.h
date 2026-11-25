// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <string>
#include <variant>
#include <vector>

//macOS only: icon and thumbnail rendering with AppKit and ImageIO (icon_loader_mac.mm), for icon_loader_mac.cpp, which implements
//icon_loader.h on top. Standard types only: the Objective-C++ side is compiled as C++23, because the macOS 26 SDK's CoreGraphics
//headers combine enumerators of different enumeration types, which C++26 no longer allows (P2864), so they can't be part of a
//C++26 translation unit. Thread-safe: AppKit renders icons on any thread.
namespace fff::macos
{
struct RenderedImage
{
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> rgba; //straight (not premultiplied) R, G, B, A per pixel, first row = top of the image
};

struct RenderError
{
    std::string functionName;
    std::string message;
};

using RenderResult = std::variant<RenderedImage, RenderError>;

enum class IconKind
{
    ofFile,        //arg: file path
    ofExtension,   //arg: file name extension without the dot; empty: generic file
    genericFile,
    genericFolder,
    trash,
    fileManager,   //Finder
};

//at most maxSize x maxSize, aspect ratio kept, small images are not enlarged
RenderResult renderIcon(IconKind kind, const std::string& arg, int maxSize);
RenderResult renderThumbnail(const std::string& filePath, int maxSize); //image files only
}
