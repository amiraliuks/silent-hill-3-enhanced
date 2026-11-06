#include "context.h"
#include "../logger.h"
#include "../patches/patches.h"

namespace sh3e::startup
{
void InitializeGraphics(StartupContext& context)
{
    const auto& config = context.config;
    const auto executable = context.executable;
    context.screen = GetInitialScreenState(config);

    if ((config.renderResolutionX > 0) != (config.renderResolutionY > 0))
        log::Write("WARN", "[Render resolution] Set both RenderResolutionX and RenderResolutionY; leaving the internal size under game control.");
        
    if ((config.resolutionX > 0) != (config.resolutionY > 0))
        log::Write("WARN", "[Resolution] Set both ResolutionX and ResolutionY; leaving output mode under game control.");

    const bool resolutionInstalled = patches::InstallResolutionPatches(executable, config, context.screen);
    if (context.screen.overrideOutputResolution && !resolutionInstalled)
        log::Write("WARN", "[Resolution] One or more output/render/FMV patches were not fully installed.");

    const bool widescreenInstalled = patches::InstallWidescreenPatch(executable, context.screen);
    if (context.screen.aspectRatio > (4.0f / 3.0f) && !widescreenInstalled)
        log::Write("WARN", "[Widescreen] Aspect correction was not fully installed.");

    if (config.fullscreenPauseMenu &&
        !patches::InstallFullscreenPauseMenuPatch(executable, context.screen) &&
        context.screen.aspectRatio > (4.0f / 3.0f))
        log::Write("WARN", "[Fullscreen pause menu] Widescreen pause-screen patch was not installed.");

    const bool dofResolutionInstalled = patches::InstallDofResolutionPatch(executable, config.dofResolution);
    if (config.dofResolution > 0 && !dofResolutionInstalled)
        log::Write("WARN", "[DoF resolution] Requested depth-of-field resolution was not installed.");

    const bool shadowResolutionInstalled = patches::InstallShadowResolutionPatch(executable, config.shadowsResolution);
    if (config.shadowsResolution > 0 && !shadowResolutionInstalled)
        log::Write("WARN", "[Shadow resolution] Requested shadow-map resolution was not installed.");

    const bool inventoryResolutionInstalled = patches::InstallInventoryResolutionPatch(executable, config.inventoryResolution);
    if (config.inventoryResolution > 0 && !inventoryResolutionInstalled)
        log::Write("WARN", "[Inventory quad] Requested coordinate patch was not installed.");

    const bool statusScreenResolutionInstalled = patches::InstallStatusScreenResolutionPatch(executable, config.statusScreenResolution);
    if (config.statusScreenResolution > 0 && !statusScreenResolutionInstalled)
        log::Write("WARN", "[Status screen resolution] Requested image resolution was not installed.");

    const bool fogControlInstalled = patches::InstallFogControlPatch(executable, config.fogComplexity);
    if (config.fogComplexity > 0 && !fogControlInstalled)
        log::Write("WARN", "[Fog control] Requested fog coefficient was not installed.");

    if (config.removeCutsceneBorders && !patches::InstallCutsceneBordersPatch(executable))
        log::Write("WARN", "[Cutscene borders] Removal patch was not installed.");

    if (!patches::InstallFrameRatePatch(executable, config.frameRateMode))
        log::Write("WARN", "[Frame rate] Requested timing mode was not installed.");
    const bool mirrorRoomFixInstalled = patches::InstallMirrorRoomPatch(executable, config.mirrorRoomFix, config.frameRateMode);
    if (config.mirrorRoomFix && !mirrorRoomFixInstalled)
        log::Write("WARN", "[Mirror Room] Room-specific 30 FPS limiter was not installed.");
}
}