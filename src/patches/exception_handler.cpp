#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <cwchar>
#include <string>

namespace sh3e::patches
{
namespace
{
    std::wstring g_dumpDirectory;
    volatile LONG g_dumpInProgress = 0;

    LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* exceptionPointers)
    {
        if (InterlockedCompareExchange(&g_dumpInProgress, 1, 0) != 0)
            return EXCEPTION_EXECUTE_HANDLER;

        SYSTEMTIME time{};
        GetLocalTime(&time);

        wchar_t fileName[96]{};
        if (_snwprintf_s(fileName, _countof(fileName), _TRUNCATE,
                         L"sh3_%04u%02u%02u_%02u%02u%02u_%lu.dmp",
                         time.wYear, time.wMonth, time.wDay,
                         time.wHour, time.wMinute, time.wSecond,
                         GetCurrentProcessId()) < 0)
        {
            OutputDebugStringW(L"[SH3Enhanced] Could not format the crash dump filename.\n");
            return EXCEPTION_EXECUTE_HANDLER;
        }

        const auto dumpPath = g_dumpDirectory + L"\\" + fileName;
        const auto dumpFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dumpFile == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringW(L"[SH3Enhanced] Could not create the crash dump file.\n");
            return EXCEPTION_EXECUTE_HANDLER;
        }

        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;

        const auto written = MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
            exceptionPointers ? &exceptionInfo : nullptr, nullptr, nullptr);

        CloseHandle(dumpFile);
        if (!written)
        {
            DeleteFileW(dumpPath.c_str());
            OutputDebugStringW(L"[SH3Enhanced] MiniDumpWriteDump failed.\n");
        }
        else
        {
            OutputDebugStringW(L"[SH3Enhanced] Wrote a crash dump to the crash_dumps folder.\n");
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }
}

bool InstallExceptionHandler(const std::wstring& gameDirectory)
{
    if (gameDirectory.empty())
    {
        log::Write("WARN", "[Exception handler] Game directory is empty; crash dumps are unavailable.");
        return false;
    }

    g_dumpDirectory = gameDirectory + L"\\crash_dumps";
    if (!CreateDirectoryW(g_dumpDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        log::Write("WARN", "[Exception handler] Could not create the crash_dumps directory.");
        g_dumpDirectory.clear();
        return false;
    }

    SetUnhandledExceptionFilter(WriteCrashDump);
    log::Write("INFO", "[Exception handler] Unhandled-crash minidumps enabled in crash_dumps.");
    return true;
}
}
