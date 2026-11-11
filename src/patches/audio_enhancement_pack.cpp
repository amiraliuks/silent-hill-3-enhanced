#include "audio_enhancement_pack.h"
#include "../logger.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace sh3e::patches
{
namespace
{
    constexpr std::uint64_t kSectorSize = 2048;
    constexpr std::size_t kCopyBufferSize = 64 * 1024;

    struct ArchiveEntry
    {
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    struct Replacement
    {
        std::wstring path;
        std::uint32_t size = 0;
    };

    struct ArchiveJob
    {
        std::wstring archiveName;
        std::wstring sourcePath;
        std::wstring outputPath;
        std::wstring requestSuffix;
        std::map<std::uint32_t, Replacement> replacements;
        std::vector<ArchiveEntry> entries;
        std::uint64_t outputSize = 0;
    };

    std::string NarrowForLog(const std::wstring& text);

    std::uint32_t ReadU32(const std::uint8_t* bytes)
    {
        return static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8) |
            (static_cast<std::uint32_t>(bytes[2]) << 16) |
            (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    void WriteU32(std::uint8_t* bytes, std::uint32_t value)
    {
        bytes[0] = static_cast<std::uint8_t>(value);
        bytes[1] = static_cast<std::uint8_t>(value >> 8);
        bytes[2] = static_cast<std::uint8_t>(value >> 16);
        bytes[3] = static_cast<std::uint8_t>(value >> 24);
    }

    std::uint64_t AlignSector(std::uint64_t value)
    {
        return (value + (kSectorSize - 1)) & ~(kSectorSize - 1);
    }

    bool GetFileSize(HANDLE file, std::uint64_t& size)
    {
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(file, &value) || value.QuadPart < 0)
            return false;
        size = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    bool Seek(HANDLE file, std::uint64_t offset)
    {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
    }

    bool ReadExact(HANDLE file, void* data, std::size_t size)
    {
        auto* output = static_cast<std::uint8_t*>(data);
        while (size > 0)
        {
            const auto chunk = static_cast<DWORD>((std::min)(size, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD read = 0;
            if (!ReadFile(file, output, chunk, &read, nullptr) || read == 0)
                return false;
            output += read;
            size -= read;
        }
        return true;
    }

    bool WriteExact(HANDLE file, const void* data, std::size_t size)
    {
        const auto* input = static_cast<const std::uint8_t*>(data);
        while (size > 0)
        {
            const auto chunk = static_cast<DWORD>((std::min)(size, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written = 0;
            if (!WriteFile(file, input, chunk, &written, nullptr) || written == 0)
                return false;
            input += written;
            size -= written;
        }
        return true;
    }

    bool CopyRange(HANDLE source, HANDLE destination, std::uint64_t sourceOffset, std::uint64_t length)
    {
        if (!Seek(source, sourceOffset))
            return false;

        std::vector<std::uint8_t> buffer(kCopyBufferSize);
        while (length > 0)
        {
            const auto chunk = static_cast<DWORD>((std::min)(length, static_cast<std::uint64_t>(buffer.size())));
            if (!ReadExact(source, buffer.data(), chunk) || !WriteExact(destination, buffer.data(), chunk))
                return false;
            length -= chunk;
        }
        return true;
    }

    bool ReadArchive(HANDLE file, std::uint64_t fileSize, std::vector<ArchiveEntry>& entries,
        std::vector<std::uint8_t>& header)
    {
        std::uint8_t prefix[8]{};
        if (!Seek(file, 0) || !ReadExact(file, prefix, sizeof(prefix)) ||
            prefix[0] != 'A' || prefix[1] != 'F' || prefix[2] != 'S' || prefix[3] != 0)
            return false;

        const auto count = ReadU32(prefix + 4);
        if (count == 0 || count > 0x10000)
            return false;

        const auto headerSize = 8ull + static_cast<std::uint64_t>(count) * 8;
        if (headerSize > fileSize || headerSize > (std::numeric_limits<std::size_t>::max)())
            return false;

        header.resize(static_cast<std::size_t>(headerSize));
        std::copy(std::begin(prefix), std::end(prefix), header.begin());
        if (!Seek(file, 0) || !ReadExact(file, header.data(), header.size()))
            return false;

        entries.clear();
        entries.reserve(count);
        std::uint64_t previousEnd = 0;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto tableOffset = 8 + static_cast<std::size_t>(index) * 8;
            const auto offset = ReadU32(header.data() + tableOffset);
            const auto size = ReadU32(header.data() + tableOffset + 4);
            if (offset > fileSize || size > fileSize - offset)
                return false;
            if (size != 0 && (offset < headerSize || (offset % kSectorSize) != 0 || offset < previousEnd))
                return false;
            if (size != 0)
                previousEnd = static_cast<std::uint64_t>(offset) + size;
            entries.push_back({ offset, size });
        }
        return true;
    }

    bool CopyReplacement(const Replacement& replacement, HANDLE destination)
    {
        const auto source = CreateFileW(replacement.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (source == INVALID_HANDLE_VALUE)
            return false;

        std::uint64_t actualSize = 0;
        const bool valid = GetFileSize(source, actualSize) && actualSize == replacement.size &&
            CopyRange(source, destination, 0, replacement.size);
        CloseHandle(source);
        return valid;
    }

    bool BuildArchive(ArchiveJob& job)
    {
        const auto source = CreateFileW(job.sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (source == INVALID_HANDLE_VALUE)
            return false;

        std::uint64_t sourceSize = 0;
        std::vector<std::uint8_t> header;
        if (!GetFileSize(source, sourceSize) || !ReadArchive(source, sourceSize, job.entries, header))
        {
            CloseHandle(source);
            return false;
        }

        const auto originalEntries = job.entries;
        std::uint64_t cursor = AlignSector(header.size());
        for (std::uint32_t index = 0; index < originalEntries.size(); ++index)
        {
            auto& entry = job.entries[index];
            const auto replacement = job.replacements.find(index);
            const auto outputLength = replacement == job.replacements.end() ? originalEntries[index].size : replacement->second.size;
            if (cursor > (std::numeric_limits<std::uint32_t>::max)() ||
                outputLength > (std::numeric_limits<std::uint32_t>::max)() - cursor)
            {
                CloseHandle(source);
                return false;
            }

            entry.offset = static_cast<std::uint32_t>(cursor);
            entry.size = outputLength;
            const auto tableOffset = 8 + static_cast<std::size_t>(index) * 8;
            WriteU32(header.data() + tableOffset, entry.offset);
            WriteU32(header.data() + tableOffset + 4, entry.size);
            cursor = AlignSector(cursor + outputLength);
        }
        job.outputSize = cursor;

        const auto temporaryPath = job.outputPath + L".tmp";
        const auto output = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (output == INVALID_HANDLE_VALUE)
        {
            CloseHandle(source);
            return false;
        }

        bool success = WriteExact(output, header.data(), header.size());
        for (std::uint32_t index = 0; success && index < job.entries.size(); ++index)
        {
            auto& entry = job.entries[index];
            const auto replacement = job.replacements.find(index);
            const auto size = static_cast<std::uint64_t>(entry.size);
            if (size == 0)
                continue;

            if (!Seek(output, entry.offset))
            {
                success = false;
                break;
            }

            if (replacement != job.replacements.end())
                success = CopyReplacement(replacement->second, output);
            else
                success = CopyRange(source, output, originalEntries[index].offset, originalEntries[index].size);
        }

        if (success)
        {
            LARGE_INTEGER end{};
            end.QuadPart = static_cast<LONGLONG>(job.outputSize);
            success = SetFilePointerEx(output, end, nullptr, FILE_BEGIN) != FALSE && SetEndOfFile(output) != FALSE && FlushFileBuffers(output) != FALSE;
        }

        CloseHandle(output);
        CloseHandle(source);
        if (!success)
        {
            DeleteFileW(temporaryPath.c_str());
            return false;
        }

        if (!MoveFileExW(temporaryPath.c_str(), job.outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporaryPath.c_str());
            return false;
        }
        return true;
    }

    bool ReadMetadata(const std::wstring& path, std::wstring& text)
    {
        std::wifstream input(path);
        if (!input)
            return false;
        std::wostringstream stream;
        stream << input.rdbuf();
        if (input.bad())
            return false;
        text = stream.str();
        return true;
    }

    std::wstring FileFingerprint(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
            (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return {};

        const auto size = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        const auto timestamp = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
            attributes.ftLastWriteTime.dwLowDateTime;
        return std::to_wstring(size) + L":" + std::to_wstring(timestamp);
    }

    bool FindPackArchives(const std::wstring& inputRoot, const std::wstring& gameDirectory,
        const std::wstring& cacheRoot, std::vector<ArchiveJob>& jobs, std::wstring& manifest)
    {
        WIN32_FIND_DATAW data{};
        const auto pattern = inputRoot + L"\\*.afs";
        const auto search = FindFirstFileW(pattern.c_str(), &data);
        if (search == INVALID_HANDLE_VALUE)
            return false;

        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                data.cFileName[0] == L'.')
                continue;

            ArchiveJob job;
            job.archiveName = data.cFileName;
            const auto isRootArchive = _wcsicmp(job.archiveName.c_str(), L"sd.afs") == 0;
            job.sourcePath = gameDirectory + (isRootArchive
                ? L"\\data\\sound\\" + job.archiveName
                : L"\\data\\sound\\demo_afs\\" + job.archiveName);
            job.outputPath = cacheRoot + (isRootArchive
                ? L"\\data\\sound\\" + job.archiveName
                : L"\\data\\sound\\demo_afs\\" + job.archiveName);
            job.requestSuffix = isRootArchive
                ? L"data\\sound\\" + job.archiveName
                : L"data\\sound\\demo_afs\\" + job.archiveName;

            const auto originalFingerprint = FileFingerprint(job.sourcePath);
            if (originalFingerprint.empty())
            {
                FindClose(search);
                sh3e::log::Write("WARN", "[Audio Enhancement Pack] Missing base archive " + NarrowForLog(job.archiveName) + "; audio replacements were skipped.");
                return false;
            }
            manifest += L"A|" + job.archiveName + L"|" + originalFingerprint + L"\n";

            const auto entryDirectory = inputRoot + L"\\" + job.archiveName;
            WIN32_FIND_DATAW replacementData{};
            const auto replacementSearch = FindFirstFileW((entryDirectory + L"\\*.*").c_str(), &replacementData);
            if (replacementSearch == INVALID_HANDLE_VALUE)
            {
                FindClose(search);
                return false;
            }

            do
            {
                if ((replacementData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    continue;

                const std::wstring filename(replacementData.cFileName);
                const auto dot = filename.find_last_of(L'.');
                if (dot == std::wstring::npos)
                    continue;
                const auto extension = filename.substr(dot);
                if (_wcsicmp(extension.c_str(), L".wav") != 0 && _wcsicmp(extension.c_str(), L".adx") != 0)
                    continue;

                const auto underscore = filename.find(L'_');
                const auto indexText = filename.substr(0, underscore == std::wstring::npos ? dot : underscore);
                if (indexText.empty() || !std::all_of(indexText.begin(), indexText.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }

                wchar_t* end = nullptr;
                const auto parsedIndex = std::wcstoul(indexText.c_str(), &end, 10);
                if (end == indexText.c_str() || *end != L'\0' || parsedIndex > (std::numeric_limits<std::uint32_t>::max)())
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }

                const auto replacementPath = entryDirectory + L"\\" + filename;
                const auto replacementFingerprint = FileFingerprint(replacementPath);
                if (replacementFingerprint.empty())
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }

                WIN32_FILE_ATTRIBUTE_DATA attributes{};
                if (!GetFileAttributesExW(replacementPath.c_str(), GetFileExInfoStandard, &attributes))
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }
                const auto replacementSize = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
                if (replacementSize == 0 || replacementSize > (std::numeric_limits<std::uint32_t>::max)())
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }

                if (!job.replacements.emplace(static_cast<std::uint32_t>(parsedIndex), Replacement{ replacementPath, static_cast<std::uint32_t>(replacementSize) }).second)
                {
                    FindClose(replacementSearch);
                    FindClose(search);
                    return false;
                }
                manifest += L"R|" + job.archiveName + L"|" + filename + L"|" + replacementFingerprint + L"\n";
            } while (FindNextFileW(replacementSearch, &replacementData));
            FindClose(replacementSearch);

            if (job.replacements.empty())
            {
                FindClose(search);
                return false;
            }
            jobs.push_back(std::move(job));
        } while (FindNextFileW(search, &data));

        FindClose(search);
        std::sort(jobs.begin(), jobs.end(), [](const ArchiveJob& left, const ArchiveJob& right)
        {
            return _wcsicmp(left.archiveName.c_str(), right.archiveName.c_str()) < 0;
        });

        std::wstringstream manifestStream(manifest);
        std::vector<std::wstring> manifestLines;
        std::wstring line;
        while (std::getline(manifestStream, line))
            manifestLines.push_back(std::move(line));
        std::sort(manifestLines.begin(), manifestLines.end());
        manifest.clear();
        for (const auto& manifestLine : manifestLines)
            manifest += manifestLine + L"\n";
        return !jobs.empty();
    }

    bool ValidateJob(ArchiveJob& job, std::uint64_t& totalOutputSize)
    {
        const auto source = CreateFileW(job.sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (source == INVALID_HANDLE_VALUE)
            return false;
        std::uint64_t sourceSize = 0;
        std::vector<std::uint8_t> header;
        const bool valid = GetFileSize(source, sourceSize) && ReadArchive(source, sourceSize, job.entries, header);
        CloseHandle(source);
        if (!valid)
            return false;

        std::uint64_t cursor = AlignSector(header.size());
        for (const auto& replacement : job.replacements)
        {
            if (replacement.first >= job.entries.size())
                return false;
        }

        for (std::uint32_t index = 0; index < job.entries.size(); ++index)
        {
            const auto replacement = job.replacements.find(index);
            const auto size = replacement == job.replacements.end() ? job.entries[index].size : replacement->second.size;
            cursor = AlignSector(cursor + size);
        }
        if (cursor > (std::numeric_limits<std::uint32_t>::max)())
            return false;
        job.outputSize = cursor;
        totalOutputSize += cursor;
        return true;
    }

    bool CacheIsCurrent(const std::wstring& cacheRoot, const std::wstring& manifest,
        const std::vector<ArchiveJob>& jobs)
    {
        std::wstring existingManifest;
        if (!ReadMetadata(cacheRoot + L"\\source-manifest.txt", existingManifest) || existingManifest != manifest)
            return false;
        for (const auto& job : jobs)
        {
            const auto outputPath = cacheRoot + (job.archiveName == L"sd.afs"
                ? L"\\data\\sound\\" + job.archiveName
                : L"\\data\\sound\\demo_afs\\" + job.archiveName);
            const auto output = CreateFileW(outputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output == INVALID_HANDLE_VALUE)
                return false;
            std::uint64_t size = 0;
            std::vector<ArchiveEntry> entries;
            std::vector<std::uint8_t> header;
            const bool valid = GetFileSize(output, size) && ReadArchive(output, size, entries, header);
            CloseHandle(output);
            if (!valid || size != job.outputSize || entries.size() != job.entries.size())
                return false;
            for (std::uint32_t index = 0; index < entries.size(); ++index)
            {
                const auto replacement = job.replacements.find(index);
                const auto expectedSize = replacement == job.replacements.end() ? job.entries[index].size : replacement->second.size;
                if (entries[index].size != expectedSize || (entries[index].size != 0 && entries[index].offset % kSectorSize != 0))
                    return false;
            }
        }
        return true;
    }

    bool WriteMetadata(const std::wstring& cacheRoot, const std::wstring& manifest)
    {
        const auto tempPath = cacheRoot + L"\\source-manifest.txt.tmp";
        std::wofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << manifest;
        output.flush();
        if (!output)
            return false;
        output.close();
        return MoveFileExW(tempPath.c_str(), (cacheRoot + L"\\source-manifest.txt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    std::string NarrowForLog(const std::wstring& text)
    {
        if (text.empty())
            return {};
        const auto length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
            return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }
}

bool PrepareAudioEnhancementPack(const std::wstring& gameDirectory,
    std::vector<std::pair<std::wstring, std::wstring>>& redirects)
{
    redirects.clear();
    const auto packRoot = gameDirectory + L"\\SH3 Enhanced Data\\Audio Enhancement Pack";
    const auto inputRoot = packRoot + L"\\FEmulator\\AFS";
    const auto cacheRoot = packRoot + L"\\Cache";
    std::error_code error;
    std::filesystem::create_directories(packRoot, error);

    const auto attributes = GetFileAttributesW(inputRoot.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return true;

    std::vector<ArchiveJob> jobs;
    std::wstring manifest = L"SH3E-AFS-CACHE-1\n";
    if (!FindPackArchives(inputRoot, gameDirectory, cacheRoot, jobs, manifest))
    {
        sh3e::log::Write("WARN", "[Audio Enhancement Pack] The installed FEmulator\\AFS layout is incomplete or invalid; original audio archives will be used.");
        return false;
    }

    std::uint64_t totalOutputSize = 0;
    for (auto& job : jobs)
    {
        if (!ValidateJob(job, totalOutputSize))
        {
            sh3e::log::Write("WARN", "[Audio Enhancement Pack] Invalid replacement indexes or AFS data; original audio archives will be used.");
            return false;
        }
    }

    std::filesystem::create_directories(cacheRoot, error);
    if (error)
    {
        sh3e::log::Write("WARN", "[Audio Enhancement Pack] Could not create the audio archive cache folder; original audio archives will be used.");
        return false;
    }
    std::filesystem::create_directories(cacheRoot + L"\\data\\sound", error);
    if (error)
    {
        sh3e::log::Write("WARN", "[Audio Enhancement Pack] Could not create the audio archive cache folder; original audio archives will be used.");
        return false;
    }
    std::filesystem::create_directories(cacheRoot + L"\\data\\sound\\demo_afs", error);
    if (error)
    {
        sh3e::log::Write("WARN", "[Audio Enhancement Pack] Could not create the audio archive cache folder; original audio archives will be used.");
        return false;
    }

    const bool cacheCurrent = CacheIsCurrent(cacheRoot, manifest, jobs);
    if (!cacheCurrent)
    {
        bool allOutputsExist = true;
        std::uint64_t largestOutput = 0;
        for (const auto& job : jobs)
        {
            const auto outputAttributes = GetFileAttributesW(job.outputPath.c_str());
            if (outputAttributes == INVALID_FILE_ATTRIBUTES || (outputAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                allOutputsExist = false;
            largestOutput = (std::max)(largestOutput, job.outputSize);
        }

        ULARGE_INTEGER available{};
        const auto requiredSpace = (allOutputsExist ? largestOutput : totalOutputSize) + 32ull * 1024 * 1024;
        if (!GetDiskFreeSpaceExW(cacheRoot.c_str(), &available, nullptr, nullptr) || available.QuadPart < requiredSpace)
        {
            sh3e::log::Write("WARN", "[Audio Enhancement Pack] Not enough free space to build the rebuilt audio archive cache; original audio archives will be used.");
            return false;
        }

        for (auto& job : jobs)
        {
            if (!BuildArchive(job))
            {
                sh3e::log::Write("WARN", "[Audio Enhancement Pack] Could not rebuild " + NarrowForLog(job.archiveName) + "; original audio archives will be used.");
                return false;
            }
        }
        if (!WriteMetadata(cacheRoot, manifest))
            sh3e::log::Write("WARN", "[Audio Enhancement Pack] Could not save the cache manifest; archives will be rebuilt on the next launch.");
    }

    for (const auto& job : jobs)
    {
        const auto outputPath = cacheRoot + (job.archiveName == L"sd.afs"
            ? L"\\data\\sound\\" + job.archiveName
            : L"\\data\\sound\\demo_afs\\" + job.archiveName);
        redirects.emplace_back(job.requestSuffix, outputPath);
    }

    sh3e::log::Write("INFO", "[Audio Enhancement Pack] Prepared " + std::to_string(jobs.size()) +
        " replacement AFS archives. Cache size: " + std::to_string(totalOutputSize / (1024 * 1024)) + " MiB.");
    return true;
}
}
