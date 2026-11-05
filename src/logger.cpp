#include "logger.h"
#include <Windows.h>
#include <atomic>
#include <fstream>
#include <mutex>

namespace
{
    std::mutex g_mutex;
    std::wstring g_directory;
    std::atomic<ULONGLONG> g_startupTick{ 0 };

    ULONGLONG StartupElapsedMilliseconds()
    {
        const auto start = g_startupTick.load(std::memory_order_relaxed);
        return start == 0 ? 0 : GetTickCount64() - start;
    }

    std::wstring LogPath()
    {
        auto path = g_directory;
        if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
        return path + L"SH3Enhanced.log";
    }
}

void sh3e::log::SetStartupStart()
{
    ULONGLONG expected = 0;
    g_startupTick.compare_exchange_strong(expected, GetTickCount64(), std::memory_order_relaxed);
}

void sh3e::log::Initialize(const std::wstring& directory)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_directory = directory;
    std::wofstream file(LogPath(), std::ios::out | std::ios::app);
    if (file) file << L"\r\n[SH3Enhanced] [+" << StartupElapsedMilliseconds()
        << L" ms] Logging initialized.\r\n";
}

void sh3e::log::Write(const char* level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_directory.empty()) return;
    std::ofstream file(LogPath(), std::ios::out | std::ios::app);
    if (file) file << "[SH3Enhanced] [+" << StartupElapsedMilliseconds()
        << " ms] [" << level << "] " << message << "\r\n";
}