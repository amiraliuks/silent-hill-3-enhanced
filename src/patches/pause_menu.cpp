#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <cstdint>

namespace sh3e::patches
{
bool InstallFullscreenPauseMenuPatch(HMODULE executable, const ScreenState& screen)
{
    if (!executable)
        return false;
    if (screen.aspectRatio <= (4.0f / 3.0f))
        return true;

    // The fullscreen pause-screen branch begins with JE +0x36. Changing only
    // the condition to JMP keeps the pause rendering path active at widescreen
    // output sizes while leaving the title menu and the 4:3 HUD hooks alone.
    const auto pattern = BytePattern::Parse(
        "74 36 8B 08 8D 54 24 08 52 6A 00 50 FF 51 3C 85 C0 7C 25 "
        "8B 54 24 08 A1 ?? ?? ?? ?? 8B 08 6A 00 52 8B 54 24 0C 6A 00 6A 00");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Fullscreen pause menu] Signature must match once; found " +
            std::to_string(matches.size()));
        return false;
    }

    constexpr std::uint8_t fullscreenBranch = 0xEB;
    if (!WriteMemory(matches.front().address, &fullscreenBranch, sizeof(fullscreenBranch)))
    {
        log::Write("ERROR", "[Fullscreen pause menu] Could not enable the widescreen pause-screen branch.");
        return false;
    }

    log::Write("INFO", "[Fullscreen pause menu] Widescreen pause-screen branch enabled at sh3.exe+" +
        detail::Hex(matches.front().rva));
    return true;
}
}
