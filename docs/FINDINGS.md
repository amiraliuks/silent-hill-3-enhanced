# FINDINGS

---

## 1. Safe Mode Patch
Silent Hill 3 PC stores a crash flag in savedata\disp.ini. When the game exits abnormally (crash, Alt+F4), it writes SafeMode=1 to that file. On the next launch it reads the flag, shows the dialog "A crash occurred the last time the game was played. Using safe graphics options." and resets all graphics settings to defaults.

I found three code locations responsible for this behavior using x32dbg and signature scanning.

#### **Patch 1** - Crash flag write (0x0042F624)
The instruction MOV BYTE PTR DS:[0x72D8E4], 1 writes the crash flag when the game exits abnormally. 
Pattern: ```C6 05 ?? ?? ?? ?? 01 83 C4```. We NOP 7 bytes to prevent the flag from ever being written.

#### Patch 2 - Safe mode branch (0x00412E36)
The instruction CMP BYTE PTR DS:[addr], 1 followed by JE checks the flag on startup. 
Pattern: ```80 3D ?? ?? ?? ?? 01 74```. The scanner finds the CMP, the JE is 7 bytes in. We NOP the JE (2 bytes) so the game never jumps to the settings reset code path.

#### Patch 3 - disp.ini write (0x0042F5B7)
A PUSH 0 / PUSH 0 / PUSH sequence followed by a WritePrivateProfileStringA call writes SafeMode=1 to disp.ini on abnormal exit. Pattern: ```6A 00 6A 00 68 ?? ?? ?? ?? 8D 4C 24```. We NOP 16 bytes to prevent the write entirely.

Additionally we write SafeMode=0 directly to disp.ini from DllMain synchronously before any game code runs, as a safety net in case the patches load too late.

---