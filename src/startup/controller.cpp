#include "context.h"
#include "../logger.h"
#include "../patches/patches.h"

namespace sh3e::startup
{
void InitializeController(StartupContext& context)
{
    const bool buttonIconsInstalled = patches::InstallControllerButtonIcons(
        context.executable, context.config.replaceButtonText, context.bootstrap.pluginModule);
    if (context.config.replaceButtonText != 0 && !buttonIconsInstalled)
        log::Write("WARN", "[Controller icons] Control Options button-icon patch could not be installed.");
}
}