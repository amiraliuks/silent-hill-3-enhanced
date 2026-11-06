#include "discord_presence.h"
#include "game_state.h"
#include "logger.h"

#include <discord_game_sdk.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

namespace sh3e
{
namespace
{
    constexpr auto kRetryDelay = std::chrono::seconds(5);
    constexpr auto kCallbackDelay = std::chrono::milliseconds(100);
    constexpr auto kStatePollDelay = std::chrono::seconds(1);
    std::atomic<bool> g_running = false;

    using DiscordCreateFunction = EDiscordResult (DISCORD_API*)(
        DiscordVersion, DiscordCreateParams*, IDiscordCore**);

    HMODULE LoadSdk()
    {
        wchar_t executablePath[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return nullptr;

        std::wstring sdkPath(executablePath, length);
        const auto separator = sdkPath.find_last_of(L"\\/");
        
        if (separator == std::wstring::npos)
            return nullptr;

        sdkPath.resize(separator + 1);
        sdkPath += L"discord_game_sdk.dll";
        return LoadLibraryW(sdkPath.c_str());
    }

    void DISCORD_CALLBACK LogSdkMessage(void*, EDiscordLogLevel level, const char* message)
    {
        const char* label = level == DiscordLogLevel_Error ? "ERROR" : level == DiscordLogLevel_Warn ? "WARN" : "INFO";
        log::Write(label, std::string("[Discord Rich Presence] ") + message);
    }

    void DISCORD_CALLBACK LogUpdateResult(void*, EDiscordResult result)
    {
        if (result != DiscordResult_Ok)
            log::Write("WARN", "[Discord Rich Presence] Activity update failed with SDK result " + std::to_string(static_cast<int>(result)) + ".");
    }

    IDiscordCore* CreateDiscordCore(std::int64_t applicationId, HMODULE sdk)
    {
        const auto create = reinterpret_cast<DiscordCreateFunction>(GetProcAddress(sdk, "DiscordCreate"));

        if (create == nullptr)
        {
            log::Write("WARN", "[Discord Rich Presence] DiscordCreate export was not found in the Game SDK.");
            return nullptr;
        }

        DiscordCreateParams params{};
        DiscordCreateParamsSetDefault(&params);
        params.client_id = applicationId;
        params.flags = DiscordCreateFlags_NoRequireDiscord;

        IDiscordCore* core = nullptr;
        const auto result = create(DISCORD_VERSION, &params, &core);
        if (result != DiscordResult_Ok || core == nullptr)
        {
            log::Write("INFO", "[Discord Rich Presence] SDK initialization unavailable (result " +
                std::to_string(static_cast<int>(result)) + "); retrying.");
            return nullptr;
        }

        core->set_log_hook(core, DiscordLogLevel_Warn, nullptr, LogSdkMessage);
        return core;
    }

    bool PublishActivity(IDiscordCore* core, const GameState& state, DiscordTimestamp sessionStart)
    {
        auto* manager = core->get_activity_manager(core);
        if (manager == nullptr)
            return false;

        DiscordActivity activity{};
        activity.type = DiscordActivityType_Playing;
        const std::string location = state.location.empty() ? "Exploring Silent Hill" : state.location;
        strcpy_s(activity.details, location.c_str());

        if (state.hasHealth && !state.isMainMenu)
            strcpy_s(activity.state, state.health.c_str());

        activity.timestamps.start = sessionStart;
        activity.instance = false;
        manager->update_activity(manager, &activity, nullptr, LogUpdateResult);
        return true;
    }

    void RunPresence(std::int64_t applicationId)
    {
        log::Write("INFO", "[Discord Rich Presence] Starting with the official Discord Game SDK.");
        const auto sessionStart = static_cast<DiscordTimestamp>(std::time(nullptr));

        while (g_running.load())
        {
            const auto sdk = LoadSdk();
            if (sdk == nullptr)
            {
                log::Write("INFO", "[Discord Rich Presence] Could not load discord_game_sdk.dll; retrying.");
                std::this_thread::sleep_for(kRetryDelay);
                continue;
            }

            auto* core = CreateDiscordCore(applicationId, sdk);
            if (core == nullptr)
            {
                FreeLibrary(sdk);
                std::this_thread::sleep_for(kRetryDelay);
                continue;
            }

            log::Write("INFO", "[Discord Rich Presence] SDK initialized.");
            auto currentState = ReadGameState();
            PublishActivity(core, currentState, sessionStart);
            auto nextStatePoll = std::chrono::steady_clock::now() + kStatePollDelay;
            
            while (g_running.load())
            {
                if (core->run_callbacks(core) != DiscordResult_Ok)
                    break;

                if (std::chrono::steady_clock::now() >= nextStatePoll)
                {
                    const auto nextState = ReadGameState();
                    if (nextState.location != currentState.location || nextState.health != currentState.health)
                    {
                        const auto health = nextState.health.empty() ? "unavailable" : nextState.health;
                        log::Write("INFO", "[Discord Rich Presence] " + nextState.location + " | " + health +
                            " (group " + std::to_string(nextState.locationGroup) + ", id " +
                            std::to_string(nextState.locationId) + ").");
                        PublishActivity(core, nextState, sessionStart);
                        currentState = nextState;
                    }

                    nextStatePoll = std::chrono::steady_clock::now() + kStatePollDelay;
                }

                std::this_thread::sleep_for(kCallbackDelay);
            }

            core->destroy(core);
            FreeLibrary(sdk);
            std::this_thread::sleep_for(kRetryDelay);
        }
    }
}

bool StartDiscordPresence(const std::wstring& applicationId)
{
    if (applicationId.empty())
        return false;

    if (applicationId.size() > 19 || applicationId.find_first_not_of(L"0123456789") != std::wstring::npos)
    {
        log::Write("WARN", "[Discord Rich Presence] Application ID must be a decimal number with at most 19 digits.");
        return false;
    }

    std::int64_t id = 0;
    try
    {
        id = std::stoll(applicationId);
    }
    catch (const std::exception&)
    {
        log::Write("WARN", "[Discord Rich Presence] Application ID is outside the supported integer range.");
        return false;
    }

    if (id <= 0)
    {
        log::Write("WARN", "[Discord Rich Presence] Application ID must be greater than zero.");
        return false;
    }

    if (g_running.exchange(true))
        return true;

    std::thread(RunPresence, id).detach();
    return true;
}
}