#pragma once
#include <cstdint>
#include <string>

namespace sh3e
{
    struct Config
    {
        bool enableLogging = true;
        bool registryRedirect = true;
        bool fixWhiteBorder = true;
        bool exceptionHandler = true;
        bool fixMutedAudio = true;
        bool anisotropicTextureFiltering = true;
        bool disableMsaaForDof = true;
        bool removeCutsceneBorders = true;
        bool disableSafeMode = true;
        bool mirrorRoomFix = true;
        bool fullscreenPauseMenu = true;
        bool fixWishHouse = true;
        bool fixEnglishTypos = true;
        bool restoreMissingSewerText = true;
        bool unlockSH2EasterEggs = true;
        bool freeCameraEnabled = false;
        std::uint32_t cpuAffinityMask = 0;
        std::uint32_t freeCameraToggleKey = 0x21;
        std::wstring discordApplicationId = L"1567098391790489640";
        int fmvWidescreenMode = 2;
        int frameRateMode = 60;
        int resolutionX = 0;
        int resolutionY = 0;
        int renderResolutionX = 0;
        int renderResolutionY = 0;
        int dofResolution = 1024;
        int shadowsResolution = 1024;
        int inventoryResolution = 0;
        int statusScreenResolution = 1024;
        int fogComplexity = 75;
        int windowMode = 0;
        int windowedWidth = 1280;
        int windowedHeight = 720;
        int replaceButtonText = 3;
    };
    Config LoadConfig(const std::wstring& path);
    bool SaveConfig(const std::wstring& path, const Config& config);
}