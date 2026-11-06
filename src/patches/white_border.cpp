#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>

namespace sh3e::patches
{
bool InstallWhiteBorderFix(HMODULE executable)
{
    // SH3 passes WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE (0x300) to its window
    // creation path. Those extended styles produce the bright outline on
    // recent Windows versions. Remove that style while retaining the normal
    // WS_OVERLAPPEDWINDOW frame used by the window-mode patch.
    const auto pattern = BytePattern::Parse("68 00 03 00 00 FF 15 ?? ?? ?? ?? 5E 83 C4 10 C3");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[White border] Signature must match exactly once; found " + std::to_string(matches.size()));
        return false;
    }

    constexpr std::uint32_t noExtendedStyle = 0;
    if (!WriteMemory(matches.front().address + 1, &noExtendedStyle, sizeof(noExtendedStyle)))
    {
        log::Write("ERROR", "[White border] Could not clear the game's extended window style.");
        return false;
    }

    log::Write("INFO", "[White border] Extended window-edge styles disabled at sh3.exe+" + detail::Hex(matches.front().rva));
    return true;
}
}