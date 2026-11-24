// Minimal test harness for Tests/zen: CHECK-style macros that count failures and print file/line, no framework.
//
// - TEST_CASE(name) { ... } registers a test; it runs in a fresh, empty folder "testDir" (Zstring, absolute path) that is removed afterwards
// - SANDBOX_TEST_CASE(name): same, for tests that depend on the environment run.sh sets up (HOME/XDG_DATA_HOME in a sandbox, ZEN_TEST_* variables);
//   skipped unless ZEN_TEST_SANDBOX is set, so running zen_test directly can't e.g. move files into the user's real recycle bin
// - CHECK(cond), CHECK_EQUAL(a, b): count a failure and continue
// - CHECK_THROWS(E, statement...): statement must throw E (or a type derived from E)
// - CHECK_THROWS_NOT(E, NotE, statement...): statement must throw E, but not the more specific NotE (e.g. FileError, but not ErrorTargetExisting)
// - KNOWN_ISSUE(cond, note): documents an upstream bug: a failing cond is reported but not counted as failure;
//   a cond that holds (the bug no longer reproduces) is a failure, so stale markers get noticed
//
// Test code must stay portable: use std::filesystem or zen APIs only, and branch on ZEN_LINUX/ZEN_MAC/ZEN_WIN (zen/build_info.h)
// where the expected behavior legitimately differs, with a comment.
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <zen/build_info.h>
#include <zen/file_error.h>
#include <zen/file_path.h>


namespace zentest
{
using TestFunction = void (*)(const Zstring& testDir);

struct TestCase
{
    const char* name;
    TestFunction fun;
    bool needsSandbox;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar(const char* name, TestFunction fun, bool needsSandbox) { registry().push_back({name, fun, needsSandbox}); }
};

inline int checkCount      = 0;
inline int failureCount    = 0;
inline int knownIssueCount = 0;


inline void reportFailure(const char* file, int line, const std::string& what)
{
    ++failureCount;
    std::cerr << file << ':' << line << ": FAILED: " << what << std::endl;
}


//describe the exception currently being handled (call inside a catch block only)
inline std::string describeCurrentException()
{
    try { throw; }
    catch (const zen::FileError& e) { return "FileError: " + zen::utfTo<std::string>(e.toString()); }
    catch (const zen::SysError&  e) { return "SysError: "  + zen::utfTo<std::string>(e.toString()); }
    catch (const std::exception& e) { return std::string("std::exception: ") + e.what(); }
    catch (...)                     { return "unknown exception"; }
}

//------------------------------------------------------------------------------------------
inline std::string toPrintable(bool b) { return b ? "true" : "false"; }
inline std::string toPrintable(const std::string&  str) { return '"' + str + '"'; }
inline std::string toPrintable(const Zstring&      str) { return '"' + zen::utfTo<std::string>(str) + '"'; }
inline std::string toPrintable(const std::wstring& str) { return '"' + zen::utfTo<std::string>(str) + '"'; }
inline std::string toPrintable(const char*         str) { return '"' + std::string(str) + '"'; }
template <size_t N>
std::string toPrintable(const char (&str)[N]) { return toPrintable(static_cast<const char*>(str)); }

template <class T>
std::string toPrintable(const T& val)
{
    if constexpr (std::is_enum_v<T>)
        return "enum(" + std::to_string(static_cast<long long>(val)) + ')';
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(val);
    else
        return "<value>";
}

template <class T>
std::string toPrintable(const std::optional<T>& val) { return val ? toPrintable(*val) : "nullopt"; }

//------------------------------------------------------------------------------------------
//helpers independent from the zen code under test (std::filesystem), to cross-check its results

inline std::filesystem::path toPath(const Zstring& str) //Zstring is UTF-8 on all platforms
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(str.c_str()), str.size()));
}

inline Zstring toZ(const std::filesystem::path& path)
{
    const std::u8string str = path.u8string();
    return Zstring(reinterpret_cast<const char*>(str.data()), str.size());
}

inline void writeFile(const Zstring& filePath, const std::string& content)
{
    std::ofstream out(toPath(filePath), std::ios::binary | std::ios::trunc);
    out.write(content.data(), content.size());
    if (!out.good())
        throw std::runtime_error("test setup: cannot write " + zen::utfTo<std::string>(filePath));
}

inline std::string readFile(const Zstring& filePath)
{
    std::ifstream in(toPath(filePath), std::ios::binary);
    if (!in.good())
        throw std::runtime_error("test setup: cannot read " + zen::utfTo<std::string>(filePath));
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

inline bool pathExists(const Zstring& itemPath) //does not follow symlinks
{
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::symlink_status(toPath(itemPath), ec));
}

inline time_t getModTime(const Zstring& itemPath) //follows symlinks
{
    const std::filesystem::file_time_type ft = std::filesystem::last_write_time(toPath(itemPath));
    return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(ft)));
}

//"a/b/c" => path with native separators
inline Zstring nativeRelPath(Zstring relPath)
{
    for (Zchar& c : relPath)
        if (c == Zstr('/'))
            c = zen::FILE_NAME_SEPARATOR;
    return relPath;
}
}


#define ZEN_TEST_CASE_IMPL(name, needsSandbox)                                  \
    static void name(const Zstring& testDir);                                   \
    static const zentest::Registrar name##Registrar(#name, name, needsSandbox); \
    static void name([[maybe_unused]] const Zstring& testDir)

#define TEST_CASE(name)         ZEN_TEST_CASE_IMPL(name, false)
#define SANDBOX_TEST_CASE(name) ZEN_TEST_CASE_IMPL(name, true)

#define CHECK(...) /*variadic: conditions may contain commas*/                 \
    do {                                                                        \
        ++zentest::checkCount;                                                  \
        if (!(__VA_ARGS__))                                                     \
            zentest::reportFailure(__FILE__, __LINE__, #__VA_ARGS__);           \
    } while (false)

#define CHECK_EQUAL(a, b)                                                       \
    do {                                                                        \
        ++zentest::checkCount;                                                  \
        const auto& valA_ = (a);                                                \
        const auto& valB_ = (b);                                                \
        if (!(valA_ == valB_))                                                  \
            zentest::reportFailure(__FILE__, __LINE__, #a " == " #b ": " +      \
                                   zentest::toPrintable(valA_) + " != " + zentest::toPrintable(valB_)); \
    } while (false)

#define CHECK_THROWS(E, ...)                                                    \
    do {                                                                        \
        ++zentest::checkCount;                                                  \
        try                                                                     \
        {                                                                       \
            __VA_ARGS__;                                                        \
            zentest::reportFailure(__FILE__, __LINE__, "no " #E " thrown by: " #__VA_ARGS__); \
        }                                                                       \
        catch (const E&) {}                                                     \
        catch (...) { zentest::reportFailure(__FILE__, __LINE__, "expected " #E ", got " + zentest::describeCurrentException() + " from: " #__VA_ARGS__); } \
    } while (false)

#define CHECK_THROWS_NOT(E, NotE, ...)                                          \
    do {                                                                        \
        ++zentest::checkCount;                                                  \
        try                                                                     \
        {                                                                       \
            __VA_ARGS__;                                                        \
            zentest::reportFailure(__FILE__, __LINE__, "no " #E " thrown by: " #__VA_ARGS__); \
        }                                                                       \
        catch (const NotE&) { zentest::reportFailure(__FILE__, __LINE__, "expected " #E ", got the more specific " #NotE " from: " #__VA_ARGS__); } \
        catch (const E&) {}                                                     \
        catch (...) { zentest::reportFailure(__FILE__, __LINE__, "expected " #E ", got " + zentest::describeCurrentException() + " from: " #__VA_ARGS__); } \
    } while (false)

#define CHECK_NO_THROW(...)                                                     \
    do {                                                                        \
        ++zentest::checkCount;                                                  \
        try { __VA_ARGS__; }                                                    \
        catch (...) { zentest::reportFailure(__FILE__, __LINE__, "unexpected " + zentest::describeCurrentException() + " from: " #__VA_ARGS__); } \
    } while (false)

#define KNOWN_ISSUE(cond, note)                                                 \
    do {                                                                        \
        if (!(cond))                                                            \
        {                                                                       \
            ++zentest::knownIssueCount;                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": KNOWN ISSUE: " #cond " (" << (note) << ')' << std::endl; \
        }                                                                       \
        else                                                                    \
            zentest::reportFailure(__FILE__, __LINE__, "known issue no longer reproduces, update the test: " #cond); \
    } while (false)
