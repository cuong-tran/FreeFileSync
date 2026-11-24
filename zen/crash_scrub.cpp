// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "crash_scrub.h"
#include <algorithm>
#include <array>

using namespace zen;


namespace
{
const std::string_view PATH_PLACEHOLDER     = "<path>";
const std::string_view REDACTED_PLACEHOLDER = "<redacted>";

bool isAsciiAlpha(char c) { return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z'); }
bool isAsciiAlnum(char c) { return isAsciiAlpha(c) || ('0' <= c && c <= '9'); }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

char asciiLower(char c) { return 'A' <= c && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithNoCase(std::string_view str, std::string_view prefix)
{
    return str.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), str.begin(), [](char a, char b) { return asciiLower(a) == asciiLower(b); });
}

//quote characters as UTF-8 sequences, with their closing counterpart
struct QuotePair { std::string_view open; std::string_view close; };
constexpr std::array<QuotePair, 5> QUOTES
{{
    {"\"", "\""},
    {"'", "'"},
    {"\xE2\x80\x9C", "\xE2\x80\x9D"}, //“ ”
    {"\xE2\x80\x98", "\xE2\x80\x99"}, //‘ ’
    {"\xC2\xAB", "\xC2\xBB"},         //« »
}};

bool startsWithQuote(std::string_view str)
{
    return std::any_of(QUOTES.begin(), QUOTES.end(), [&](const QuotePair& q) { return str.starts_with(q.open) || str.starts_with(q.close); });
}


//schemes of FreeFileSync's path phrases and of URLs that may carry user names, host names or credentials
constexpr std::array<std::string_view, 4> PHRASE_PREFIXES{"gdrive:", "mtp:", "zip:", "sftp:"}; //"sftp:" also without "//"

//does a path or path phrase start here?
bool isPathStart(std::string_view str)
{
    if (str.size() < 2)
        return false;

    //POSIX absolute path or network path: "/x", "//server"; not a lone "/" or "/ "
    if (str[0] == '/')
        return !isSpace(str[1]) && !startsWithQuote(str.substr(1));
    //home folder: "~/x"
    if (str[0] == '~' && str[1] == '/')
        return true;
    //UNC or long path: "\\server\share", "\\?\C:\"
    if (str[0] == '\\' && str[1] == '\\')
        return str.size() > 2 && !isSpace(str[2]);
    //drive: "C:\", "C:/"
    if (str.size() >= 3 && isAsciiAlpha(str[0]) && str[1] == ':' && (str[2] == '\\' || str[2] == '/'))
        return true;

    for (std::string_view prefix : PHRASE_PREFIXES)
        if (startsWithNoCase(str, prefix) && str.size() > prefix.size() && !isSpace(str[prefix.size()]))
            return true;

    //any URL: "<scheme>://..." (ftp, https with credentials, file, smb, ...)
    size_t i = 0;
    while (i < str.size() && (isAsciiAlnum(str[i]) || str[i] == '+' || str[i] == '-' || str[i] == '.'))
        ++i;
    return i > 0 && isAsciiAlpha(str[0]) && str.substr(i).starts_with("://");
}


//text before a path start: not inside a word ("and/or", "1/2", "a:\b" in "data:\x" stay)
bool isBoundaryBefore(std::string_view text, size_t pos)
{
    if (pos == 0)
        return true;
    const char c = text[pos - 1];
    return isSpace(c) || c == '(' || c == '[' || c == '{' || c == '<' || c == '=' || c == ',' || c == ';' || c == '|' || c == '>';
}


//"<path>" plus the extension of the last path component, if it has a short one (letters, digits, "_")
std::string makePlaceholder(std::string_view path)
{
    std::string out(PATH_PLACEHOLDER);
    const std::string_view name = getPathFileName(path);
    if (const size_t dot = name.rfind('.');
        dot != std::string_view::npos && dot > 0 && dot + 1 < name.size())
    {
        const std::string_view ext = name.substr(dot + 1);
        if (ext.size() <= 8 && std::all_of(ext.begin(), ext.end(), [](char c) { return isAsciiAlnum(c) || c == '_'; })) //e.g. ".ffs_gui"
            (out += '.') += ext;
    }
    return out;
}


//end of an unquoted path starting at "pos": the next whitespace or quote; trailing sentence punctuation is not part of it.
//A Windows path (with backslashes) continues over a space while the next word contains a backslash: "C:\Program Files\x"
size_t findUnquotedPathEnd(std::string_view text, size_t pos)
{
    const bool backslashPath = text.substr(pos, 3).find('\\') != std::string_view::npos;

    auto wordEnd = [&](size_t i)
    {
        while (i < text.size() && !isSpace(text[i]) && !startsWithQuote(text.substr(i)))
            ++i;
        return i;
    };
    size_t end = wordEnd(pos);

    if (backslashPath)
        for (;;)
        {
            size_t next = end;
            while (next < text.size() && text[next] == ' ')
                ++next;
            if (next == end || next >= text.size())
                break;
            const size_t nextEnd = wordEnd(next);
            if (text.substr(next, nextEnd - next).find('\\') == std::string_view::npos)
                break;
            end = nextEnd;
        }

    while (end > pos && (text[end - 1] == '.' || text[end - 1] == ',' || text[end - 1] == ';' ||
                         text[end - 1] == ':' || text[end - 1] == ')' || text[end - 1] == ']' || text[end - 1] == '!' || text[end - 1] == '?'))
        --end;
    return end;
}


std::string replaceSensitiveWords(const std::string& text, const std::vector<std::string>& words)
{
    std::string out = text;
    for (const std::string& word : words)
    {
        if (word.size() < 3) //"a", "pc": too likely to be a part of normal text
            continue;
        std::string tmp;
        size_t pos = 0;
        for (;;)
        {
            const size_t hit = [&]
            {
                for (size_t i = pos; i + word.size() <= out.size(); ++i)
                    if (startsWithNoCase(std::string_view(out).substr(i), word))
                        return i;
                return std::string::npos;
            }();
            if (hit == std::string::npos)
                break;
            const bool wholeWord = (hit == 0 || !isAsciiAlnum(out[hit - 1])) &&
                                   (hit + word.size() == out.size() || !isAsciiAlnum(out[hit + word.size()]));
            tmp.append(out, pos, hit - pos);
            if (wholeWord)
                tmp += REDACTED_PLACEHOLDER;
            else
                tmp.append(out, hit, word.size());
            pos = hit + word.size();
        }
        tmp.append(out, pos);
        out = std::move(tmp);
    }
    return out;
}
}


std::string_view zen::getPathFileName(std::string_view path)
{
    while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        path.remove_suffix(1);
    const size_t sep = path.find_last_of("/\\");
    return sep == std::string_view::npos ? path : path.substr(sep + 1);
}


std::string zen::scrubPaths(std::string_view text, const std::vector<std::string>& sensitiveWords)
{
    std::string out;
    out.reserve(text.size());

    size_t i = 0;
    while (i < text.size())
    {
        const std::string_view rest = text.substr(i);

        //quoted text that starts like a path: replace all of it, spaces included
        if (const auto itQ = std::find_if(QUOTES.begin(), QUOTES.end(), [&](const QuotePair& q) { return rest.starts_with(q.open); });
            itQ != QUOTES.end())
        {
            const size_t contentPos = i + itQ->open.size();
            const size_t closePos = text.find(itQ->close, contentPos);
            if (closePos != std::string_view::npos && isPathStart(text.substr(contentPos, closePos - contentPos)))
            {
                out += itQ->open;
                out += makePlaceholder(text.substr(contentPos, closePos - contentPos));
                out += itQ->close;
                i = closePos + itQ->close.size();
                continue;
            }
            out += itQ->open;
            i = contentPos;
            continue;
        }

        if (isBoundaryBefore(text, i) && isPathStart(rest))
        {
            const size_t end = findUnquotedPathEnd(text, i);
            out += makePlaceholder(text.substr(i, end - i));
            i = end;
            continue;
        }

        out += text[i];
        ++i;
    }

    return replaceSensitiveWords(out, sensitiveWords);
}
