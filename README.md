# Silent Hill 3 Enhanced

A community-made enhancement project for Silent Hill 3 PC, fixing bugs, improving graphics, and restoring content cut from the PC release.

> **Not affiliated with Konami, the Silent Hill 2 Enhanced Edition project, or Steam006's Silent Hill 3 PC Fix.**
> This is an independent project built from scratch.

---

## Planned Features

### Graphics
- [ ] **Widescreen Fix** — Correct FOV and aspect ratio for modern monitors
- [ ] **Custom Resolution** — Set any screen and render resolution
- [ ] **Remove Black Bars** — Removes the permanent black bars on screen
- [ ] **Remove Cutscene Borders** — Removes the borders added during cutscenes
- [ ] **4:3 2D Elements** — Keeps HUD and menus at 4:3 while gameplay uses full widescreen
- [ ] **Fullscreen Pause Menu** — Stretches the pause menu to fill the screen
- [ ] **FMV Fix** — FMVs displayed at correct 4:3 aspect ratio and optionally fullscreen
- [ ] **DoF Resolution** — Increases depth of field render resolution (default: 1024)
- [ ] **Shadow Resolution** — Increases shadow map resolution (default: 1024)
- [ ] **Inventory Resolution** — Increases inventory background resolution (default: 1024)
- [ ] **Status Screen Resolution** — Increases status screen resolution (default: 1024)
- [ ] **Anisotropic Texture Filtering** — Enables highest supported AF level
- [ ] **Fog Control** — Adjusts fog complexity to match PS2 quality
- [ ] **Motion Trail Fix** — Fixes dark motion trails caused by bloom on light sources
- [ ] **Animated Wall Texture Fix** — Fixes broken animated textures when using d3d8to9

### Performance / Framerate
- [x] **60 FPS Mode** — Stable 60fps with proper game speed
- [x] **Multiple FPS Modes** — 30fps / 60fps / unlocked
- [ ] **Mirror Room Fix** — Forces 30fps in the hospital storeroom where effects require it
- [ ] **MSAA / DoF Fix** — Disables MSAA which corrupts the depth of field effect; recommends FXAA instead

### System / Stability
- [x] **Registry Redirect** — Redirects HKEY_LOCAL_MACHINE registry reads to HKEY_CURRENT_USER, fixing "game not installed properly" error without admin rights
- [x] **Safe Mode Disable** — Prevents the game from reverting settings after a crash or Alt+F4
- [ ] **Options Menu Freeze Fix** — Fixes the game hanging in the options menu
- [ ] **CPU Affinity Fix** — Configurable CPU core affinity to fix frame rate fluctuation
- [ ] **Windowed / Borderless Mode** — Run the game in a window or borderless fullscreen
- [ ] **White Border Fix** — Fixes white border rendering on Windows 10/11
- [ ] **d3d8to9** — Converts DirectX 8 to DirectX 9 for better compatibility and performance
- [ ] **Exception Handler** — Installs a crash handler that writes minidump files

### Audio
- [ ] **Restore Missing Dialogue** — Restores dialogue audio removed from the PC release
- [ ] **Restore Beta Sounds** — Optionally restores cut beta sound effects
- [ ] **Audio Pulse Fix** — Replaces unreliable PulseEvent audio sync
- [ ] **Muted Audio Fix** — Fixes audio getting muted after a crash during the TV or Lisa scene

### Content / Text
- [ ] **"Wish House" Fix** — Corrects all instances of "Hope House" to "Wish House" (EN)
- [ ] **Typo Fixes** — Fixes several typos across subway, apartments, and hospital levels (EN)
- [ ] **Missing Sewer Text** — Restores untranslated text in the sewer level (EN)
- [ ] **Unlock SH2 Easter Eggs** — Unlocks Silent Hill 2 references without completing the game

---

## Installation

1. Install Silent Hill 3 PC
2. Download the latest [SH3Enhanced release](https://github.com/amiraliuks/silent-hill-3-enhanced/releases)
3. Extract all files into the Silent Hill 3 root directory (where `sh3.exe` is)
4. Launch the game normally

SH3Enhanced uses [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — no separate launcher needed.

> **Supported executable:** `sh3.exe` CRC32 `372ae792`

---

## Configuration

Edit `SH3Enhanced.ini` in the game directory to toggle features. All options are documented inside the file.

---

## Building from Source

**Requirements:**
- Visual Studio 2022 or newer
- Windows SDK
- Platform: Win32 (x86) — SH3 is a 32-bit process

**Steps:**
1. Clone the repo
2. Open `SH3Enhanced.sln`
3. Set configuration to `Release | Win32`
4. Build — output is `SH3Enhanced.asi`

---

## Credits

- **Polymega** — SilentHillDatabase (memory address research)
- **07151129** — sh3proxy (d3d8 wrapper reference)
- **Steam006** — Silent Hill 3 PC Fix (bug research reference)
- **Nemesis2000** — Original SH2 fog fix technique
- **ThirteenAG** — Ultimate ASI Loader, widescreen fix research
- **elishacloud** — Silent Hill 2 Enhanced Edition (project structure reference)

---

## Disclaimer
This project does not include any game files. You must own a legitimate copy of Silent Hill 3 PC.