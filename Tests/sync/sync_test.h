// Helpers for the headless sync tests (Tests/sync): folder trees built and checked with std::filesystem, and one call that runs
// the real engine (fff::compare(), fff::synchronize()) like a batch job, with a non-interactive ProcessCallback that records
// every error and warning instead of asking.
//
// The test harness (TEST_CASE, CHECK...) is Tests/zen/test.h. Test code stays portable: std::filesystem or zen/engine APIs only,
// with ZEN_LINUX/ZEN_MAC branches where the expected behavior differs (and a comment why).
#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zen/file_access.h>
#include <zen/process_exec.h>
#include <zen/string_tools.h>
#include <FreeFileSync/Source/base/comparison.h>
#include <FreeFileSync/Source/base/synchronization.h>
#include <FreeFileSync/Source/base/algorithm.h>
#include <FreeFileSync/Source/base/lock_holder.h>
#include <FreeFileSync/Source/afs/concrete.h>
#include "../zen/test.h"


namespace synctest
{
using namespace zentest;

//------------------------------------------------------------------------------------------
//folder trees

struct Entry
{
    enum class Type { file, folder, symlink } type = Type::file;
    std::string content;    //file content; symlink: target
    time_t      modTime = 0; //files only

    bool operator==(const Entry&) const = default;
};
using Tree = std::map<std::string /*relative path, '/'-separated, UTF-8*/, Entry>;

//all items below rootPath, without following symlinks; names in skipNames (the database, the directory lock) are left out
Tree readTree(const Zstring& rootPath, const std::set<std::string>& skipNames = {".sync.ffs_db", "sync.ffs_lock"}); //throws std::filesystem::filesystem_error

std::string describe(const Entry& e);

//every difference is reported as a failure; mtimes are compared within modTimeTolerance seconds (e.g. 2 for FAT)
void checkTreesEqual(const char* file, int line, const Tree& actual, const Tree& expected, int modTimeTolerance = 0);
#define CHECK_TREES_EQUAL(actual, expected, ...) synctest::checkTreesEqual(__FILE__, __LINE__, actual, expected __VA_OPT__(,) __VA_ARGS__)

//modification times used by the tests: whole seconds, safely in the past, far enough apart to never be "equal within tolerance"
inline time_t timeBase() { return std::time(nullptr) / 3600 * 3600 - 10 * 24 * 3600; } //10 days ago, on the hour
constexpr time_t TIME_STEP = 1000; //seconds

void makeFile   (const Zstring& rootPath, const std::string& relPath, const std::string& content, time_t modTime); //throws
void makeFolder (const Zstring& rootPath, const std::string& relPath);
void makeSymlink(const Zstring& rootPath, const std::string& relPath, const std::string& target);
void setModTime (const Zstring& itemPath, time_t modTime);
void makeTree   (const Zstring& rootPath, const Tree& tree); //creates exactly what readTree() would return

Zstring itemPath(const Zstring& rootPath, const std::string& relPath); //'/'-separated relPath => native path

//------------------------------------------------------------------------------------------
//engine runs

struct SyncCfg
{
    Zstring left;  //folder path phrases as the GUI takes them: native paths, zip:<archive>
    Zstring right;

    fff::CompareVariant  compareVar     = fff::CompareVariant::timeSize;
    fff::SymLinkHandling handleSymlinks = fff::SymLinkHandling::exclude;
    fff::SyncVariant     syncVar        = fff::SyncVariant::mirror;

    fff::DeletionVariant deletion = fff::DeletionVariant::permanent;
    Zstring              versioningFolder;
    fff::VersioningStyle versioningStyle = fff::VersioningStyle::replace;

    fff::FilterConfig filter;
    bool copyPermissions = false;
};

struct RunResult
{
    //recorded by the ProcessCallback
    std::vector<std::string> errors;   //reportError() (always answered "ignore"), reportFatalError(), logMessage(error)
    std::vector<std::string> warnings; //reportWarning(), logMessage(warning)

    //what compare() planned, active rows only
    std::map<fff::SyncOperation, int> ops;
    std::vector<std::string> plan; //"<rel path>: <operation>", for diagnostics
    int conflicts = 0;
    bool allEqual = false; //compare() found nothing to do

    int opCount(fff::SyncOperation op) const { auto it = ops.find(op); return it != ops.end() ? it->second : 0; }
    std::string toString() const; //plan, errors and warnings, for failure messages
};

RunResult compareOnly(const SyncCfg& cfg); //throws on engine exceptions it should never let through
RunResult runSync    (const SyncCfg& cfg); //compare() + synchronize(), with the directory locks held across both, like a batch run

//the run must report no error (warnings are allowed unless the test checks them)
#define CHECK_NO_ERRORS(res)                                                                         \
    do {                                                                                            \
        ++zentest::checkCount;                                                                      \
        if (!(res).errors.empty())                                                                  \
            zentest::reportFailure(__FILE__, __LINE__, "unexpected errors:\n" + (res).toString());  \
    } while (false)

//------------------------------------------------------------------------------------------
std::string runShell(const Zstring& cmdLine); //throw SysError: output of a command that must succeed

bool runningAsRoot(); //permission tests: root reads everything

//the real (not sandboxed) home folder of the user running the tests (run.sh sets SYNC_TEST_REAL_HOME), e.g. for the macOS trash
Zstring realHomePath();

//a name unique to this run, so items moved to a shared place (the macOS trash) can be found and removed again
std::string uniqueName(const std::string& base);
}
