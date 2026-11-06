#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace sh3e::patches
{
bool InstallInventoryResolutionPatch(HMODULE executable, int resolution)
{
    if (!executable || resolution < 0 || resolution > 1024)
    {
        log::Write("WARN", "[Inventory quad] Invalid module or size; patch skipped.");
        return false;
    }

    if (resolution == 0)
    {
        log::Write("INFO", "[Inventory quad] Using the game's default dimensions.");
        return true;
    }

    const auto pattern = BytePattern::Parse("B8 00 01 00 00 BA 00 FF FF FF 8B E8");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Inventory quad] Signature must match once; found " +
            std::to_string(matches.size()));
        return false;
    }

    // This pair sets opposite horizontal bounds for the first pause-menu quad.
    // The vertical bounds are separate values and remain at the game defaults.
    std::uint8_t patchedInstructions[12]{};
    std::memcpy(patchedInstructions, reinterpret_cast<const void*>(matches.front().address), sizeof(patchedInstructions));
    const auto width = static_cast<std::uint32_t>(resolution);
    const auto height = static_cast<std::int32_t>(-resolution);
    std::memcpy(patchedInstructions + 1, &width, sizeof(width));
    std::memcpy(patchedInstructions + 6, &height, sizeof(height));
    if (!WriteMemory(matches.front().address, patchedInstructions, sizeof(patchedInstructions)))
    {
        log::Write("ERROR", "[Inventory quad] Could not update horizontal bounds.");
        return false;
    }

    log::Write("INFO", "[Inventory quad] First pause-menu quad horizontal bounds set to +/-" + std::to_string(resolution) +
        " at sh3.exe+" + detail::Hex(matches.front().rva));
    return true;
}
}
