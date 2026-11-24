// Symbolic link handling: exclude, copy as link ("asLink"), follow

#include "sync_test.h"

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;


namespace
{
const time_t t0 = timeBase();

void makeLinkTrees(const Zstring& left, const Zstring& right)
{
    makeTree(left,
    {
        {"file.txt",      {Entry::Type::file, "link target", t0}},
        {"sub",           {Entry::Type::folder}},
        {"sub/x.txt",     {Entry::Type::file, "in linked folder", t0}},
        {"link to file",  {Entry::Type::symlink, "file.txt"}},  //relative targets: valid on both sides
        {"link to sub",   {Entry::Type::symlink, "sub"}},
        {"broken link",   {Entry::Type::symlink, "missing target"}},
    });
    makeTree(right,
    {
        {"right link",    {Entry::Type::symlink, "anywhere"}}, //right only
    });
}
}


TEST_CASE(syncSymlinksExclude)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeLinkTrees(left, right);

    const RunResult res = runSync({.left = left, .right = right, .handleSymlinks = SymLinkHandling::exclude});
    CHECK_NO_ERRORS(res);
    //links are invisible to the sync: not copied, and the right side's link is not deleted
    CHECK_TREES_EQUAL(readTree(right), (Tree
    {
        {"file.txt",   {Entry::Type::file, "link target", t0}},
        {"sub",        {Entry::Type::folder}},
        {"sub/x.txt",  {Entry::Type::file, "in linked folder", t0}},
        {"right link", {Entry::Type::symlink, "anywhere"}},
    }));
}


TEST_CASE(syncSymlinksAsLink)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeLinkTrees(left, right);

    const RunResult res = runSync({.left = left, .right = right, .handleSymlinks = SymLinkHandling::asLink});
    CHECK_NO_ERRORS(res);
    //copied as links with the same target text (broken ones too); the right-only link is deleted
    CHECK_TREES_EQUAL(readTree(right), readTree(left));
    CHECK(compareOnly({.left = left, .right = right, .handleSymlinks = SymLinkHandling::asLink}).allEqual);
}


TEST_CASE(syncSymlinksFollow)
{
    const Zstring left  = appendPath(testDir, "left");
    const Zstring right = appendPath(testDir, "right");
    makeLinkTrees(left, right);

    const RunResult res = runSync({.left = left, .right = right, .handleSymlinks = SymLinkHandling::follow});
    //the broken links (left and right) cannot be followed: reported, the rest is synchronized
    CHECK_EQUAL(res.errors.size(), 2u);
    CHECK(std::any_of(res.errors.begin(), res.errors.end(), [](const std::string& e) { return contains(e, "broken link"); }));
    CHECK(std::any_of(res.errors.begin(), res.errors.end(), [](const std::string& e) { return contains(e, "right link"); }));

    CHECK_TREES_EQUAL(readTree(right), (Tree
    {
        {"file.txt",          {Entry::Type::file, "link target", t0}},
        {"sub",               {Entry::Type::folder}},
        {"sub/x.txt",         {Entry::Type::file, "in linked folder", t0}},
        {"link to file",      {Entry::Type::file, "link target", t0}},      //the target's content and time
        {"link to sub",       {Entry::Type::folder}},                        //the target's content
        {"link to sub/x.txt", {Entry::Type::file, "in linked folder", t0}},
        {"right link",        {Entry::Type::symlink, "anywhere"}},          //followed: its target is missing => error, left alone
    }));
    if (res.errors.size() != 2)
        std::cerr << res.toString();
}
