// Test runner for the zen platform layer tests
//
// usage: zen_test [--root <folder>] [--list] [<test name>...]
//   --root: parent folder for the per-test temporary folders (default: the system temp folder); must exist
//   names:  run only these tests (default: all)
//
// Each test runs in a new folder "<root>/zen_test-<random>/<test name>", removed afterwards with everything in it.

#include <random>
#include "test.h"

using namespace zentest;
namespace fs = std::filesystem;


int main(int argc, char* argv[])
{
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
    const fs::path basePath = rootPath / ("zen_test-" + std::to_string(std::random_device()()));
    if (!fs::create_directory(basePath))
    {
        std::cerr << "folder already exists: " << basePath << '\n';
        return 2;
    }
    const fs::path baseCanonical = fs::canonical(basePath); //tests compare paths: avoid symlinks in the temp folder path (e.g. /tmp => /private/tmp on macOS)

    for (const TestCase& tc : tests)
    {
        const int failuresBefore = failureCount;
        std::cout << "== " << tc.name << std::endl;

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

        std::error_code ec;
        fs::permissions(testPath, fs::perms::owner_all, fs::perm_options::add, ec); //in case a test left it read-only
        fs::remove_all(testPath, ec);
        if (ec)
            reportFailure(tc.name, 0, "cannot remove test folder: " + ec.message());

        if (failureCount != failuresBefore)
            std::cout << "   " << failureCount - failuresBefore << " failure(s)" << std::endl;
    }

    std::error_code ec;
    fs::remove(basePath, ec);
    if (ec)
        reportFailure("main", 0, "cannot remove base folder (not empty?): " + ec.message());

    std::cout << "== zen tests: " << tests.size() << " test cases, " << checkCount << " checks, " <<
              failureCount << " failures, " << knownIssueCount << " known issues" << std::endl;
    return failureCount == 0 ? 0 : 1;
}
