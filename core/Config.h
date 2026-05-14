#pragma once
#include <windows.h>
#include <string>

// ============================================================
//  Config.h
//
// Small helper for reading values from the mod's INI file.
// It uses the built-in Windows profile APIs so we don't need
// any external config library.
// 
//  Example:
//    [Log]
//    Log = 1
// ============================================================

class Config
{
public:
    // Load the config file path relative to the game directory
    // Should be called once during startup
    void Load(const wchar_t* filename)
    {
        // Build absolute path: game dir + filename
        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, modulePath, MAX_PATH);

        // Remove the executable name, keep only the directory
        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';

        wcscpy_s(m_path, modulePath);
        wcscat_s(m_path, filename);
    }

    // Read an integer from the INI file
    // Returns defaultVal if the entry doesn't exist
    int GetInt(const wchar_t* section, const wchar_t* key, int defaultVal = 0) const
    {
        return static_cast<int>(
            GetPrivateProfileIntW(section, key, defaultVal, m_path));
    }

    // Reads a boolean value
    // Anything other than 0 is treated as true
    bool GetBool(const wchar_t* section, const wchar_t* key, bool defaultVal = false) const
    {
        return GetInt(section, key, defaultVal ? 1 : 0) != 0;
    }

    // Reads a float from the INI
    // The Win32 profile API only returns strings/ints,
    // so floats are parsed manually
    float GetFloat(const wchar_t* section, const wchar_t* key, float defaultVal = 0.0f) const
    {
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(section, key, L"", buf, 64, m_path);
        if (buf[0] == L'\0') return defaultVal;
        return static_cast<float>(_wtof(buf));
    }

    // Read a string value
    std::wstring GetString(const wchar_t* section, const wchar_t* key, const wchar_t* defaultVal = L"") const
    {
        wchar_t buf[512] = {};
        GetPrivateProfileStringW(section, key, defaultVal, buf, 512, m_path);
        return std::wstring(buf);
    }

private:
    wchar_t m_path[MAX_PATH] = {};
};

// Global config instance shared between patch modules
extern Config g_cfg;