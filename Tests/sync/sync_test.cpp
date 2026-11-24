// Implementation of sync_test.h

#include "sync_test.h"
#include <cstdlib>
#include <random>
#include <zen/file_path.h>

using namespace zen;
using namespace fff;
using namespace zentest;
using namespace synctest;
namespace fs = std::filesystem;


namespace
{
std::string toUtf8(const std::wstring& str) { return utfTo<std::string>(str); }


const char* opName(SyncOperation op)
{
    switch (op)
    {
        //*INDENT-OFF*
        case SO_CREATE_LEFT:         return "create left";
        case SO_CREATE_RIGHT:        return "create right";
        case SO_DELETE_LEFT:         return "delete left";
        case SO_DELETE_RIGHT:        return "delete right";
        case SO_OVERWRITE_LEFT:      return "overwrite left";
        case SO_OVERWRITE_RIGHT:     return "overwrite right";
        case SO_MOVE_LEFT_FROM:      return "move left from";
        case SO_MOVE_LEFT_TO:        return "move left to";
        case SO_MOVE_RIGHT_FROM:     return "move right from";
        case SO_MOVE_RIGHT_TO:       return "move right to";
        case SO_RENAME_LEFT:         return "rename left";
        case SO_RENAME_RIGHT:        return "rename right";
        case SO_DO_NOTHING:          return "do nothing";
        case SO_EQUAL:               return "equal";
        case SO_UNRESOLVED_CONFLICT: return "conflict";
        //*INDENT-ON*
    }
    return "?";
}


//non-interactive status handler: never asks, never cancels; records what a batch run would log
class RecordingCallback : public ProcessCallback
{
public:
    explicit RecordingCallback(RunResult& result) : result_(result) {}

    void initNewPhase(int itemsTotal, int64_t bytesTotal, ProcessPhase phaseId) override {}
    void updateDataProcessed(int itemsDelta, int64_t bytesDelta) override {}
    void updateDataTotal    (int itemsDelta, int64_t bytesDelta) override {}
    void requestUiUpdate(bool force) override {}
    void updateStatus(std::wstring&& msg) override {}

    void logMessage(const std::wstring& msg, MsgType type) override
    {
        switch (type)
        {
            case MsgType::info:
                break;
            case MsgType::warning:
                result_.warnings.push_back(toUtf8(msg));
                break;
            case MsgType::error:
                result_.errors.push_back("logged: " + toUtf8(msg));
                break;
        }
    }

    void reportWarning(const std::wstring& msg, bool& warningActive) override { result_.warnings.push_back(toUtf8(msg)); }

    Response reportError(const ErrorInfo& errorInfo) override
    {
        result_.errors.push_back(toUtf8(errorInfo.msg));
        return ProcessCallback::ignore; //like a batch job with "ignore errors": the sync continues
    }

    void reportFatalError(const std::wstring& msg) override { result_.errors.push_back("fatal: " + toUtf8(msg)); }

private:
    RunResult& result_;
};


MainConfiguration toMainCfg(const SyncCfg& cfg)
{
    MainConfiguration mainCfg;
    mainCfg.cmpCfg.compareVar     = cfg.compareVar;
    mainCfg.cmpCfg.handleSymlinks = cfg.handleSymlinks;

    mainCfg.syncCfg.directionCfg           = getDefaultSyncCfg(cfg.syncVar);
    mainCfg.syncCfg.deletionVariant        = cfg.deletion;
    mainCfg.syncCfg.versioningFolderPhrase = cfg.versioningFolder;
    mainCfg.syncCfg.versioningStyle        = cfg.versioningStyle;

    mainCfg.globalFilter = cfg.filter;

    mainCfg.firstPair.folderPathPhraseLeft  = cfg.left;
    mainCfg.firstPair.folderPathPhraseRight = cfg.right;
    return mainCfg;
}


void recordPlan(FolderComparison& folderCmp, RunResult& result)
{
    const auto record = [&](FileSystemObject& fsObj)
    {
        if (!fsObj.isActive())
            return;
        const SyncOperation op = fsObj.getSyncOperation();
        if (op == SO_EQUAL)
            return;
        ++result.ops[op];
        const Zstring relPath = fsObj.isEmpty<SelectSide::left>() ? fsObj.getRelativePath<SelectSide::right>() : fsObj.getRelativePath<SelectSide::left>();
        result.plan.push_back(utfTo<std::string>(relPath) + ": " + opName(op));
    };
    for (SharedRef<BaseFolderPair>& baseFolder : folderCmp)
        visitFSObjectRecursively(static_cast<ContainerObject&>(baseFolder.ref()),
                                 [&](FolderPair&  folder) { record(folder); },
                                 [&](FilePair&    file)   { record(file); },
                                 [&](SymlinkPair& link)   { record(link); });

    result.conflicts = SyncStatistics(folderCmp).conflictCount();
    result.allEqual  = allElementsEqual(folderCmp);
}


RunResult runEngine(const SyncCfg& cfg, bool doSync)
{
    RunResult result;
    RecordingCallback callback(result);

    const MainConfiguration mainCfg = toMainCfg(cfg);
    WarningDialogs warnings; //all warnings on: they end up in result.warnings
    std::unique_ptr<LockHolder> dirLocks;

    FolderComparison folderCmp = compare(warnings,
                                         2 /*fileTimeTolerance: GlobalConfig's default*/,
                                         {} /*requestPassword*/,
                                         false /*runWithBackgroundPriority*/,
                                         true  /*createDirLocks*/,
                                         dirLocks,
                                         extractCompareCfg(mainCfg),
                                         callback);
    recordPlan(folderCmp, result);

    if (doSync && !folderCmp.empty())
        synchronize(std::chrono::system_clock::now(),
                    false /*verifyCopiedFiles*/,
                    false /*copyLockedFiles*/,
                    cfg.copyPermissions,
                    true  /*failSafeFileCopy*/,
                    false /*runWithBackgroundPriority*/,
                    extractSyncCfg(mainCfg),
                    folderCmp,
                    warnings,
                    callback);
    return result;
}
}


RunResult synctest::compareOnly(const SyncCfg& cfg) { return runEngine(cfg, false); }
RunResult synctest::runSync    (const SyncCfg& cfg) { return runEngine(cfg, true); }


std::string RunResult::toString() const
{
    std::string out = "   plan (" + std::to_string(plan.size()) + "):\n";
    for (const std::string& line : plan)
        out += "      " + line + '\n';
    out += "   errors (" + std::to_string(errors.size()) + "):\n";
    for (const std::string& msg : errors)
        out += "      " + replaceCpy(msg, '\n', ' ') + '\n';
    out += "   warnings (" + std::to_string(warnings.size()) + "):\n";
    for (const std::string& msg : warnings)
        out += "      " + replaceCpy(msg, '\n', ' ') + '\n';
    return out;
}

//------------------------------------------------------------------------------------------

Zstring synctest::itemPath(const Zstring& rootPath, const std::string& relPath)
{
    return appendPath(rootPath, nativeRelPath(utfTo<Zstring>(relPath)));
}


void synctest::setModTime(const Zstring& path, time_t modTime)
{
    fs::last_write_time(toPath(path), std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(modTime)));
}


void synctest::makeFolder(const Zstring& rootPath, const std::string& relPath)
{
    fs::create_directories(toPath(itemPath(rootPath, relPath)));
}


void synctest::makeFile(const Zstring& rootPath, const std::string& relPath, const std::string& content, time_t modTime)
{
    const Zstring path = itemPath(rootPath, relPath);
    fs::create_directories(toPath(path).parent_path());
    writeFile(path, content);
    setModTime(path, modTime);
}


void synctest::makeSymlink(const Zstring& rootPath, const std::string& relPath, const std::string& target)
{
    const Zstring path = itemPath(rootPath, relPath);
    fs::create_directories(toPath(path).parent_path());
    fs::create_symlink(toPath(utfTo<Zstring>(target)), toPath(path));
}


void synctest::makeTree(const Zstring& rootPath, const Tree& tree)
{
    fs::create_directories(toPath(rootPath));
    for (const auto& [relPath, e] : tree)
        switch (e.type)
        {
            case Entry::Type::file:    makeFile   (rootPath, relPath, e.content, e.modTime); break;
            case Entry::Type::folder:  makeFolder (rootPath, relPath); break;
            case Entry::Type::symlink: makeSymlink(rootPath, relPath, e.content); break;
        }
}


Tree synctest::readTree(const Zstring& rootPath, const std::set<std::string>& skipNames)
{
    Tree tree;
    const fs::path root = toPath(rootPath);
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) //does not follow symlinks
    {
        const fs::path& p = it->path();
        if (skipNames.contains(toZ(p.filename()).c_str()))
        {
            if (it->is_directory() && !it->is_symlink())
                it.disable_recursion_pending();
            continue;
        }
        const std::u8string rel = p.lexically_relative(root).generic_u8string();
        const std::string relPath(rel.begin(), rel.end());

        Entry e;
        const fs::file_status st = fs::symlink_status(p);
        if (fs::is_symlink(st))
        {
            e.type = Entry::Type::symlink;
            const std::u8string target = fs::read_symlink(p).u8string();
            e.content.assign(target.begin(), target.end());
        }
        else if (fs::is_directory(st))
            e.type = Entry::Type::folder;
        else
        {
            e.type = Entry::Type::file;
            e.content = readFile(toZ(p));
            e.modTime = getModTime(toZ(p));
        }
        tree.emplace(relPath, std::move(e));
    }
    return tree;
}


std::string synctest::describe(const Entry& e)
{
    switch (e.type)
    {
        case Entry::Type::folder:
            return "folder";
        case Entry::Type::symlink:
            return "symlink -> \"" + e.content + '"';
        case Entry::Type::file:
            return "file, " + std::to_string(e.content.size()) + " bytes \"" + (e.content.size() <= 40 ? e.content : e.content.substr(0, 40) + "...") +
                   "\", mtime " + std::to_string(e.modTime);
    }
    return "?";
}


void synctest::checkTreesEqual(const char* file, int line, const Tree& actual, const Tree& expected, int modTimeTolerance)
{
    ++checkCount;
    std::string diff;
    for (const auto& [relPath, e] : expected)
        if (auto it = actual.find(relPath); it == actual.end())
            diff += "   missing: " + relPath + " (" + describe(e) + ")\n";
        else
        {
            const Entry& a = it->second;
            if (a.type != e.type || a.content != e.content ||
                std::abs(static_cast<long long>(a.modTime - e.modTime)) > modTimeTolerance)
                diff += "   different: " + relPath + ": " + describe(a) + ", expected " + describe(e) + '\n';
        }
    for (const auto& [relPath, a] : actual)
        if (!expected.contains(relPath))
            diff += "   unexpected: " + relPath + " (" + describe(a) + ")\n";

    if (!diff.empty())
        reportFailure(file, line, "folder trees differ:\n" + diff);
}

//------------------------------------------------------------------------------------------

std::string synctest::runShell(const Zstring& cmdLine) //throw SysError
{
    const auto [exitCode, output] = consoleExecute(cmdLine, std::nullopt); //throw SysError, SysErrorTimeOut
    if (exitCode != 0)
        throw SysError(L"exit code " + numberTo<std::wstring>(exitCode) + L": " + utfTo<std::wstring>(cmdLine) + L"\n" + utfTo<std::wstring>(output));
    return utfTo<std::string>(output);
}


bool synctest::runningAsRoot()
{
    //portable check without <unistd.h>: can a file without any permission still be read?
    const fs::path probe = fs::temp_directory_path() / ("sync_test_root_probe_" + std::to_string(std::random_device()()));
    writeFile(toZ(probe), "x");
    fs::permissions(probe, fs::perms::none);
    std::ifstream in(probe, std::ios::binary);
    const bool readable = in.good() && in.get() == 'x';
    in.close();
    fs::permissions(probe, fs::perms::owner_all);
    fs::remove(probe);
    return readable;
}


Zstring synctest::realHomePath()
{
    if (const char* home = std::getenv("SYNC_TEST_REAL_HOME"))
        return home;
    if (const char* home = std::getenv("HOME"))
        return home;
    return {};
}


std::string synctest::uniqueName(const std::string& base)
{
    static const std::string runId = std::to_string(std::random_device()());
    return "sync_test " + runId + ' ' + base;
}
