# MODLOG: GrandTheftMinecraft

Minecraft creative mode inside GTA V Legacy: Minecraft UI and items, GTA world, and Minecraft things that
interact with GTA (blocks with collision, TNT, ender pearls, and mobs that fight NPCs).

## Facts
- Game: GTA V **Legacy**, Steam appid 271590, game build **1.0.3889.0**.
- Modding copy: `/media/gamedisk/SteamLibrary/steamapps/common/GTA Modding/` on Linux,
  **`D:\SteamLibrary\steamapps\common\GTA Modding`** on Windows (gamedisk = D:). This is a separate copy from
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
- C++ ScriptHookV ASI, built with mingw-w64 (Linux: cross; Windows: `scoop install mingw`, GCC 16.2). `./build.sh`.
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
- 2026-10-04 (Windows session): toolchain = scoop mingw + ffmpeg, pip Pillow/numpy (no MSVC installed; VS 2022
  folder is empty). `tools/gen_natives.py` run: 6701 natives, compiles clean. `tools/extract_mc.py` written and run:
  69 blocks, 3 items, 46 particles, 48 sounds into `build/GrandTheftMinecraft/` (iso icons verified on a contact
  sheet). Stage 1 code written (modules: log, config, input, render2d, items, world, blockrender, collision, fx,
  interact, flight, gui, audio, main). `install.ps1` (+ `-Remove`, manifest-based) written. Not yet run in game.
  In-game test keys: F6 mode, F9 debug overlay, F10 drawTexture calibration, F11 DRAW_POLY stress (0/2k/5k/10k/20k).

## Uninstall (what install.ps1 adds to the game folder)
`ScriptHookV.dll`, `dinput8.dll`, `args.txt` (only if it created them), `GrandTheftMinecraft.asi`,
`GrandTheftMinecraft\` (data, `gtm.log`, `world.txt`, `config.ini`). `install.ps1 -Remove` deletes exactly those and
copies the world/config into `_gtm_backup\`.
- 2026-10-04: installed stage 1 into GTA Modding (user approved). User launches PlayGTAV.exe from that folder and tests themselves.
- 2026-10-04 test 1 (user): HUD/hotbar size and crispness correct (drawTexture maths sizeX=w/W, sizeY=h/W, factor
  W/H is RIGHT). TNT works, cars collide with blocks (invisible frozen prop_box_wood01a 0.965x0.965x0.795 collides).
  Stunt block stt_prop_stunt_bblock_sml1 is 3.6x8.4x0.4, not a cube. Problems: blocks look "cursed" (4x4 colour
  grids), GTA's first-person arm shows (door-reach IK), held items too big/off-screen, inventory cursor invisible
  (SHV drawTexture draws over GTA's cursor). Fixes: 16x16 near detail with row-run merging, budget 20000,
  SET_ENTITY_LOCALLY_INVISIBLE in first person, own cursor sprite, smaller hand items.
- 2026-10-04 test 2: full-res textures correct, but **DRAW_POLY is depth-tested against GTA's world, not against other
  DRAW_POLYs** (no depth write): far blocks drew over near ones. Fix: painter's order (plan detail near-first for the
  budget, emit far to near). GTA's arm still appears (phone calls) → SET_ENTITY_VISIBLE(ped,false) while in first
  person. Held item is now 3D (hand.cpp: Minecraft's ItemInHandRenderer + firstperson_righthand transforms, x/y scaled
  by tan(fov/2)/tan(35deg)); offline harness gta/tests/hand_test.cpp fakes SHV and records DRAW_POLY,
  render_tris.py rasterizes it. Python on Windows: always open source files with encoding='utf-8' (a cp1252 write
  failure truncated blockrender.cpp once; restored from git).
- 2026-10-04 test 3: painter's order fixed overlap. User: low-res LOD kicks in too close → full 16x16 out to FullDetailDistance=24 m (halving per doubling), budget 30000. Sword sweep sprite removed on request.
- 2026-10-04: placing a block inside a car launched it out of sight (frozen prop depenetration). Fix: make_room() in interact.cpp pushes overlapping vehicles/peds (OBB from GET_ENTITY_MATRIX + model dims, SAT vs cells) to the nearest free spot up to 6 m, else lifts them on top.
- 2026-10-04 Stage 2 build: OpenIV installed for the user (OpenIV.asi at %LOCALAPPDATA%\New Technology Studio\Apps\
  OpenIV\Games\Five\x64). tools/gtmpack (net8, CodeWalker.Core @485d56b, netstandard2.0) builds 69 gtm_<block>.ydr
  (24-vert 1 m cube, centred, default/cutout/alpha shader, embedded 512x128 A8R8G8B8 DDS [top|side|bottom], Box bound
  margin 0.04) + gtm_blocks.ytyp (flags 32, lodDist 300) into dlc.rpf, layout copied from mpbusiness2
  (RPF_FILE props/gtm_blocks.rpf + DLC_ITYP_REQUEST props/gtm_blocks.ityp, GROUP_STARTUP changeset). Templates found by
  exporting vanilla prop_cs_cardbox_01.ydr (plain box, default.sps, verts CCW from outside). CodeWalker can't write NG
  without generating encrypt tables (slow) -> archives are OPEN (OpenIV.asi loads them). dlclist entry added to a
  mods\update\update.rpf copy (SetEncryptionType OPEN). Gotchas: user's global NuGet config has a dead local source
  (project nuget.config with <clear/>); prop_box_wood01a is a .yft not .ydr. Runtime: collision.cpp spawns visible
  gtm_* props for exposed blocks within PropRadius 150 (cap 900, camera-nearest); polygons only for the rest.
