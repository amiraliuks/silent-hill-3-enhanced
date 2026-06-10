#include "FPS.h"
#include "../core/Memory.h"
#include "../core/Log.h"
#include "../thirdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>

#pragma comment(lib, "winmm.lib")

// ============================================================
// Patches/FPS.cpp
//
// 60fps / framerate fix for Silent Hill 3 PC.
//
// Original research done in x32dbg and cross-referenced
// with sh3proxy. Several timing-related addresses differed
// from public SH3 databases and had to be located manually.
// 
// Verified against:
//  sh3.exe (9/3/2003)
// ============================================================

namespace Patches
{
    static uint32_t g_fpsDesired = 60;

    // Known addresses (all verified in x32dbg)
    static const uintptr_t ADDR_SYNC_CALL = 0x0041992D; // 5-byte sync call
    static const uintptr_t ADDR_BEFORE_SCENE = 0x0041B5F0; // BeginScene sync hook
    static const uintptr_t ADDR_SLOWDOWN = 0x005F15E0; // slowdown fn entry (also hosts event-rate scaling)

    // Game data addresses
    static const uintptr_t ADDR_RENDERTIME = 0x070E67AC; // gRendertimeScaled
    static const uintptr_t ADDR_FPS_SCALED = 0x070E67C4; // gFPSScaled
    static const uintptr_t ADDR_EVENT_INT = 0x070E67C0; // event rate int
    static const uintptr_t ADDR_EVENT_FLOAT = 0x070E67A8; // event rate float

    // High resolution timer, falls back to timeGetTime if QPC isn't available
    static LARGE_INTEGER g_perfFreq;
    static LARGE_INTEGER g_perfStart;
    static bool          g_usePerfCounter = false;

    static uint32_t GetTicks()
    {
        if (g_usePerfCounter)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            now.QuadPart -= g_perfStart.QuadPart;
            now.QuadPart *= 1000;
            now.QuadPart /= g_perfFreq.QuadPart;
            return (uint32_t)now.QuadPart;
        }
        return timeGetTime();
    }

    // Frame delta (used for event rate scaling)
    static uint32_t g_tdiff = 0;

    static void UpdateTdiff()
    {
        static uint32_t prev = 0;
        uint32_t now = GetTicks();
        g_tdiff = now - prev;
        prev = now;
    }

    // QPC timer setup. Used to hook the game's timer init at 0x41B270 for this,
    // but that clobbered the original and forced the target to the refresh rate.
    static void InitTimer()
    {
        g_usePerfCounter = QueryPerformanceFrequency(&g_perfFreq) != 0;
        if (g_usePerfCounter)
            QueryPerformanceCounter(&g_perfStart);
    }

    // Runs before every BeginScene, this is where we do the actual frame pacing
    // Sleeps in 1ms intervals when enough time remains,
    // then spin-waits for final precision timing.
    static const uint32_t WAIT_THRESH_MS = 10;

    static void __cdecl Hook_SyncBeforeScene()
    {
        UpdateTdiff();

        // Unlocked: don't cap (also avoids the divide-by-zero below)
        if (g_fpsDesired == 0)
            return;

        static uint32_t nmsRem = 0;
        static uint32_t prevTime = 0;

        if (!prevTime)
            prevTime = GetTicks();

        uint32_t tstart = GetTicks();

        int sleepCount = 0;
        while (!sleepCount)
        {
            // Advance target time by one frame interval
            prevTime += (nmsRem + 1000) / g_fpsDesired;
            nmsRem = (nmsRem + 1000) % g_fpsDesired;

            if (prevTime > tstart)
                sleepCount++;

            // Sleep briefly if we have time to spare
            if (prevTime >= tstart + WAIT_THRESH_MS)
                Sleep(1);

            // Spin for remaining time
            while (prevTime > GetTicks())
                ;

            tstart = GetTicks();
        }
    }

    // Hooked at the slowdown fn entry. Kills the game's slowdown logic and
    // rescales the event-rate globals each frame, else logic runs ~2x at 60fps.
    // 17.0f is ~1000ms/60fps. (Scaling was a separate hook at 0x5F1A73, but
    // that's mid-function not an entry, so it crashed.)
    static void __cdecl Hook_FrameTiming()
    {
        float rate = 1.0f;

        // Only scale if the frame took longer than ~20ms (i.e. below 50fps)
        if (g_tdiff > 1000 / 50)
            rate = (float)g_tdiff / 17.0f;

        *reinterpret_cast<uint32_t*>(ADDR_EVENT_INT) = (uint32_t)rate;
        *reinterpret_cast<float*>   (ADDR_EVENT_FLOAT) = rate;
        *reinterpret_cast<float*>   (ADDR_RENDERTIME) = rate * 0.016666666f;
        *reinterpret_cast<float*>   (ADDR_FPS_SCALED) = 60.0f / rate;
    }

    // Install a function replacement hook using MinHook
    static bool HookFunction(uintptr_t target, void* hook, const char* name)
    {
        if (MH_CreateHook(reinterpret_cast<void*>(target),
            hook, nullptr) != MH_OK)
        {
            LOG("FPS: Failed to create hook for %s", name);
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK)
        {
            LOG("FPS: Failed to enable hook for %s", name);
            return false;
        }
        LOG("FPS: Hooked %s at 0x%08X", name, target);
        return true;
    }

    // Public entry point
    void ApplyFPS(int mode)
    {
        if (mode == 0)
        {
            LOG("FPS: Mode 0, original framerate unchanged");
            return;
        }

        // 60 is the only rate the scaling is tuned for; mode 2 leaves it uncapped
        if (mode == 1)
            g_fpsDesired = 60;
        else if (mode == 2)
            g_fpsDesired = 0; // unlocked — Hook_SyncBeforeScene won't cap
        else
        {
            LOG("FPS: Unknown mode %d, original framerate unchanged", mode);
            return;
        }

        LOG("FPS: Applying mode %d (target: %u fps)...", mode, g_fpsDesired);

        // Init our timer before any hook uses it
        InitTimer();

        // Init MinHook
        if (MH_Initialize() != MH_OK)
        {
            LOG("FPS: MinHook init failed");
            return;
        }

        // 1. NOP the original 5-byte sync call at 0x41992D
        //    This disables the game's built-in frame pacing
        Memory::NOP(ADDR_SYNC_CALL, 5);
        LOG("FPS: NOPed sync call at 0x%08X", ADDR_SYNC_CALL);

        // 2. Hook BeginScene sync — our frame pacer runs here
        HookFunction(ADDR_BEFORE_SCENE, (void*)Hook_SyncBeforeScene, "SyncBeforeScene");

        // 3. Replace slowdown fn entry — disables slowdown + does event-rate scaling
        HookFunction(ADDR_SLOWDOWN, (void*)Hook_FrameTiming, "FrameTiming");

        LOG("FPS: All hooks installed");
    }
}