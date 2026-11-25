// zen/sys_info.h, zen/sys_version.h, zen/process_exec.h, zen/thread.h, zen/sys_error.h

#include <atomic>
#include <zen/sys_info.h>
#include <zen/sys_version.h>
#include <zen/process_exec.h>
#include <zen/thread.h>
#include <zen/file_access.h>
#include <zen/process_priority.h>
#include "test.h"

#ifdef ZEN_LINUX
    #include <gio/gio.h> //G_IO_ERROR
#endif

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


TEST_CASE(sysInfo)
{
    Zstring value;
    CHECK_NO_THROW(value = getLoginUser());
    CHECK(!value.empty());
    CHECK_NO_THROW(value = getHostName());
    CHECK(!value.empty());
    CHECK(!endsWith(value, ".local")); //stripped

    CHECK_NO_THROW((void)getComputerModel()); //"best-effort: empty if not available"
    CHECK(!getOsDescription().empty());

    //process path: the test executable itself
    CHECK_NO_THROW(value = getProcessPath());
    CHECK(fs::is_regular_file(toPath(value)));
    CHECK(fs::path(toPath(value)).filename().string().starts_with("zen_test"));

    CHECK_NO_THROW(value = getUserHome());
    CHECK(fs::is_directory(toPath(value)));
    CHECK_NO_THROW(value = getUserDataPath());
    CHECK(!value.empty());
    try
    {
        value = getUserDownloadsPath(); //throw FileError
        CHECK(!value.empty());
    }
    catch (const FileError& e)
    {
#ifdef ZEN_LINUX
        //upstream bug: non-root users get "xdg-user-dir DOWNLOAD" through /bin/sh; if xdg-user-dir is not installed, sh exits with 127,
        //which consoleExecute() turns into SysError, so the "~/Downloads" fallback in sys_info.cpp is never reached
        if (contains(e.toString(), L"xdg-user-dir"))
            KNOWN_ISSUE(false, "getUserDownloadsPath() fails without xdg-user-dir instead of falling back to ~/Downloads");
        else
#endif
            reportFailure(__FILE__, __LINE__, "getUserDownloadsPath(): " + utfTo<std::string>(e.toString()));
    }
    CHECK_NO_THROW((void)runningElevated());
}


TEST_CASE(sysVersion)
{
    const OsVersionDetail ver = getOsVersion();
    CHECK(!ver.osName.empty());
#ifdef ZEN_LINUX
    CHECK(ver.version.major >= 0); //rolling releases (e.g. Arch) have no version: 0.0
#else
    CHECK(ver.version.major > 0);
    CHECK(!ver.osVersionRaw.empty());
#endif
    CHECK(ver.version <=> OsVersion{ver.version.major, ver.version.minor} == std::strong_ordering::equal);
    CHECK((OsVersion{10, 2} < OsVersion{10, 10}));
    CHECK((OsVersion{9, 99} < OsVersion{10, 0}));
}


TEST_CASE(processExec)
{
    //exit code and output
    {
        const auto [exitCode, output] = consoleExecute("echo hello", std::nullopt); //throw SysError, SysErrorTimeOut
        CHECK_EQUAL(exitCode, 0);
        CHECK_EQUAL(trimCpy(output), Zstring("hello"));
    }
    {
        const auto [exitCode, output] = consoleExecute("exit 3", std::nullopt);
        CHECK_EQUAL(exitCode, 3);
    }
#if defined ZEN_LINUX || defined ZEN_MAC
    {
        //stderr is captured, too
        const auto [exitCode, output] = consoleExecute("echo out; echo err >&2; exit 42", 10'000);
        CHECK_EQUAL(exitCode, 42);
        CHECK(contains(output, "out") && contains(output, "err"));
    }
    //command not found: /bin/sh exits with 127 => SysError with the shell's message
    CHECK_THROWS(SysError, (void)consoleExecute("zen-test-no-such-command-xyz", std::nullopt));

    //timeout
    const auto startTime = std::chrono::steady_clock::now();
    //"SysErrorTimeOut leaves zombie process behind": keep the child short-lived, so it doesn't outlive the test by more than a moment
    CHECK_THROWS(SysErrorTimeOut, (void)consoleExecute("sleep 1", 200));
    CHECK(std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(900)); //returned at the timeout, not when the child ended
#else
    //Windows: "cmd.exe returns exit code 1 if file not found (instead of throwing SysError)"
    {
        const auto [exitCode, output] = consoleExecute("zen-test-no-such-command-xyz", std::nullopt);
        CHECK(exitCode != 0);
    }
    CHECK_THROWS(SysErrorTimeOut, (void)consoleExecute("ping -n 10 127.0.0.1", 200));
#endif

#ifdef ZEN_MAC
    //openWithDefaultApp(): "open" fails for a missing item (opening a real one would start an app on the test machine)
    CHECK_THROWS(FileError, openWithDefaultApp(appendPath(testDir, "missing file")));
#endif

    //escapeCommandArg(): round trip through the shell
    for (const Zstring& arg : {Zstring("plain"), Zstring("with space"), Zstring("quote\"s and 'single'"), Zstring("$HOME `cmd` \\ & | ; < > ( ) # ~ *")})
    {
#if defined ZEN_LINUX || defined ZEN_MAC
        const auto [exitCode, output] = consoleExecute("printf '%s' " + escapeCommandArg(arg), std::nullopt);
        CHECK_EQUAL(exitCode, 0);
        CHECK_EQUAL(output, arg);
#else
        CHECK(!escapeCommandArg(arg).empty()); //cmd.exe quoting differs; checked once the Windows implementation exists
#endif
    }
}


TEST_CASE(processPriority)
{
    //background: lower I/O priority (Linux: ioprio class "best effort" 6, macOS: IOPOL_UTILITY), restored by the destructor
    CHECK_NO_THROW(SetProcessPriority{ProcessPriority::background});
    CHECK_NO_THROW(SetProcessPriority{ProcessPriority::normal});
    {
        std::unique_ptr<SetProcessPriority> prio;
        CHECK_NO_THROW(prio = std::make_unique<SetProcessPriority>(ProcessPriority::background));
        std::unique_ptr<SetProcessPriority> nested; //nested scopes restore in reverse order
        CHECK_NO_THROW(nested = std::make_unique<SetProcessPriority>(ProcessPriority::background));
    }
}


TEST_CASE(threads)
{
    CHECK(runningOnMainThread());

    //runAsync
    auto ft = runAsync([] { return std::make_pair(runningOnMainThread(), 42); });
    const auto [onMain, value] = ft.get();
    CHECK(!onMain);
    CHECK_EQUAL(value, 42);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 4; ++i)
        futures.push_back(runAsync([i] { return i * i; }));
    CHECK(waitForAllTimed(futures.begin(), futures.end(), std::chrono::seconds(10)));
    int sum = 0;
    for (auto& f : futures)
    {
        CHECK(isReady(f));
        sum += f.get();
    }
    CHECK_EQUAL(sum, 0 + 1 + 4 + 9);

    //InterruptibleThread: requestStop() ends interruptibleSleep() with ThreadStopRequest
    std::atomic<bool> started = false;
    std::atomic<bool> stopped = false;
    {
        InterruptibleThread t([&]
        {
            setCurrentThreadName("zen test worker");
            started = true;
            try
            {
                interruptibleSleep(std::chrono::seconds(60)); //throw ThreadStopRequest
            }
            catch (ThreadStopRequest&) { stopped = true; }
        });
        while (!started)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto startTime = std::chrono::steady_clock::now();
        t.requestStop();
        t.join();
        CHECK(std::chrono::steady_clock::now() - startTime < std::chrono::seconds(10));
    }
    CHECK(stopped);

    //destructor interrupts and joins
    stopped = false;
    {
        InterruptibleThread t([&]
        {
            try
            {
                for (;;)
                {
                    interruptionPoint(); //throw ThreadStopRequest
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            catch (ThreadStopRequest&) { stopped = true; }
        });
    }
    CHECK(stopped);

    //Protected
    Protected<std::vector<int>> prot;
    {
        std::vector<InterruptibleThread> workers;
        for (int i = 0; i < 4; ++i)
            workers.emplace_back([&prot]
            {
                for (int j = 0; j < 1000; ++j)
                    prot.access([](std::vector<int>& v) { v.push_back(1); });
            });
        for (InterruptibleThread& w : workers)
            w.join();
    }
    CHECK_EQUAL(prot.access([](const std::vector<int>& v) { return v.size(); }), static_cast<size_t>(4000));

    //ThreadGroup
    std::atomic<int> counter = 0;
    {
        ThreadGroup<std::function<void()>> group("zen test group", 3);
        for (int i = 0; i < 100; ++i)
            group.run([&counter] { ++counter; });
        group.wait();
        CHECK_EQUAL(counter.load(), 100);
    }

    //AsyncFirstResult: first successful job wins
    AsyncFirstResult<int> firstResult;
    firstResult.addJob([] { return std::optional<int>(); });
    firstResult.addJob([] { return std::optional<int>(7); });
    CHECK(firstResult.timedWait(std::chrono::seconds(10)));
    CHECK_EQUAL(firstResult.get(), std::optional<int>(7));

    AsyncFirstResult<int> noResult;
    noResult.addJob([] { return std::optional<int>(); });
    CHECK_EQUAL(noResult.get(), std::optional<int>()); //"none if all jobs failed"

    //parallelTransform
    const std::vector<int> input{1, 2, 3, 4, 5};
    const std::vector<int> output = parallelTransform(input, [](int i) { return 10 * i; }, "zen test transform");
    CHECK((output == std::vector<int>{10, 20, 30, 40, 50}));
}


TEST_CASE(sysError)
{
#ifdef ZEN_WIN
    const ErrorCode ecNotFound = 2; //ERROR_FILE_NOT_FOUND
    const std::wstring ecName = L"ERROR_FILE_NOT_FOUND";
#else
    const ErrorCode ecNotFound = ENOENT;
    const std::wstring ecName = L"ENOENT";
#endif
    const std::wstring msg = formatSystemError("open", ecNotFound);
    CHECK(startsWith(msg, ecName + L": "));
    CHECK(endsWith(msg, L" [open]"));
    CHECK(msg.size() > ecName.size() + std::wstring(L":  [open]").size()); //has a description
    CHECK(!getSystemErrorDescription(ecNotFound).empty());

    //unknown code: still some text
    CHECK(!formatSystemError("fn", static_cast<ErrorCode>(987654)).empty());

    //composition: "<code>: <message> [<function>]", trimmed
    CHECK_EQUAL(formatSystemError("fn", L"CODE", L"message"), std::wstring(L"CODE: message [fn]"));
    CHECK_EQUAL(formatSystemError("fn", L"", L" message "), std::wstring(L"message [fn]"));
    CHECK_EQUAL(formatSystemError("", L"CODE", L""), std::wstring(L"CODE"));

    //getLastError() and THROW_LAST_SYS_ERROR
#ifndef ZEN_WIN
    errno = ENOENT;
    CHECK_EQUAL(getLastError(), ENOENT);
#endif
    try
    {
        fs::path missing = fs::temp_directory_path() / "zen-test-no-such-file-xyz";
        (void)getFileSize(toZ(missing)); //throw FileError
        reportFailure(__FILE__, __LINE__, "getFileSize() on missing file did not throw");
    }
    catch (const FileError& e)
    {
        CHECK(contains(e.toString(), ecName)); //error code is part of the details
        CHECK(contains(e.toString(), L"zen-test-no-such-file-xyz")); //path is part of the message
    }

    try
    {
#ifndef ZEN_WIN
        errno = EACCES;
#endif
        THROW_LAST_SYS_ERROR("someFunction");
    }
    catch (const SysError& e)
    {
        CHECK(endsWith(e.toString(), L"[someFunction]"));
#ifndef ZEN_WIN
        CHECK(startsWith(e.toString(), L"EACCES"));
#endif
    }

    CHECK_THROWS(SysError, ASSERT_SYSERROR(1 + 1 == 3));
    CHECK_NO_THROW(ASSERT_SYSERROR(1 + 1 == 2));

#ifdef ZEN_LINUX
    //GLib errors (GIO-based features on Linux)
    CHECK(!formatGlibError("g_fn", nullptr).empty());
    GError* error = ::g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_NOENT, "glib message");
    ZEN_ON_SCOPE_EXIT(::g_error_free(error));
    const std::wstring glibMsg = formatGlibError("g_fn", error);
    CHECK(endsWith(glibMsg, L"[g_fn]"));
    //upstream bug: GFileError values are not errno values (G_FILE_ERROR_NOENT == 4 == EINTR), yet sys_error.cpp formats
    //error->code with formatSystemError(functionName, ErrorCode), which yields "EINTR: Interrupted system call" and drops the GLib message
    KNOWN_ISSUE(startsWith(glibMsg, L"ENOENT"), "formatGlibError(): G_FILE_ERROR code formatted as errno: " + utfTo<std::string>(glibMsg));

    GError* ioError = ::g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "gio message");
    ZEN_ON_SCOPE_EXIT(::g_error_free(ioError));
    CHECK_EQUAL(formatGlibError("g_fn", ioError), std::wstring(L"G_IO_ERROR_NOT_FOUND: gio message [g_fn]"));
#endif

    //FileError: message + details
    const FileError fe(L"message", L"details");
    CHECK_EQUAL(fe.toString(), std::wstring(L"message\n\ndetails"));
    CHECK_EQUAL(fmtPath(Zstring("/a b")), std::wstring(L"\"/a b\""));
}
