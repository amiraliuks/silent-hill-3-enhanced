# PS2 exclusive costumes

The converted PS2 model pairs and their notice are kept under `assets/ps2-exclusive-costumes/`. They do not include Konami's PC game archives. The archive preparation tool adds the models to copies of the selected PC installation's `chrpl.arc` archive and compressed `arc.arc` catalog. The generated archives go under `SH3 Enhanced Data\PS2 Exclusive Costumes\data`; the installed game archives are not overwritten.

## Prepare local archives

From the repository root, run:

```powershell
node tools/prepare-ps2-costume-archives.mjs `
  "F:\Silent Hill Collection\Silent Hill 3" `
  "D:\Development\ps2_game\costume-prototype" `
  "F:\Silent Hill Collection\Silent Hill 3\SH3 Enhanced Data\PS2 Exclusive Costumes\data"
```

The input folder must contain these converted files for each costume: `<PS2 model>_pc_prototype.mdl` and `<PS2 model>_pc_chhaa_prototype.mdl`. The script reports an error and stops if a required model is missing, the PC archives are malformed, or the new paths already exist.

At startup, SH3 Enhanced detects both prepared archives, redirects the game to them, fills the 14 empty player-model resource slots, and adds the PS2 passwords to the existing password lookup in the supported PC executable. Entering a password unlocks its outfit but leaves Heather's selected costume unchanged; choose an unlocked outfit from **Extra Costume**. With no prepared archives present, the original archives and costume table are used.

The password-to-costume mapping is:

| Costume | Password | Costume ID | Success-message ID |
| --- | --- | ---: | ---: |
| EGM | `EGMpretaporter` | 5 | `0x35` |
| Gamespy.com | `iaml33t` | 9 | `0x34` |
| 13ème RUE | `Suspense` | 4 | `0x2D` |
| PSM | `badical` | 16 | `0x33` |
| OPM | `SH3_OPiuM` | 13 | `0x32` |
| GamePro | `ProTip` | 7 | `0x2F` |
| Game Informer | `gameinformer` | 6 | `0x30` |
| IGN.COM | `IGN_pickleboy` | 12 | `0x2E` |
| GMR | `GMRownzjoo` | 10 | `0x31` |
| Play | `sLmLdGhSmKfBfH` | 14 | `0x2C` |
| GN | `IwannabeaGJ` | 11 | `0x2A` |
| PS2RO | `MATADOR` | 15 | `0x40` |
| GAMEREACTOR | `SH3_Wrestlarn` | 8 | `0x29` |
| OPS2 | `extra_thumbs` | 28 | `0x2B` |

## Current limits

The imported and converted model pairs are still prototypes. The costume IDs are paired with the model-resource slots used by the supported PC executable's outfit table. All password unlocks and wardrobe entries have been confirmed in-game; outfit model rendering and alternate-model behavior still need a complete stability pass. Keep a backed-up save for testing; entering a password updates the game's costume-unlock state.
