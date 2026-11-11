#include "context.h"
#include "../logger.h"
#include "../patches/patches.h"
#include <Windows.h>
#include <string>

namespace sh3e::startup
{
DWORD WINAPI InitializePlugin(void* bootstrapParameter)
{
    auto& bootstrap = *static_cast<BootstrapState*>(bootstrapParameter);
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);

    StartupContext context{ bootstrap };
    context.executable = GetModuleHandleW(nullptr);
    context.executablePath = executablePath;
    context.gameDirectory = context.executablePath;
    const auto slash = context.gameDirectory.find_last_of(L"\\");
    
    if (slash != std::wstring::npos)
        context.gameDirectory.resize(slash);
    
    context.config = LoadConfig(context.gameDirectory + L"\\SH3Enhanced.ini");

    if (context.config.enableLogging)
        log::Initialize(context.gameDirectory);

    log::Write("INFO", "Plugin version: 1.1.0-dev.1");
    log::Write("INFO", "Silent Hill 3 Enhanced loaded.");

    if (context.config.disableMsaaForDof)
        log::Write("INFO", "[MSAA / DoF] Multisampling requests will be disabled for DoF compatibility.");
    else
        log::Write("INFO", "[MSAA / DoF] Multisampling override is disabled.");

    InitializeSystem(context);
    InitializeGraphics(context);
    InitializeContent(context);
    patches::SignalContentArchiveReady();
    InitializeController(context);
    LogStartupDiagnostics(context);

    log::Write("INFO", "[Startup timing] Plugin initialization finished.");
    return 0;
}
}
