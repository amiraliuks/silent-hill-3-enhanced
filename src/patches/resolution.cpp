#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sh3e::patches
{
namespace
{
    using DisplayResolutionSetter = int(__cdecl*)(int, int);
    using RenderResourceResolutionSetter = unsigned int(__cdecl*)(int, int);
    DisplayResolutionSetter g_originalDisplayResolutionSetter = nullptr;
    RenderResourceResolutionSetter g_originalRenderResourceResolutionSetter = nullptr;
    int g_displayWidth = 0, g_displayHeight = 0;
    int g_renderWidth = 0, g_renderHeight = 0;
    int g_fmvWidescreenMode = 2;
    float g_fmvOffset1 = 0.0f, g_fmvOffset2 = 0.0f, g_fmvOffset3 = 0.0f;
    float g_zeroFloat = 0.0f;
    LONG g_loggedDisplayOverride = 0, g_loggedRenderOverride = 0, g_loggedRenderDimensions = 0;
    using detail::EncodeRelativeJump;
    using detail::Hex;

    void UpdateFmvOffsets(int renderWidth, int renderHeight)
    {
        if (renderWidth <= 0 || renderHeight <= 0 || g_displayWidth <= 0 || g_displayHeight <= 0)
            return;

        float fmvAspect = 4.0f / 3.0f;
        if (g_fmvWidescreenMode == 1)
            fmvAspect = 964.0f / 622.0f;
        else if (g_fmvWidescreenMode == 2)
            fmvAspect = 16.0f / 9.0f;

        const auto screenWidth = static_cast<float>(g_displayWidth);
        const auto screenHeight = static_cast<float>(g_displayHeight);
        const auto horizontalPadding = (screenWidth - screenHeight * fmvAspect) * 0.5f;
        g_fmvOffset1 = static_cast<float>(renderWidth) * (horizontalPadding / screenWidth);
        g_fmvOffset2 = static_cast<float>(renderWidth) - g_fmvOffset1;
        g_fmvOffset3 = static_cast<float>(renderHeight);
    }

    int __cdecl DisplayResolutionHook(int width, int height)
    {
        if (g_displayWidth > 0 && g_displayHeight > 0)
        {
            width = g_displayWidth;
            height = g_displayHeight;
            if (InterlockedExchange(&g_loggedDisplayOverride, 1) == 0)
            {
                std::ostringstream message;
                message << "[Resolution] Game output/backbuffer request overridden to " << width << 'x' << height;
                sh3e::log::Write("INFO", message.str());
            }
        }
        
        return g_originalDisplayResolutionSetter ? g_originalDisplayResolutionSetter(width, height) : 0;
    }

    unsigned int __cdecl RenderResourceResolutionHook(int width, int height)
    {
        if (g_renderWidth > 0 && g_renderHeight > 0)
        {
            width = g_renderWidth;
            height = g_renderHeight;
            if (InterlockedExchange(&g_loggedRenderOverride, 1) == 0)
            {
                std::ostringstream message;
                message << "[Render resolution] Internal render-target dimensions overridden to " << width << 'x' << height;
                sh3e::log::Write("INFO", message.str());
            }
        }

        if (width > 0 && height > 0)
        {
            UpdateFmvOffsets(width, height);
            if (InterlockedExchange(&g_loggedRenderDimensions, 1) == 0)
            {
                std::ostringstream message;
                message << "[FMV] Internal render-target dimensions observed as " << width << 'x' << height;
                sh3e::log::Write("INFO", message.str());
            }
        }

        return g_originalRenderResourceResolutionSetter ? g_originalRenderResourceResolutionSetter(width, height) : 0;
    }

    bool InstallDisplayResolutionHook(HMODULE executable)
    {
        const auto pattern = sh3e::BytePattern::Parse(
            "8B 0D ?? ?? ?? ?? 8B 44 24 04 3B C8 8B 4C 24 08 75 08 39 0D ?? ?? ?? ?? 74 1D");
        if (!pattern)
            return false;

        const auto matches = sh3e::FindPattern(executable, *pattern);
        if (matches.size() != 1)
        {
            sh3e::log::Write("WARN", "[Resolution] Display-mode setter signature must match exactly once; found " + std::to_string(matches.size()));
            return false;
        }

        // Ghidra confirms the first instruction is the complete 6-byte MOV ECX,[global].
        constexpr std::size_t stolenBytes = 6;
        auto* target = reinterpret_cast<std::uint8_t*>(matches.front().address);
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
        {
            sh3e::log::Write("ERROR", "[Resolution] Could not allocate the display-mode trampoline.");
            return false;
        }

        std::memcpy(trampoline, target, stolenBytes);
        if (!EncodeRelativeJump(trampoline + stolenBytes,
                                reinterpret_cast<std::uintptr_t>(trampoline + stolenBytes),
                                target + stolenBytes))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Resolution] The display-mode trampoline jump is out of range.");
            return false;
        }

        FlushInstructionCache(GetCurrentProcess(), trampoline, stolenBytes + 5);

        std::array<std::uint8_t, stolenBytes> patch{};
        patch.fill(0x90);
        if (!EncodeRelativeJump(patch.data(), reinterpret_cast<std::uintptr_t>(target),
                                reinterpret_cast<const void*>(&DisplayResolutionHook)))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Resolution] The display-mode hook jump is out of range.");
            return false;
        }

        g_originalDisplayResolutionSetter = reinterpret_cast<DisplayResolutionSetter>(trampoline);
        if (!sh3e::WriteMemory(reinterpret_cast<std::uintptr_t>(target), patch.data(), patch.size()))
        {
            g_originalDisplayResolutionSetter = nullptr;
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Resolution] Could not patch the display-mode setter.");
            return false;
        }

        sh3e::log::Write("INFO", "[Resolution] Display-mode setter hook installed at sh3.exe+" + Hex(matches.front().rva));
        return true;
    }

    bool InstallRenderResourceResolutionHook(HMODULE executable)
    {
        const auto pattern = sh3e::BytePattern::Parse("51 A1 ?? ?? ?? ?? 53 8B 5C 24 0C");
        if (!pattern)
            return false;

        const auto matches = sh3e::FindPattern(executable, *pattern);
        if (matches.size() != 1)
        {
            sh3e::log::Write("WARN", "[Render resolution] Resource setter signature must match exactly once; found " + std::to_string(matches.size()));
            return false;
        }

        // Ghidra confirms the first two instructions are PUSH ECX and MOV EAX,[global], six bytes total.
        constexpr std::size_t stolenBytes = 6;
        auto* target = reinterpret_cast<std::uint8_t*>(matches.front().address);
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
        {
            sh3e::log::Write("ERROR", "[Render resolution] Could not allocate the resource-resolution trampoline.");
            return false;
        }

        std::memcpy(trampoline, target, stolenBytes);
        if (!EncodeRelativeJump(trampoline + stolenBytes,
                                reinterpret_cast<std::uintptr_t>(trampoline + stolenBytes),
                                target + stolenBytes))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Render resolution] The resource-resolution trampoline jump is out of range.");
            return false;
        }

        FlushInstructionCache(GetCurrentProcess(), trampoline, stolenBytes + 5);

        std::array<std::uint8_t, stolenBytes> patch{};
        patch.fill(0x90);
        if (!EncodeRelativeJump(patch.data(), reinterpret_cast<std::uintptr_t>(target),
                                reinterpret_cast<const void*>(&RenderResourceResolutionHook)))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Render resolution] The resource-resolution hook jump is out of range.");
            return false;
        }

        g_originalRenderResourceResolutionSetter = reinterpret_cast<RenderResourceResolutionSetter>(trampoline);
        if (!sh3e::WriteMemory(reinterpret_cast<std::uintptr_t>(target), patch.data(), patch.size()))
        {
            g_originalRenderResourceResolutionSetter = nullptr;
            VirtualFree(trampoline, 0, MEM_RELEASE);
            sh3e::log::Write("ERROR", "[Render resolution] Could not patch the resource-resolution setter.");
            return false;
        }

        sh3e::log::Write("INFO", "[Render resolution] Resource setter hook installed at sh3.exe+" + Hex(matches.front().rva));
        return true;
    }

    bool RedirectFloatOperandToZero(const sh3e::PatternMatch& match)
    {
        const auto pointer = reinterpret_cast<std::uintptr_t>(&g_zeroFloat);
        if (pointer > UINT32_MAX)
            return false;
        const auto pointer32 = static_cast<std::uint32_t>(pointer);
        return sh3e::WriteMemory(match.address + 2, &pointer32, sizeof(pointer32));
    }

    bool RedirectFloatOperand(const sh3e::PatternMatch& match, const float* value)
    {
        const auto pointer = reinterpret_cast<std::uintptr_t>(value);
        if (pointer > UINT32_MAX)
            return false;
        const auto pointer32 = static_cast<std::uint32_t>(pointer);
        return sh3e::WriteMemory(match.address + 2, &pointer32, sizeof(pointer32));
    }

    bool InstallFmvAspectFix(HMODULE executable, const sh3e::ScreenState& screen)
    {
        if (screen.aspectRatio <= (4.0f / 3.0f))
            return true;

        const auto offset1Pattern = sh3e::BytePattern::Parse("D9 05 ?? ?? ?? ?? D8 25 ?? ?? ?? ?? 83 C8 FF");
        const auto offset2Pattern = sh3e::BytePattern::Parse("D9 05 ?? ?? ?? ?? C7 44 24 0C 00 00 00 00 D8 25");
        const auto fullscreenOffsetPattern = sh3e::BytePattern::Parse("D9 05 ?? ?? ?? ?? 89 44 24 14 D8 25 ?? ?? ?? ?? C7");
        const auto fullscreenHeightPattern = sh3e::BytePattern::Parse("D9 05 ?? ?? ?? ?? D8 25 ?? ?? ?? ?? D9 C3 D9 5C");
        if (!offset1Pattern || !offset2Pattern || !fullscreenOffsetPattern || !fullscreenHeightPattern)
            return false;

        const auto offset1Matches = sh3e::FindPattern(executable, *offset1Pattern);
        const auto offset2Matches = sh3e::FindPattern(executable, *offset2Pattern);
        if (offset1Matches.size() != 1 || offset2Matches.size() != 1)
        {
            sh3e::log::Write("WARN", "[FMV] Aspect signatures must each match once; found " +
                std::to_string(offset1Matches.size()) + " and " + std::to_string(offset2Matches.size()));
            return false;
        }

        if (!RedirectFloatOperand(offset1Matches.front(), &g_fmvOffset1) ||
            !RedirectFloatOperand(offset2Matches.front(), &g_fmvOffset2))
        {
            sh3e::log::Write("ERROR", "[FMV] Could not install the 4:3 crop offsets.");
            return false;
        }

        // Keep the fullscreen FMV bounds consistent with the selected aspect
        // ratio in every widescreen mode, including the original 4:3 mode.
        {
            const auto fullscreenOffsetMatches = sh3e::FindPattern(executable, *fullscreenOffsetPattern);
            const auto fullscreenHeightMatches = sh3e::FindPattern(executable, *fullscreenHeightPattern);
            if (fullscreenOffsetMatches.size() != 1 || fullscreenHeightMatches.size() != 1 ||
                !RedirectFloatOperandToZero(fullscreenOffsetMatches.front()) ||
                !RedirectFloatOperand(fullscreenHeightMatches.front(), &g_fmvOffset3))
            {
                sh3e::log::Write("ERROR", "[FMV] Could not install the fullscreen frame bounds.");
                return false;
            }
        }

        sh3e::log::Write("INFO", "[FMV] Aspect correction installed; mode=" + std::to_string(g_fmvWidescreenMode));
        return true;
    }
}

bool InstallResolutionPatches(HMODULE executable, const Config& config, const ScreenState& screen)
{
    g_displayWidth = screen.width;
    g_displayHeight = screen.height;
    g_renderWidth = screen.renderWidth;
    g_renderHeight = screen.renderHeight;
    g_fmvWidescreenMode = config.fmvWidescreenMode;

    const auto displayHookInstalled = screen.overrideOutputResolution
        ? InstallDisplayResolutionHook(executable)
        : false;
    const auto renderHookInstalled = InstallRenderResourceResolutionHook(executable);
    const auto fmvAspectFixInstalled = InstallFmvAspectFix(executable, screen);

    if (g_renderWidth > 0 && g_renderHeight > 0 && !renderHookInstalled)
        log::Write("WARN", "[Render resolution] Internal render dimensions are not being overridden.");
    else if (g_renderWidth == 0 || g_renderHeight == 0)
        log::Write("INFO", "[Render resolution] Internal render-size override disabled; using the game's selected dimensions.");
    if (!renderHookInstalled)
        log::Write("WARN", "[FMV] Could not observe internal render dimensions; video scaling may be unavailable.");
    if (screen.overrideOutputResolution && !displayHookInstalled)
        log::Write("WARN", "[Resolution] Output dimensions are not being overridden.");
    else if (!screen.overrideOutputResolution)
        log::Write("INFO", "[Resolution] Output-mode override disabled; using the game's selected resolution.");
    if (screen.aspectRatio > (4.0f / 3.0f) && !fmvAspectFixInstalled)
        log::Write("WARN", "[FMV] Aspect correction was not installed.");

    return (!screen.overrideOutputResolution || displayHookInstalled) && renderHookInstalled && fmvAspectFixInstalled;
}
}
