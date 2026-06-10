#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

// --------------------------------------------------------
// Scanner.h
//
// Basic pattern scanning utilities used for locating code
// and data inside sh3.exe at runtime.
//
// Static addresses are fine for some things, but code tends
// to move around between different executable builds.
// Pattern scanning is more reliable since it searches for
// a sequence of bytes instead of assuming a fixed address.
// 
// Mask format:
//  'x' - exact byte match
//  '?' - wildcard / ignore byte
// --------------------------------------------------------

namespace Scanner
{
    // Scan a memory range for a byte pattern
    inline uintptr_t FindPattern(uintptr_t start, size_t size, const char* pattern, const char* mask)
    {
        size_t patternLength = strlen(mask);

        // avoid unsigned underflow when the range is smaller than the pattern
        if (size < patternLength)
            return 0;

        for (size_t i = 0; i <= size - patternLength; i++)
        {
            bool found = true;
            for (size_t j = 0; j < patternLength; j++)
            {
                if (mask[j] == 'x' && reinterpret_cast<uint8_t*>(start)[i + j] != static_cast<uint8_t>(pattern[j]))
                {
                    found = false;
                    break;
                }
            }

            if (found)
                return start + i;
        }

        return 0;
    }

    // Scan executable sections of sh3.exe
    // Usually this means the .text section
    inline uintptr_t ScanExe(const char* pattern, const char* mask)
    {
        HMODULE base = GetModuleHandleA(NULL);
        if (!base) return 0;

        // Read PE headers to locate executable sections
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<uintptr_t>(base) + dos->e_lfanew);

        auto* section = IMAGE_FIRST_SECTION(nt);

        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
        {
            // Skip non-executable sections
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;

            uintptr_t sectionStart = reinterpret_cast<uintptr_t>(base)
                + section->VirtualAddress;
            uintptr_t sectionSize = section->Misc.VirtualSize;

            uintptr_t result = FindPattern(sectionStart, sectionSize,
                pattern, mask);
            if (result) return result;
        }

        return 0;
    }

    // Scan a manually specified address range
    inline uintptr_t ScanRange(uintptr_t start, uintptr_t end, const char* pattern, const char* mask)
    {
        return FindPattern(start, end - start, pattern, mask);
    }
}