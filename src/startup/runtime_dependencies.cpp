#include "runtime_dependencies.h"

#include "../config.h"
#include "../logger.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace
{
    struct MissingDependency
    {
        const wchar_t* feature;
        std::wstring path;
    };

    bool FileExists(const std::wstring& path)
    {
        const auto attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring JoinPath(const std::wstring& directory, const wchar_t* fileName)
    {
        auto path = directory;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            path.push_back(L'\\');
        path += fileName;
        return path;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};
        const auto required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
            return {};
        std::string result(static_cast<size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
        result.resize(static_cast<size_t>(required - 1));
        return result;
    }

    std::wstring FormatWindowsError(unsigned long errorCode)
    {
        wchar_t* buffer = nullptr;
        const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorCode, 0,
            reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message = length != 0 && buffer != nullptr ? buffer : L"Unknown Windows error";
        if (buffer != nullptr)
            LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            message.pop_back();
        return message;
    }
}

sh3e::RuntimeDependencyStatus sh3e::CheckRuntimeDependencies(const Config& config, const std::wstring& gameDirectory)
{
    RuntimeDependencyStatus status{};
    std::vector<MissingDependency> missing;

    // Xidi support has no INI toggle; ReplaceButtonText only affects the separate prompt renderer.
    const auto xidiPath = JoinPath(gameDirectory, L"Xidi.32.dll");
    if (FileExists(xidiPath))
        log::Write("INFO", "[Runtime dependencies] XInput controller support is enabled; found Xidi.32.dll.");
    else
        missing.push_back({ L"XInput controller support", xidiPath });

    if (config.enableRichPresence && !config.discordApplicationId.empty())
    {
        const auto discordPath = JoinPath(gameDirectory, L"discord_game_sdk.dll");
        if (FileExists(discordPath))
        {
            status.discordPresenceAvailable = true;
            log::Write("INFO", "[Runtime dependencies] Discord Rich Presence is enabled; found discord_game_sdk.dll.");
        }
        else
        {
            missing.push_back({ L"Discord Rich Presence", discordPath });
        }
    }
    else
    {
        log::Write("INFO", "[Runtime dependencies] Discord Rich Presence is disabled or has no application ID; the Game SDK was not checked.");
    }

    if (!missing.empty())
    {
        std::wstring warning = L"SH3 Enhanced could not find a runtime file needed by an enabled feature:";
        for (const auto& dependency : missing)
        {
            log::Write("WARN", "[Runtime dependencies] Missing runtime file: " +
                ToUtf8(dependency.path) + " (" + ToUtf8(dependency.feature) + ").");
            warning += L"\n\n" + dependency.path + L"\nRequired for " + dependency.feature + L".";
        }
        warning += L"\n\nThe affected optional feature will be disabled. The game will continue.";
        MessageBoxW(nullptr, warning.c_str(), L"SH3 Enhanced runtime files missing",
            MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }

    return status;
}

void sh3e::ReportRuntimeDependencyLoadFailure(const wchar_t* feature, const std::wstring& path, unsigned long errorCode)
{
    const auto error = FormatWindowsError(errorCode);
    log::Write("WARN", "[Runtime dependencies] Could not load runtime file: " +
        ToUtf8(path) + " (" + ToUtf8(feature) + "); " +
        std::to_string(errorCode) + ": " + ToUtf8(error) + ".");

    std::wstring warning = L"SH3 Enhanced could not load a runtime file for ";
    warning += feature;
    warning += L":\n\n" + path + L"\n\nWindows error ";
    warning += std::to_wstring(errorCode) + L": " + error +
        L"\n\nThat optional feature will be disabled. The game will continue.";
    MessageBoxW(nullptr, warning.c_str(), L"SH3 Enhanced runtime file could not load",
        MB_OK | MB_ICONWARNING | MB_TOPMOST);
}
