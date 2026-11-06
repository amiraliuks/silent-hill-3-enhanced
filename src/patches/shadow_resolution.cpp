#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sh3e::patches
{
namespace
{
    bool WriteFloatOperand(const PatternMatch& match, std::size_t offset, std::uint32_t value)
    {
        return WriteMemory(match.address + offset, &value, sizeof(value));
    }

    bool WriteIntegerOperand(const PatternMatch& match, std::size_t offset, std::uint32_t value)
    {
        return WriteMemory(match.address + offset, &value, sizeof(value));
    }
}

bool InstallShadowResolutionPatch(HMODULE executable, int resolution)
{
    if (!executable || resolution < 0 || resolution > 4096)
    {
        log::Write("WARN", "[Shadow resolution] Invalid module or resolution; patch skipped.");
        return false;
    }

    if (resolution == 0)
    {
        log::Write("INFO", "[Shadow resolution] Using the game's default shadow dimensions.");
        return true;
    }

    const auto stackPattern = BytePattern::Parse("C7 44 24 ?? 00 00 80 43");
    const auto absolutePattern = BytePattern::Parse("C7 05 ?? ?? ?? ?? 00 00 80 43");
    const auto dimensionsPattern = BytePattern::Parse("68 00 01 00 00 68 00 01 00 00");
    if (!stackPattern || !absolutePattern || !dimensionsPattern)
        return false;

    const auto stackMatches = FindPattern(executable, *stackPattern);
    const auto absoluteMatches = FindPattern(executable, *absolutePattern);
    const auto dimensionsMatches = FindPattern(executable, *dimensionsPattern);
    if (stackMatches.size() != 4 || absoluteMatches.size() != 23 || dimensionsMatches.size() != 9)
    {
        log::Write("WARN", "[Shadow resolution] Expected signature counts 4/23/9; found " +
            std::to_string(stackMatches.size()) + "/" + std::to_string(absoluteMatches.size()) + "/" +
            std::to_string(dimensionsMatches.size()) + "; patch skipped.");
        return false;
    }

    const auto floatResolution = static_cast<float>(resolution);
    std::uint32_t floatBits = 0;
    std::memcpy(&floatBits, &floatResolution, sizeof(floatBits));

    // These instructions initialize shadow dimensions in stack locals.
    for (const auto& match : stackMatches)
    {
        if (!WriteFloatOperand(match, 4, floatBits))
        {
            log::Write("ERROR", "[Shadow resolution] Could not update a stack shadow dimension.");
            return false;
        }
    }

    // In the two known shadow setup blocks, matches 1 through 16 reference
    // shadow-width/height globals. Match 0 and matches 17 through 22 are unrelated.
    for (std::size_t index = 1; index <= 16; ++index)
    {
        if (!WriteFloatOperand(absoluteMatches[index], 6, floatBits))
        {
            log::Write("ERROR", "[Shadow resolution] Could not update a shadow-map dimension.");
            return false;
        }
    }

    // These three creation calls pass integer width/height pairs for shadows.
    for (const std::size_t index : {3u, 4u, 5u})
    {
        if (!WriteIntegerOperand(dimensionsMatches[index], 1, static_cast<std::uint32_t>(resolution)) ||
            !WriteIntegerOperand(dimensionsMatches[index], 6, static_cast<std::uint32_t>(resolution)))
        {
            log::Write("ERROR", "[Shadow resolution] Could not update a shadow surface size.");
            return false;
        }
    }

    log::Write("INFO", "[Shadow resolution] Dynamic shadow maps set to " + std::to_string(resolution) + "x" + std::to_string(resolution) + " (4 stack values, 16 globals, 3 surface pairs).");
    return true;
}
}