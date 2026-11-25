// Test runner for the headless sync tests (Tests/sync): the runner of Tests/zen (same harness, test.h) plus the engine's global
// state: the file systems are initialized once (fff::initAfs(), with an empty config folder) and torn down at the end, like the
// apps do. No wxWidgets initialization: the engine needs no wxApp, translations are off (English texts).
//
// usage: sync_test [--root <folder>] [--list] [<test name>...]
//        sync_test --write-batch <file.ffs_batch> <left folder> <right folder> <log folder>
//        sync_test --write-gui <file.ffs_gui> <left folder> <right folder>
//        sync_test --write-global <GlobalSettings.xml>
//        sync_test --write-global-unanswered <GlobalSettings.xml>
//   --root: parent folder for the per-test temporary folders (default: the system temp folder); must exist
//   names:  run only these tests (default: all)
//   --write-batch: writes a batch job that mirrors <left folder> to <right folder> through the engine's own config writer
//                  (fff::writeConfig()), for running the app in batch mode in CI: no popups (errors cancel the job), the progress
//                  window closes itself, the log goes to <log folder>
//   --write-gui:    writes a GUI configuration with <left folder> and <right folder> as the folder pair (FreeFileSync compares at
//                   once when started with it and both folders exist)
//   --write-global: writes default global settings that skip the first-start dialogs (welcome dialog of this version done, update
//                   check just done, the crash report notice answered with "don't send"), for starting the app's main window in
//                   CI: FreeFileSync <file.ffs_gui> <GlobalSettings.xml>
//   --write-global-unanswered: the same without an answer to the crash report notice, which a crash reporting build then shows
//
// Each test runs in a new folder "<root>/sync_test-<random>/<test name>", removed afterwards with everything in it.
// SANDBOX_TEST_CASE tests (recycle bin) run only if ZEN_TEST_SANDBOX is set, as Tests/sync/run.sh does after pointing HOME and
// XDG_DATA_HOME into a temporary folder (Linux: the trash then lives there; macOS: see test_deletion.cpp).

#include <cstdlib>
#include <ctime>
#include <random>
#include <FreeFileSync/Source/afs/concrete.h>
#include <FreeFileSync/Source/config.h>
#include <FreeFileSync/Source/version/version.h>
#include "../zen/test.h"

using namespace zentest;
namespace fs = std::filesystem;


int main(int argc, char* argv[])
{
    if (argc == 6 && std::string(argv[1]) == "--write-batch")
        try
        {
            fff::FfsBatchConfig cfg;
            cfg.guiCfg.mainCfg.syncCfg.directionCfg = fff::getDefaultSyncCfg(fff::SyncVariant::mirror);
            cfg.guiCfg.mainCfg.syncCfg.deletionVariant = fff::DeletionVariant::permanent;
            cfg.guiCfg.mainCfg.firstPair.folderPathPhraseLeft  = argv[3];
            cfg.guiCfg.mainCfg.firstPair.folderPathPhraseRight = argv[4];
            cfg.guiCfg.mainCfg.altLogFolderPathPhrase = argv[5];
            cfg.batchExCfg.autoCloseSummary   = true;
            cfg.batchExCfg.batchErrorHandling = fff::BatchErrorHandling::cancel;
            fff::writeConfig(cfg, argv[2]); //throw FileError
            return 0;
        }
        catch (const zen::FileError& e)
        {
            std::cerr << zen::utfTo<std::string>(e.toString()) << '\n';
            return 1;
        }

    if (argc == 5 && std::string(argv[1]) == "--write-gui")
        try
        {
            fff::FfsGuiConfig cfg;
            cfg.mainCfg.firstPair.folderPathPhraseLeft  = argv[3];
            cfg.mainCfg.firstPair.folderPathPhraseRight = argv[4];
            fff::writeConfig(cfg, argv[2]); //throw FileError
            return 0;
        }
        catch (const zen::FileError& e)
        {
            std::cerr << zen::utfTo<std::string>(e.toString()) << '\n';
            return 1;
        }

    if (argc == 3 && (std::string(argv[1]) == "--write-global" || std::string(argv[1]) == "--write-global-unanswered"))
        try
        {
            fff::GlobalConfig cfg;
            cfg.welcomeDialogLastVersion = fff::ffsVersion;
            cfg.lastUpdateCheck = std::time(nullptr);
            if (std::string(argv[1]) == "--write-global")
                cfg.crashReports = false; //no first-start notice in a crash reporting build, and nothing sent
            fff::writeConfig(cfg, argv[2]); //throw FileError
            return 0;
        }
        catch (const zen::FileError& e)
        {
            std::cerr << zen::utfTo<std::string>(e.toString()) << '\n';
            return 1;
        }

    fs::path rootPath = fs::temp_directory_path();
    std::vector<std::string> selected;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc)
            rootPath = toPath(argv[++i]);
        else if (arg == "--list")
            listOnly = true;
        else if (arg.starts_with("--"))
        {
            std::cerr << "unknown option: " << arg << '\n';
            return 2;
        }
        else
            selected.push_back(arg);
    }

    std::vector<TestCase> tests;
    for (const TestCase& tc : registry())
        if (selected.empty() || std::find(selected.begin(), selected.end(), tc.name) != selected.end())
            tests.push_back(tc);

    for (const std::string& name : selected)
        if (std::none_of(registry().begin(), registry().end(), [&](const TestCase& tc) { return name == tc.name; }))
        {
            std::cerr << "unknown test: " << name << '\n';
            return 2;
        }

    if (listOnly)
    {
        for (const TestCase& tc : tests)
            std::cout << tc.name << '\n';
        return 0;
    }

    //fresh base folder: fail instead of reusing an existing one
    const fs::path basePath = rootPath / ("sync_test-" + std::to_string(std::random_device()()));
    if (!fs::create_directory(basePath))
    {
        std::cerr << "folder already exists: " << basePath << '\n';
        return 2;
    }
    const fs::path baseCanonical = fs::canonical(basePath); //tests compare paths: avoid symlinks in the temp folder path (e.g. /tmp => /private/tmp on macOS)

    //engine globals: AFS config (Google Drive sessions, CA file) in a folder of its own, removed at the end
    const fs::path afsConfigPath = baseCanonical / "afs config";
    fs::create_directory(afsConfigPath);
    fff::initAfs({toZ(afsConfigPath) /*resourceDirPath: not used by the native, zip, FTP and SFTP code*/, toZ(afsConfigPath)});

    const bool inSandbox = std::getenv("ZEN_TEST_SANDBOX") != nullptr;
    size_t skippedCount = 0;

    for (const TestCase& tc : tests)
    {
        const int failuresBefore = failureCount;
        std::cout << "== " << tc.name << std::endl;

        if (tc.needsSandbox && !inSandbox)
        {
            std::cout << "   SKIPPED: needs the sandbox set up by Tests/sync/run.sh (ZEN_TEST_SANDBOX is not set)" << std::endl;
            ++skippedCount;
            continue;
        }

        const fs::path testPath = baseCanonical / tc.name;
        fs::create_directory(testPath);
        try
        {
            tc.fun(toZ(testPath));
        }
        catch (...)
        {
            reportFailure(tc.name, 0, "test aborted by " + describeCurrentException());
        }

        //in case a test left items read-only or unreadable: pass by pass, as folders made readable reveal their content
        std::error_code ec;
        fs::permissions(testPath, fs::perms::owner_all, fs::perm_options::add, ec);
        for (int pass = 0; pass < 3; ++pass)
            for (auto it = fs::recursive_directory_iterator(testPath, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (!it->is_symlink())
                    fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
        ec.clear();
        fs::remove_all(testPath, ec);
        if (ec)
            reportFailure(tc.name, 0, "cannot remove test folder: " + ec.message());

        if (failureCount != failuresBefore)
            std::cout << "   " << failureCount - failuresBefore << " failure(s)" << std::endl;
    }

    fff::teardownAfs();

    std::error_code ec;
    fs::remove_all(afsConfigPath, ec); //Google Drive's (empty) session store
    fs::remove(basePath, ec);
    if (ec)
        reportFailure("main", 0, "cannot remove base folder (not empty?): " + ec.message());

    std::cout << "== sync tests: " << tests.size() - skippedCount << " test cases, " << checkCount << " checks, " <<
              failureCount << " failures, " << knownIssueCount << " known issues";
    if (skippedCount > 0)
        std::cout << ", " << skippedCount << " skipped (no sandbox)";
    std::cout << std::endl;
    return failureCount == 0 ? 0 : 1;
}
