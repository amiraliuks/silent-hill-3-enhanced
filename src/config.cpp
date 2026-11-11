#include "config.h"
#include <Windows.h>
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <cwchar>

namespace
{
    std::wstring Trim(const std::wstring& value)
    {
        const auto first = value.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return {};
        return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    }

    bool ParseBool(const std::wstring& value, bool fallback)
    {
        if (value == L"true" || value == L"True" || value == L"1") return true;
        if (value == L"false" || value == L"False" || value == L"0") return false;
        return fallback;
    }
    
    bool ParseDimension(const std::wstring& value, int& result)
    {
        wchar_t* end = nullptr;
        const auto parsed = std::wcstol(value.c_str(), &end, 10);

        if (end == value.c_str() || *end != L'\0' || parsed < 0 || parsed > 16384)
            return false;

        result = static_cast<int>(parsed);
        return true;
    }

    bool ParseAffinityMask(const std::wstring& value, std::uint32_t& result)
    {
        if (value.empty() || value.front() == L'-') return false;
        errno = 0;
        wchar_t* end = nullptr;
        const int base = value.size() > 2 && value[0] == L'0' && (value[1] == L'x' || value[1] == L'X') ? 16 : 10;
        const auto parsed = std::wcstoull(value.c_str(), &end, base);

        if (errno == ERANGE || end == value.c_str() || *end != L'\0' || parsed > UINT32_MAX)
            return false;

        result = static_cast<std::uint32_t>(parsed);
        return true;
    }

}

sh3e::Config sh3e::LoadConfig(const std::wstring& path)
{
    Config config;
    std::wifstream input(path);

    if (!input)
    {
        std::wofstream output(path);
        if (output)
            output << LR"SH3INI([General]
EnableLogging=true

[System]
; Redirect Silent Hill 3 registry reads from HKLM to a per-user HKCU key.
RegistryRedirect=true

; Remove the bright window outline on Windows 10/11.
WhiteBorderFix=true

; Write a Windows minidump after an unhandled game crash.
ExceptionHandler=true

; Clear a persisted Windows audio-session mute when SH3 starts.
FixMutedAudio=true

; CPU affinity bitmask: 0 lets Windows schedule normally; 0x1=CPU 0, 0x3=CPU 0 and 1.
CpuAffinityMask=0

[Graphics]
; Enable the highest anisotropic texture-filtering level supported by the device.
AnisotropicTextureFiltering=true

; Font quality: 0=original, 2=2x, 4=4x (default).
FontScale=4

; Disable multisample anti-aliasing requests to avoid corrupting the depth-of-field effect.
DisableMSAAForDoF=true

; Game output/backbuffer resolution. 0,0 follows the current Windows desktop resolution; set both to force a size.
ResolutionX=0
ResolutionY=0

; Internal render-target resolution (separate from output size). 0,0 leaves it at the game's default render size.
RenderResolutionX=0
RenderResolutionY=0

; Depth-of-field/otherworld-corrosion target resolution; 0 keeps game defaults; maximum 4096.
DOFRes=1024

; Dynamic shadow-map resolution; 0 keeps game defaults; maximum 4096.
ShadowsRes=1024

; Experimental: adjusts one pause-menu quad's horizontal bounds; 0 keeps the game default.
; The inventory texture-resolution target is still being traced; this does not sharpen item icons.
InventoryRes=1024

; Status-screen image resolution; 0 keeps game defaults; maximum 4096.
StatusScreenRes=1024

; Fog detail coefficient; 25 is the executable default; 0 leaves the game unchanged.
FogComplexity=75

; Window mode: 0=fullscreen, 1=windowed, 2=borderless fullscreen.
WindowMode=0

; Windowed client size unless both ResolutionX/Y are set.
WindowedWidth=1280
WindowedHeight=720

; Expand the in-game pause screen to widescreen.
FullscreenPauseMenu=true

; FMV aspect: 0=4:3, 1=fill widescreen (crop), 2=16:9 source (default).
FMVWidescreenMode=2

; Frame rate mode: 0=unlocked, 30=30 FPS, 60=60 FPS.
FrameRateMode=60

; Prevent broken mirror/lighting effects by capping the Brookhaven Hospital storeroom at 30 FPS; restores the selected rate on exit (requires FrameRateMode=60).
MirrorRoomFix=true

; Prevent the game from reverting graphics settings after an abnormal exit.
DisableSafeMode=true

; Remove the horizontal borders added to cutscenes.
RemoveCutsceneBorders=true

; Control Options controller labels: 0=off, 1=generic, 2=Xbox, 3=PlayStation (default), 4=Nintendo.
ReplaceButtonText=3

[Content]
; Correct English references to Hope House in the message archive.
FixWishHouse=true

; Correct English text errors in the subway, apartments, and hospital.
FixEnglishTypos=true

; Restore the missing English text at the sewer wine rack.
RestoreMissingSewerText=true

; Unlock the Silent Hill 2 references without a cleared SH2 save.
UnlockSH2EasterEggs=true

; Restore the fourteen PS2-exclusive media outlet costumes. This feature adds runtime game-memory patches.
EnablePS2ExclusiveCostumes=true

[Audio]
; Load the Silent Hill 3 Audio Enhancement Pack when installed. Restart the game after changing this setting.
EnableAudioEnhancementPack=true

[Discord]
; Enable Discord Rich Presence. Enabled by default; set false to turn it off.
EnableRichPresence=true

; Discord Developer Portal application ID. Leave empty to disable Rich Presence.
DiscordApplicationId=1567098391790489640

)SH3INI";
        return config;
    }

    std::wstring section, line;
    while (std::getline(input, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']')
        {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        if (section == L"General")
        {
            const auto equals = line.find(L'=');
            if (equals != std::wstring::npos && Trim(line.substr(0, equals)) == L"EnableLogging")
                config.enableLogging = ParseBool(Trim(line.substr(equals + 1)), config.enableLogging);
        }
        else if (section == L"System")
        {
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) continue;
            const auto key = Trim(line.substr(0, equals));
            const auto rawValue = Trim(line.substr(equals + 1));

            if (key == L"CpuAffinityMask")
            {
                ParseAffinityMask(rawValue, config.cpuAffinityMask);
                continue;
            }

            const auto value = ParseBool(rawValue, true);
            if (key == L"RegistryRedirect") config.registryRedirect = value;
            else if (key == L"WhiteBorderFix") config.fixWhiteBorder = value;
            else if (key == L"ExceptionHandler") config.exceptionHandler = value;
            else if (key == L"FixMutedAudio") config.fixMutedAudio = value;
        }
        else if (section == L"Graphics")
        {
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) continue;
            const auto key = Trim(line.substr(0, equals));
            const auto value = Trim(line.substr(equals + 1));

            if (key == L"RemoveCutsceneBorders")
            {
                config.removeCutsceneBorders = ParseBool(value, config.removeCutsceneBorders);
                continue;
            }

            if (key == L"AnisotropicTextureFiltering")
            {
                config.anisotropicTextureFiltering = ParseBool(value, config.anisotropicTextureFiltering);
                continue;
            }

            if (key == L"FontScale")
            {
                int scale = config.fontScale;
                if (ParseDimension(value, scale) && (scale == 0 || scale == 2 || scale == 4))
                    config.fontScale = scale;
                continue;
            }

            if (key == L"DisableMSAAForDoF")
            {
                config.disableMsaaForDof = ParseBool(value, config.disableMsaaForDof);
                continue;
            }

            if (key == L"DisableSafeMode")
            {
                config.disableSafeMode = ParseBool(value, config.disableSafeMode);
                continue;
            }

            if (key == L"FullscreenPauseMenu")
            {
                config.fullscreenPauseMenu = ParseBool(value, config.fullscreenPauseMenu);
                continue;
            }

            if (key == L"DOFRes" || key == L"DoFResolution")
            {
                int resolution = config.dofResolution;
                if (ParseDimension(value, resolution) && resolution <= 4096)
                    config.dofResolution = resolution;
                continue;
            }

            if (key == L"ShadowsRes")
            {
                int resolution = config.shadowsResolution;
                if (ParseDimension(value, resolution) && resolution <= 4096)
                    config.shadowsResolution = resolution;
                continue;
            }

            if (key == L"InventoryRes" || key == L"InventoryResolution")
            {
                int resolution = config.inventoryResolution;
                if (ParseDimension(value, resolution) && resolution <= 1024)
                    config.inventoryResolution = resolution;
                continue;
            }

            if (key == L"StatusScreenRes" || key == L"StatusScreenResolution")
            {
                int resolution = config.statusScreenResolution;
                if (ParseDimension(value, resolution) && resolution <= 4096)
                    config.statusScreenResolution = resolution;
                continue;
            }

            if (key == L"FogComplexity")
            {
                int complexity = config.fogComplexity;
                if (ParseDimension(value, complexity) && complexity <= 4096)
                    config.fogComplexity = complexity;
                continue;
            }

            if (key == L"FMVWidescreenMode")
            {
                int mode = config.fmvWidescreenMode;
                if (ParseDimension(value, mode) && mode <= 2)
                    config.fmvWidescreenMode = mode;
                continue;
            }

            if (key == L"FrameRateMode")
            {
                int mode = config.frameRateMode;
                if (ParseDimension(value, mode) && (mode == 0 || mode == 30 || mode == 60))
                    config.frameRateMode = mode;
                continue;
            }

            if (key == L"MirrorRoomFix")
            {
                config.mirrorRoomFix = ParseBool(value, config.mirrorRoomFix);
                continue;
            }

            if (key == L"WindowMode")
            {
                int mode = config.windowMode;
                if (ParseDimension(value, mode) && mode <= 2)
                    config.windowMode = mode;
                continue;
            }

            if (key == L"ReplaceButtonText")
            {
                int style = config.replaceButtonText;
                if (ParseDimension(value, style) && style <= 4)
                    config.replaceButtonText = style;
                continue;
            }

            int* target = nullptr;
            if (key == L"ResolutionX") target = &config.resolutionX;
            else if (key == L"ResolutionY") target = &config.resolutionY;
            else if (key == L"RenderResolutionX") target = &config.renderResolutionX;
            else if (key == L"RenderResolutionY") target = &config.renderResolutionY;
            if (target)
            {
                ParseDimension(value, *target);
                continue;
            }

            if (key == L"WindowedWidth" || key == L"WindowedHeight")
            {
                int dimension = 0;
                if (ParseDimension(value, dimension) && dimension >= 320 && dimension <= 16384)
                {
                    if (key == L"WindowedWidth") config.windowedWidth = dimension;
                    else config.windowedHeight = dimension;
                }
            }
        }
        else if (section == L"Content")
        {
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) continue;
            const auto key = Trim(line.substr(0, equals));
            const auto value = Trim(line.substr(equals + 1));
            if (key == L"FixWishHouse") config.fixWishHouse = ParseBool(value, config.fixWishHouse);
            else if (key == L"FixEnglishTypos") config.fixEnglishTypos = ParseBool(value, config.fixEnglishTypos);
            else if (key == L"RestoreMissingSewerText") config.restoreMissingSewerText = ParseBool(value, config.restoreMissingSewerText);
            else if (key == L"UnlockSH2EasterEggs") config.unlockSH2EasterEggs = ParseBool(value, config.unlockSH2EasterEggs);
            else if (key == L"EnablePS2ExclusiveCostumes") config.enablePS2ExclusiveCostumes = ParseBool(value, config.enablePS2ExclusiveCostumes);
        }
        else if (section == L"Discord")
        {
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) continue;
            const auto key = Trim(line.substr(0, equals));
            const auto value = Trim(line.substr(equals + 1));

            if (key == L"EnableRichPresence")
                config.enableRichPresence = ParseBool(value, config.enableRichPresence);
            else if (key == L"DiscordApplicationId" && value.size() <= 19 &&
                std::all_of(value.begin(), value.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                config.discordApplicationId = value;
        }
        else if (section == L"Audio")
        {
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) continue;
            const auto key = Trim(line.substr(0, equals));
            const auto value = Trim(line.substr(equals + 1));
            if (key == L"EnableAudioEnhancementPack")
                config.enableAudioEnhancementPack = ParseBool(value, config.enableAudioEnhancementPack);
        }
    }

    return config;
}

bool sh3e::SaveConfig(const std::wstring& path, const Config& config)
{
    const auto writeBool = [&](const wchar_t* section, const wchar_t* key, bool value)
        {
            return WritePrivateProfileStringW(section, key, value ? L"true" : L"false", path.c_str()) != FALSE;
        };
    const auto writeInt = [&](const wchar_t* section, const wchar_t* key, int value)
        {
            const auto text = std::to_wstring(value);
            return WritePrivateProfileStringW(section, key, text.c_str(), path.c_str()) != FALSE;
        };
    const auto writeMask = [&]()
        {
            wchar_t text[16]{};
            swprintf_s(text, L"0x%X", config.cpuAffinityMask);
            return WritePrivateProfileStringW(L"System", L"CpuAffinityMask", text, path.c_str()) != FALSE;
        };

    bool ok = true;
    ok &= writeBool(L"General", L"EnableLogging", config.enableLogging);
    ok &= writeBool(L"System", L"RegistryRedirect", config.registryRedirect);
    ok &= writeBool(L"System", L"WhiteBorderFix", config.fixWhiteBorder);
    ok &= writeBool(L"System", L"ExceptionHandler", config.exceptionHandler);
    ok &= writeBool(L"System", L"FixMutedAudio", config.fixMutedAudio);
    ok &= writeMask();
    ok &= writeBool(L"Graphics", L"AnisotropicTextureFiltering", config.anisotropicTextureFiltering);
    ok &= writeInt(L"Graphics", L"FontScale", config.fontScale);
    ok &= writeBool(L"Graphics", L"DisableMSAAForDoF", config.disableMsaaForDof);
    ok &= writeInt(L"Graphics", L"ResolutionX", config.resolutionX);
    ok &= writeInt(L"Graphics", L"ResolutionY", config.resolutionY);
    ok &= writeInt(L"Graphics", L"RenderResolutionX", config.renderResolutionX);
    ok &= writeInt(L"Graphics", L"RenderResolutionY", config.renderResolutionY);
    ok &= writeInt(L"Graphics", L"DOFRes", config.dofResolution);
    ok &= writeInt(L"Graphics", L"ShadowsRes", config.shadowsResolution);
    ok &= writeInt(L"Graphics", L"InventoryRes", config.inventoryResolution);
    ok &= writeInt(L"Graphics", L"StatusScreenRes", config.statusScreenResolution);
    ok &= writeInt(L"Graphics", L"FogComplexity", config.fogComplexity);
    ok &= writeInt(L"Graphics", L"WindowMode", config.windowMode);
    ok &= writeInt(L"Graphics", L"WindowedWidth", config.windowedWidth);
    ok &= writeInt(L"Graphics", L"WindowedHeight", config.windowedHeight);
    ok &= writeBool(L"Graphics", L"FullscreenPauseMenu", config.fullscreenPauseMenu);
    ok &= writeInt(L"Graphics", L"FMVWidescreenMode", config.fmvWidescreenMode);
    ok &= writeInt(L"Graphics", L"FrameRateMode", config.frameRateMode);
    ok &= writeBool(L"Graphics", L"MirrorRoomFix", config.mirrorRoomFix);
    ok &= writeBool(L"Graphics", L"DisableSafeMode", config.disableSafeMode);
    ok &= writeBool(L"Graphics", L"RemoveCutsceneBorders", config.removeCutsceneBorders);
    ok &= writeInt(L"Graphics", L"ReplaceButtonText", config.replaceButtonText);
    ok &= writeBool(L"Content", L"FixWishHouse", config.fixWishHouse);
    ok &= writeBool(L"Content", L"FixEnglishTypos", config.fixEnglishTypos);
    ok &= writeBool(L"Content", L"RestoreMissingSewerText", config.restoreMissingSewerText);
    ok &= writeBool(L"Content", L"UnlockSH2EasterEggs", config.unlockSH2EasterEggs);
    ok &= writeBool(L"Content", L"EnablePS2ExclusiveCostumes", config.enablePS2ExclusiveCostumes);
    ok &= writeBool(L"Audio", L"EnableAudioEnhancementPack", config.enableAudioEnhancementPack);
    ok &= writeBool(L"Discord", L"EnableRichPresence", config.enableRichPresence);
    ok &= WritePrivateProfileStringW(L"Discord", L"DiscordApplicationId", config.discordApplicationId.c_str(), path.c_str()) != FALSE;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return ok;
}
