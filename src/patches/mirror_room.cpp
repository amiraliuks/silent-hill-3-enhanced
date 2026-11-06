#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

// The game is 32-bit, and these names are referenced directly by the naked
// x86 detours below.
extern "C"
{
    void* g_mirrorRoomStateTrampoline = nullptr;
    void* g_mirrorRoomLimiterTrampoline = nullptr;
    volatile LONG g_mirrorRoomActive = 0;
    volatile LONG* g_mirrorRoomFrameCap = nullptr;
}

namespace sh3e::patches
{
namespace
{
    using detail::EncodeRelativeJump;
    using detail::Hex;

    constexpr std::size_t kStatePatchLength = 5;
    constexpr std::size_t kLimiterPatchLength = 6;

    void ObserveRoomState(LONG fileId, const void* state)
    {
        if (fileId < 0 || fileId >= 16 || !state)
            return;

        // Ghidra shows the file name pointer at record offset +8. SH3ReadFile
        // prints this same field with a %s format.
        const auto* name = *reinterpret_cast<const char* const*>(static_cast<const std::uint8_t*>(state) + 8);
        if (!name)
            return;

        const char* basename = name;
        std::size_t length = 0;
        for (; length < 260 && name[length] != '\0'; ++length)
        {
            if (name[length] == '/' || name[length] == '\\')
                basename = name + length + 1;
        }

        if (length == 260)
            return;

        LONG nextState = -1;
        if (_stricmp(basename, "hu32.cam") == 0)
            nextState = 1;
        else if (_stricmp(basename, "hu21.cam") == 0 || _stricmp(basename, "sys_title.tex") == 0)
            nextState = 0;

        if (nextState >= 0)
        {
            const auto previous = InterlockedExchange(&g_mirrorRoomActive, nextState);
            if (previous != nextState)
            {
                sh3e::log::Write("INFO", nextState
                    ? "[Mirror Room] Storeroom camera resource detected; room-specific cap is active."
                    : "[Mirror Room] Storeroom camera resource cleared; normal frame cap restored.");
            }
        }
    }

    __declspec(naked) void MirrorRoomStateHook()
    {
        __asm
        {
            pushfd
            pushad

            // The original helper is cdecl and takes the file ID at entry
            // [esp+4]. Save it across the trampoline call.
            mov eax, dword ptr [esp + 28h]
            push eax
            call dword ptr [g_mirrorRoomStateTrampoline]
            add esp, 4

            // Preserve the helper's returned state pointer as the hook's EAX
            // return value while notifying the room tracker.
            mov dword ptr [esp + 1Ch], eax
            push eax
            mov eax, dword ptr [esp + 2Ch]
            push eax
            call ObserveRoomState
            add esp, 8

            popad
            popfd
            ret
        }
    }

    __declspec(naked) void MirrorRoomLimiterHook()
    {
        __asm
        {
            // Keep a per-call marker on the stack so the hook is safe if the
            // limiter is re-entered. This function has no arguments.
            push 0
            pushfd
            pushad

            cmp dword ptr [g_mirrorRoomActive], 0
            je call_original_limiter
            mov edx, dword ptr [g_mirrorRoomFrameCap]
            test edx, edx
            jz call_original_limiter
            cmp dword ptr [edx], 60
            jne call_original_limiter
            mov dword ptr [edx], 30
            mov dword ptr [esp + 24h], 1

        call_original_limiter:
            popad
            popfd
            call dword ptr [g_mirrorRoomLimiterTrampoline]

            pushfd
            pushad
            cmp dword ptr [esp + 24h], 0
            je restore_done
            mov edx, dword ptr [g_mirrorRoomFrameCap]
            test edx, edx
            jz restore_done
            mov dword ptr [edx], 60

        restore_done:
            popad
            popfd
            add esp, 4
            ret
        }
    }

    std::uint8_t* MakeTrampoline(std::uintptr_t target, std::size_t patchLength)
    {
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
            return nullptr;

        std::memcpy(trampoline, reinterpret_cast<const void*>(target), patchLength);
        if (!EncodeRelativeJump(trampoline + patchLength,
                reinterpret_cast<std::uintptr_t>(trampoline + patchLength),
                reinterpret_cast<const void*>(target + patchLength)))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return nullptr;
        }

        FlushInstructionCache(GetCurrentProcess(), trampoline, patchLength + 5);
        return trampoline;
    }

    bool MakeEntryPatch(std::uintptr_t target, const void* hook, std::size_t length,
        std::array<std::uint8_t, 8>& patch)
    {
        patch.fill(0x90);
        return length >= 5 && length <= patch.size() &&
            EncodeRelativeJump(patch.data(), target, hook);
    }
}

bool InstallMirrorRoomPatch(HMODULE executable, bool enabled, int frameRateMode)
{
    if (!enabled)
        return true;
    if (!executable || frameRateMode != 60)
    {
        sh3e::log::Write("WARN", "[Mirror Room] Requires the 60 FPS mode; room-specific cap was not installed.");
        return false;
    }

    const auto statePattern = sh3e::BytePattern::Parse(
        "57 8B 7C 24 08 85 FF 7C ?? 83 FF 10 7D ?? 56 8B 35");
    const auto limiterPattern = sh3e::BytePattern::Parse(
        "55 8B EC 83 E4 F8 83 EC 38 A1 ?? ?? ?? ?? 53 55 33 ED 3B C5 56 57 0F 84 ?? ?? ?? ?? 8B 1D ?? ?? ?? ??");
    if (!statePattern || !limiterPattern)
        return false;

    const auto stateMatches = sh3e::FindPattern(executable, *statePattern);
    const auto limiterMatches = sh3e::FindPattern(executable, *limiterPattern);
    if (stateMatches.size() != 1 || limiterMatches.size() != 1)
    {
        sh3e::log::Write("WARN", "[Mirror Room] Expected one file-state getter and frame-limiter signature; found " +
            std::to_string(stateMatches.size()) + " and " + std::to_string(limiterMatches.size()) + ".");
        return false;
    }

    // At limiter +0x1e, Ghidra confirms `mov ebx,[absolute address]` reads
    // DAT_0072c790, the FPS cap (0 means default 60).
    std::uint32_t capAddress = 0;
    std::memcpy(&capAddress, reinterpret_cast<const void*>(limiterMatches.front().address + 0x1e), sizeof(capAddress));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const std::uint8_t*>(executable) + dos->e_lfanew);
    const auto imageBegin = reinterpret_cast<std::uintptr_t>(executable);
    const auto imageEnd = imageBegin + nt->OptionalHeader.SizeOfImage;
    if (capAddress < imageBegin || capAddress + sizeof(LONG) > imageEnd || (capAddress % alignof(LONG)) != 0)
    {
        sh3e::log::Write("WARN", "[Mirror Room] Frame-cap global operand is outside the executable image or unaligned.");
        return false;
    }

    const auto stateAddress = stateMatches.front().address;
    const auto limiterAddress = limiterMatches.front().address;
    auto* stateTrampoline = MakeTrampoline(stateAddress, kStatePatchLength);
    auto* limiterTrampoline = MakeTrampoline(limiterAddress, kLimiterPatchLength);
    if (!stateTrampoline || !limiterTrampoline)
    {
        if (stateTrampoline) VirtualFree(stateTrampoline, 0, MEM_RELEASE);
        if (limiterTrampoline) VirtualFree(limiterTrampoline, 0, MEM_RELEASE);
        sh3e::log::Write("ERROR", "[Mirror Room] Could not allocate detour trampolines.");
        return false;
    }

    std::array<std::uint8_t, 8> statePatch{};
    std::array<std::uint8_t, 8> limiterPatch{};
    if (!MakeEntryPatch(stateAddress, reinterpret_cast<const void*>(&MirrorRoomStateHook), kStatePatchLength, statePatch) ||
        !MakeEntryPatch(limiterAddress, reinterpret_cast<const void*>(&MirrorRoomLimiterHook), kLimiterPatchLength, limiterPatch))
    {
        VirtualFree(stateTrampoline, 0, MEM_RELEASE);
        VirtualFree(limiterTrampoline, 0, MEM_RELEASE);
        return false;
    }

    std::array<std::uint8_t, kStatePatchLength> originalState{};
    std::array<std::uint8_t, kLimiterPatchLength> originalLimiter{};
    std::memcpy(originalState.data(), reinterpret_cast<const void*>(stateAddress), originalState.size());
    std::memcpy(originalLimiter.data(), reinterpret_cast<const void*>(limiterAddress), originalLimiter.size());

    g_mirrorRoomStateTrampoline = stateTrampoline;
    g_mirrorRoomLimiterTrampoline = limiterTrampoline;
    g_mirrorRoomFrameCap = reinterpret_cast<volatile LONG*>(static_cast<std::uintptr_t>(capAddress));
    InterlockedExchange(&g_mirrorRoomActive, 0);

    if (!sh3e::WriteMemory(stateAddress, statePatch.data(), kStatePatchLength))
    {
        g_mirrorRoomStateTrampoline = nullptr;
        g_mirrorRoomLimiterTrampoline = nullptr;
        g_mirrorRoomFrameCap = nullptr;
        VirtualFree(stateTrampoline, 0, MEM_RELEASE);
        VirtualFree(limiterTrampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!sh3e::WriteMemory(limiterAddress, limiterPatch.data(), kLimiterPatchLength))
    {
        sh3e::WriteMemory(stateAddress, originalState.data(), originalState.size());
        g_mirrorRoomStateTrampoline = nullptr;
        g_mirrorRoomLimiterTrampoline = nullptr;
        g_mirrorRoomFrameCap = nullptr;
        VirtualFree(stateTrampoline, 0, MEM_RELEASE);
        VirtualFree(limiterTrampoline, 0, MEM_RELEASE);
        return false;
    }

    sh3e::log::Write("INFO", "[Mirror Room] Resource-state and zero-argument frame-limiter hooks installed; cap global at sh3.exe+" +
        Hex(capAddress - reinterpret_cast<std::uintptr_t>(executable)) + ". Enabled only while hu32.cam is active.");
    return true;
}
}
