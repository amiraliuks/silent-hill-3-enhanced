#include "context.h"
#include "../logger.h"
#include "../patches/patches.h"

namespace sh3e::startup
{
void InitializeContent(StartupContext& context)
{
    const auto& config = context.config;
    const bool hasTextFixes = config.fixWishHouse || config.fixEnglishTypos || config.restoreMissingSewerText;

    if (hasTextFixes)
        log::Write("INFO", "[Startup timing] Beginning message-archive preparation.");

    const bool contentFixesInstalled = hasTextFixes && patches::InstallContentTextPatches(context.gameDirectory, config);

    if (hasTextFixes)
    {
        log::Write("INFO", "[Startup timing] Finished message-archive preparation.");
        if (!contentFixesInstalled)
            log::Write("WARN", "[Content] One or more text patches could not be installed.");
    }

    if (config.unlockSH2EasterEggs && !patches::InstallSH2EasterEggUnlock(context.executable))
        log::Write("WARN", "[SH2 Easter eggs] Save-data override was not installed.");

    if (!patches::InstallOptionsMenuFreezeFix(context.executable))
        log::Write("WARN", "[Options] Freeze fix was not installed.");

    if (config.disableSafeMode)
    {
        if (context.bootstrap.safeModeFixInstalled)
            log::Write("INFO", "[Safe mode] Settings-reset path disabled before game startup.");
        else
            log::Write("WARN", "[Safe mode] Could not disable the settings-reset path before game startup.");
    }
}
}