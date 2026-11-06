# Silent Hill 3 Enhanced

<p>
  <a href="https://github.com/amiraliuks/silent-hill-3-enhanced"><img alt="GitHub repository" src="https://img.shields.io/badge/GitHub-Silent%20Hill%203%20Enhanced-181717?style=for-the-badge&logo=github"></a>
  <img alt="Personal project" src="https://img.shields.io/badge/Personal%20project-independent-6f1010?style=for-the-badge">
  <img alt="Platform: Windows" src="https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows">
  <img alt="Architecture: x86" src="https://img.shields.io/badge/Target-x86-444444?style=for-the-badge">
</p>

<p align="center">
  <img src="https://i.imgur.com/Sbdf12A.png" alt="Silent Hill 3 Enhanced in-game screenshot" width="900">
</p>

SH3 Enhanced is a project that makes the PC version of *Silent Hill 3* run properly on modern systems, with widescreen, 60 FPS, controller support, and fixes for long-standing bugs.

The mod runs as an ASI plugin and does not include game data. It is an independent project and is not affiliated with or endorsed by Konami.

<p align="center">
  <a href="#features">Features</a> - <a href="#planned-work">Planned work</a> - <a href="#installation">Installation</a> - <a href="#configuration">Configuration</a> - <a href="#build">Build</a> - <a href="#project-layout">Project layout</a>
</p>

## Features

### Graphics

- Widescreen aspect-ratio and field-of-view correction, with gameplay side bars removed.
- Windowed and borderless fullscreen modes, plus configurable output and internal render resolutions.
- Experimental 4:3 interface positioning; disabled by default because it can misalign mouse selection. The pause menu can independently expand to widescreen.
- Cutscene border removal and three FMV display modes, including 16:9 sources.
- Configurable depth-of-field, shadow-map, and status-screen image resolutions.
- Highest supported anisotropic filtering and adjustable fog complexity.
- MSAA request suppression to prevent depth-of-field corruption; no post-processing injector is used.
- `InventoryRes` adjusts one pause-menu quad's horizontal bounds. It does not upscale textures and remains off by default because it can cause a translucent rectangle in the Status preview.

### Controls

- XInput controller support through the bundled 32-bit Xidi-to-DirectInput bridge.
- Kenney controller prompts for generic, Xbox, PlayStation, and Nintendo layouts. Keyboard bindings remain text.

### Performance and stability

- 30 FPS, 60 FPS, and unlocked timing modes, with a 30 FPS cap for the hospital mirror storeroom when using 60 FPS mode.
- Options-menu freeze fix, safe-mode reset prevention, Windows audio-session mute recovery, and a Windows 10/11 bright-border fix.
- Optional CPU affinity mask and crash minidumps.

### Content

- English typo corrections, restored sewer wine-rack text, and the “Hope House” to “Wish House” correction.
- Unlocks the four Silent Hill 2 references without requiring a cleared SH2 save.
- Redirects SH3 registry paths to the folder containing the running game.

The features above have been implemented; some graphics behavior and compatibility still depend on the game executable and display setup. The experimental `InventoryRes` behavior is called out separately because it has a known visual artifact.

## Planned work

- Upscale the title/splash artwork and establish a reversible replacement workflow. (`data/pic/sy/sys_title.tex` (512×512); the opening movie is a separate asset)
- Identify and assess the inventory backdrop and item atlases. The known item atlases are already 1024×1024.
- Add a safe way to load custom picture replacements, including files stored in `pic.arc`, without editing original game files.
- Implement Achievement system, using the achievements of Silent Hill HD Collection.
- Find way to use the [Silent Hill 3 Audio Enhancement Pack](https://www.moddb.com/mods/silent-hill-3-audio-enhancement-pack) without installing [Reloaded II](https://github.com/Reloaded-Project/Reloaded-II).
- Leave audio restoration for later: investigate missing dialogue, optional beta sounds, and the `PulseEvent` synchronization issue.

## Installation

1. Use a legitimate copy of the Windows PC release of *Silent Hill 3*.
2. Put the x86 [`dinput8.dll` build of Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) beside `sh3.exe`.
3. Copy `SH3Enhanced.asi`, `SH3Enhanced.ini`, `discord_game_sdk.dll`, and `Xidi.32.dll` from `build/Win32/Release` beside `sh3.exe`.
4. Start the game. If a feature does not load, check `SH3Enhanced.log` beside `sh3.exe`.

The supported game executable has CRC32 `372AE792`; other versions may not match the patch signatures.

## Configuration

Use the **SH3 Enhanced Configuration Tool** or edit `SH3Enhanced.ini` directly. The tool can save settings beside a selected `sh3.exe` and launch the game; it does not install the ASI plugin or loader. Settings apply after restarting the game.

<p align="center">
  <img src="https://i.imgur.com/T7YkD6t.png" alt="SH3 Enhanced Configuration Tool" width="850">
</p>

Edit `SH3Enhanced.ini` in the game folder. These are the settings most users may want to change:

| Setting | Default | Description |
| --- | --- | --- |
| `ResolutionX`, `ResolutionY` | `0, 0` | Output/backbuffer size. `0, 0` follows the desktop; set both to request a size. |
| `RenderResolutionX`, `RenderResolutionY` | `0, 0` | Internal 3D render-target size, independent of output size. `0, 0` leaves it under game control. |
| `WindowMode` | `0` | `0` fullscreen, `1` windowed, `2` borderless fullscreen. |
| `WindowedWidth`, `WindowedHeight` | `1280, 720` | Window client size when `WindowMode=1` and no output size is forced. |
| `Keep2DElementsAt4x3` | `false` | Experimental 4:3 positioning for supported interface elements. It can misalign mouse selection, so it is off by default. |
| `FullscreenPauseMenu` | `true` | Expand the pause screen to widescreen independently of the 4:3 interface setting. |
| `FMVWidescreenMode` | `2` | `0` original 4:3 framing, `1` crop to fill widescreen, `2` use 16:9 video sources. |
| `DOFRes` | `1024` | Depth-of-field/otherworld-corrosion target resolution; `0` uses the game default. Maximum `4096`. |
| `ShadowsRes` | `1024` | Dynamic shadow-map resolution; `0` uses the game default. Maximum `4096`. |
| `InventoryRes` | `0` | Experimental horizontal bounds for one pause-menu quad, not texture resolution. Maximum `1024`. |
| `StatusScreenRes` | `1024` | Status-screen image resolution; `0` uses the game default. Maximum `4096`. |
| `FogComplexity` | `75` | Fog detail coefficient; `25` is the executable default and `0` leaves the game unchanged. |
| `FrameRateMode` | `60` | `0` unlocked, `30`, or `60` FPS timing. |
| `MirrorRoomFix` | `true` | Temporarily cap the mirror storeroom at 30 FPS. Requires `FrameRateMode=60`. |
| `ReplaceButtonText` | `3` | Controller prompts: `0` game labels, `1` generic, `2` Xbox, `3` PlayStation, `4` Nintendo. |
| `AnisotropicTextureFiltering` | `true` | Use the highest anisotropy level supported by the device. |
| `DisableMSAAForDoF` | `true` | Prevent multisample requests that can corrupt depth of field. Does not add FXAA. |
| `RemoveCutsceneBorders` | `true` | Remove the borders added to in-engine cutscenes. |
| `DisableSafeMode` | `true` | Prevent graphics settings from being reset after an abnormal exit. |
| `CpuAffinityMask` | `0` | `0` leaves Windows scheduling unchanged; a nonzero bitmask limits game threads to selected logical processors. |
| `ExceptionHandler` | `true` | Write a timestamped Windows minidump after an unhandled crash. |
| `RegistryRedirect` | `true` | Redirect SH3 install-path reads to the current user's registry key and running game folder. |
| `WhiteBorderFix` | `true` | Remove the extended window edge that can appear bright on Windows 10/11. |
| `FixMutedAudio` | `true` | Clear a persisted Windows audio-session mute when SH3 starts. |
| `FixWishHouse`, `FixEnglishTypos`, `RestoreMissingSewerText` | `true` | Enable the corresponding English text corrections. |
| `UnlockSH2EasterEggs` | `true` | Show the four SH2 references without a completed SH2 save. |
| `EnableLogging` | `true` | Write `SH3Enhanced.log` beside `sh3.exe`. |

For example, set `ResolutionX=1920` and `ResolutionY=1080` to request a 1920×1080 output buffer. Set both render-resolution values separately only if you also want to force the internal render size. In exclusive fullscreen, SH3 may switch the display mode to match a forced output size. Windowed and borderless modes avoid that exclusive display-mode switch.

`CpuAffinityMask` applies to the whole game process. Limiting processors is an optional workaround to compare on systems with stutter or freezes; it is not a guaranteed performance improvement.

The text fixes use a temporary patched copy of `data\msg.arc`; the original archive remains unchanged.

## Build

Requirements: Windows, Visual Studio with the MSVC v145 toolset, the Windows SDK, and the Win32/x86 target.

Build `sh2e.slnx` with `Release | Win32`. The plugin is written to `build/Win32/Release/SH3Enhanced.asi`.

## Project layout

- `src/dllmain.cpp` initializes the plugin and installs patch modules.
- `src/startup/` coordinates system, content, graphics, controller, and diagnostic startup phases; early hooks remain in `DllMain`.
- `src/patches/` contains the individual game patches.
- `SH3Enhanced.ini` documents plugin settings.
- `config-tool/` contains the Windows Configuration Tool.
- `docs/DATABASE.md` indexes executable locations and research references.
- `docs/reverse-engineering.md` records executable findings and unverified behavior.
- `third_party/` contains the Discord Game SDK, Xidi, and selected Kenney controller prompts.
- `tools/BuildControllerIconAtlas.ps1` regenerates the embedded controller prompt atlas.
- `web/` contains the static project website.

The Discord Rich Presence implementation uses the 32-bit Game SDK and reports the current area. Discord must be running and Rich Presence must be enabled for the application in the Discord Developer Portal. The SDK is archived, and its successor does not support the game's 32-bit process.

## Credits

[**Amir Aliu**](https://github.com/amiraliuks) created and maintains SH3 Enhanced, including its game patches, configuration tool, and project website.

[**ThirteenAG**](https://github.com/ThirteenAG) for the [WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) research used as a behavior reference. SH3 Enhanced's patch logic is independently implemented.

**Steam006** - for the [Silent Hill 3 PC Fix](https://community.pcgamingwiki.com/files/file/1331-silent-hill-3-pc-fix-by-steam006/) research, a useful reference when checking known PC-version issues and fixes.

[**Polymega**](https://github.com/Polymega) for [SilentHillDatabase](https://github.com/Polymega/SilentHillDatabase) and its memory-address research, which helps locate and verify relevant data in the Silent Hill 3 executable.

[**07151129**](https://github.com/07151129) for [sh3proxy](https://github.com/07151129/sh3proxy) research that informed how SH3 Enhanced bypasses the Silent Hill 2 save requirement for its extra content. The project uses its own implementation.

### Additional software

[**ThirteenAG**](https://github.com/ThirteenAG) for [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), used to load the SH3 Enhanced plugin beside the game's executable.

[**Samuel Grossman**](https://github.com/samuelgr) for [Xidi](https://github.com/samuelgr/Xidi) lets XInput controllers work through Silent Hill 3's DirectInput interface. The Xidi license is included with the project.

[**Discord Game SDK**](https://discord.com/developers/docs/game-sdk/sdk-starter-guide) - Optional setting for Discord Rich Presence, which can show the current area and Heather's health when available.

### Additional assets

[**Kenney**](https://kenney.nl/) for the controller button, stick, and d-pad icons use CC0 artwork from the [Input Prompts](https://kenney.nl/assets/input-prompts) pack.

## Disclaimer

This project is unofficial and is not affiliated with or endorsed by Konami. It contains no game data, music, video, or other Silent Hill 3 assets. You must provide your own copy of the game.

This project contains AI-assisted code.

AI-assisted code is reviewed, tested, and refined as part of the development process. The project remains an independent, personal effort focused on learning C++, reverse engineering, and understanding the inner workings of Silent Hill 3.

## Project website

The website source is in [`web/`](web/). Its homepage brings together project features, visual comparisons, installation and configuration guides, FAQs, and these credits.
