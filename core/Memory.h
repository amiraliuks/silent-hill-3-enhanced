#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// ============================================================
//  Memory.h
//
//  Helpers for patching and reading SH3 memory at runtime.
// 
// Since the ASI is loaded directly into the game's process,
// it shares the same address space as sh3.exe. That means
// game memory can be accessed with normal pointer writes.
// 
// Code sections still need to be temporarily unprotected
// before they can be modified.
// ============================================================

namespace Memory
{
    // Change protection -> write bytes -> restore protection
	// Return false if VirtualProtect fails
    inline bool Patch(uintptr_t address, const void* data, size_t size)
    {
        DWORD oldProtect;

        // Temporarily allow writes to the target region
        if (!VirtualProtect(reinterpret_cast<void*>(address),
            size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(reinterpret_cast<void*>(address), data, size);

        // Restore the original page protection
        VirtualProtect(reinterpret_cast<void*>(address),
            size, oldProtect, &oldProtect);

        // Make sure the CPU doesn't execute stale instructions
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(address), size);

        return true;
    }

    // Write a single byte
    inline bool PatchByte(uintptr_t address, uint8_t value)
    {
        return Patch(address, &value, sizeof(value));
    }

    // Write a 32-bit integer
    inline bool PatchDword(uintptr_t address, uint32_t value)
    {
        return Patch(address, &value, sizeof(value));
    }

    // Write a float value
    inline bool PatchFloat(uintptr_t address, float value)
    {
        return Patch(address, &value, sizeof(value));
    }

    // Fill a region with x86 NOP instructions (0x90)
    // Commonly used to disable existing game code
    inline bool NOP(uintptr_t address, size_t count)
    {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address),
            count, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memset(reinterpret_cast<void*>(address), 0x90, count);

        VirtualProtect(reinterpret_cast<void*>(address),
            count, oldProtect, &oldProtect);

        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(address), count);
        return true;
    }

    // Read a value directly from memory
    template<typename T>
    inline T Read(uintptr_t address)
    {
        return *reinterpret_cast<T*>(address);
    }

    // Write a value directly to memory
    // Intended for writable data regions only
    template<typename T>
    inline void Write(uintptr_t address, T value)
    {
        *reinterpret_cast<T*>(address) = value;
    }
}