# MODLOG: GrandTheftMinecraft

Minecraft creative mode inside GTA V Legacy: Minecraft UI and items, GTA world, and Minecraft things that
interact with GTA (blocks with collision, TNT, ender pearls, and mobs that fight NPCs).

## Facts
- Game: GTA V **Legacy**, Steam appid 271590, game build **1.0.3889.0**.
- Modding copy: `/media/gamedisk/SteamLibrary/steamapps/common/GTA Modding/`. This is a separate copy from
  Steam's `Grand Theft Auto V`, launched by the user through a **non-Steam shortcut** under Proton.
- Runs under **Proton** (prefix version 11.0-100; wine-mono, no real .NET Framework). This is why the mod is
  a native C++ ASI and not ScriptHookVDotNet.
- ScriptHookV **3889.0** (dev-c.com, `ScriptHookV_3889.0_1158.13.zip`); SDK 1.0.617.1a.
  - Legacy ASI loader: `dinput8.dll`. The `xinput1_4.dll` in the zip is the **Enhanced** loader; don't use it.
  - Proton needs `WINEDLLOVERRIDES="dinput8=n,b"`, otherwise Wine's builtin dinput8 wins and no ASI loads.
  - `args.txt` (`-nobattleye -noBE`) goes in the game folder. GTA Online is never to be touched.
- Minecraft assets come from the user's own Prism Launcher install:
  `~/.local/share/PrismLauncher/libraries/com/mojang/minecraft/1.21.11/minecraft-1.21.11-client.jar`.
  Sounds come from `~/.local/share/PrismLauncher/assets` (asset index 29). They are extracted at build time
  into `build/`, which is gitignored and never committed.
- Native hashes come from alloc8or's nativedb (`third_party/natives.json`); `tools/gen_natives.py`
  generates the wrappers.

## Route
- C++ ScriptHookV ASI, cross-compiled on Linux with mingw-w64.
  - ScriptHookV exports are MSVC-mangled, so the ASI binds them at runtime with GetProcAddress
    (`gta/src/shv.cpp`). No import lib is needed.
- Minecraft UI: SHV `createTexture`/`drawTexture` with PNGs pre-scaled with nearest-neighbour (crisp pixels).
- Stage 1 blocks: drawn with `DRAW_POLY`, using face colours sampled from the real textures with Minecraft
  face shading. Collision comes from invisible frozen stock props.
- Stage 2 (planned): an add-on DLC pack (`mods/` folder, OpenIV.asi) with real textured 1 m cube props per
  block. GTA then gives them lighting, shadows and collision.
- Stage 3 (planned): mobs. Invisible combat peds with Minecraft box-part props attached to their bones, so
  GTA's walk animations move the limbs.

## Known engine limits
- Around 1500 script objects crashes GTA (passthrough field note). Collision props are capped at 350 by
  default and stream in nearest-first.
- drawTexture: at most 64 on-screen instances per texture.

## Stages
- [ ] Stage 1: ASI + MC HUD/inventory + placeholder blocks + TNT + ender pearl + flint&steel + sword + flight
- [ ] Stage 2: DLC block props (real textures, lighting)
- [ ] Stage 3: mobs vs NPCs, spawn eggs
- [ ] Stage 4: bow/arrows, more items, sounds polish, chat commands

## Log
- 2026-10-04: recon. Chose a C++ ASI (Proton) and OpenIV for stage 2 (user OK'd it).
