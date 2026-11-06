#include "screen_state.h"
#include <Windows.h>

sh3e::ScreenState sh3e::GetInitialScreenState(const Config& config)
{
    ScreenState screen;
    const auto desktopWidth = GetSystemMetrics(SM_CXSCREEN);
    const auto desktopHeight = GetSystemMetrics(SM_CYSCREEN);
    const bool explicitOutputResolution = config.resolutionX > 0 && config.resolutionY > 0;
    
    screen.autoOutputResolution = config.resolutionX == 0 && config.resolutionY == 0 && config.windowMode != 1;
    screen.overrideOutputResolution = explicitOutputResolution || screen.autoOutputResolution || config.windowMode == 1;
    screen.width = explicitOutputResolution ? config.resolutionX : (config.windowMode == 1 ? config.windowedWidth : desktopWidth);
    screen.height = explicitOutputResolution ? config.resolutionY : (config.windowMode == 1 ? config.windowedHeight : desktopHeight);
    const bool hasRenderResolution = config.renderResolutionX > 0 && config.renderResolutionY > 0;
    screen.renderWidth = hasRenderResolution ? config.renderResolutionX : 0;
    screen.renderHeight = hasRenderResolution ? config.renderResolutionY : 0;

    if (screen.width > 0 && screen.height > 0)
    {
        screen.widthF = static_cast<float>(screen.width);
        screen.heightF = static_cast<float>(screen.height);
        screen.aspectRatio = screen.widthF / screen.heightF;
        screen.hudScale = (4.0f / 3.0f) / screen.aspectRatio;
        screen.hudOffset = (screen.widthF - screen.heightF * (4.0f / 3.0f)) * 0.5f;
    }
    
    return screen;
}