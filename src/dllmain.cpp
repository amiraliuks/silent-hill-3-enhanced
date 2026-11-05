#include "config.h"
#include "discord_presence.h"
#include "logger.h"
#include "patches/patch_utils.h"
#include "patches/patches.h"
#include "patches/muted_audio.h"
#include "pattern.h"
#include "screen_state.h"
#include <Windows.h>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

namespace
{
    bool g_registryRedirectInstalled = false;
    bool g_windowModeHooksInstalled = true;
    bool g_safeModeFixInstalled = false;

    bool ReadIniBoolean(const wchar_t* section, const wchar_t* key, bool defaultValue, const std::wstring& iniPath)
    {
        wchar_t value[16]{};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false",
            value, static_cast<DWORD>(std::size(value)), iniPath.c_str());
        if (_wcsicmp(value, L"true") == 0 || std::wcscmp(value, L"1") == 0)
            return true;
        if (_wcsicmp(value, L"false") == 0 || std::wcscmp(value, L"0") == 0)
            return false;
        return defaultValue;
    }

    std::uint32_t Crc32(const std::vector<std::uint8_t>& data)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const auto byte : data)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    DWORD WINAPI InitializePlugin(void* moduleParameter)
    {
        const auto pluginModule = static_cast<HMODULE>(moduleParameter);
        wchar_t executablePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        std::wstring directory = executablePath;
        const auto slash = directory.find_last_of(L"\\");
        if (slash != std::wstring::npos) directory.resize(slash);
        const auto iniPath = directory + L"\\SH3Enhanced.ini";
        const auto config = sh3e::LoadConfig(iniPath);
        if (config.enableLogging)
            sh3e::log::Initialize(directory);
        sh3e::log::Write("INFO", "Plugin version: 0.1.0-dev");
        sh3e::log::Write("INFO", "Silent Hill 3 Enhanced loaded.");
        if (config.disableMsaaForDof)
            sh3e::log::Write("INFO", "[MSAA / DoF] Multisampling requests will be disabled for DoF compatibility.");
        else
            sh3e::log::Write("INFO", "[MSAA / DoF] Multisampling override is disabled.");
        const auto exceptionHandlerInstalled = config.exceptionHandler
            ? sh3e::patches::InstallExceptionHandler(directory)
            : false;
        if (config.exceptionHandler && !exceptionHandlerInstalled)
            sh3e::log::Write("WARN", "[Exception handler] Could not install the minidump crash handler.");
        const auto mutedAudioRecoveryStarted = config.fixMutedAudio
            ? sh3e::patches::StartMutedAudioRecovery()
            : false;
        if (config.fixMutedAudio && !mutedAudioRecoveryStarted)
            sh3e::log::Write("WARN", "[Muted audio] Could not start Windows audio-session recovery.");

        const auto exe = GetModuleHandleW(nullptr);
        sh3e::log::Write("INFO", "Executable module base: " + sh3e::patches::detail::Hex(reinterpret_cast<std::uintptr_t>(exe)));
        sh3e::StartDiscordPresence(config.discordApplicationId);
        const auto registryPathsSeeded = config.registryRedirect && g_registryRedirectInstalled
            ? sh3e::patches::SeedRegistryRedirectPaths()
            : false;
        const auto whiteBorderFixInstalled = config.fixWhiteBorder
            ? sh3e::patches::InstallWhiteBorderFix(exe)
            : false;
        const auto cpuAffinityApplied = sh3e::patches::ApplyCpuAffinity(config.cpuAffinityMask);
        const char* modeName = config.windowMode == 0 ? "fullscreen" :
            (config.windowMode == 1 ? "windowed" : "borderless fullscreen");
        {
            if (g_windowModeHooksInstalled)
                sh3e::log::Write("INFO", std::string("[Window mode] ") + modeName + " mode hooks installed; waiting for the game window.");
            else
                sh3e::log::Write("WARN", std::string("[Window mode] Could not install ") + modeName + " hooks.");
        }
        if (config.fixWishHouse || config.fixEnglishTypos || config.restoreMissingSewerText)
            sh3e::log::Write("INFO", "[Startup timing] Beginning message-archive preparation.");
        const auto contentFixesInstalled = (config.fixWishHouse || config.fixEnglishTypos || config.restoreMissingSewerText)
            ? sh3e::patches::InstallContentTextPatches(directory, config)
            : false;
        if (config.fixWishHouse || config.fixEnglishTypos || config.restoreMissingSewerText)
            sh3e::log::Write("INFO", "[Startup timing] Finished message-archive preparation.");
        const auto sh2EasterEggsInstalled = config.unlockSH2EasterEggs
            ? sh3e::patches::InstallSH2EasterEggUnlock(exe)
            : false;
        const auto optionsFixInstalled = sh3e::patches::InstallOptionsMenuFreezeFix(exe);
        const auto safeModeFixInstalled = config.disableSafeMode && g_safeModeFixInstalled;
        if (config.disableSafeMode)
        {
            if (safeModeFixInstalled)
                sh3e::log::Write("INFO", "[Safe mode] Settings-reset path disabled before game startup.");
            else
                sh3e::log::Write("WARN", "[Safe mode] Could not disable the settings-reset path before game startup.");
        }
        const auto screen = sh3e::GetInitialScreenState(config);
        const auto resolutionFixInstalled = sh3e::patches::InstallResolutionPatches(exe, config, screen);
        const auto widescreenFixInstalled = sh3e::patches::InstallWidescreenPatch(exe, screen);
        const auto fullscreenPauseMenuInstalled = config.fullscreenPauseMenu
            ? sh3e::patches::InstallFullscreenPauseMenuPatch(exe, screen)
            : true;
        const auto dofResolutionInstalled = sh3e::patches::InstallDofResolutionPatch(exe, config.dofResolution);
        const auto shadowResolutionInstalled = sh3e::patches::InstallShadowResolutionPatch(exe, config.shadowsResolution);
        const auto inventoryResolutionInstalled = sh3e::patches::InstallInventoryResolutionPatch(exe, config.inventoryResolution);
        const auto statusScreenResolutionInstalled = sh3e::patches::InstallStatusScreenResolutionPatch(exe, config.statusScreenResolution);
        const auto fogControlInstalled = sh3e::patches::InstallFogControlPatch(exe, config.fogComplexity);
        const auto cutsceneBordersFixInstalled = config.removeCutsceneBorders
            ? sh3e::patches::InstallCutsceneBordersPatch(exe)
            : false;
        const auto frameRateFixInstalled = sh3e::patches::InstallFrameRatePatch(exe, config.frameRateMode);
        const auto mirrorRoomFixInstalled = sh3e::patches::InstallMirrorRoomPatch(exe, config.mirrorRoomFix, config.frameRateMode);
        const auto buttonIconsInstalled = sh3e::patches::InstallControllerButtonIcons(
            exe, config.replaceButtonText, pluginModule);
        if (config.replaceButtonText != 0 && !buttonIconsInstalled)
            sh3e::log::Write("WARN", "[Controller icons] Control Options button-icon patch could not be installed.");

        const auto file = CreateFileW(executablePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            const auto size = GetFileSize(file, nullptr);
            std::vector<std::uint8_t> bytes(size);
            DWORD read = 0;
            const bool loaded = ReadFile(file, bytes.data(), size, &read, nullptr) != FALSE && read == size;
            CloseHandle(file);
            if (loaded && bytes.size() >= sizeof(IMAGE_DOS_HEADER))
            {
                const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
                if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0 && bytes.size() >= static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32))
                {
                    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
                    std::ostringstream info;
                    info << "Executable file size: " << size << ", PE timestamp: 0x" << std::hex << std::uppercase << nt->FileHeader.TimeDateStamp
                        << ", CRC32: 0x" << Crc32(bytes) << ", machine: 0x" << nt->FileHeader.Machine;
                    sh3e::log::Write("INFO", info.str());
                }
            }
        }

        std::ostringstream display;
        if (screen.autoOutputResolution)
            display << "Game output/backbuffer resolution follows desktop: " << screen.width << 'x' << screen.height;
        else if (screen.overrideOutputResolution)
            display << "Configured game output/backbuffer resolution: " << screen.width << 'x' << screen.height;
        else
            display << "Desktop resolution: " << screen.width << 'x' << screen.height
            << ", output-mode override disabled";
        display << ", aspect ratio: " << std::fixed << std::setprecision(6) << screen.aspectRatio;
        if (config.windowMode == 0)
            display << ", fullscreen mode";
        else if (config.windowMode == 1)
            display << ", windowed mode";
        else if (config.windowMode == 2)
            display << ", borderless fullscreen mode";
        if (screen.renderWidth > 0 && screen.renderHeight > 0)
            display << ", configured internal render-target resolution: " << screen.renderWidth << 'x' << screen.renderHeight;
        else
            display << ", internal render-target override: disabled (game default)";
        sh3e::log::Write("INFO", display.str());

        if ((config.fixWishHouse || config.fixEnglishTypos || config.restoreMissingSewerText) && !contentFixesInstalled)
            sh3e::log::Write("WARN", "[Content] One or more text patches could not be installed.");
        if (config.registryRedirect && !g_registryRedirectInstalled)
            sh3e::log::Write("WARN", "[Registry redirect] Registry-open hooks were not installed.");
        if (config.registryRedirect && g_registryRedirectInstalled && !registryPathsSeeded)
            sh3e::log::Write("WARN", "[Registry redirect] Per-user install paths were not fully prepared.");
        if (config.fixWhiteBorder && !whiteBorderFixInstalled)
            sh3e::log::Write("WARN", "[White border] Windows border correction was not installed.");
        if (!cpuAffinityApplied && config.cpuAffinityMask != 0)
            sh3e::log::Write("WARN", "[CPU affinity] Requested processor mask was not applied.");
        if (config.unlockSH2EasterEggs && !sh2EasterEggsInstalled)
            sh3e::log::Write("WARN", "[SH2 Easter eggs] Save-data override was not installed.");
        if (config.renderResolutionX > 0 != (config.renderResolutionY > 0))
            sh3e::log::Write("WARN", "[Render resolution] Set both RenderResolutionX and RenderResolutionY; leaving the internal size under game control.");
        if (config.resolutionX > 0 != (config.resolutionY > 0))
            sh3e::log::Write("WARN", "[Resolution] Set both ResolutionX and ResolutionY; leaving output mode under game control.");
        if (config.disableSafeMode && !safeModeFixInstalled)
            sh3e::log::Write("WARN", "[Safe mode] Settings-reset protection was not installed.");
        if (!g_windowModeHooksInstalled)
            sh3e::log::Write("WARN", "[Window mode] Requested display mode is inactive because its hooks were not installed.");
        if (!optionsFixInstalled)
            sh3e::log::Write("WARN", "[Options] Freeze fix was not installed.");
        if (screen.overrideOutputResolution && !resolutionFixInstalled)
            sh3e::log::Write("WARN", "[Resolution] One or more output/render/FMV patches were not fully installed.");
        if (screen.aspectRatio > (4.0f / 3.0f) && !widescreenFixInstalled)
            sh3e::log::Write("WARN", "[Widescreen] Aspect correction was not fully installed.");
        if (config.fullscreenPauseMenu && screen.aspectRatio > (4.0f / 3.0f) && !fullscreenPauseMenuInstalled)
            sh3e::log::Write("WARN", "[Fullscreen pause menu] Widescreen pause-screen patch was not installed.");
        if (config.dofResolution > 0 && !dofResolutionInstalled)
            sh3e::log::Write("WARN", "[DoF resolution] Requested depth-of-field resolution was not installed.");
        if (config.shadowsResolution > 0 && !shadowResolutionInstalled)
            sh3e::log::Write("WARN", "[Shadow resolution] Requested shadow-map resolution was not installed.");
        if (config.inventoryResolution > 0 && !inventoryResolutionInstalled)
            sh3e::log::Write("WARN", "[Inventory quad] Requested coordinate patch was not installed.");
        if (config.statusScreenResolution > 0 && !statusScreenResolutionInstalled)
            sh3e::log::Write("WARN", "[Status screen resolution] Requested image resolution was not installed.");
        if (config.fogComplexity > 0 && !fogControlInstalled)
            sh3e::log::Write("WARN", "[Fog control] Requested fog coefficient was not installed.");
        if (config.removeCutsceneBorders && !cutsceneBordersFixInstalled)
            sh3e::log::Write("WARN", "[Cutscene borders] Removal patch was not installed.");
        if (!frameRateFixInstalled)
            sh3e::log::Write("WARN", "[Frame rate] Requested timing mode was not installed.");
        if (config.mirrorRoomFix && !mirrorRoomFixInstalled)
            sh3e::log::Write("WARN", "[Mirror Room] Room-specific 30 FPS limiter was not installed.");
        sh3e::patches::SignalContentArchiveReady();
        sh3e::log::Write("INFO", "[Startup timing] Plugin initialization finished.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        sh3e::log::SetStartupStart();
        DisableThreadLibraryCalls(module);
        sh3e::patches::InstallContentFileRedirectHooks(GetModuleHandleW(nullptr));
        wchar_t executablePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        std::wstring directory = executablePath;
        const auto slash = directory.find_last_of(L"\\");
        if (slash != std::wstring::npos)
        {
            directory.resize(slash);
            wchar_t redirectValue[16] = L"true";
            const auto iniPath = directory + L"\\SH3Enhanced.ini";
            const auto disableSafeMode = ReadIniBoolean(L"Graphics", L"DisableSafeMode", true, iniPath);
            g_safeModeFixInstalled = disableSafeMode
                ? sh3e::patches::InstallSafeModeDisable(GetModuleHandleW(nullptr))
                : false;
            GetPrivateProfileStringW(L"System", L"RegistryRedirect", L"true", redirectValue,
                static_cast<DWORD>(std::size(redirectValue)), iniPath.c_str());
            if (_wcsicmp(redirectValue, L"false") != 0 && std::wcscmp(redirectValue, L"0") != 0)
                g_registryRedirectInstalled = sh3e::patches::InstallRegistryRedirect(GetModuleHandleW(nullptr), directory);

            auto mode = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowMode", 0, iniPath.c_str()));
            if (mode < 0 || mode > 2)
                mode = 0;
            const auto configuredWidth = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"ResolutionX", 0, iniPath.c_str()));
            const auto configuredHeight = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"ResolutionY", 0, iniPath.c_str()));
            const auto configuredWindowWidth = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowedWidth", 1280, iniPath.c_str()));
            const auto configuredWindowHeight = static_cast<int>(GetPrivateProfileIntW(L"Graphics", L"WindowedHeight", 720, iniPath.c_str()));
            const auto windowedWidth = configuredWindowWidth >= 320 && configuredWindowWidth <= 16384 ? configuredWindowWidth : 1280;
            const auto windowedHeight = configuredWindowHeight >= 320 && configuredWindowHeight <= 16384 ? configuredWindowHeight : 720;
            const bool customOutputSize = configuredWidth > 0 && configuredWidth <= 16384 &&
                configuredHeight > 0 && configuredHeight <= 16384;
            const auto defaultWidth = mode == 0 ? GetSystemMetrics(SM_CXSCREEN) : windowedWidth;
            const auto defaultHeight = mode == 0 ? GetSystemMetrics(SM_CYSCREEN) : windowedHeight;
            sh3e::patches::SetMsaaDoFFixEnabled(
                ReadIniBoolean(L"Graphics", L"DisableMSAAForDoF", true, iniPath));
            g_windowModeHooksInstalled = sh3e::patches::InstallWindowModeHooks(GetModuleHandleW(nullptr), mode,
                customOutputSize ? configuredWidth : defaultWidth,
                customOutputSize ? configuredHeight : defaultHeight,
                ReadIniBoolean(L"Graphics", L"AnisotropicTextureFiltering", true, iniPath));
        }
        if (const auto thread = CreateThread(nullptr, 0, InitializePlugin, module, 0, nullptr)) CloseHandle(thread);
        else sh3e::patches::SignalContentArchiveReady();
    }
    return TRUE;
}
