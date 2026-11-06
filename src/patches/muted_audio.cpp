#include "muted_audio.h"
#include "../logger.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>

namespace
{
    constexpr DWORD kPollIntervalMs = 1000;
    constexpr int kStartupPollCount = 30;

    template <typename T>
    void Release(T*& value)
    {
        if (value)
        {
            value->Release();
            value = nullptr;
        }
    }

    bool RestoreMutedSessionsOnDevice(IMMDevice* device, DWORD gameProcessId, bool& gameSessionFound)
    {
        IAudioSessionManager2* manager = nullptr;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&manager))))
            return false;

        IAudioSessionEnumerator* sessions = nullptr;
        if (FAILED(manager->GetSessionEnumerator(&sessions)))
        {
            Release(manager);
            return false;
        }

        int count = 0;
        sessions->GetCount(&count);
        bool restored = false;
        for (int index = 0; index < count; ++index)
        {
            IAudioSessionControl* session = nullptr;
            if (FAILED(sessions->GetSession(index, &session)))
                continue;

            IAudioSessionControl2* sessionDetails = nullptr;
            DWORD processId = 0;
            if (SUCCEEDED(session->QueryInterface(__uuidof(IAudioSessionControl2),
                reinterpret_cast<void**>(&sessionDetails))) &&
                SUCCEEDED(sessionDetails->GetProcessId(&processId)) && processId == gameProcessId)
            {
                gameSessionFound = true;
                ISimpleAudioVolume* volume = nullptr;
                if (SUCCEEDED(session->QueryInterface(__uuidof(ISimpleAudioVolume),
                    reinterpret_cast<void**>(&volume))))
                {
                    BOOL muted = FALSE;
                    const GUID eventContext{};
                    if (SUCCEEDED(volume->GetMute(&muted)) && muted &&
                        SUCCEEDED(volume->SetMute(FALSE, &eventContext)))
                        restored = true;
                    Release(volume);
                }
            }

            Release(sessionDetails);
            Release(session);
        }

        Release(sessions);
        Release(manager);
        return restored;
    }

    bool RestoreMutedGameSessions(bool& gameSessionFound)
    {
        IMMDeviceEnumerator* devices = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&devices))))
            return false;

        IMMDeviceCollection* outputs = nullptr;
        if (FAILED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &outputs)))
        {
            Release(devices);
            return false;
        }

        UINT count = 0;
        outputs->GetCount(&count);
        bool restored = false;
        const DWORD gameProcessId = GetCurrentProcessId();
        for (UINT index = 0; index < count; ++index)
        {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(outputs->Item(index, &device)))
            {
                restored |= RestoreMutedSessionsOnDevice(device, gameProcessId, gameSessionFound);
                Release(device);
            }
        }

        Release(outputs);
        Release(devices);
        return restored;
    }

    DWORD WINAPI MutedAudioRecoveryThread(void*)
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
            return 0;

        for (int attempt = 0; attempt < kStartupPollCount; ++attempt)
        {
            bool gameSessionFound = false;
            if (RestoreMutedGameSessions(gameSessionFound))
            {
                sh3e::log::Write("INFO", "[Muted audio] Restored SH3's muted Windows audio session at startup.");
                break;
            }

            if (gameSessionFound)
                break;
            Sleep(kPollIntervalMs);
        }

        CoUninitialize();
        return 0;
    }
}

bool sh3e::patches::StartMutedAudioRecovery()
{
    const HANDLE thread = CreateThread(nullptr, 0, MutedAudioRecoveryThread, nullptr, 0, nullptr);
    if (!thread)
        return false;

    CloseHandle(thread);
    sh3e::log::Write("INFO", "[Muted audio] Startup recovery is active; checking for a persisted SH3 audio-session mute.");
    return true;
}
