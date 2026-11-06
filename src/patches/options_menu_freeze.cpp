#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>

namespace sh3e::patches
{
bool InstallOptionsMenuFreezeFix(HMODULE executable)
{
    using detail::Hex;

    const auto pattern = BytePattern::Parse("81 EC D8 00 00 00 55 8B AC");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Options] Freeze-fix signature must match exactly once; found " + std::to_string(matches.size()));
        return false;
    }

    constexpr std::uint8_t returnInstruction = 0xC3;
    if (!WriteMemory(matches.front().address, &returnInstruction, sizeof(returnInstruction)))
    {
        log::Write("ERROR", "[Options] Could not patch the options-menu freeze path.");
        return false;
    }

    log::Write("INFO", "[Options] Freeze fix installed at sh3.exe+" + detail::Hex(matches.front().rva));
    return true;
}
}
