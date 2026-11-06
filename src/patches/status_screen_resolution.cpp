#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace sh3e::patches
{
namespace
{
    struct MemoryChange
    {
        std::uintptr_t address = 0;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> replacement;
    };

    bool ResolveImageVa(std::uintptr_t moduleBase, std::uint32_t imageBase, std::uint32_t va, std::uintptr_t imageSize, std::size_t length, std::uintptr_t& address)
    {
        if (va < imageBase)
            return false;
        const auto rva = static_cast<std::uintptr_t>(va - imageBase);
        if (rva > imageSize || length > imageSize - rva)
            return false;
        address = moduleBase + rva;
        return true;
    }

    bool AddChange(std::vector<MemoryChange>& changes, std::uintptr_t address, const void* replacement, std::size_t length, std::uintptr_t imageBase, std::uintptr_t imageSize)
    {
        if (!address || !replacement || length == 0 || address < imageBase)
            return false;
        const auto rva = address - imageBase;
        if (rva > imageSize || length > imageSize - rva)
            return false;

        MemoryChange change;
        change.address = address;
        change.original.resize(length);
        change.replacement.resize(length);
        std::memcpy(change.original.data(), reinterpret_cast<const void*>(address), length);
        std::memcpy(change.replacement.data(), replacement, length);
        changes.push_back(std::move(change));
        return true;
    }
}

bool InstallStatusScreenResolutionPatch(HMODULE executable, int resolution)
{
    if (!executable || resolution < 0 || resolution > 4096)
    {
        log::Write("WARN", "[Status screen resolution] Invalid module or resolution; patch skipped.");
        return false;
    }

    if (resolution == 0)
    {
        log::Write("INFO", "[Status screen resolution] Using the game's default image dimensions.");
        return true;
    }

    const auto tablePattern = BytePattern::Parse("8B 3C 85 ?? ?? ?? ?? 8B 04 85 ?? ?? ?? ?? 56 6A 00 6A 15 6A 01 6A 01");
    const auto drawPatternA = BytePattern::Parse("BE ?? ?? ?? ?? 56 56 89 5C 24 24 89 5C 24 28 89 5C 24 20 8B 08 50");
    const auto drawPatternB = BytePattern::Parse("BE ?? ?? ?? ?? 8D A4 24 00 00 00 00 8B 2C 01 89 28 83 C0 04 4E");
    const auto dataPattern = BytePattern::Parse("68 ?? ?? ?? ?? 6A 02 6A 05 50 FF 91 20 01 00 00 A1 ?? ?? ?? ?? 8B 10 6A 01");
    if (!tablePattern || !drawPatternA || !drawPatternB || !dataPattern)
        return false;

    const auto tableMatches = FindPattern(executable, *tablePattern);
    const auto drawMatchesA = FindPattern(executable, *drawPatternA);
    const auto drawMatchesB = FindPattern(executable, *drawPatternB);
    const auto dataMatches = FindPattern(executable, *dataPattern);

    if (tableMatches.size() != 1 || drawMatchesA.size() != 1 || drawMatchesB.size() != 1 || dataMatches.size() != 7)
    {
        log::Write("WARN", "[Status screen resolution] Signature counts changed (expected 1/1/1/7; found " +
            std::to_string(tableMatches.size()) + "/" + std::to_string(drawMatchesA.size()) + "/" +
            std::to_string(drawMatchesB.size()) + "/" + std::to_string(dataMatches.size()) + "); patch skipped.");
        return false;
    }

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;

    const auto imageSize = static_cast<std::uintptr_t>(nt->OptionalHeader.SizeOfImage);
    const auto preferredBase = nt->OptionalHeader.ImageBase;
    const auto resolutionValue = static_cast<std::uint32_t>(resolution);
    const auto resolutionFloat = static_cast<float>(resolution);
    std::vector<MemoryChange> changes;
    changes.reserve(8);

    // The initializer reads the two status-image dimensions from separate tables.
    for (const auto operandOffset : { 3u, 10u })
    {
        std::uint32_t targetVa = 0;
        std::memcpy(&targetVa, reinterpret_cast<const void*>(tableMatches.front().address + operandOffset), sizeof(targetVa));
        std::uintptr_t target = 0;
        if (!ResolveImageVa(moduleBase, preferredBase, targetVa, imageSize, sizeof(resolutionValue), target) || !AddChange(changes, target, &resolutionValue, sizeof(resolutionValue), moduleBase, imageSize))
        {
            log::Write("WARN", "[Status screen resolution] Resource table address is outside the executable image.");
            return false;
        }
    }

    // These two routines draw the status-screen image and use a square edge length.
    for (const auto& match : { drawMatchesA.front(), drawMatchesB.front() })
    {
        const auto operand = match.address + 1;
        if (!AddChange(changes, operand, &resolutionValue, sizeof(resolutionValue), moduleBase, imageSize))
        {
            log::Write("WARN", "[Status screen resolution] Draw-size operand is outside the executable image.");
            return false;
        }
    }

    // WFP's second occurrence resolves to the verified 0x6B0D00 status-image
    // descriptor. Its four 64.0f values are the source image dimensions.
    std::uint32_t descriptorVa = 0;
    std::memcpy(&descriptorVa, reinterpret_cast<const void*>(dataMatches[1].address + 1), sizeof(descriptorVa));
    std::uintptr_t descriptor = 0;
    if (!ResolveImageVa(moduleBase, preferredBase, descriptorVa, imageSize, 0x50, descriptor))
    {
        log::Write("WARN", "[Status screen resolution] Image descriptor is outside the executable image.");
        return false;
    }

    constexpr std::array<std::uintptr_t, 4> dimensionOffsets{ 0x00, 0x30, 0x34, 0x4C };
    for (const auto offset : dimensionOffsets)
    {
        float original = 0.0f;
        std::memcpy(&original, reinterpret_cast<const void*>(descriptor + offset), sizeof(original));
        if (original != 64.0f)
        {
            log::Write("WARN", "[Status screen resolution] Image descriptor did not match the expected 64x64 defaults; patch skipped.");
            return false;
        }

        if (!AddChange(changes, descriptor + offset, &resolutionFloat, sizeof(resolutionFloat), moduleBase, imageSize))
        {
            log::Write("WARN", "[Status screen resolution] Image dimensions exceed the executable image.");
            return false;
        }
    }

    std::size_t written = 0;
    for (; written < changes.size(); ++written)
    {
        const auto& change = changes[written];
        if (!WriteMemory(change.address, change.replacement.data(), change.replacement.size()))
        {
            // Restore every target in case a protection change failed partway through.
            for (std::size_t rollback = 0; rollback <= written; ++rollback)
            {
                const auto& prior = changes[rollback];
                WriteMemory(prior.address, prior.original.data(), prior.original.size());
            }

            log::Write("ERROR", "[Status screen resolution] Could not update all status-image dimensions; reverted partial writes.");
            return false;
        }
    }

    log::Write("INFO", "[Status screen resolution] Image dimensions set to " + std::to_string(resolution) +
        "x" + std::to_string(resolution) + " (4 verified patch sites).");
    return true;
}
}