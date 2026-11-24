// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************
#pragma once

#include <string>
#include <string_view>
#include <vector>


namespace zen
{
/*  Removes file system paths and names of the user and the machine from text sent with a crash report (zen/crash_report.h).
    Works on UTF-8 text, platform-independently (a report from any OS can contain any kind of path):

    - absolute paths: /home/john/a.txt, ~/a.txt, C:\Users\john\a.txt, C:/a.txt, \\server\share\a.txt, \\?\C:\a.txt
    - FreeFileSync path phrases and URLs: sftp://, ftp://, https://user:pw@host/..., gdrive:, mtp:, zip:, file://
    - quoted text starting like a path, spaces included: "C:\My Files\a.txt"
    - an unquoted Windows path continues over spaces while the next word still contains a backslash: C:\Program Files\x
    - each of sensitiveWords (e.g. user and host name, at least 3 characters) where it stands as a whole word

    A path becomes "<path>", followed by the extension of its last part if it has a short one ("<path>.txt"; letters, digits and "_", at most 8).
    Sensitive words become "<redacted>". Text without paths stays unchanged.                                          */
std::string scrubPaths(std::string_view text, const std::vector<std::string>& sensitiveWords = {});

//the last component of a path, e.g. for module names: "/usr/lib/libc.so.6" -> "libc.so.6", "C:\a\b.dll" -> "b.dll"
std::string_view getPathFileName(std::string_view path);
}
