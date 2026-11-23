// zen/file_io.h

#include <zen/file_io.h>
#include "test.h"

using namespace zen;
using namespace zentest;
namespace fs = std::filesystem;


namespace
{
std::string makeContent(size_t size)
{
    std::string content(size, '\0');
    uint32_t state = 12345;
    for (char& c : content)
    {
        state = state * 1103515245 + 12345; //deterministic pseudo-random bytes, including 0
        c = static_cast<char>(state >> 24);
    }
    return content;
}
}


TEST_CASE(fileIoContent)
{
    for (const size_t size : {static_cast<size_t>(0), static_cast<size_t>(1), static_cast<size_t>(100), FileBase::defaultBlockSize, FileBase::defaultBlockSize + 1, static_cast<size_t>(5'000'017)})
    {
        const Zstring filePath = appendPath(testDir, Zstr("file_") + numberTo<Zstring>(size));
        const std::string content = makeContent(size);

        int64_t bytesWritten = 0;
        CHECK_NO_THROW(setFileContent(filePath, content, [&](int64_t delta) { bytesWritten += delta; }));
        CHECK_EQUAL(bytesWritten, static_cast<int64_t>(size));
        CHECK(readFile(filePath) == content);
        CHECK_EQUAL(getFileSize(filePath), static_cast<uint64_t>(size));

        int64_t bytesRead = 0;
        std::string readBack;
        CHECK_NO_THROW(readBack = getFileContent(filePath, [&](int64_t delta) { bytesRead += delta; }));
        CHECK(readBack == content);
        CHECK_EQUAL(bytesRead, static_cast<int64_t>(size));
    }

    //setFileContent(): "overwrites if existing + transactional"
    const Zstring filePath = appendPath(testDir, "overwrite.txt");
    writeFile(filePath, "old content, longer than the new one");
    CHECK_NO_THROW(setFileContent(filePath, "new", nullptr));
    CHECK_EQUAL(readFile(filePath), std::string("new"));
    size_t itemCount = 0;
    for ([[maybe_unused]] const fs::directory_entry& entry : fs::directory_iterator(toPath(testDir)))
        ++itemCount;
    CHECK_EQUAL(itemCount, static_cast<size_t>(7)); //6 files from above + this one: no temporary files left behind

    //callback exceptions are passed through; the old content survives an aborted write
    CHECK_THROWS(std::runtime_error, setFileContent(filePath, makeContent(1'000'000), [](int64_t) { throw std::runtime_error("abort"); }));
    CHECK_EQUAL(readFile(filePath), std::string("new"));

    //errors
    CHECK_THROWS(FileError, (void)getFileContent(appendPath(testDir, "missing"), nullptr));
    CHECK_THROWS(FileError, setFileContent(appendPath(testDir, nativeRelPath("missing/file")), "x", nullptr));
    CHECK_THROWS(FileError, (void)getFileContent(testDir, nullptr)); //folder
}


TEST_CASE(fileIoStreams)
{
    const std::string content = makeContent(3 * FileBase::defaultBlockSize + 77);
    const Zstring filePath = appendPath(testDir, "stream.bin");

    //buffered output in odd-sized chunks
    {
        FileOutputBuffered out(filePath, nullptr); //throw FileError, ErrorTargetExisting
        for (size_t pos = 0; pos < content.size(); pos += 1000)
            out.write(content.data() + pos, std::min<size_t>(1000, content.size() - pos)); //throw FileError
        out.finalize(); //throw FileError
    }
    CHECK(readFile(filePath) == content);
    CHECK_THROWS(ErrorTargetExisting, FileOutputBuffered(filePath, nullptr));

    //buffered input: "return bytesToRead bytes unless end of stream"
    {
        FileInputBuffered in(filePath, nullptr); //throw FileError, ErrorFileLocked
        std::string readBack;
        std::vector<char> buf(777);
        for (;;)
        {
            const size_t bytesRead = in.read(buf.data(), buf.size()); //throw FileError, ErrorFileLocked
            readBack.append(buf.data(), bytesRead);
            if (bytesRead < buf.size())
            {
                CHECK_EQUAL(in.read(buf.data(), buf.size()), static_cast<size_t>(0));
                break;
            }
        }
        CHECK(readBack == content);
    }

    //plain streams: short reads/writes allowed, 0 means EOF
    const Zstring plainPath = appendPath(testDir, "plain.bin");
    {
        FileOutputPlain out(plainPath); //throw FileError, ErrorTargetExisting
        CHECK_NO_THROW(out.reserveSpace(content.size()));
        size_t pos = 0;
        while (pos < content.size()) //"CONTRACT: bytesToWrite > 0", and a multiple of the block size or less (assert in file_io.cpp)
            pos += out.tryWrite(content.data() + pos, std::min(out.getBlockSize(), content.size() - pos)); //throw FileError
        CHECK(out.getBlockSize() > 0);
        CHECK_EQUAL(out.getFilePath(), plainPath);
        out.close(); //throw FileError
    }
    {
        FileInputPlain in(plainPath); //throw FileError, ErrorFileLocked
        std::string readBack;
        std::vector<char> buf(in.getBlockSize());
        for (;;)
        {
            const size_t bytesRead = in.tryRead(buf.data(), buf.size()); //throw FileError, ErrorFileLocked
            if (bytesRead == 0)
                break;
            readBack.append(buf.data(), bytesRead);
        }
        CHECK(readBack == content);
    }

    //output not closed: "file is considered incomplete and will be deleted"
    const Zstring incompletePath = appendPath(testDir, "incomplete.bin");
    {
        FileOutputPlain out(incompletePath);
        CHECK_EQUAL(out.tryWrite("abc", 3), static_cast<size_t>(3));
        CHECK(pathExists(incompletePath));
    }
    CHECK(!pathExists(incompletePath));
    {
        FileOutputBuffered out(incompletePath, nullptr);
        out.write("abc", 3);
    } //no finalize()
    CHECK(!pathExists(incompletePath));

    //errors
    CHECK_THROWS(FileError, FileInputPlain(appendPath(testDir, "missing")));
    CHECK_THROWS(ErrorTargetExisting, FileOutputPlain{plainPath});
    CHECK(readFile(plainPath) == content); //not truncated
    CHECK_THROWS_NOT(FileError, ErrorTargetExisting, FileOutputPlain(appendPath(testDir, nativeRelPath("missing/file"))));

    //getPathWithTempName(): same folder, different name
    const Zstring tempPath = getPathWithTempName(filePath);
    CHECK(tempPath != filePath);
    CHECK(startsWith(tempPath, filePath));
    CHECK(endsWith(tempPath, Zstr(".tmp")));
    CHECK_EQUAL(getParentFolderPath(tempPath), getParentFolderPath(filePath));
}
