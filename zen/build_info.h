// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

//target platform: exactly one of ZEN_LINUX, ZEN_MAC, ZEN_WIN (test these, not the compiler's macros)
#if defined __linux__
    #define ZEN_LINUX
#elif defined __APPLE__
    #define ZEN_MAC
#elif defined _WIN32
    #define ZEN_WIN
#else
    #error Unsupported platform: FreeFileSync builds for Linux, macOS and Windows only
#endif


namespace zen
{
enum class BuildArch
{
    bit32,
    bit64,

#ifdef __LP64__
    program = bit64
#else
    program = bit32
#endif
};

static_assert((BuildArch::program == BuildArch::bit32 ? 32 : 64) == sizeof(void*) * 8);


//harmonize with os_arch enum in update_checks table:
#if defined ZEN_MAC && defined __arm64__ //Apple silicon (Linux keeps upstream's value)
constexpr const char* cpuArchName = "arm64";
#else
constexpr const char* cpuArchName = BuildArch::program == BuildArch::bit32 ? "i686": "x86-64";
#endif

}
