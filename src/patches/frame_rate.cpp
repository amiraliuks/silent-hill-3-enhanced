#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace sh3e::patches
{
namespace
{
    using detail::EncodeRelativeJump;
    using detail::Hex;

    bool InstallFrameRateFix(HMODULE executable, int mode)
    {
        if (mode == 60)
        {
            const auto pattern = sh3e::BytePattern::Parse("4A 03 C8 A3 ?? ?? ?? ?? 89 15");
            if (!pattern)
                return false;
            const auto matches = sh3e::FindPattern(executable, *pattern);
            if (matches.size() != 1)
            {
                sh3e::log::Write("WARN", "[Frame rate] 60 FPS signature must match once; found " + std::to_string(matches.size()));
                return false;
            }

            const std::uint8_t sixtyFpsOpcode = 0x42; // The verified 60 FPS timing correction.
            if (!sh3e::WriteMemory(matches.front().address, &sixtyFpsOpcode, sizeof(sixtyFpsOpcode)))
            {
                sh3e::log::Write("ERROR", "[Frame rate] Could not apply the 60 FPS timing correction.");
                return false;
            }

            sh3e::log::Write("INFO", "[Frame rate] 60 FPS timing correction installed at sh3.exe+" + Hex(matches.front().rva));
            return true;
        }

        if (mode != 0 && mode != 30)
            return false;

        const auto statePattern = sh3e::BytePattern::Parse("8B 44 24 04 A3 ?? ?? ?? ?? 33 C0");
        const auto frameRateStatePattern = sh3e::BytePattern::Parse("A1 ?? ?? ?? ?? 53 55 33 ED");
        if (!statePattern || !frameRateStatePattern)
            return false;

        const auto stateMatches = sh3e::FindPattern(executable, *statePattern);
        const auto frameRateStateMatches = sh3e::FindPattern(executable, *frameRateStatePattern);
        std::uintptr_t sharedStateAddress = 0;
        sh3e::PatternMatch frameRateFunction{};
        std::size_t overlaps = 0;
        for (const auto& stateMatch : stateMatches)
        {
            std::uint32_t stateAddress = 0;
            std::memcpy(&stateAddress, reinterpret_cast<const void*>(stateMatch.address + 5), sizeof(stateAddress));
            for (const auto& functionMatch : frameRateStateMatches)
            {
                std::uint32_t functionStateAddress = 0;
                std::memcpy(&functionStateAddress, reinterpret_cast<const void*>(functionMatch.address + 1), sizeof(functionStateAddress));
                if (stateAddress == functionStateAddress)
                {
                    sharedStateAddress = stateAddress;
                    frameRateFunction = functionMatch;
                    ++overlaps;
                }
            }
        }

        if (overlaps != 1)
        {
            sh3e::log::Write("WARN", "[Frame rate] Could not uniquely identify the timing-state function; shared references=" + std::to_string(overlaps));
            return false;
        }

        auto* target = reinterpret_cast<std::uint8_t*>(frameRateFunction.address);
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
            return false;

        std::size_t jumpOffset = 0;
        if (mode == 30)
        {
            // Preserve the game's unlocked value (0); cap other modes at 30 FPS (2).
            trampoline[0] = 0xA1;
            std::memcpy(trampoline + 1, &sharedStateAddress, sizeof(std::uint32_t));
            trampoline[5] = 0x85; trampoline[6] = 0xC0; // test eax,eax
            trampoline[7] = 0x74; trampoline[8] = 0x05; // je over mov eax,2
            trampoline[9] = 0xB8;
            const std::uint32_t thirtyFps = 2;
            std::memcpy(trampoline + 10, &thirtyFps, sizeof(thirtyFps));
            jumpOffset = 14;
        }
        else
        {
            trampoline[0] = 0x33; trampoline[1] = 0xC0; // xor eax,eax (unlocked)
            jumpOffset = 2;
        }

        if (!EncodeRelativeJump(trampoline + jumpOffset,
                                reinterpret_cast<std::uintptr_t>(trampoline + jumpOffset),
                                target + 5))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        FlushInstructionCache(GetCurrentProcess(), trampoline, jumpOffset + 5);

        std::array<std::uint8_t, 5> patch{};
        if (!EncodeRelativeJump(patch.data(), reinterpret_cast<std::uintptr_t>(target), trampoline) ||
            !sh3e::WriteMemory(frameRateFunction.address, patch.data(), patch.size()))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        const auto label = mode == 30 ? "30 FPS" : "unlocked";
        sh3e::log::Write("INFO", std::string("[Frame rate] ") + label + " mode installed at sh3.exe+" + Hex(frameRateFunction.rva));
        return true;
    }
}

bool InstallFrameRatePatch(HMODULE executable, int mode)
{
    return InstallFrameRateFix(executable, mode);
}
}
