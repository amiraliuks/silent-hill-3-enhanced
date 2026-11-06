#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace sh3e::patches
{
bool InstallDofResolutionPatch(HMODULE executable, int resolution)
{
    if (!executable || resolution < 0 || resolution > 4096)
    {
        log::Write("WARN", "[DoF resolution] Invalid module or resolution; patch skipped.");
        return false;
    }

    if (resolution == 0)
    {
        log::Write("INFO", "[DoF resolution] Using the game's default target dimensions.");
        return true;
    }

    const auto pattern = BytePattern::Parse(
        "8B 3C 85 ?? ?? ?? ?? 8B 04 85 ?? ?? ?? ?? 56 6A 00 6A 15 6A 01 6A 01");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[DoF resolution] Resource-table signature must match once; found " +
            std::to_string(matches.size()));
        return false;
    }

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;

    std::uint32_t tableVa = 0;
    std::memcpy(&tableVa, reinterpret_cast<const void*>(matches.front().address + 3), sizeof(tableVa));
    if (tableVa < nt->OptionalHeader.ImageBase)
    {
        log::Write("WARN", "[DoF resolution] Resource table address is outside the executable image.");
        return false;
    }

    const auto tableRva = static_cast<std::uintptr_t>(tableVa - nt->OptionalHeader.ImageBase);
    constexpr std::uintptr_t patchSpan = 24;
    const auto imageSize = static_cast<std::uintptr_t>(nt->OptionalHeader.SizeOfImage);
    if (tableRva < 8 || tableRva > imageSize || patchSpan > imageSize - (tableRva - 8))
    {
        log::Write("WARN", "[DoF resolution] Resource table entries exceed the executable image.");
        return false;
    }

    const auto table = moduleBase + tableRva;
    // The four selected DWORDs are the game's two depth-of-field target pairs.
    // Keep all writes within this verified 24-byte resource-size table span.
    std::uint32_t replacement[6]{};
    std::memcpy(replacement, reinterpret_cast<const void*>(table - 8), sizeof(replacement));
    replacement[0] = static_cast<std::uint32_t>(resolution);
    replacement[1] = static_cast<std::uint32_t>(resolution);
    replacement[4] = static_cast<std::uint32_t>(resolution);
    replacement[5] = static_cast<std::uint32_t>(resolution);
    if (!WriteMemory(table - 8, replacement, sizeof(replacement)))
    {
        log::Write("ERROR", "[DoF resolution] Could not update the depth-of-field resource table.");
        return false;
    }

    log::Write("INFO", "[DoF resolution] Target size set to " + std::to_string(resolution) +
        "x" + std::to_string(resolution) + " at sh3.exe+" + detail::Hex(tableRva));
    return true;
}
}
