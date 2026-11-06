#include "context.h"
#include "../logger.h"
#include <Windows.h>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sh3e::startup
{
namespace
{
    std::uint32_t Crc32(const std::vector<std::uint8_t>& data)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const auto byte : data)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        
        return ~crc;
    }

    void LogExecutableIdentity(const StartupContext& context)
    {
        const auto file = CreateFileW(context.executablePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        const auto size = GetFileSize(file, nullptr);
        std::vector<std::uint8_t> bytes(size);
        DWORD read = 0;
        const bool loaded = ReadFile(file, bytes.data(), size, &read, nullptr) != FALSE && read == size;
        CloseHandle(file);
        if (!loaded || bytes.size() < sizeof(IMAGE_DOS_HEADER))
            return;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            bytes.size() < static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32))
            return;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
        std::ostringstream info;
        info << "Executable file size: " << size << ", PE timestamp: 0x" << std::hex << std::uppercase << nt->FileHeader.TimeDateStamp
             << ", CRC32: 0x" << Crc32(bytes) << ", machine: 0x" << nt->FileHeader.Machine;
        log::Write("INFO", info.str());
    }

    void LogDisplaySummary(const StartupContext& context)
    {
        const auto& screen = context.screen;
        const auto& config = context.config;
        std::ostringstream display;
        if (screen.autoOutputResolution)
            display << "Game output/backbuffer resolution follows desktop: " << screen.width << 'x' << screen.height;
        else if (screen.overrideOutputResolution)
            display << "Configured game output/backbuffer resolution: " << screen.width << 'x' << screen.height;
        else
            display << "Desktop resolution: " << screen.width << 'x' << screen.height
                    << ", output-mode override disabled";
        display << ", aspect ratio: " << std::fixed << std::setprecision(6) << screen.aspectRatio;
        if (config.windowMode == 0)
            display << ", fullscreen mode";
        else if (config.windowMode == 1)
            display << ", windowed mode";
        else if (config.windowMode == 2)
            display << ", borderless fullscreen mode";
        if (screen.renderWidth > 0 && screen.renderHeight > 0)
            display << ", configured internal render-target resolution: " << screen.renderWidth << 'x' << screen.renderHeight;
        else
            display << ", internal render-target override: disabled (game default)";
        log::Write("INFO", display.str());
    }

}

void LogStartupDiagnostics(const StartupContext& context)
{
    LogExecutableIdentity(context);
    LogDisplaySummary(context);
}
}