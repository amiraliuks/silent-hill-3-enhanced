#pragma once
#include "../config.h"
#include "../screen_state.h"
#include <Windows.h>

namespace sh3e::patches
{
    bool InstallOptionsMenuFreezeFix(HMODULE executable);
    bool InstallSafeModeDisable(HMODULE executable);
    bool InstallResolutionPatches(HMODULE executable, const Config& config, const ScreenState& screen);
    bool InstallWidescreenPatch(HMODULE executable, const ScreenState& screen);
    bool InstallFullscreenPauseMenuPatch(HMODULE executable, const ScreenState& screen);
    bool InstallDofResolutionPatch(HMODULE executable, int resolution);
    bool InstallShadowResolutionPatch(HMODULE executable, int resolution);
    bool InstallInventoryResolutionPatch(HMODULE executable, int resolution);
    bool InstallStatusScreenResolutionPatch(HMODULE executable, int resolution);
    bool InstallFogControlPatch(HMODULE executable, int complexity);
    bool SetFogComplexity(int complexity);
    bool InstallCutsceneBordersPatch(HMODULE executable);
    bool InstallFrameRatePatch(HMODULE executable, int mode);
    bool InstallMirrorRoomPatch(HMODULE executable, bool enabled, int frameRateMode);
    bool InstallContentFileRedirectHooks(HMODULE executable);
    void SignalContentArchiveReady();
    bool InstallContentTextPatches(const std::wstring& gameDirectory, const Config& config);
    bool InstallSH2EasterEggUnlock(HMODULE executable);
    bool InstallRegistryRedirect(HMODULE executable, const std::wstring& gameDirectory);
    bool InstallWhiteBorderFix(HMODULE executable);
    bool ApplyCpuAffinity(std::uint32_t affinityMask);
    bool InstallExceptionHandler(const std::wstring& gameDirectory);
    bool SeedRegistryRedirectPaths();
    bool RegistryRedirectWasUsed();
    bool InstallWindowModeHooks(HMODULE executable, int mode, int width, int height, bool anisotropicTextureFiltering);
    bool InstallAnisotropicFiltering(void* device, bool enabled);
    bool InstallControllerButtonIcons(HMODULE executable, int style, HMODULE pluginModule);
    bool HookControllerButtonIconsDevice(void* device);
    bool SetAnisotropicFilteringLive(bool enabled);
    void ReapplyAnisotropicFiltering(void* device);
    void SetMsaaDoFFixEnabled(bool enabled);
    bool EnforceNoMultisampling(DWORD* multiSampleType);
}