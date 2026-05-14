#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>

// ============================================================
// Log.h
//
// Simple logging helper used by the patch system.
//
// Mainly useful for debugging failed pattern scans or verifying
// that hooks were applied at the ecxpected addresses.
// 
// Logging modes (set in the INI):
//  0 = disabled
//  1 = write to SH3Enhanced.log
//  2 = output to a console window (useful during development)
// ============================================================

namespace Log
{
    enum class Mode { Off = 0, File = 1, Console = 2 };

    inline Mode   g_mode = Mode::Off;
    inline FILE* g_file = nullptr;

    inline void Init(Mode mode)
    {
        g_mode = mode;

        if (mode == Mode::Console)
        {
            AllocConsole();
            SetConsoleTitleW(L"SH3Enhanced Debug");
            freopen_s(&g_file, "CONOUT$", "w", stdout);
        }
        else if (mode == Mode::File)
        {
            fopen_s(&g_file, "SH3Enhanced.log", "w");
        }
    }

    inline void Write(const char* fmt, ...)
    {
        if (g_mode == Mode::Off || !g_file) return;

        va_list args;
        va_start(args, fmt);
        vfprintf(g_file, fmt, args);
        fprintf(g_file, "\n");
        fflush(g_file);
        va_end(args);
    }

    // Helper for reporting patch scan results
    // address == 0 means the pattern was not found
    inline void PatchResult(const char* name, uintptr_t address)
    {
        if (address)
            Write("[OK]   %-40s --> 0x%08X", name, address);
        else
            Write("[FAIL] %-40s --> pattern not found", name);
    }

    inline void Shutdown()
    {
        if (g_file && g_mode == Mode::File)
            fclose(g_file);
    }
}

// Shorter logging macro so we don't have to type Log::Write everywhere
#define LOG(fmt, ...) Log::Write(fmt, ##__VA_ARGS__)