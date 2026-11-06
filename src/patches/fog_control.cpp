#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace sh3e::patches
{
namespace
{
// The executable's original fog coefficient is stored in shared game data.
// Keep the replacement owned by the ASI and redirect only the fog calculation
// that reads it, rather than changing the shared constant for every subsystem.
float g_fogComplexity = 75.0f;
volatile LONG g_fogControlInstalled = 0;
}

bool InstallFogControlPatch(HMODULE executable, int complexity)
{
    if (!executable || complexity < 0 || complexity > 4096)
    {
        log::Write("WARN", "[Fog control] Invalid module or coefficient; patch skipped.");
        return false;
    }

    const auto pattern = BytePattern::Parse(
        "D8 3D ?? ?? ?? ?? D9 1D ?? ?? ?? ?? 83 C4 08 C3");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Fog control] Fog-calculation signature must match once; found " +
            std::to_string(matches.size()));
        return false;
    }

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
    {
        log::Write("WARN", "[Fog control] Invalid executable DOS header; patch skipped.");
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase > UINT32_MAX)
    {
        log::Write("WARN", "[Fog control] Expected a 32-bit SH3 executable; patch skipped.");
        return false;
    }

    std::uint32_t originalCoefficientVa = 0;
    std::memcpy(&originalCoefficientVa,
        reinterpret_cast<const void*>(matches.front().address + 2), sizeof(originalCoefficientVa));
    if (originalCoefficientVa < nt->OptionalHeader.ImageBase)
    {
        log::Write("WARN", "[Fog control] Fog coefficient pointer is outside the executable image.");
        return false;
    }

    const auto coefficientRva = static_cast<std::uintptr_t>(originalCoefficientVa - nt->OptionalHeader.ImageBase);
    if (coefficientRva > nt->OptionalHeader.SizeOfImage || sizeof(float) > nt->OptionalHeader.SizeOfImage - coefficientRva)
    {
        log::Write("WARN", "[Fog control] Fog coefficient address exceeds the executable image.");
        return false;
    }

    float originalCoefficient = 0.0f;
    std::memcpy(&originalCoefficient, reinterpret_cast<const void*>(moduleBase + coefficientRva), sizeof(originalCoefficient));
    if (originalCoefficient != 25.0f)
    {
        log::Write("WARN", "[Fog control] Expected the original 25.0 fog coefficient; found " +
            std::to_string(originalCoefficient) + ". Patch skipped.");
        return false;
    }

    // This calculation stores its result through a second absolute operand.
    // Validate that operand too, guarding against a coincidental signature.
    std::uint32_t outputVa = 0;
    std::memcpy(&outputVa, reinterpret_cast<const void*>(matches.front().address + 8), sizeof(outputVa));
    constexpr std::uintptr_t expectedOutputRva = 0x2B82E8;
    if (outputVa < nt->OptionalHeader.ImageBase ||
        static_cast<std::uintptr_t>(outputVa - nt->OptionalHeader.ImageBase) != expectedOutputRva)
    {
        log::Write("WARN", "[Fog control] Fog calculation output did not match the verified SH3 site.");
        return false;
    }

    g_fogComplexity = static_cast<float>(complexity == 0 ? 25 : complexity);
    if (!detail::RedirectFloatOperand(matches.front(), &g_fogComplexity))
    {
        log::Write("ERROR", "[Fog control] Could not redirect the fog coefficient operand.");
        return false;
    }

    InterlockedExchange(&g_fogControlInstalled, 1);
    log::Write("INFO", "[Fog control] Fog coefficient redirected from original 25 to " +
        std::to_string(complexity == 0 ? 25 : complexity) + " at sh3.exe+" + detail::Hex(matches.front().rva) +
        "; the value can now be changed live.");
    return true;
}

bool SetFogComplexity(int complexity)
{
    if (complexity < 0 || complexity > 4096 ||
        InterlockedCompareExchange(&g_fogControlInstalled, 0, 0) == 0)
        return false;
    const float value = static_cast<float>(complexity == 0 ? 25 : complexity);
    LONG bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_fogComplexity), bits);
    return true;
}
}
