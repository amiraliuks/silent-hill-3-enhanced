#pragma once

#include <string>

namespace sh3e
{
    struct Config;

    struct RuntimeDependencyStatus
    {
        bool discordPresenceAvailable = false;
    };

    RuntimeDependencyStatus CheckRuntimeDependencies(const Config& config, const std::wstring& gameDirectory);
    void ReportRuntimeDependencyLoadFailure(const wchar_t* feature, const std::wstring& path, unsigned long errorCode);
}
