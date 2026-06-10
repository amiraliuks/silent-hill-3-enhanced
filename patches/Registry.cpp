#include "Registry.h"
#include "../core/Log.h"
#include <windows.h>

// ============================================================
//  Patches/Registry.cpp
//
//  Handles SH3 registry compatibility fixes.
// 
// The PC version expects registry values under KONAMI keys,
// normally stored in HKLM. On modern Windows setups this can
// become unreliable due to permissions, missing installers,
// or WOW64 redirection for 32-bit applications.
// 
// This patch does two things:
//  1. Creates the expected registry keys and paths early
//      during startup.
//  2. Redirects registry reads from HKLM to HKCU through
//      simple IAT hooks.
// ============================================================

namespace Patches
{
    static const wchar_t* SH3_SUBKEY_W = L"SOFTWARE\\KONAMI\\SILENT HILL 3";
    static const wchar_t* SH3_SUBKEY_WOW_W = L"SOFTWARE\\WOW6432Node\\KONAMI\\SILENT HILL 3";

    // Write SH3 path values into a registry key
    static void WritePathsToKey(HKEY root, const wchar_t* subkey,
        const wchar_t* installDir,
        const wchar_t* saveDir)
    {
        HKEY hKey = nullptr;
        DWORD disp = 0;

        LSTATUS r = RegCreateKeyExW(
            root, subkey, 0, nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE | KEY_WOW64_32KEY,
            nullptr, &hKey, &disp);

        if (r != ERROR_SUCCESS) return;

        auto SetStr = [&](const wchar_t* name, const wchar_t* val)
            {
                DWORD sz = (DWORD)((wcslen(val) + 1) * sizeof(wchar_t));
                RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)val, sz);
            };

        SetStr(L"installdir", installDir);
        SetStr(L"data", installDir);
        SetStr(L"sound", installDir);
        SetStr(L"movie", installDir);
        SetStr(L"save", saveDir);

        RegCloseKey(hKey);
    }

    // Create the registry keys SH3 expects
    // Called very early during process startup
    void WriteRegistryKeysNow()
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (!slash) return;
        *(slash + 1) = L'\0'; // Keep only the directory path

        wchar_t savePath[MAX_PATH] = {};
        wcscpy_s(savePath, exePath);
        wcscat_s(savePath, L"savedata\\");

        // HKCU (both paths, no admin needed)
        WritePathsToKey(HKEY_CURRENT_USER, SH3_SUBKEY_W, exePath, savePath);
        WritePathsToKey(HKEY_CURRENT_USER, SH3_SUBKEY_WOW_W, exePath, savePath);

        // HKLM paths
        // These may fail silently without admin rights
        WritePathsToKey(HKEY_LOCAL_MACHINE, SH3_SUBKEY_W, exePath, savePath);
        WritePathsToKey(HKEY_LOCAL_MACHINE, SH3_SUBKEY_WOW_W, exePath, savePath);
    }

    // Redirects HKLM registry reads to HKCU
    static LSTATUS WINAPI HookRegOpenKeyExA(
        HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions,
        REGSAM samDesired, PHKEY phkResult)
    {
        if (hKey == HKEY_LOCAL_MACHINE && lpSubKey &&
            (strstr(lpSubKey, "SILENT HILL 3") ||
                strstr(lpSubKey, "KONAMI")))
        {
            hKey = HKEY_CURRENT_USER;
        }
        return RegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    // Redirect HKLM registry reads to HKCU
    static LSTATUS WINAPI HookRegOpenKeyExW(
        HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
        REGSAM samDesired, PHKEY phkResult)
    {
        if (hKey == HKEY_LOCAL_MACHINE && lpSubKey &&
            (wcsstr(lpSubKey, L"SILENT HILL 3") ||
                wcsstr(lpSubKey, L"KONAMI")))
        {
            hKey = HKEY_CURRENT_USER;
        }
        return RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    // Replace a function inside the executable's import table
    static void PatchIATEntry(const char* targetDll,
        const char* funcName,
        void* hookFunc)
    {
        HMODULE base = GetModuleHandleA(NULL);
        auto* dos = (IMAGE_DOS_HEADER*)base;
        auto* nt = (IMAGE_NT_HEADERS*)((uintptr_t)base + dos->e_lfanew);
        auto& idir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        auto* desc = (IMAGE_IMPORT_DESCRIPTOR*)((uintptr_t)base + idir.VirtualAddress);

        for (; desc->Name; desc++)
        {
            const char* dll = (const char*)((uintptr_t)base + desc->Name);
            if (_stricmp(dll, targetDll) != 0) continue;

            auto* thunk = (IMAGE_THUNK_DATA*)((uintptr_t)base + desc->FirstThunk);

            // some binaries null out OriginalFirstThunk; fall back to FirstThunk
            DWORD nameThunkRva = desc->OriginalFirstThunk
                ? desc->OriginalFirstThunk
                : desc->FirstThunk;
            auto* orig = (IMAGE_THUNK_DATA*)((uintptr_t)base + nameThunkRva);

            for (; thunk->u1.Function; thunk++, orig++)
            {
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
                auto* ibn = (IMAGE_IMPORT_BY_NAME*)
                    ((uintptr_t)base + orig->u1.AddressOfData);
                if (strcmp(ibn->Name, funcName) != 0) continue;

                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t),
                    PAGE_EXECUTE_READWRITE, &old);
                thunk->u1.Function = (uintptr_t)hookFunc;
                VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t),
                    old, &old);
                return;
            }
        }
    }

    // Install registry redirect hooks
    void ApplyRegistryRedirect()
    {
        PatchIATEntry("ADVAPI32.dll", "RegOpenKeyExA", (void*)HookRegOpenKeyExA);
        PatchIATEntry("ADVAPI32.dll", "RegOpenKeyExW", (void*)HookRegOpenKeyExW);
    }
}