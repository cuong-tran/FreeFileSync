// Filters: include/exclude (hard filter: applies to both sides, excluded items are neither copied nor deleted),
// time span and file size (soft filters: mark rows inactive after the comparison)

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;


TEST_CASE(syncFilterIncludeExclude)
{
    const time_t t0 = timeBase();
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"a.txt",            {Entry::Type::file, "a", t0}},
        {"b.log",            {Entry::Type::file, "b", t0}},
        {"sub/c.txt",        {Entry::Type::file, "c", t0}},
        {"sub/d.log",        {Entry::Type::file, "d", t0}},
        {"skip/e.txt",       {Entry::Type::file, "e", t0}},     //excluded folder at the base
        {"sub/skip/f.txt",   {Entry::Type::file, "f", t0}},     //excluded folder further down
        {"sub/g.TXT",        {Entry::Type::file, "g", t0}},     //filters ignore case
    });
    makeTree(right,
    {
        {"r.log",            {Entry::Type::file, "not included: stays", t0}},
        {"r.txt",            {Entry::Type::file, "included right only: deleted by mirror", t0}},
        {"skip/old.txt",     {Entry::Type::file, "excluded: stays", t0}},
    });

    const FilterConfig filter{.includeFilter = Zstr("*.txt"), .excludeFilter = Zstr("*/skip/")};
    const RunResult res = runSync({.left = left, .right = right, .filter = filter});
    CHECK_NO_ERRORS(res);

    const Tree expected
    {
        {"a.txt",        {Entry::Type::file, "a", t0}},
        {"sub",          {Entry::Type::folder}},
        {"sub/c.txt",    {Entry::Type::file, "c", t0}},
        {"sub/g.TXT",    {Entry::Type::file, "g", t0}},
        {"r.log",        {Entry::Type::file, "not included: stays", t0}},
        {"skip",         {Entry::Type::folder}},
        {"skip/old.txt", {Entry::Type::file, "excluded: stays", t0}},
    };
    CHECK_TREES_EQUAL(readTree(right), expected);
}


TEST_CASE(syncFilterTimeSpan)
{
    const time_t now = std::time(nullptr);
    const time_t old = now - 30 * 24 * 3600;
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"recent.txt",   {Entry::Type::file, "changed an hour ago", now - 3600}},
        {"old.txt",      {Entry::Type::file, "changed a month ago", old}},
    });
    makeTree(right,
    {
        {"old right.txt",    {Entry::Type::file, "old, right only: outside the time span, stays", old}},
        {"recent right.txt", {Entry::Type::file, "recent, right only: deleted", now - 7200}},
    });

    const FilterConfig filter{.timeSpan = 2, .unitTimeSpan = UnitTime::lastDays};
    const RunResult res = runSync({.left = left, .right = right, .filter = filter});
    CHECK_NO_ERRORS(res);

    CHECK_TREES_EQUAL(readTree(right), (Tree
    {
        {"recent.txt",    {Entry::Type::file, "changed an hour ago", now - 3600}},
        {"old right.txt", {Entry::Type::file, "old, right only: outside the time span, stays", old}},
    }));
}


TEST_CASE(syncFilterSize)
{
    const time_t t0 = timeBase();
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeTree(left,
    {
        {"small.txt",  {Entry::Type::file, std::string(5,   's'), t0}},
        {"medium.txt", {Entry::Type::file, std::string(50,  'm'), t0}},
        {"large.txt",  {Entry::Type::file, std::string(500, 'l'), t0}},
    });
    makeFolder(right, "");

    const FilterConfig filter{.sizeMin = 10, .unitSizeMin = UnitSize::byte, .sizeMax = 100, .unitSizeMax = UnitSize::byte};
    const RunResult res = runSync({.left = left, .right = right, .filter = filter});
    CHECK_NO_ERRORS(res);

    CHECK_TREES_EQUAL(readTree(right), (Tree{{"medium.txt", {Entry::Type::file, std::string(50, 'm'), t0}}}));
}
