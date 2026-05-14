#pragma once
#include <windows.h>

// ============================================================
//  Prevent SH3 from forcing safe mode settings after a crash
// ============================================================

namespace Patches
{
    inline void FixDispIni()
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* slash = strrchr(exePath, '\\');
        if (!slash) return;
        *(slash + 1) = '\0'; // Keep only the game directory

        char dispIni[MAX_PATH] = {};
        strcpy_s(dispIni, exePath);
        strcat_s(dispIni, "savedata\\disp.ini");

        // Clear the SafeMode flag
        WritePrivateProfileStringA("Display", "SafeMode", "0", dispIni);
    }
}