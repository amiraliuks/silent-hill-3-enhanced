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
    static uint32_t g_fpsDesierd = 60;

    // Known addresses (all verified in x32dbg)
    static const uintptr_t ADDR_TIMER_INIT = 0x0041B270; // timer init func
    static const uintptr_t ADDR_SYNC_CALL = 0x0041992D; // 5-byte sync call
    static const uintptr_t ADDR_BEFORE_SCENE = 0x0041B5F0; // BeginScene sync hook
    static const uintptr_t ADDR_EVENT_RATE = 0x005F1A73; // event rate scaler
    static const uintptr_t ADDR_SLOWDOWN = 0x005F15E0; // slowdown coeff

    // Game data addresses
    static const uintptr_t ADDR_REFRESH_RATE = 0x0072C790; // monitor refresh rate
    static const uintptr_t ADDR_RENDERTIME = 0x0070E67AC; // gRendertimeScaled
    static const uintptr_t ADDR_FPS_SCALED = 0x0070E67C4; // gFPSScaled
    static const uintptr_t ADDR_EVENT_INT = 0x0070E67C0; // event rate int
    static const uintptr_t ADDR_EVENT_FLOAT = 0x0070E67A8; // event rate float

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

    // Replaces the game's timer init at 0x41B270
    // Reads monitor refresh rate from 0x72C790 if the game already grabbed it
    static int __cdecl Hook_TimerInit()
    {
        g_usePerfCounter = QueryPerformanceFrequency(&g_perfFreq) != 0;
        if (g_usePerfCounter)
            QueryPerformanceCounter(&g_perfStart);

        // Try to use monitor refresh rate
        uint32_t refreshRate = *reinterpret_cast<uint32_t*>(ADDR_REFRESH_RATE);
        if (refreshRate > 0 && refreshRate <= 360)
            g_fpsDesierd = refreshRate;

        LOG("FPS: Timer init hooked, target FPS = %u", g_fpsDesierd);
        return 0;
    }

    // Runs before every BeginScene, this is where we do the actual frame pacing
    // Sleeps in 1ms intervals when enough time remains,
    // then spin-waits for final precision timing.
    static const uint32_t WAIT_THRESH_MS = 10;

    static void __cdecl Hook_SyncBeforeScene()
    {
        UpdateTdiff();

        static uint32_t nmsRem = 0;
        static uint32_t prevTime = 0;

        if (!prevTime)
            prevTime = GetTicks();

        uint32_t tstart = GetTicks();

        int sleepCount = 0;
        while (!sleepCount)
        {
            // Advance target time by one frame interval
            prevTime += (nmsRem + 1000) / g_fpsDesierd;
            nmsRem = (nmsRem + 1000) % g_fpsDesierd;

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

    // Without this the game logic runs at double speed at 60fps
    // scales the event rate values the engine reads every frame
    // 17.0f is ~1000ms/60fps, keeps movement/physics correct
    static void __cdecl Hook_ScaleEventRate()
    {
        float rate = 1.0f;

        // Only scale if frame took longer than ~20fps threshold
        if (g_tdiff > 1000 / 50)
            rate = (float)g_tdiff / 17.0f;

        *reinterpret_cast<uint32_t*>(ADDR_EVENT_INT) = (uint32_t)rate;
        *reinterpret_cast<float*>   (ADDR_EVENT_FLOAT) = rate;
        *reinterpret_cast<float*>   (ADDR_RENDERTIME) = rate * 0.016666666f;
        *reinterpret_cast<float*>   (ADDR_FPS_SCALED) = 60.0f / rate;
    }

    // Disable original slowdown coefficient logic
    static void __cdecl Hook_Slowdown() {}

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

        // Set target FPS
        if (mode == 1)
            g_fpsDesierd = 60;
        else if (mode == 2)
            g_fpsDesierd = 0; // unlocked — Hook_SyncBeforeScene won't limit

        LOG("FPS: Applying mode %d (target: %u fps)...", mode, g_fpsDesierd);

        // Init MinHook
        if (MH_Initialize() != MH_OK)
        {
            LOG("FPS: MinHook init failed");
            return;
        }

        // 1. Hook timer init — sets up our high-res timer
        HookFunction(ADDR_TIMER_INIT, (void*)Hook_TimerInit, "TimerInit");

        // 2. NOP the original 5-byte sync call at 0x41992D
        //    This disables the game's built-in frame pacing
        Memory::NOP(ADDR_SYNC_CALL, 5);
        LOG("FPS: NOPed sync call at 0x%08X", ADDR_SYNC_CALL);

        // 3. Hook BeginScene sync — our frame pacer runs here
        HookFunction(ADDR_BEFORE_SCENE, (void*)Hook_SyncBeforeScene, "SyncBeforeScene");

        // 4. Hook event rate scaler — fixes game speed at 60fps
        HookFunction(ADDR_EVENT_RATE, (void*)Hook_ScaleEventRate, "ScaleEventRate");

        // 5. Replace slowdown coefficient with empty function
        HookFunction(ADDR_SLOWDOWN, (void*)Hook_Slowdown, "Slowdown");

        LOG("FPS: All hooks installed");
    }
}