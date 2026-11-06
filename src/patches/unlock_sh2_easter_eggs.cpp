#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include "../pattern.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace sh3e::patches
{
namespace
{
    static_assert(sizeof(void*) == sizeof(std::uint32_t), "The SH3 hook requires a 32-bit build.");

    // sh3proxy identifies these as gRegHasSH2 and gCheckedRegSH2 in the
    // supported 372AE792 executable. They are accessed relative to the PE base.
    constexpr std::uintptr_t kHasSH2Rva = 0x7CC250;
    constexpr std::uintptr_t kCheckedSH2Rva = 0x7CC251;
    constexpr std::uintptr_t kUpdateSH2InstallDirRva = 0x1E9760;
    constexpr std::array<std::uint8_t, 6> kExpectedFunctionBytes = {
        0xA0, 0x51, 0xC2, 0xBC, 0x00, 0x84
    };

    std::uint8_t* g_sh2Flags = nullptr;
    std::uint32_t g_overrideCallback = 0;

    extern "C" int __cdecl ForceSH2References()
    {
        if (g_sh2Flags)
        {
            g_sh2Flags[0] = 1;
            g_sh2Flags[1] = 1;
        }

        return 1;
    }
}

bool InstallSH2EasterEggUnlock(HMODULE executable)
{
    if (!executable)
        return false;

    auto* base = reinterpret_cast<std::uint8_t*>(executable);
    auto* target = base + kUpdateSH2InstallDirRva;
    if (std::memcmp(target, kExpectedFunctionBytes.data(), kExpectedFunctionBytes.size()) != 0)
    {
        log::Write("WARN", "[SH2 Easter eggs] Game-version signature mismatch at sh3.exe+" +
            detail::Hex(kUpdateSH2InstallDirRva) + "; patch not installed.");
        return false;
    }

    g_sh2Flags = base + kHasSH2Rva;
    g_overrideCallback = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&ForceSH2References));

    // Replace updateSH2InstallDir with a small absolute indirect jump to our
    // callback. The callback marks SH2 as installed and its check as complete,
    // matching sh3proxy's behavior without depending on Windows registry state.
    const auto callbackSlot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_overrideCallback));
    const std::array<std::uint8_t, 6> jump = {
        0xFF, 0x25,
        static_cast<std::uint8_t>(callbackSlot & 0xFF),
        static_cast<std::uint8_t>((callbackSlot >> 8) & 0xFF),
        static_cast<std::uint8_t>((callbackSlot >> 16) & 0xFF),
        static_cast<std::uint8_t>((callbackSlot >> 24) & 0xFF),
    };

    if (!WriteMemory(reinterpret_cast<std::uintptr_t>(target), jump.data(), jump.size()))
    {
        g_sh2Flags = nullptr;
        g_overrideCallback = 0;
        log::Write("ERROR", "[SH2 Easter eggs] Could not install the SH2 save-check override.");
        return false;
    }

    ForceSH2References();
    log::Write("INFO", "[SH2 Easter eggs] SH2 save-data check overridden at sh3.exe+0x1E9760.");
    return true;
}
}