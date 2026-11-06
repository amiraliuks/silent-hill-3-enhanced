#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace sh3e::patches
{
namespace
{
    using RegOpenKeyExAFunction = LSTATUS(WINAPI*)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
    using RegOpenKeyAFunction = LSTATUS(WINAPI*)(HKEY, LPCSTR, PHKEY);
    using RegOpenKeyExWFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
    using RegOpenKeyWFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, PHKEY);

    RegOpenKeyExAFunction g_originalRegOpenKeyExA = nullptr;
    RegOpenKeyAFunction g_originalRegOpenKeyA = nullptr;
    RegOpenKeyExWFunction g_originalRegOpenKeyExW = nullptr;
    RegOpenKeyWFunction g_originalRegOpenKeyW = nullptr;
    std::wstring g_gameDirectory;
    volatile LONG g_userKeySeedState = 0;
    volatile LONG g_redirectObserved = 0;
    volatile LONG g_redirectLogWritten = 0;
    volatile LONG g_registryLogReady = 0;

    template <typename CharT>
    CharT FoldCase(CharT value);

    template <>
    char FoldCase(char value)
    {
        if (value >= 'a' && value <= 'z')
            return static_cast<char>(value - ('a' - 'A'));
        return value;
    }

    template <>
    wchar_t FoldCase(wchar_t value)
    {
        return static_cast<wchar_t>(std::towupper(value));
    }

    template <typename CharT>
    bool StartsWithInsensitive(std::basic_string_view<CharT> value, std::basic_string_view<CharT> prefix)
    {
        if (value.size() < prefix.size())
            return false;
        for (std::size_t index = 0; index < prefix.size(); ++index)
            if (FoldCase(value[index]) != FoldCase(prefix[index]))
                return false;
        return true;
    }

    template <typename CharT>
    bool IsKonamiGameKey(std::basic_string_view<CharT> subKey)
    {
        constexpr CharT kKonamiPrefix[] = { 'S','O','F','T','W','A','R','E','\\','K','O','N','A','M','I' };
        constexpr CharT kSH3Key[] = { 'S','I','L','E','N','T',' ','H','I','L','L',' ','3' };
        if (!StartsWithInsensitive(subKey, std::basic_string_view<CharT>(kKonamiPrefix, std::size(kKonamiPrefix))))
            return false;

        subKey.remove_prefix(std::size(kKonamiPrefix));
        if (!subKey.empty() && subKey.front() != static_cast<CharT>('\\'))
            return false;
        if (subKey.empty())
            return false;
        subKey.remove_prefix(1);
        const auto matchesGame = [subKey](std::basic_string_view<CharT> name)
        {
            if (!StartsWithInsensitive(subKey, name))
                return false;
            return subKey.size() == name.size() || subKey[name.size()] == static_cast<CharT>('\\');
        };
        return matchesGame(std::basic_string_view<CharT>(kSH3Key, std::size(kSH3Key)));
    }

    bool ShouldRedirect(HKEY root, LPCSTR subKey)
    {
        return root == HKEY_LOCAL_MACHINE && subKey && IsKonamiGameKey(std::string_view(subKey));
    }

    bool ShouldRedirect(HKEY root, LPCWSTR subKey)
    {
        return root == HKEY_LOCAL_MACHINE && subKey && IsKonamiGameKey(std::wstring_view(subKey));
    }

    bool EnsureUserGameKey();

    void RecordRedirect()
    {
        InterlockedExchange(&g_redirectObserved, 1);
        if (InterlockedCompareExchange(&g_registryLogReady, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_redirectLogWritten, 1, 0) == 0)
            log::Write("INFO", "[Registry redirect] Redirected an SH3 install-key open from HKLM to HKCU.");
    }

    LSTATUS WINAPI RedirectRegOpenKeyExA(HKEY root, LPCSTR subKey, DWORD options, REGSAM access, PHKEY result)
    {
        if (ShouldRedirect(root, subKey) && EnsureUserGameKey())
        {
            RecordRedirect();
            root = HKEY_CURRENT_USER;
        }

        return g_originalRegOpenKeyExA(root, subKey, options, access, result);
    }

    LSTATUS WINAPI RedirectRegOpenKeyA(HKEY root, LPCSTR subKey, PHKEY result)
    {
        if (ShouldRedirect(root, subKey) && EnsureUserGameKey())
        {
            RecordRedirect();
            root = HKEY_CURRENT_USER;
        }

        return g_originalRegOpenKeyA(root, subKey, result);
    }

    LSTATUS WINAPI RedirectRegOpenKeyExW(HKEY root, LPCWSTR subKey, DWORD options, REGSAM access, PHKEY result)
    {
        if (ShouldRedirect(root, subKey) && EnsureUserGameKey())
        {
            RecordRedirect();
            root = HKEY_CURRENT_USER;
        }

        return g_originalRegOpenKeyExW(root, subKey, options, access, result);
    }

    LSTATUS WINAPI RedirectRegOpenKeyW(HKEY root, LPCWSTR subKey, PHKEY result)
    {
        if (ShouldRedirect(root, subKey) && EnsureUserGameKey())
        {
            RecordRedirect();
            root = HKEY_CURRENT_USER;
        }

        return g_originalRegOpenKeyW(root, subKey, result);
    }

    bool ReplaceImportThunk(DWORD* thunk, DWORD replacement, DWORD& original)
    {
        DWORD oldProtection = 0;
        if (!VirtualProtect(thunk, sizeof(*thunk), PAGE_READWRITE, &oldProtection))
            return false;

        original = *thunk;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(thunk), static_cast<LONG>(replacement));

        DWORD ignored = 0;
        VirtualProtect(thunk, sizeof(*thunk), oldProtection, &ignored);
        return true;
    }

    bool SeedUserGameKey(const std::wstring& gameDirectory)
    {
        constexpr wchar_t keyPath[] = L"SOFTWARE\\KONAMI\\SILENT HILL 3";
        HKEY key = nullptr;
        DWORD disposition = 0;
        auto status = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
        if (status != ERROR_SUCCESS)
            return false;

        std::wstring root = gameDirectory;
        if (!root.empty() && root.back() != L'\\')
            root.push_back(L'\\');
        const std::wstring save = root + L"savedata\\";
        const std::pair<const wchar_t*, const std::wstring*> defaults[] = {
            { L"installdir", &root }, { L"data", &root }, { L"movie", &root },
            { L"sound", &root }, { L"save", &save }
        };

        for (const auto& entry : defaults)
        {
            const auto bytes = static_cast<DWORD>((entry.second->size() + 1) * sizeof(wchar_t));
            status = RegSetValueExW(key, entry.first, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(entry.second->c_str()), bytes);
            if (status != ERROR_SUCCESS)
                break;
        }

        if (status == ERROR_SUCCESS)
        {
            for (const auto& entry : defaults)
            {
                wchar_t value[32768]{};
                DWORD type = 0;
                DWORD bytes = sizeof(value);
                status = RegQueryValueExW(key, entry.first, nullptr, &type,
                    reinterpret_cast<BYTE*>(value), &bytes);
                if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
                    value[std::size(value) - 1] != L'\0' || std::wstring(value) != *entry.second)
                {
                    status = ERROR_INVALID_DATA;
                    break;
                }
            }
        }

        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    bool UserGameKeyMatches(const std::wstring& gameDirectory)
    {
        constexpr wchar_t keyPath[] = L"SOFTWARE\\KONAMI\\SILENT HILL 3";
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            return false;

        std::wstring root = gameDirectory;
        if (!root.empty() && root.back() != L'\\')
            root.push_back(L'\\');
        const std::wstring save = root + L"savedata\\";
        const std::pair<const wchar_t*, const std::wstring*> expected[] = {
            { L"installdir", &root }, { L"data", &root }, { L"movie", &root },
            { L"sound", &root }, { L"save", &save }
        };

        bool matches = true;
        for (const auto& entry : expected)
        {
            wchar_t value[32768]{};
            DWORD type = 0;
            DWORD bytes = sizeof(value);
            const auto status = RegQueryValueExW(key, entry.first, nullptr, &type,
                reinterpret_cast<BYTE*>(value), &bytes);
            if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
                value[std::size(value) - 1] != L'\0' || std::wstring(value) != *entry.second)
            {
                matches = false;
                break;
            }
        }

        RegCloseKey(key);
        return matches;
    }

    bool EnsureUserGameKey()
    {
        for (;;)
        {
            const auto state = InterlockedCompareExchange(&g_userKeySeedState, 0, 0);
            if (state == 1)
            {
                Sleep(0);
                continue;
            }

            if (state == 2 && UserGameKeyMatches(g_gameDirectory))
                return true;
            if (state == -1)
                return false;

            if (InterlockedCompareExchange(&g_userKeySeedState, 1, state) == state)
            {
                const bool success = SeedUserGameKey(g_gameDirectory);
                InterlockedExchange(&g_userKeySeedState, success ? 2 : -1);
                return success;
            }
        }
    }

    bool InstallImportHooks(HMODULE executable)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(executable);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return false;

        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
            return false;

        bool hooked = false;
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        for (; descriptor->Name != 0; ++descriptor)
        {
            if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0)
                continue;

            auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
            auto* table = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData != 0; ++names, ++table)
            {
                if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
                    continue;
                const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                const auto* name = reinterpret_cast<const char*>(import->Name);
                DWORD* slot = reinterpret_cast<DWORD*>(&table->u1.Function);
                DWORD original = 0;
                DWORD replacement = 0;

                if (std::strcmp(name, "RegOpenKeyExA") == 0)
                {
                    if (!g_originalRegOpenKeyExA)
                        g_originalRegOpenKeyExA = reinterpret_cast<RegOpenKeyExAFunction>(static_cast<std::uintptr_t>(*slot));
                    replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectRegOpenKeyExA));
                }
                else if (std::strcmp(name, "RegOpenKeyA") == 0)
                {
                    if (!g_originalRegOpenKeyA)
                        g_originalRegOpenKeyA = reinterpret_cast<RegOpenKeyAFunction>(static_cast<std::uintptr_t>(*slot));
                    replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectRegOpenKeyA));
                }
                else if (std::strcmp(name, "RegOpenKeyExW") == 0)
                {
                    if (!g_originalRegOpenKeyExW)
                        g_originalRegOpenKeyExW = reinterpret_cast<RegOpenKeyExWFunction>(static_cast<std::uintptr_t>(*slot));
                    replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectRegOpenKeyExW));
                }
                else if (std::strcmp(name, "RegOpenKeyW") == 0)
                {
                    if (!g_originalRegOpenKeyW)
                        g_originalRegOpenKeyW = reinterpret_cast<RegOpenKeyWFunction>(static_cast<std::uintptr_t>(*slot));
                    replacement = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&RedirectRegOpenKeyW));
                }

                if (replacement && ReplaceImportThunk(slot, replacement, original))
                    hooked = true;
            }
        }

        return hooked;
    }
}

bool InstallRegistryRedirect(HMODULE executable, const std::wstring& gameDirectory)
{
    if (!executable || gameDirectory.empty())
        return false;
    g_gameDirectory = gameDirectory;
    g_userKeySeedState = 0;
    g_redirectObserved = 0;
    g_redirectLogWritten = 0;
    g_registryLogReady = 0;
    if (!InstallImportHooks(executable))
        return false;
    return true;
}

bool SeedRegistryRedirectPaths()
{
    const auto success = EnsureUserGameKey();
    InterlockedExchange(&g_registryLogReady, 1);
    if (success)
    {
        const auto pathLength = WideCharToMultiByte(CP_UTF8, 0, g_gameDirectory.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string path;
        if (pathLength > 1)
        {
            path.resize(static_cast<std::size_t>(pathLength));
            if (WideCharToMultiByte(CP_UTF8, 0, g_gameDirectory.c_str(), -1, path.data(), pathLength, nullptr, nullptr) > 0)
                path.pop_back();
            else
                path.clear();
        }

        log::Write("INFO", "[Registry redirect] HKCU install paths verified for the running game folder: " +
            (path.empty() ? std::string("(path unavailable)") : path));
    }
    else
        log::Write("WARN", "[Registry redirect] Could not set all per-user install paths.");

    if (RegistryRedirectWasUsed() && InterlockedCompareExchange(&g_redirectLogWritten, 1, 0) == 0)
        log::Write("INFO", "[Registry redirect] Redirected an SH3 install-key open from HKLM to HKCU.");
    return success;
}

bool RegistryRedirectWasUsed()
{
    return InterlockedCompareExchange(&g_redirectObserved, 0, 0) != 0;
}
}
