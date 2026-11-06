#include "startup/startup.h"
#include "patches/patches.h"
#include "logger.h"
#include <Windows.h>
#include <cwchar>
#include <iterator>
#include <string>

namespace
{
    sh3e::startup::BootstrapState g_bootstrap;

    bool ReadIniBoolean(const wchar_t* section, const wchar_t* key, bool defaultValue, const std::wstring& iniPath)
    {
        wchar_t value[16]{};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false", value, static_cast<DWORD>(std::size(value)), iniPath.c_str());
        if (_wcsicmp(value, L"true") == 0 || std::wcscmp(value, L"1") == 0)
            return true;

        if (_wcsicmp(value, L"false") == 0 || std::wcscmp(value, L"0") == 0)
            return false;
            
        return defaultValue;
    }

    void InstallEarlyHooks(sh3e::startup::BootstrapState& state)
    {
        wchar_t executablePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        std::wstring directory = executablePath;
        const auto slash = directory.find_last_of(L"\\");
        
        if (slash == std::wstring::npos)
            return;

        directory.resize(slash);
        const auto iniPath = directory + L"\\SH3Enhanced.ini";
        const auto executable = GetModuleHandleW(nullptr);

        state.safeModeFixInstalled = ReadIniBoolean(L"Graphics", L"DisableSafeMode", true, iniPath)
            ? sh3e::patches::InstallSafeModeDisable(executable)
            : false;

        wchar_t redirectValue[16] = L"true";
        GetPrivateProfileStringW(L"System", L"RegistryRedirect", L"true", redirectValue, static_cast<DWORD>(std::size(redirectValue)), iniPath.c_str());
        if (_wcsicmp(redirectValue, L"false") != 0 && std::wcscmp(redirectValue, L"0") != 0)
            state.registryRedirectInstalled = sh3e::patches::InstallRegistryRedirect(executable, directory);

        auto mode = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowMode", 0, iniPath.c_str()));
        if (mode < 0 || mode > 2)
            mode = 0;
            
        const auto configuredWidth = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"ResolutionX", 0, iniPath.c_str()));
        const auto configuredHeight = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"ResolutionY", 0, iniPath.c_str()));
        const auto configuredWindowWidth = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowedWidth", 1280, iniPath.c_str()));
        const auto configuredWindowHeight = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowedHeight", 720, iniPath.c_str()));
        const auto windowedWidth = configuredWindowWidth >= 320 && configuredWindowWidth <= 16384 ? configuredWindowWidth : 1280;
        const auto windowedHeight = configuredWindowHeight >= 320 && configuredWindowHeight <= 16384 ? configuredWindowHeight : 720;
        const bool customOutputSize = configuredWidth > 0 && configuredWidth <= 16384 && configuredHeight > 0 && configuredHeight <= 16384;
        const auto defaultWidth = mode == 0 ? GetSystemMetrics(SM_CXSCREEN) : windowedWidth;
        const auto defaultHeight = mode == 0 ? GetSystemMetrics(SM_CYSCREEN) : windowedHeight;

        sh3e::patches::SetMsaaDoFFixEnabled(ReadIniBoolean(L"Graphics", L"DisableMSAAForDoF", true, iniPath));
        state.windowModeHooksInstalled = sh3e::patches::InstallWindowModeHooks(executable, mode,
            customOutputSize ? configuredWidth : defaultWidth,
            customOutputSize ? configuredHeight : defaultHeight,
            ReadIniBoolean(L"Graphics", L"AnisotropicTextureFiltering", true, iniPath));
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        sh3e::log::SetStartupStart();
        DisableThreadLibraryCalls(module);
        g_bootstrap.pluginModule = module;

        const auto executable = GetModuleHandleW(nullptr);
        sh3e::patches::InstallContentFileRedirectHooks(executable);
        InstallEarlyHooks(g_bootstrap);

        if (const auto thread = CreateThread(nullptr, 0, sh3e::startup::InitializePlugin, &g_bootstrap, 0, nullptr))
            CloseHandle(thread);
        else
            sh3e::patches::SignalContentArchiveReady();
    }
    
    return TRUE;
}