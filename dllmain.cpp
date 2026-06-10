#include "patches/DispIni.h"
#include <windows.h>
#include "core/Config.h"
#include "core/Log.h"
#include "patches/Registry.h"
#include "patches/SafeMode.h"
#include "patches/FPS.h"

// ============================================================
// dllmain.cpp
//
// Main ASI entry point.
// Loaded by Ultimate ASI Loader through LoadLibrary,
// after which Windows calls DllMain().
// ============================================================

Config g_cfg;

static void Init()
{
    // Load config
    g_cfg.Load(L"SH3Enhanced.ini");

    // Initialize logging
    int logMode = g_cfg.GetInt(L"Log", L"Log", 0);
    Log::Init(static_cast<Log::Mode>(logMode));

    LOG("SH3Enhanced starting...");
    LOG("Build date: " __DATE__ " " __TIME__);

    // Registry compatibility fixes
    Patches::ApplyRegistryRedirect();

    // Disable safe mode reset
    if (g_cfg.GetBool(L"SilentHill3", L"DisableSafeMode", true))
        Patches::ApplySafeMode();

    // Framerate / timing (0 = original 30fps, 1 = 60fps, 2 = unlocked)
    Patches::ApplyFPS(g_cfg.GetInt(L"Graphics", L"FPSMode", 1));

    // TODO: Add more patches here

    LOG("SH3Enhanced initialized.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // SH3 checks registry keys very early, so this needs to happen immediately
        Patches::WriteRegistryKeysNow();
        Patches::FixDispIni();

        // Run remaining initialization outside DllMain
        CreateThread(
            nullptr, 0,
            [](LPVOID) -> DWORD { Init(); return 0; },
            nullptr, 0, nullptr
        );
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log::Shutdown();
    }

    return TRUE;
}
