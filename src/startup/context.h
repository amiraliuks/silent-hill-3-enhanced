#pragma once
#include "startup.h"
#include "../config.h"
#include "../screen_state.h"
#include <string>

namespace sh3e::startup
{
    struct StartupContext
    {
        BootstrapState& bootstrap;
        HMODULE executable = nullptr;
        std::wstring executablePath;
        std::wstring gameDirectory;
        Config config;
        ScreenState screen;
    };

    void InitializeSystem(StartupContext& context);
    void InitializeContent(StartupContext& context);
    void InitializeGraphics(StartupContext& context);
    void InitializeController(StartupContext& context);
    void LogStartupDiagnostics(const StartupContext& context);
}