#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace sh3e::patches
{
namespace
{
    using CreateFileAFunction = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    using CreateFileWFunction = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

    CreateFileAFunction g_originalCreateFileA = nullptr;
    CreateFileWFunction g_originalCreateFileW = nullptr;
    HANDLE g_archiveReadyEvent = nullptr;
    bool g_fileOpenHooksInstalled = false;
    std::wstring g_overrideArchivePath;
    std::string g_overrideArchivePathA;

    constexpr std::uint32_t kMessageCount = 371;
    constexpr std::uint32_t kExpectedWishHouseReferences = 23;
    constexpr std::size_t kArchiveTableOffset = 0x10;
    constexpr std::size_t kArchiveRecordSize = 0x10;
    constexpr std::uint8_t kArchiveSignature[] = { 0x07, 0x05, 0x03, 0x20 };
    constexpr std::uint8_t kWishHouseName[] = { '(', 'O', 'P', 'E', 0, '(', 'O', 'U', 'S', 'E' };
    constexpr std::uint8_t kWishHouseReplacement[] = { '7', ')', '3', '(' };
    constexpr std::uint8_t kSewerFirstLine[] = {
        '#','R','R','Y','I','N','G',0,'M','O','R','E',0,'O','F',0,'T','H','E','S','E',0,
        'B','O','T','T','L','E','S',0,'W','O','U','L','D',0,'S','L','O','W',0,'M','E',0,
        'D','O','W','N'
    };
    constexpr std::uint8_t kSewerSecondLine[] = {
        'A','N','D',0,'I',0,'D','O','N',0x07,'T',0,'D','R','I','N','K',0,'W','I','N','E',0,
        'A','N','Y','W','A','Y'
    };

    struct ArchiveRecord
    {
        std::size_t headerOffset;
        std::size_t dataOffset;
        std::size_t size;
    };

    std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    bool ReadArchiveRecords(const std::vector<std::uint8_t>& bytes, std::vector<ArchiveRecord>& records)
    {
        records.clear();
        if (bytes.size() < kArchiveTableOffset || !std::equal(std::begin(kArchiveSignature), std::end(kArchiveSignature), bytes.begin()))
            return false;

        const auto count = ReadU32(bytes, 4);
        if (count != kMessageCount || kArchiveTableOffset + static_cast<std::size_t>(count) * kArchiveRecordSize > bytes.size())
            return false;

        std::size_t previousEnd = kArchiveTableOffset + static_cast<std::size_t>(count) * kArchiveRecordSize;
        records.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto header = kArchiveTableOffset + static_cast<std::size_t>(index) * kArchiveRecordSize;
            const auto offset = ReadU32(bytes, header);
            const auto size = ReadU32(bytes, header + 8);
            const auto uncompressedSize = ReadU32(bytes, header + 12);
            if (offset < previousEnd || offset > bytes.size() || size != uncompressedSize || size > bytes.size() - offset)
                return false;
            records.push_back({ header, offset, size });
            previousEnd = static_cast<std::size_t>(offset) + size;
        }

        return true;
    }

    bool ReplaceInRecord(std::vector<std::uint8_t>& bytes, std::vector<ArchiveRecord>& records,
        std::size_t recordIndex, const std::vector<std::uint8_t>& find, const std::vector<std::uint8_t>& replacement)
    {
        if (recordIndex >= records.size() || find.empty())
            return false;

        auto& record = records[recordIndex];
        const auto first = bytes.begin() + record.dataOffset;
        const auto last = first + record.size;
        const auto match = std::search(first, last, find.begin(), find.end());
        if (match == last || std::search(match + 1, last, find.begin(), find.end()) != last)
            return false;

        const auto absolute = static_cast<std::size_t>(match - bytes.begin());
        const auto oldSize = find.size();
        const auto newSize = replacement.size();
        bytes.erase(bytes.begin() + absolute, bytes.begin() + absolute + oldSize);
        bytes.insert(bytes.begin() + absolute, replacement.begin(), replacement.end());
        const auto delta = static_cast<std::ptrdiff_t>(newSize) - static_cast<std::ptrdiff_t>(oldSize);

        if (delta != 0)
        {
            record.size = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(record.size) + delta);
            WriteU32(bytes, record.headerOffset + 8, static_cast<std::uint32_t>(record.size));
            WriteU32(bytes, record.headerOffset + 12, static_cast<std::uint32_t>(record.size));
            for (std::size_t i = 0; i < records.size(); ++i)
            {
                if (i == recordIndex || records[i].dataOffset < absolute)
                    continue;
                records[i].dataOffset = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(records[i].dataOffset) + delta);
                WriteU32(bytes, records[i].headerOffset, static_cast<std::uint32_t>(records[i].dataOffset));
            }
        }

        return true;
    }

    bool ReplaceByteAt(std::vector<std::uint8_t>& bytes, const ArchiveRecord& record, std::size_t offset,
        std::uint8_t expected, std::uint8_t replacement)
    {
        if (offset >= record.size || bytes[record.dataOffset + offset] != expected)
            return false;
        bytes[record.dataOffset + offset] = replacement;
        return true;
    }

    bool EndsWithMsgArchive(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        constexpr wchar_t suffix[] = L"\\data\\msg.arc";
        constexpr std::size_t suffixLength = (sizeof(suffix) / sizeof(suffix[0])) - 1;
        if (path.size() == suffixLength - 1 && path == L"data\\msg.arc")
            return true;
        return path.size() >= suffixLength && path.compare(path.size() - suffixLength, suffixLength, suffix) == 0;
    }

    bool EndsWithMsgArchive(const char* path)
    {
        if (!path)
            return false;
        std::string normalized(path);
        std::replace(normalized.begin(), normalized.end(), '/', '\\');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        constexpr char suffix[] = "\\data\\msg.arc";
        constexpr std::size_t suffixLength = sizeof(suffix) - 1;
        if (normalized == "data\\msg.arc")
            return true;
        return normalized.size() >= suffixLength && normalized.compare(normalized.size() - suffixLength, suffixLength, suffix) == 0;
    }

    HANDLE WINAPI RedirectedCreateFileA(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile)
    {
        if (fileName && EndsWithMsgArchive(fileName))
        {
            if (g_overrideArchivePathA.empty() && g_archiveReadyEvent)
                WaitForSingleObject(g_archiveReadyEvent, 10000);
            if (!g_overrideArchivePathA.empty() && g_originalCreateFileA)
                return g_originalCreateFileA(g_overrideArchivePathA.c_str(), desiredAccess, shareMode,
                    securityAttributes, creationDisposition, flagsAndAttributes, templateFile);
        }

        return g_originalCreateFileA
            ? g_originalCreateFileA(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile)
            : INVALID_HANDLE_VALUE;
    }

    HANDLE WINAPI RedirectedCreateFileW(LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile)
    {
        if (fileName && EndsWithMsgArchive(fileName))
        {
            if (g_overrideArchivePath.empty() && g_archiveReadyEvent)
                WaitForSingleObject(g_archiveReadyEvent, 10000);
            if (!g_overrideArchivePath.empty() && g_originalCreateFileW)
                return g_originalCreateFileW(g_overrideArchivePath.c_str(), desiredAccess, shareMode,
                    securityAttributes, creationDisposition, flagsAndAttributes, templateFile);
        }

        return g_originalCreateFileW
            ? g_originalCreateFileW(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile)
            : INVALID_HANDLE_VALUE;
    }

    bool ReplaceImportThunk(DWORD* thunk, DWORD replacement, DWORD& original)
    {
        DWORD oldProtection = 0;
        if (!VirtualProtect(thunk, sizeof(*thunk), PAGE_READWRITE, &oldProtection))
            return false;

        original = *thunk;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(thunk), static_cast<LONG>(replacement));

        DWORD ignored = 0;
        VirtualProtect(thunk, sizeof(*thunk), oldProtection, &ignored);
        return true;
    }

    bool InstallFileOpenRedirect(HMODULE executable)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(executable);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return false;

        const auto& importDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDirectory.VirtualAddress == 0 || importDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
            return false;

        bool hookedA = false;
        bool hookedW = false;
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);

        for (; descriptor->Name != 0; ++descriptor)
        {
            if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0)
                continue;

            auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
            auto* addressTable = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);

            for (; names->u1.AddressOfData != 0; ++names, ++addressTable)
            {
                if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
                    continue;
                const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                const auto* functionName = reinterpret_cast<const char*>(import->Name);

                if (std::strcmp(functionName, "CreateFileA") == 0)
                {
                    DWORD original = 0;
                    auto* slot = reinterpret_cast<DWORD*>(&addressTable->u1.Function);
                    g_originalCreateFileA = reinterpret_cast<CreateFileAFunction>(static_cast<std::uintptr_t>(*slot));
                    const auto replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectedCreateFileA));
                    if (ReplaceImportThunk(slot, replacement, original))
                    {
                        g_originalCreateFileA = reinterpret_cast<CreateFileAFunction>(static_cast<std::uintptr_t>(original));
                        hookedA = true;
                    }
                    else
                    {
                        g_originalCreateFileA = nullptr;
                    }
                }
                else if (std::strcmp(functionName, "CreateFileW") == 0)
                {
                    DWORD original = 0;
                    auto* slot = reinterpret_cast<DWORD*>(&addressTable->u1.Function);
                    g_originalCreateFileW = reinterpret_cast<CreateFileWFunction>(static_cast<std::uintptr_t>(*slot));
                    const auto replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectedCreateFileW));
                    if (ReplaceImportThunk(slot, replacement, original))
                    {
                        g_originalCreateFileW = reinterpret_cast<CreateFileWFunction>(static_cast<std::uintptr_t>(original));
                        hookedW = true;
                    }
                    else
                    {
                        g_originalCreateFileW = nullptr;
                    }
                }
            }
        }

        return hookedA || hookedW;
    }

    bool LoadFile(const std::wstring& path, std::vector<std::uint8_t>& bytes)
    {
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0 ||
            fileSize.QuadPart > static_cast<LONGLONG>((std::numeric_limits<DWORD>::max)()))
        {
            CloseHandle(file);
            return false;
        }

        bytes.resize(static_cast<std::size_t>(fileSize.QuadPart));
        DWORD bytesRead = 0;
        const bool loaded = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr) != FALSE && static_cast<std::size_t>(bytesRead) == bytes.size();
        CloseHandle(file);
        return loaded;
    }

    bool SaveFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes)
    {
        const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD bytesWritten = 0;
        const bool saved = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr) != FALSE && static_cast<std::size_t>(bytesWritten) == bytes.size() && FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        return saved;
    }

    bool PatchMessageArchive(std::vector<std::uint8_t>& bytes, bool fixWishHouse, bool fixEnglishTypos,
        bool restoreMissingSewerText, std::uint32_t& referenceCount, std::uint32_t& typoFixCount, std::uint32_t& sewerFixCount)
    {
        referenceCount = 0;
        typoFixCount = 0;
        sewerFixCount = 0;
        std::vector<ArchiveRecord> records;
        if (!ReadArchiveRecords(bytes, records))
            return false;

        if (fixWishHouse)
        {
            for (const auto& record : records)
            {
                auto position = record.dataOffset;
                const auto end = position + record.size;
                while (position + sizeof(kWishHouseName) <= end)
                {
                    if (std::memcmp(bytes.data() + position, kWishHouseName, sizeof(kWishHouseName)) == 0)
                    {
                        std::memcpy(bytes.data() + position, kWishHouseReplacement, sizeof(kWishHouseReplacement));
                        ++referenceCount;
                        position += sizeof(kWishHouseName);
                    }
                    else
                    {
                        ++position;
                    }
                }
            }

            if (referenceCount != kExpectedWishHouseReferences)
                return false;
        }

        if (fixEnglishTypos)
        {
            // These are the English archive records containing the affected messages.
            if (!ReplaceByteAt(bytes, records[6], 0x1edf, 'E', 'Y')) // "makes me head hurt" -> "makes my head hurt"
                return false;

            const std::vector<std::uint8_t> apartmentSign = {
                'A','P','A','R','T','M','E','N','T',0,'I','S',0,'N','E','A','R'
            };
            const std::vector<std::uint8_t> apartmentSignFixed = {
                'A','P','A','R','T','M','E','N','T','S',0,'I','S',0,'N','E','A','R'
            };
            if (!ReplaceInRecord(bytes, records, 80, apartmentSign, apartmentSignFixed))
                return false;

            // The memoir line uses the game font's special S glyph for this letter.
            if (!ReplaceByteAt(bytes, records[44], 0x1176, 'S', '3'))
                return false;

            const std::vector<std::uint8_t> subwayText = {
                '4','H','E','R','E',0,'A','R','E',0,'S','T','E','P','L','A','D','D','E','R',0,
                'A','N','D',0xff,0xfd,'W','H','E','E','L','B','A','R','R','O','W','S',0,'B','L','O','C','K','I','N','G',0,
                'T','H','E',0,'W','A','Y'
            };
            const std::vector<std::uint8_t> subwayTextFixed = {
                '4','H','E','R','E',0,'I','S',0,'A',0,'S','T','E','P',0,'L','A','D','D','E','R',0,
                0x0c,0,'A',0,'W','H','E','E','L','B','A','R','R','O','W',0xff,0xfd,'A','N','D',0,
                'O','T','H','E','R',0,'S','T','U','F','F',0,
                'B','L','O','C','K','I','N','G',0,'T','H','E',0,'W','A','Y'
            };

            if (!ReplaceInRecord(bytes, records, 76, subwayText, subwayTextFixed))
                return false;

            typoFixCount = 4;
        }

        if (restoreMissingSewerText)
        {
            if (records.size() <= 72 || records[72].size < 0xb20)
                return false;

            std::memcpy(bytes.data() + records[72].dataOffset + 0xab0, kSewerFirstLine, sizeof(kSewerFirstLine));
            std::memcpy(bytes.data() + records[72].dataOffset + 0xae8, kSewerSecondLine, sizeof(kSewerSecondLine));
            // The original patch reserves fixed-width text slots (0x31 and 0x23 bytes).
            std::fill(bytes.begin() + records[72].dataOffset + 0xab0 + sizeof(kSewerFirstLine), bytes.begin() + records[72].dataOffset + 0xab0 + 0x31, 0);
            std::fill(bytes.begin() + records[72].dataOffset + 0xae8 + sizeof(kSewerSecondLine), bytes.begin() + records[72].dataOffset + 0xae8 + 0x23, 0);
            sewerFixCount = 1;
        }

        return true;
    }

    bool CreateSidecar(const std::wstring& gameDirectory, bool fixWishHouse, bool fixEnglishTypos,
        bool restoreMissingSewerText, std::wstring& sidecarPath, std::uint32_t& referenceCount,
        std::uint32_t& typoFixCount, std::uint32_t& sewerFixCount)
    {
        const auto sourcePath = gameDirectory + L"\\data\\msg.arc";
        std::vector<std::uint8_t> bytes;
        if (!LoadFile(sourcePath, bytes))
        {
            sh3e::log::Write("WARN", "[Content] Could not read data\\msg.arc.");
            return false;
        }

        if (!PatchMessageArchive(bytes, fixWishHouse, fixEnglishTypos, restoreMissingSewerText,
            referenceCount, typoFixCount, sewerFixCount))
        {
            sh3e::log::Write("WARN", "[Content] msg.arc did not match the expected English message layout.");
            return false;
        }

        wchar_t tempPath[MAX_PATH]{};
        const auto tempLength = GetTempPathW(MAX_PATH, tempPath);
        if (tempLength == 0 || tempLength >= MAX_PATH)
            return false;

        std::wstring directory(tempPath, tempLength);
        directory += L"SH3Enhanced_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;

        sidecarPath = directory + L"msg.arc";
        if (!SaveFile(sidecarPath, bytes))
        {
            DeleteFileW(sidecarPath.c_str());
            return false;
        }

        const auto ansiLength = WideCharToMultiByte(CP_ACP, 0, sidecarPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (ansiLength <= 0)
        {
            DeleteFileW(sidecarPath.c_str());
            return false;
        }

        g_overrideArchivePathA.resize(static_cast<std::size_t>(ansiLength));
        if (WideCharToMultiByte(CP_ACP, 0, sidecarPath.c_str(), -1, g_overrideArchivePathA.data(), ansiLength, nullptr, nullptr) == 0)
        {
            DeleteFileW(sidecarPath.c_str());
            g_overrideArchivePathA.clear();
            return false;
        }

        g_overrideArchivePathA.pop_back();
        return true;
    }
}

bool InstallContentFileRedirectHooks(HMODULE executable)
{
    if (!g_archiveReadyEvent)
        g_archiveReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!g_archiveReadyEvent)
        return false;

    g_fileOpenHooksInstalled = InstallFileOpenRedirect(executable);
    if (!g_fileOpenHooksInstalled)
        SetEvent(g_archiveReadyEvent);

    return g_fileOpenHooksInstalled;
}

void SignalContentArchiveReady()
{
    if (g_archiveReadyEvent)
        SetEvent(g_archiveReadyEvent);
}

bool InstallContentTextPatches(const std::wstring& gameDirectory, const Config& config)
{
    if (!g_fileOpenHooksInstalled)
    {
        sh3e::log::Write("ERROR", "[Content] Message archive file-open hooks were not installed.");
        return false;
    }

    std::uint32_t referenceCount = 0;
    std::uint32_t typoFixCount = 0;
    std::uint32_t sewerFixCount = 0;

    if (!CreateSidecar(gameDirectory, config.fixWishHouse, config.fixEnglishTypos, config.restoreMissingSewerText, g_overrideArchivePath, referenceCount, typoFixCount, sewerFixCount))
        return false;

    if (config.fixWishHouse)
        sh3e::log::Write("INFO", "[Content] Wish House name corrected in " + std::to_string(referenceCount) + " references.");

    if (config.fixEnglishTypos)
        sh3e::log::Write("INFO", "[Content] English typo fixes installed (" + std::to_string(typoFixCount) + " corrections).");

    if (config.restoreMissingSewerText)
        sh3e::log::Write("INFO", "[Content] Missing sewer text restored in " + std::to_string(sewerFixCount) + " message entry.");

    sh3e::log::Write("INFO", "[Content] Temporary message archive prepared; the installed data\\msg.arc was left unchanged.");
    return true;
}
}