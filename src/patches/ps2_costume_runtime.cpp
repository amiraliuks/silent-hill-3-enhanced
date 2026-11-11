#include "ps2_costume_runtime.h"
#include "ps2_costume_runtime_data.h"
#include "../logger.h"
#include "../pattern.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

namespace sh3e::patches
{
namespace
{
    using runtime_data::RuntimePatch;
    struct AppliedPatch
    {
        std::uintptr_t address;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> replacement;
    };

    void* g_characterArena = nullptr;
    void* g_characterCode = nullptr;
    void* g_modelTextureStorage = nullptr;
    void* g_modelTextureCode = nullptr;
    bool g_modelRuntimeInstalled = false;

    void* AllocateBelow(std::size_t bytes, std::size_t alignment)
    {
        constexpr std::uintptr_t kSearchStart = 0x10000000;
        constexpr std::uintptr_t kSearchLimit = 0x78000000;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const auto granularity = (std::max<std::uintptr_t>)(info.dwAllocationGranularity, alignment);
        const auto alignUp = [granularity](std::uintptr_t value) {
            return (value + granularity - 1) / granularity * granularity;
        };

        std::uintptr_t cursor = kSearchStart;
        while (cursor < kSearchLimit && bytes < kSearchLimit - cursor)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)))
                break;
            const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const auto regionEnd = regionStart + region.RegionSize;
            if (region.State == MEM_FREE)
            {
                const auto candidate = alignUp((std::max)(cursor, regionStart));
                if (candidate >= regionStart && candidate <= regionEnd && bytes <= regionEnd - candidate &&
                    candidate < kSearchLimit && bytes <= kSearchLimit - candidate)
                {
                    void* allocation = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes,
                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                    if (allocation)
                        return allocation;
                }
            }
            if (regionEnd <= cursor)
                break;
            cursor = regionEnd;
        }
        return nullptr;
    }

    bool MakeExecutable(void* address, std::size_t bytes)
    {
        DWORD oldProtection = 0;
        return VirtualProtect(address, bytes, PAGE_EXECUTE_READ, &oldProtection) != FALSE &&
            FlushInstructionCache(GetCurrentProcess(), address, bytes) != FALSE;
    }

    bool AddJumpPatch(HMODULE executable, const RuntimePatch& patch, const std::uint8_t* code,
        std::vector<AppliedPatch>& patches)
    {
        const auto length = static_cast<std::size_t>(patch.length);
        if (length < 5)
            return false;

        const auto site = reinterpret_cast<std::uintptr_t>(executable) + (patch.address - 0x400000);
        AppliedPatch change{};
        change.address = site;
        change.original.resize(length);
        change.replacement.assign(length, 0x90);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(site),
            change.original.data(), length, &bytesRead) || bytesRead != length ||
            std::memcmp(change.original.data(), patch.expected, length) != 0)
            return false;

        const auto destination = reinterpret_cast<std::uintptr_t>(code) + patch.entry;
        const auto displacement = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(site + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
            return false;
        change.replacement[0] = 0xE9;
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(change.replacement.data() + 1, &relative, sizeof(relative));
        patches.push_back(std::move(change));
        return true;
    }

    bool RollBack(const std::vector<AppliedPatch>& patches, std::size_t count)
    {
        bool result = true;
        while (count > 0)
        {
            --count;
            result = WriteMemory(patches[count].address, patches[count].original.data(),
                patches[count].original.size()) && result;
        }
        return result;
    }

    void ReleaseAllocations()
    {
        if (g_modelTextureCode) VirtualFree(g_modelTextureCode, 0, MEM_RELEASE);
        if (g_modelTextureStorage) VirtualFree(g_modelTextureStorage, 0, MEM_RELEASE);
        if (g_characterCode) VirtualFree(g_characterCode, 0, MEM_RELEASE);
        if (g_characterArena) VirtualFree(g_characterArena, 0, MEM_RELEASE);
        g_modelTextureCode = nullptr;
        g_modelTextureStorage = nullptr;
        g_characterCode = nullptr;
        g_characterArena = nullptr;
    }

    bool IsSupportedImage(HMODULE executable)
    {
        if (!executable || reinterpret_cast<std::uintptr_t>(executable) != 0x400000)
            return false;
        const auto* base = reinterpret_cast<const std::uint8_t*>(executable);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && nt->OptionalHeader.ImageBase == 0x400000;
    }
}

bool InstallPS2CostumeModelRuntime(void* executableValue)
{
    if (g_modelRuntimeInstalled)
        return true;
    auto executable = static_cast<HMODULE>(executableValue);
    if (!IsSupportedImage(executable))
    {
        log::Write("WARN", "[PS2 costumes] Runtime model extensions require the supported 32-bit PC executable; model swaps remain disabled.");
        return false;
    }

    constexpr auto characterCodeBytes = sizeof(runtime_data::kCharacterCode);
    constexpr auto textureCodeBytes = sizeof(runtime_data::kModelTextureCode);
    g_characterArena = AllocateBelow(runtime_data::kCharacterArenaBytes, runtime_data::kCharacterArenaAlignment);
    g_characterCode = AllocateBelow(characterCodeBytes, 16);
    g_modelTextureStorage = AllocateBelow(runtime_data::kModelTextureStorageBytes, 16);
    g_modelTextureCode = AllocateBelow(textureCodeBytes, 16);
    if (!g_characterArena || !g_characterCode || !g_modelTextureStorage || !g_modelTextureCode)
    {
        ReleaseAllocations();
        log::Write("WARN", "[PS2 costumes] Could not reserve memory for the model extensions; model swaps remain disabled.");
        return false;
    }

    std::memcpy(g_characterCode, runtime_data::kCharacterCode, characterCodeBytes);
    std::memcpy(g_modelTextureCode, runtime_data::kModelTextureCode, textureCodeBytes);
    auto* characterCode = static_cast<std::uint8_t*>(g_characterCode);
    auto* textureCode = static_cast<std::uint8_t*>(g_modelTextureCode);
    for (const auto offset : runtime_data::kCharacterArenaPointerRelocations)
    {
        const auto pointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_characterArena));
        std::memcpy(characterCode + offset, &pointer, sizeof(pointer));
    }
    for (const auto offset : runtime_data::kCharacterExternalBranches)
    {
        std::int32_t relative = 0;
        std::memcpy(&relative, characterCode + offset, sizeof(relative));
        const auto adjusted = static_cast<std::int64_t>(relative) + 0x10000000 -
            static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(g_characterCode));
        if (adjusted < INT32_MIN || adjusted > INT32_MAX)
        {
            ReleaseAllocations();
            log::Write("WARN", "[PS2 costumes] Character extension could not be relocated safely; model swaps remain disabled.");
            return false;
        }
        const auto adjusted32 = static_cast<std::int32_t>(adjusted);
        std::memcpy(characterCode + offset, &adjusted32, sizeof(adjusted32));
    }
    for (const auto& relocation : runtime_data::kModelStorageRelocations)
    {
        const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_modelTextureStorage) + relocation[1]);
        std::memcpy(textureCode + relocation[0], &address, sizeof(address));
    }

    if (!MakeExecutable(g_characterCode, characterCodeBytes) || !MakeExecutable(g_modelTextureCode, textureCodeBytes))
    {
        ReleaseAllocations();
        log::Write("WARN", "[PS2 costumes] Could not protect the runtime model extensions; model swaps remain disabled.");
        return false;
    }

    std::vector<AppliedPatch> patches;
    patches.reserve(std::size(runtime_data::kCharacterPatches) + std::size(runtime_data::kModelTexturePatches));
    for (const auto& patch : runtime_data::kCharacterPatches)
    {
        if (!AddJumpPatch(executable, patch, characterCode, patches))
        {
            ReleaseAllocations();
            log::Write("WARN", "[PS2 costumes] Character extension signature did not match; model swaps remain disabled.");
            return false;
        }
    }
    for (const auto& patch : runtime_data::kModelTexturePatches)
    {
        if (!AddJumpPatch(executable, patch, textureCode, patches))
        {
            ReleaseAllocations();
            log::Write("WARN", "[PS2 costumes] Model texture extension signature did not match; model swaps remain disabled.");
            return false;
        }
    }

    std::size_t applied = 0;
    for (; applied < patches.size(); ++applied)
    {
        if (!WriteMemory(patches[applied].address, patches[applied].replacement.data(), patches[applied].replacement.size()))
            break;
    }
    if (applied != patches.size())
    {
        const bool rolledBack = RollBack(patches, (std::min)(applied + 1, patches.size()));
        ReleaseAllocations();
        log::Write("WARN", rolledBack
            ? "[PS2 costumes] Runtime model extension installation failed and was rolled back; model swaps remain disabled."
            : "[PS2 costumes] Runtime model extension installation failed; rollback was incomplete.");
        return false;
    }

    g_modelRuntimeInstalled = true;
    log::Write("INFO", "[PS2 costumes] Installed the character and 32-slot texture extensions in memory; sh3.exe on disk is unchanged.");
    return true;
}
}
