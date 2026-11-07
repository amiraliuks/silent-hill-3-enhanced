#include "context.h"
#include "../discord_presence.h"
#include "../logger.h"
#include "../patches/patches.h"
#include "../patches/patch_utils.h"
#include "../patches/muted_audio.h"
#include "runtime_dependencies.h"
#include <cstdint>

namespace sh3e::startup
{
void InitializeSystem(StartupContext& context)
{
    const auto& config = context.config;
    if (config.exceptionHandler && !patches::InstallExceptionHandler(context.gameDirectory))
        log::Write("WARN", "[Exception handler] Could not install the minidump crash handler.");

    if (config.fixMutedAudio && !patches::StartMutedAudioRecovery())
        log::Write("WARN", "[Muted audio] Could not start Windows audio-session recovery.");

    log::Write("INFO", "Executable module base: " + patches::detail::Hex(reinterpret_cast<std::uintptr_t>(context.executable)));
    const auto runtimeDependencies = CheckRuntimeDependencies(config, context.gameDirectory);
    if (config.enableRichPresence && runtimeDependencies.discordPresenceAvailable)
        StartDiscordPresence(config.discordApplicationId);

    if (config.registryRedirect)
    {
        if (!context.bootstrap.registryRedirectInstalled)
            log::Write("WARN", "[Registry redirect] Registry-open hooks were not installed.");
        else if (!patches::SeedRegistryRedirectPaths())
            log::Write("WARN", "[Registry redirect] Per-user install paths were not fully prepared.");
    }

    if (config.fixWhiteBorder && !patches::InstallWhiteBorderFix(context.executable))
        log::Write("WARN", "[White border] Windows border correction was not installed.");

    if (!patches::ApplyCpuAffinity(config.cpuAffinityMask) && config.cpuAffinityMask != 0)
        log::Write("WARN", "[CPU affinity] Requested processor mask was not applied.");

    const char* modeName = config.windowMode == 0 ? "fullscreen" : (config.windowMode == 1 ? "windowed" : "borderless fullscreen");
    if (context.bootstrap.windowModeHooksInstalled)
        log::Write("INFO", std::string("[Window mode] ") + modeName + " mode hooks installed; waiting for the game window.");
    else
        log::Write("WARN", std::string("[Window mode] Could not install ") + modeName + " hooks.");
}
}
