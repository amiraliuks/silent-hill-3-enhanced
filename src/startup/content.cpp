#include "context.h"
#include "../logger.h"
#include "../patches/audio_enhancement_pack.h"
#include "../patches/patches.h"
#include <utility>
#include <vector>

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

    std::vector<std::pair<std::wstring, std::wstring>> audioRedirects;
    if (config.enableAudioEnhancementPack &&
        patches::PrepareAudioEnhancementPack(context.gameDirectory, audioRedirects) && !audioRedirects.empty())
        patches::SetAudioArchiveRedirects(audioRedirects);

#if !defined(SH3E_DISABLE_PS2_COSTUMES)
    if (config.enablePS2ExclusiveCostumes)
    {
        if (!patches::InstallPS2CostumePasswordTable(context.executable))
            log::Write("WARN", "[PS2 costumes] Password-only fallback could not be installed.");
#if !defined(SH3E_DISABLE_PS2_COSTUME_MODELS)
        std::vector<std::pair<std::wstring, std::wstring>> costumeArchiveRedirects;
        if (patches::PreparePS2CostumeArchiveRedirects(context.gameDirectory, costumeArchiveRedirects) &&
            !costumeArchiveRedirects.empty())
        {
            if (patches::InstallPS2CostumeModelRuntime(context.executable) &&
                patches::InstallPS2CostumeResourcePaths(context.executable))
                patches::SetCostumeArchiveRedirects(costumeArchiveRedirects);
            else
                log::Write("WARN", "[PS2 costumes] Model archive redirects were not enabled; passwords remain available.");
        }
#endif
    }
    else
        log::Write("INFO", "[PS2 costumes] Disabled in configuration; costume passwords and model patches were skipped.");
#endif

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
