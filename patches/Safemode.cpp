#include "SafeMode.h"
#include "../core/Memory.h"
#include "../core/Scanner.h"
#include "../core/Log.h"

// ============================================================
// Prevent SH3 from forcing safe mode after crashes.
// Read FINDINGS.md for details on how this works.
// ============================================================

namespace Patches
{
    void ApplySafeMode()
    {
        LOG("SafeMode: Scanning for safe mode checks...");

        // Disable crash flag write
        {
            uintptr_t addr = Scanner::ScanExe(
                "\xC6\x05\x00\x00\x00\x00\x01\x83\xC4",
                "xx????xxx"
            );

            if (addr)
            {
                Memory::NOP(addr, 7);
                Log::PatchResult("SafeMode_Write", addr);
            }
            else
                Log::PatchResult("SafeMode_Write", 0);
        }

        // Disable safe mode reset branch
        {
            uintptr_t addr = Scanner::ScanExe(
                "\x80\x3D\x00\x00\x00\x00\x01\x74",
                "xx????xx"
            );

            if (addr)
            {
                Memory::NOP(addr + 7, 2);
                Log::PatchResult("SafeMode_ReadBranch", addr + 7);
            }
            else
                Log::PatchResult("SafeMode_ReadBranch", 0);
        }

        // Prevent SafeMode=1 from being written to disp.ini
        {
            uintptr_t addr = Scanner::ScanExe(
                "\x6A\x00\x6A\x00\x68\x00\x00\x00\x00\x8D\x4C\x24",
                "xxxxx????xxx"
            );

            if (addr)
            {
                Memory::NOP(addr, 16);
                Log::PatchResult("SafeMode_IniWrite", addr);
            }
            else
                Log::PatchResult("SafeMode_IniWrite", 0);
        }
    }
}