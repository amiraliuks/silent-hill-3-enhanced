# Xidi runtime

This directory contains the x86 `Xidi.32.dll` runtime from Xidi v5.0.0.

The build copies the runtime beside `SH3Enhanced.asi` in `build/Win32/Release`. For game use, place it beside the x86 Ultimate ASI Loader `dinput8.dll` in the Silent Hill 3 folder. Ultimate ASI Loader recognizes `Xidi.32.dll` as its Xidi companion module, so the loader keeps its `dinput8.dll` filename and continues loading ASI plugins.

Xidi provides DirectInput and WinMM interfaces backed by XInput controllers. SH3 imports `DirectInput8Create`, so this bridges XInput controllers to the input API the game uses. No `Xidi.ini` is required for its default mapping. Rename or remove `Xidi.32.dll` to disable the bridge.

Upstream: <https://github.com/samuelgr/Xidi>
Release: <https://github.com/samuelgr/Xidi/releases/tag/v5.0.0>
License: see [LICENSE](LICENSE) (BSD 3-Clause).
