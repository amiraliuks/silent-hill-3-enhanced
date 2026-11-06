#pragma once
#include <Windows.h>

namespace sh3e::startup
{
    struct BootstrapState
    {
        HMODULE pluginModule = nullptr;
        bool registryRedirectInstalled = false;
        bool windowModeHooksInstalled = true;
        bool safeModeFixInstalled = false;
    };

    DWORD WINAPI InitializePlugin(void* bootstrapParameter);
}