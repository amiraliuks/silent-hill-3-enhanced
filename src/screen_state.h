#pragma once
#include "config.h"
namespace sh3e
{
    struct ScreenState
    {
        int width = 0, height = 0, renderWidth = 0, renderHeight = 0;
        bool overrideOutputResolution = false;
        bool autoOutputResolution = false;
        float widthF = 0.0f, heightF = 0.0f, aspectRatio = 4.0f / 3.0f;
        float hudScale = 1.0f, hudOffset = 0.0f;
    };
    ScreenState GetInitialScreenState(const Config& config);
}