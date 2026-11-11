# Silent Hill 3 Audio Enhancement Pack

SH3 Enhanced can load the pack's AFS audio replacements without Reloaded II.

## Install

1. Extract the Silent Hill 3 Audio Enhancement Pack.
2. Copy its `Mods\Silent Hill 3 Audio Enhancement Pack\FEmulator` folder into:

   ```text
   <Silent Hill 3 folder>\SH3 Enhanced Data\Audio Enhancement Pack\FEmulator
   ```

3. Start the game with SH3 Enhanced installed.

When **Enable Silent Hill 3 Audio Enhancement Pack** is enabled, SH3 Enhanced creates the `SH3 Enhanced Data\Audio Enhancement Pack` folder automatically if it does not exist. You can copy `FEmulator` into that folder before starting the game.

The pack is enabled by default when installed. To compare with the original PC audio, uncheck **Enable Silent Hill 3 Audio Enhancement Pack** on the Configuration Tool's **Content** tab, or set `EnableAudioEnhancementPack=false` in the `[Audio]` section of `SH3Enhanced.ini`. Restart the game after changing the setting.

SH3 Enhanced rebuilds the modified AFS archives into `SH3 Enhanced Data\Audio Enhancement Pack\Cache` and redirects the game to those copies. The original game archives remain unchanged. The first launch with the pack installed needs about 1.5 GiB of free space for the cache; later launches reuse it. Delete the `Cache` folder to rebuild it after changing the pack or game archives.

The audio AFS replacements are loaded. The pack's optional loose picture replacements are not included in this feature.
