#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>
#include <cstring>

namespace sh3e::patches
{
bool InstallSafeModeDisable(HMODULE executable)
{
    const auto pattern = BytePattern::Parse("E8 ?? ?? ?? ?? BF 01 00 00 00 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 6A 01");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Safe mode] Signature must match exactly once; found " + std::to_string(matches.size()));
        return false;
    }

    const auto& match = matches.front();
    std::int32_t callDisplacement = 0;
    std::memcpy(&callDisplacement, reinterpret_cast<const void*>(match.address + 1), sizeof(callDisplacement));
    const auto callTarget = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(match.address + 5) + callDisplacement);
    const auto expectedTarget = reinterpret_cast<std::uintptr_t>(executable) + 0x1F0950;
    if (callTarget != expectedTarget)
    {
        log::Write("WARN", "[Safe mode] Fallback-reset call target did not match the verified function.");
        return false;
    }

    // This call restores the default graphics values after the game detects
    // an abnormal exit. Skip it and clear the return flag that triggers the
    // safe-mode warning in the startup caller.
    constexpr std::uint8_t skipResetCall[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };

    // The following file-open uses "wt" to rewrite disp.ini with the fallback
    // settings. An empty mode makes that open fail while leaving the later
    // normal read path untouched.
    static const char emptyString[] = "";
    const auto emptyStringAddress = reinterpret_cast<std::uintptr_t>(emptyString);
    if (emptyStringAddress > UINT32_MAX)
        return false;

    constexpr std::uint32_t disableds = 0;
    const auto stringPointer = static_cast<std::uint32_t>(emptyStringAddress);
    if (!WriteMemory(match.address, skipResetCall, sizeof(skipResetCall)) ||
        !WriteMemory(match.address + 6, &disabled, sizeof(disabled)) ||
        !WriteMemory(match.address + 11, &stringPointer, sizeof(stringPointer)))
    {
        log::Write("ERROR", "[Safe mode] Could not disable the fallback reset and settings-file rewrite.");
        return false;
    }

    log::Write("INFO", "[Safe mode] Fallback reset, warning, and disp.ini rewrite disabled at sh3.exe+" + detail::Hex(match.rva));
    return true;
}
}