# GrandTheftMinecraft

Minecraft creative mode inside GTA V **Legacy** story mode: the Minecraft hotbar, inventory, items and blocks, in
GTA's world. Built as a native ScriptHookV ASI (C++), so it also runs under Proton.

Bring your own game files: the Minecraft textures and sounds are extracted from **your** Minecraft install at build
time. Nothing from Minecraft, GTA or ScriptHookV is in this repo.

## Build
1. Toolchain: mingw-w64 (`scoop install mingw` on Windows), Python 3 with Pillow + numpy, ffmpeg.
2. `python tools/extract_mc.py` (defaults to `mc_source/minecraft-1.21.11-client.jar` + `mc_source/assets`; see
   `--help` to point at `%APPDATA%\.minecraft` instead). Writes `build/GrandTheftMinecraft/`.
3. `./build.sh` → `build/GrandTheftMinecraft.asi`.
4. Stage 2 (textured block props): .NET 8 SDK and CodeWalker's source in `third_party/CodeWalker`
   (`git clone https://github.com/dexyfex/CodeWalker`), then `dotnet build -c Release tools/gtmpack` and
   `tools/gtmpack/bin/Release/net8.0-windows/gtmpack.exe build "<GTA folder>" build/dlc_src build/dlc/gtm/dlc.rpf`.
   The GTA folder is only read for the archive keys (from your own GTA5.exe).

## Install (GTA V Legacy, Steam)
Use a **separate copy** of the game for mods, never GTA Online.
```
powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir "D:\SteamLibrary\steamapps\common\GTA Modding"
```
It installs ScriptHookV 3889.0 + the Legacy ASI loader (`dinput8.dll`), `args.txt` (`-nobattleye`), the ASI and the
`GrandTheftMinecraft\` data folder. Stage 2 also copies `OpenIV.asi` from your OpenIV install, puts the block pack
in `mods\update\x64\dlcpacks\gtm\dlc.rpf` and registers it in `mods\update\update.rpf` (a copy of the game's; the
original is never modified). `-NoDlc` skips Stage 2. Launch `PlayGTAV.exe` in that folder and pick **Story Mode**.

Uninstall: `install.ps1 -Remove` (your world and config are copied to `_gtm_backup\`).

Linux/Proton: launch options `WINEDLLOVERRIDES="dinput8=n,b" %command%`.

## Controls (Minecraft mode, on by default)
| Key | Action |
|---|---|
| F6 | Minecraft mode on/off |
| 1-9, mouse wheel | hotbar slot |
| LMB | break block / hit (sword: 7 hearts, knock-back) |
| RMB | place block / use item (ender pearl, flint & steel) |
| MMB | pick block |
| E | creative inventory (tabs, 1-9 over an item to put it in the hotbar, Esc closes) |
| Space ×2 | creative flight (Space up, Ctrl down, Shift faster) |
| F9 / F10 / F11 | debug overlay / GUI calibration / DRAW_POLY stress test |

Settings: `GrandTheftMinecraft\config.ini`. Log: `GrandTheftMinecraft\gtm.log`. Builds: `world.txt`.

Built with Claude Code (AI-assisted).
