#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace sh3e::patches
{
namespace
{
    float g_zeroFloat = 0.0f;
    float g_horizontalFovScale = 0.88f;
    float g_verticalFovScale = 1.0f;
    using detail::Hex;
    using detail::RedirectFloatOperand;

    bool PatchFloatLoadAndReturn(std::uintptr_t address, const float* value)
    {
        const auto pointer = reinterpret_cast<std::uintptr_t>(value);
        if (pointer > UINT32_MAX)
            return false;

        // Load the replacement x87 value and return from the game's scalar helper.
        std::array<std::uint8_t, 7> code{ 0xD9, 0x05, 0, 0, 0, 0, 0xC3 };
        const auto pointer32 = static_cast<std::uint32_t>(pointer);
        std::memcpy(code.data() + 2, &pointer32, sizeof(pointer32));
        return sh3e::WriteMemory(address, code.data(), code.size());
    }

    bool InstallWidescreenAspectFix(HMODULE executable, const sh3e::ScreenState& screen)
    {
        if (screen.aspectRatio <= (4.0f / 3.0f))
            return false;

        g_horizontalFovScale = 0.88f * screen.hudScale;
        g_verticalFovScale = 1.0f;

        const auto fovPattern = sh3e::BytePattern::Parse(
            "6A 02 E8 ?? ?? ?? ?? 83 C4 04 85 C0 ?? ?? D9 05 ?? ?? ?? ?? C3");
        const auto leftBarPattern = sh3e::BytePattern::Parse(
            "D8 0D ?? ?? ?? ?? DC C0 D9 54 24 0C D9 5C 24 04 E8");
        const auto rightBarPattern = sh3e::BytePattern::Parse(
            "D8 0D ?? ?? ?? ?? A3 ?? ?? ?? ?? A1 ?? ?? ?? ?? 6A 00 D9 5C 24 14");

        if (!fovPattern || !leftBarPattern || !rightBarPattern)
            return false;

        const auto fovMatches = sh3e::FindPattern(executable, *fovPattern);
        const auto leftBarMatches = sh3e::FindPattern(executable, *leftBarPattern);
        const auto rightBarMatches = sh3e::FindPattern(executable, *rightBarPattern);

        if (fovMatches.size() != 2)
        {
            sh3e::log::Write("WARN", "[Widescreen] FOV signatures must match exactly twice; found " +
                std::to_string(fovMatches.size()));
            return false;
        }

        if (leftBarMatches.size() != 1 || rightBarMatches.size() != 1)
        {
            sh3e::log::Write("WARN", "[Widescreen] Side-bar signatures must each match once; found " +
                std::to_string(leftBarMatches.size()) + " and " + std::to_string(rightBarMatches.size()));
            return false;
        }

        // Resolve all signatures before modifying the executable. Otherwise a
        // missing side-bar signature could leave only the FOV patch installed.
        if (!PatchFloatLoadAndReturn(fovMatches[0].address, &g_horizontalFovScale) ||
            !PatchFloatLoadAndReturn(fovMatches[1].address, &g_verticalFovScale))
        {
            sh3e::log::Write("ERROR", "[Widescreen] Could not install the FOV corrections.");
            return false;
        }

        if (!RedirectFloatOperand(leftBarMatches.front(), &g_zeroFloat) ||
            !RedirectFloatOperand(rightBarMatches.front(), &g_zeroFloat))
        {
            sh3e::log::Write("ERROR", "[Widescreen] Could not remove the side-bar offsets.");
            return false;
        }

        std::ostringstream message;
        message << "[Widescreen] 4:3 side bars removed; aspect=" << std::fixed << std::setprecision(4)
                << screen.aspectRatio << ", horizontal scale=" << g_horizontalFovScale;
        sh3e::log::Write("INFO", message.str());
        return true;
    }
}

bool InstallWidescreenPatch(HMODULE executable, const ScreenState& screen)
{
    return InstallWidescreenAspectFix(executable, screen);
}
}
