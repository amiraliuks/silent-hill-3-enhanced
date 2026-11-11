#include "ps2_costumes.h"
#include "../logger.h"
#include "../pattern.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace sh3e::patches
{
namespace
{
#if !defined(SH3E_DISABLE_PS2_COSTUME_MODELS)
    struct CostumeResource
    {
        const wchar_t* key;
        std::uint16_t modelResource;
        std::uint16_t alternateResource;
    };

    // These are the unused player-model pairs referenced by each costume ID in
    // the supported PC executable's outfit-resource table. Keep each model
    // paired with the resource IDs used by its password's costume ID.
    constexpr std::array<CostumeResource, 14> kCostumes{{
        { L"13emerue", 281, 293 },
        { L"egm", 271, 283 },
        { L"gameinformer", 277, 289 },
        { L"gamepro", 278, 290 },
        { L"gamereactor", 267, 268 },
        { L"gamespy", 282, 294 },
        { L"gmr", 275, 287 },
        { L"gn", 273, 285 },
        { L"ign", 276, 288 },
        { L"opm", 279, 291 },
        { L"play", 274, 286 },
        { L"ps2ro", 272, 284 },
        { L"psm", 280, 292 },
        { L"ops2", 315, 316 },
    }};

    constexpr std::uintptr_t kResourcePointerTableRva = 0x310C90;
    constexpr std::size_t kResourcePointerCount = 318;
#endif
    constexpr std::uintptr_t kCostumePasswordMapVa = 0x0071E1B4;
    constexpr std::uintptr_t kCostumePasswordHashVa = 0x0071E1E8;
    constexpr std::uintptr_t kPasswordLookupRva = 0x001D7F90;
    constexpr std::size_t kPasswordLookupSize = 0x129;
    constexpr std::uintptr_t kPasswordScreenRva = 0x001D89D0;
    constexpr std::size_t kPasswordScreenSize = 0x41C;
    constexpr std::uintptr_t kPasswordCountInstructionRva = 0x001D809B;
    constexpr std::uintptr_t kPasswordCostumeSelectCallRvas[] = { 0x001D8C7D, 0x001D8D22 };
    constexpr std::size_t kVanillaPasswordCount = 12;
    constexpr std::size_t kPS2PasswordCount = 14;
    constexpr std::size_t kPasswordCount = kVanillaPasswordCount + kPS2PasswordCount;
#if !defined(SH3E_DISABLE_PS2_COSTUME_INVENTORY)
    constexpr std::uintptr_t kCostumeInventoryTableVa = 0x0071E02C;
    constexpr std::uintptr_t kCostumeInventoryFunctionRva = 0x001D7540;
    constexpr std::size_t kCostumeInventoryFunctionSize = 0xD6;
    constexpr std::size_t kVanillaCostumeInventoryEntryCount = 12;
    constexpr std::size_t kCostumeInventoryEntryCount = kVanillaCostumeInventoryEntryCount + kPS2PasswordCount;
    std::uint16_t* g_costumeInventoryTable = nullptr;
#endif
#if !defined(SH3E_DISABLE_PS2_COSTUME_MODELS)
    std::array<std::string, kCostumes.size() * 2> g_resourcePaths;
#endif
    void* g_costumePasswordTables = nullptr;

    struct MemoryPatch
    {
        std::uintptr_t address;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> replacement;
    };

    std::string Narrow(const std::wstring& value)
    {
        const auto length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
            return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        if (!WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr))
            return {};
        result.pop_back();
        return result;
    }

    bool ReadMemory(const void* address, void* output, std::size_t size)
    {
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(GetCurrentProcess(), address, output, size, &bytesRead) != FALSE && bytesRead == size;
    }

#if !defined(SH3E_DISABLE_PS2_COSTUME_MODELS)
    bool IsExpectedExecutable(HMODULE executable, std::uintptr_t& tableAddress)
    {
        if (!executable)
            return false;

        auto* base = reinterpret_cast<std::uint8_t*>(executable);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase != 0x400000 ||
            nt->OptionalHeader.SizeOfImage < kResourcePointerTableRva + kResourcePointerCount * sizeof(std::uint32_t))
            return false;

        tableAddress = reinterpret_cast<std::uintptr_t>(base) + kResourcePointerTableRva;
        std::uint32_t knownPathPointer = 0;
        if (!ReadMemory(reinterpret_cast<const void*>(tableAddress + sizeof(std::uint32_t)), &knownPathPointer, sizeof(knownPathPointer)) ||
            knownPathPointer == 0)
            return false;

        constexpr char knownPath[] = "data/pcchr/pl/chhaa.mdl";
        char actualPath[sizeof(knownPath)]{};
        if (!ReadMemory(reinterpret_cast<const void*>(knownPathPointer), actualPath, sizeof(actualPath)) ||
            std::memcmp(actualPath, knownPath, sizeof(knownPath)) != 0)
            return false;

        return true;
    }
#endif

    std::array<std::uint8_t, 16> MakePasswordHash(const char* password, std::uint8_t salt)
    {
        std::array<std::uint8_t, 16> value{};
        for (std::size_t i = 0; i < value.size(); ++i)
            value[i] = static_cast<std::uint8_t>((salt + 0x40) * 2 + i * 3);
        for (std::size_t i = 0; password[i] && i < value.size(); ++i)
            value[i] = static_cast<std::uint8_t>(password[i]);

        const auto b = value;
        value[0] = static_cast<std::uint8_t>(b[6] * 2 - b[13] + b[0]);
        value[1] = static_cast<std::uint8_t>((b[11] | b[13]) - 0x1f);
        value[2] = static_cast<std::uint8_t>(b[4] * 6 - b[9] + b[12]);
        value[3] = static_cast<std::uint8_t>(b[9] * salt - b[0] * b[6]);
        value[4] = static_cast<std::uint8_t>((salt + 7) * 2 + b[0]);
        value[5] = static_cast<std::uint8_t>((salt & 1) * b[11] + b[7] * 3);
        value[6] = static_cast<std::uint8_t>((b[2] & b[9]) + b[14]);
        value[7] = static_cast<std::uint8_t>(b[9] - b[13] + b[8] + b[10]);
        value[8] = static_cast<std::uint8_t>((b[12] & 0xdd) - (b[12] | 0x33) + b[2]);
        value[9] = static_cast<std::uint8_t>((b[3] * 3 + salt) * 2 - (b[0] | 0x24));
        value[10] = static_cast<std::uint8_t>(b[2] * 4 + b[12] + 0x29);
        value[11] = static_cast<std::uint8_t>(b[4] * 2 - b[6] + b[8]);
        value[12] = static_cast<std::uint8_t>((b[5] | b[11]) & b[14]);
        value[13] = static_cast<std::uint8_t>(b[11] * 16 + (b[11] >> 4) + b[9]);
        value[14] = static_cast<std::uint8_t>(b[1] * 7 - b[8] * 2 - salt);
        value[15] = static_cast<std::uint8_t>(b[15] - salt + 0x81);
        return value;
    }

    bool CollectPointerPatches(HMODULE executable, std::uintptr_t functionRva, std::size_t functionSize,
        std::uintptr_t oldAddress, std::uintptr_t newAddress, std::size_t expectedCount,
        std::vector<MemoryPatch>& patches)
    {
        if (oldAddress > UINT32_MAX || newAddress > UINT32_MAX)
            return false;

        const auto functionAddress = reinterpret_cast<std::uintptr_t>(executable) + functionRva;
        std::vector<std::uint8_t> code(functionSize);
        if (!ReadMemory(reinterpret_cast<const void*>(functionAddress), code.data(), code.size()))
            return false;

        const auto oldValue = static_cast<std::uint32_t>(oldAddress);
        const auto newValue = static_cast<std::uint32_t>(newAddress);
        std::size_t found = 0;
        for (std::size_t i = 0; i + sizeof(oldValue) <= code.size(); ++i)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, code.data() + i, sizeof(value));
            if (value != oldValue)
                continue;

            MemoryPatch patch{};
            patch.address = functionAddress + i;
            patch.original.resize(sizeof(oldValue));
            patch.replacement.resize(sizeof(newValue));
            std::memcpy(patch.original.data(), &oldValue, sizeof(oldValue));
            std::memcpy(patch.replacement.data(), &newValue, sizeof(newValue));
            patches.push_back(std::move(patch));
            ++found;
            i += sizeof(oldValue) - 1;
        }
        return found == expectedCount;
    }

    bool InstallPS2CostumePasswords(HMODULE executable)
    {
        if (g_costumePasswordTables)
            return true;

        auto* image = reinterpret_cast<std::uint8_t*>(executable);
        const auto oldMapAddress = reinterpret_cast<std::uintptr_t>(image) + (kCostumePasswordMapVa - 0x400000);
        const auto oldHashAddress = reinterpret_cast<std::uintptr_t>(image) + (kCostumePasswordHashVa - 0x400000);
        constexpr std::size_t mapBytes = kPasswordCount * 4;
        constexpr std::size_t hashOffset = (mapBytes + 15) & ~std::size_t(15);
        constexpr std::size_t hashBytes = kPasswordCount * 16;
        auto* tables = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, hashOffset + hashBytes,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!tables)
            return false;

        if (!ReadMemory(reinterpret_cast<const void*>(oldMapAddress), tables, kVanillaPasswordCount * 4) ||
            !ReadMemory(reinterpret_cast<const void*>(oldHashAddress), tables + hashOffset, kVanillaPasswordCount * 16))
        {
            VirtualFree(tables, 0, MEM_RELEASE);
            return false;
        }

        struct PasswordEntry
        {
            const char* password;
            std::uint16_t costumeId;
            std::uint16_t messageId;
        };
        // Keep the password hashes in the PS2 source order. Costume IDs do not
        // follow the media-outlet list order: ProTip is ID 7 and GameReactor is ID 8.
        // Message IDs are indexes in system_eng_msg.mes, not costume IDs.
        constexpr PasswordEntry ps2Passwords[] = {
            { "EGMpretaporter", 5, 0x35 },
            { "iaml33t", 9, 0x34 },
            { "Suspense", 4, 0x2D },
            { "badical", 16, 0x33 },
            { "SH3_OPiuM", 13, 0x32 },
            { "ProTip", 7, 0x2F },
            { "gameinformer", 6, 0x30 },
            { "IGN_pickleboy", 12, 0x2E },
            { "GMRownzjoo", 10, 0x31 },
            { "sLmLdGhSmKfBfH", 14, 0x2C },
            { "IwannabeaGJ", 11, 0x2A },
            { "MATADOR", 15, 0x40 },
            { "SH3_Wrestlarn", 8, 0x29 },
            { "extra_thumbs", 28, 0x2B },
        };
        static_assert(sizeof(ps2Passwords) / sizeof(ps2Passwords[0]) == kPS2PasswordCount);
        for (std::size_t i = 0; i < kPS2PasswordCount; ++i)
        {
            const auto index = kVanillaPasswordCount + i;
            const std::uint16_t mapping[2]{ ps2Passwords[i].costumeId, ps2Passwords[i].messageId };
            std::memcpy(tables + index * 4, mapping, sizeof(mapping));
            const auto hash = MakePasswordHash(ps2Passwords[i].password, static_cast<std::uint8_t>(index + 13));
            std::memcpy(tables + hashOffset + index * 16, hash.data(), hash.size());
        }

        std::vector<MemoryPatch> patches;
        const auto newMapAddress = reinterpret_cast<std::uintptr_t>(tables);
        const auto newHashAddress = reinterpret_cast<std::uintptr_t>(tables + hashOffset);
        bool referencesValid = true;
        for (std::size_t i = 0; i < 8; ++i)
            referencesValid = referencesValid && CollectPointerPatches(executable, kPasswordLookupRva,
                kPasswordLookupSize, oldHashAddress + i, newHashAddress + i, 1, patches);
        referencesValid = referencesValid && CollectPointerPatches(executable, kPasswordScreenRva,
            kPasswordScreenSize, oldMapAddress, newMapAddress, 6, patches);
        referencesValid = referencesValid && CollectPointerPatches(executable, kPasswordScreenRva,
            kPasswordScreenSize, oldMapAddress + 2, newMapAddress + 2, 2, patches);

        const auto countAddress = reinterpret_cast<std::uintptr_t>(image) + kPasswordCountInstructionRva;
        std::uint8_t countInstruction[3]{};
        referencesValid = referencesValid && ReadMemory(reinterpret_cast<const void*>(countAddress),
            countInstruction, sizeof(countInstruction)) &&
            countInstruction[0] == 0x83 && countInstruction[1] == 0xFB && countInstruction[2] == kVanillaPasswordCount;
        if (referencesValid)
        {
            patches.push_back({ countAddress + 2, { static_cast<std::uint8_t>(kVanillaPasswordCount) },
                { static_cast<std::uint8_t>(kPasswordCount) } });
            for (const auto callRva : kPasswordCostumeSelectCallRvas)
            {
                const auto callAddress = reinterpret_cast<std::uintptr_t>(image) + callRva;
                std::uint8_t callBytes[5]{};
                if (!ReadMemory(reinterpret_cast<const void*>(callAddress), callBytes, sizeof(callBytes)) ||
                    callBytes[0] != 0xE8)
                {
                    referencesValid = false;
                    break;
                }

                const auto displacement = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(callBytes[1]) |
                    (static_cast<std::uint32_t>(callBytes[2]) << 8) |
                    (static_cast<std::uint32_t>(callBytes[3]) << 16) |
                    (static_cast<std::uint32_t>(callBytes[4]) << 24));
                if (static_cast<std::uintptr_t>(callAddress + sizeof(callBytes) + displacement) !=
                    reinterpret_cast<std::uintptr_t>(executable) + (0x0060C5A0 - 0x00400000))
                {
                    referencesValid = false;
                    break;
                }

                patches.push_back({ callAddress, std::vector<std::uint8_t>(callBytes, callBytes + sizeof(callBytes)),
                    { 0x90, 0x90, 0x90, 0x90, 0x90 } });
            }
        }

        if (!referencesValid)
        {
            VirtualFree(tables, 0, MEM_RELEASE);
            log::Write("WARN", "[PS2 costumes] Password code did not match the supported PC executable; PS2 costume passwords were not added.");
            return false;
        }

        std::size_t applied = 0;
        for (; applied < patches.size(); ++applied)
        {
            const auto& patch = patches[applied];
            if (!WriteMemory(patch.address, patch.replacement.data(), patch.replacement.size()))
                break;
        }
        if (applied != patches.size())
        {
            for (std::size_t rollback = applied + 1; rollback > 0; --rollback)
            {
                const auto& patch = patches[rollback - 1];
                WriteMemory(patch.address, patch.original.data(), patch.original.size());
            }
            VirtualFree(tables, 0, MEM_RELEASE);
            log::Write("WARN", "[PS2 costumes] Could not extend the costume password lookup; PS2 costume passwords were not added.");
            return false;
        }

        g_costumePasswordTables = tables;
        log::Write("INFO", "[PS2 costumes] Added all fourteen PS2 costume passwords; codes unlock outfits without changing Heather's selected costume.");
        return true;
    }

#if !defined(SH3E_DISABLE_PS2_COSTUME_INVENTORY)
    bool InstallPS2CostumeInventoryItems(HMODULE executable)
    {
        if (g_costumeInventoryTable)
            return true;

        constexpr std::uint16_t vanillaEntries[] = {
            3, 82, 17, 105, 18, 100, 19, 103, 20, 96, 21, 99,
            22, 101, 23, 98, 24, 102, 25, 106, 26, 104, 27, 97,
        };
        constexpr std::uint16_t ps2Entries[] = {
            4, 83, 5, 84, 6, 85, 7, 86, 8, 87, 9, 88, 10, 89,
            11, 90, 12, 91, 13, 92, 14, 93, 15, 94, 16, 95, 28, 107,
        };

        static_assert(sizeof(vanillaEntries) / sizeof(vanillaEntries[0]) == kVanillaCostumeInventoryEntryCount * 2);
        static_assert(sizeof(ps2Entries) / sizeof(ps2Entries[0]) == kPS2PasswordCount * 2);

        constexpr std::size_t tableWordCount = (kCostumeInventoryEntryCount + 1) * 2;
        constexpr std::size_t tableBytes = tableWordCount * sizeof(std::uint16_t);
        constexpr std::size_t vanillaTableBytes = sizeof(vanillaEntries) + 2 * sizeof(std::uint16_t);
        auto* table = static_cast<std::uint16_t*>(VirtualAlloc(nullptr, tableBytes,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!table)
            return false;

        auto* image = reinterpret_cast<std::uint8_t*>(executable);
        const auto oldTableAddress = reinterpret_cast<std::uintptr_t>(image) + (kCostumeInventoryTableVa - 0x400000);
        if (!ReadMemory(reinterpret_cast<const void*>(oldTableAddress), table, vanillaTableBytes) ||
            std::memcmp(table, vanillaEntries, sizeof(vanillaEntries)) != 0 ||
            table[kVanillaCostumeInventoryEntryCount * 2] != 0x1D ||
            table[kVanillaCostumeInventoryEntryCount * 2 + 1] != 0)
        {
            VirtualFree(table, 0, MEM_RELEASE);
            log::Write("WARN", "[PS2 costumes] The vanilla inventory costume table did not match; PS2 shirts will not be added to the wardrobe.");
            return false;
        }

        std::memcpy(table + kVanillaCostumeInventoryEntryCount * 2, ps2Entries, sizeof(ps2Entries));
        table[tableWordCount - 2] = 0x1D;
        table[tableWordCount - 1] = 0;

        std::vector<MemoryPatch> patches;
        const auto oldAddress = reinterpret_cast<std::uintptr_t>(image) + (kCostumeInventoryTableVa - 0x400000);
        const auto newAddress = reinterpret_cast<std::uintptr_t>(table);
        if (!CollectPointerPatches(executable, kCostumeInventoryFunctionRva, kCostumeInventoryFunctionSize,
            oldAddress, newAddress, 2, patches))
        {
            VirtualFree(table, 0, MEM_RELEASE);
            log::Write("WARN", "[PS2 costumes] The inventory handler did not match; PS2 shirts will not be added to the wardrobe.");
            return false;
        }

        std::size_t applied = 0;
        for (; applied < patches.size(); ++applied)
        {
            const auto& patch = patches[applied];
            if (!WriteMemory(patch.address, patch.replacement.data(), patch.replacement.size()))
                break;
        }
        if (applied != patches.size())
        {
            for (std::size_t rollback = applied; rollback > 0; --rollback)
            {
                const auto& patch = patches[rollback - 1];
                WriteMemory(patch.address, patch.original.data(), patch.original.size());
            }
            VirtualFree(table, 0, MEM_RELEASE);
            log::Write("WARN", "[PS2 costumes] Could not extend the wardrobe item list; PS2 shirts were not added to the inventory.");
            return false;
        }

        g_costumeInventoryTable = table;
        log::Write("INFO", "[PS2 costumes] Connected all fourteen unlocked PS2 outfits to their wardrobe inventory items.");
        return true;
    }
#endif
}

#if !defined(SH3E_DISABLE_PS2_COSTUMES)
bool InstallPS2CostumePasswordTable(void* executable)
{
    return InstallPS2CostumePasswords(static_cast<HMODULE>(executable));
}
#endif

#if !defined(SH3E_DISABLE_PS2_COSTUMES) && !defined(SH3E_DISABLE_PS2_COSTUME_MODELS)
bool PreparePS2CostumeArchiveRedirects(const std::wstring& gameDirectory,
    std::vector<std::pair<std::wstring, std::wstring>>& redirects)
{
    redirects.clear();
    const auto costumeDataRoot = gameDirectory + L"\\SH3 Enhanced Data\\PS2 Exclusive Costumes\\data";
    const auto catalogPath = costumeDataRoot + L"\\arc.arc";
    const auto archivePath = costumeDataRoot + L"\\chrpl.arc";
    const auto catalogAttributes = GetFileAttributesW(catalogPath.c_str());
    const auto archiveAttributes = GetFileAttributesW(archivePath.c_str());
    const bool hasCatalog = catalogAttributes != INVALID_FILE_ATTRIBUTES && (catalogAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    const bool hasArchive = archiveAttributes != INVALID_FILE_ATTRIBUTES && (archiveAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    if (!hasCatalog && !hasArchive)
        return true;
    if (!hasCatalog || !hasArchive)
    {
        log::Write("WARN", "[PS2 costumes] Prepared archives are incomplete; the original character archive will be used.");
        return false;
    }

    redirects.emplace_back(L"data\\arc.arc", catalogPath);
    redirects.emplace_back(L"data\\chrpl.arc", archivePath);
    return true;
}

bool InstallPS2CostumeResourcePaths(void* executable)
{
    std::uintptr_t tableAddress = 0;
    if (!IsExpectedExecutable(static_cast<HMODULE>(executable), tableAddress))
    {
        log::Write("WARN", "[PS2 costumes] The player resource table did not match the supported PC executable; costume paths were not installed.");
        return false;
    }

    struct Change { std::uint16_t resource; const char* path; };
    std::array<Change, kCostumes.size() * 2> changes{};
    for (std::size_t i = 0; i < kCostumes.size(); ++i)
    {
        const auto key = Narrow(kCostumes[i].key);
        if (key.empty())
            return false;
        g_resourcePaths[i * 2] = "data/pcchr/pl/sh3e_" + key + ".mdl";
        g_resourcePaths[i * 2 + 1] = "data/pcchr/pl/sh3e_r" + key + ".mdl";
        changes[i * 2] = { kCostumes[i].modelResource, g_resourcePaths[i * 2].c_str() };
        changes[i * 2 + 1] = { kCostumes[i].alternateResource, g_resourcePaths[i * 2 + 1].c_str() };
    }

    for (const auto& change : changes)
    {
        if (change.resource >= kResourcePointerCount || change.path == nullptr)
            return false;
        std::uint32_t existing = 1;
        if (!ReadMemory(reinterpret_cast<const void*>(tableAddress + change.resource * sizeof(std::uint32_t)), &existing, sizeof(existing)) || existing != 0)
        {
            log::Write("WARN", "[PS2 costumes] A reserved player resource slot is occupied; costume paths were not installed.");
            return false;
        }
    }

    std::size_t written = 0;
    for (; written < changes.size(); ++written)
    {
        const auto pointer = reinterpret_cast<std::uintptr_t>(changes[written].path);
        if (pointer > UINT32_MAX)
            break;
        const auto pointer32 = static_cast<std::uint32_t>(pointer);
        if (!WriteMemory(tableAddress + changes[written].resource * sizeof(std::uint32_t),
            &pointer32, sizeof(pointer32)))
            break;
    }
    if (written != changes.size())
    {
        const std::uint32_t nullPointer = 0;
        while (written > 0)
        {
            --written;
            WriteMemory(tableAddress + changes[written].resource * sizeof(std::uint32_t), &nullPointer, sizeof(nullPointer));
        }
        log::Write("WARN", "[PS2 costumes] Could not install all player resource paths; the original outfits remain available.");
        return false;
    }

    if (!InstallPS2CostumePasswords(static_cast<HMODULE>(executable)))
    {
        const std::uint32_t nullPointer = 0;
        for (const auto& change : changes)
            WriteMemory(tableAddress + change.resource * sizeof(std::uint32_t), &nullPointer, sizeof(nullPointer));
        return false;
    }

#if !defined(SH3E_DISABLE_PS2_COSTUME_INVENTORY)
    if (!InstallPS2CostumeInventoryItems(static_cast<HMODULE>(executable)))
        log::Write("WARN", "[PS2 costumes] Passwords and models are active, but unlocked PS2 shirts may not appear in the in-game wardrobe.");
#endif

    log::Write("INFO", "[PS2 costumes] Registered fourteen PS2 outfit model pairs in the player resource table.");
    return true;
}
#endif
}
