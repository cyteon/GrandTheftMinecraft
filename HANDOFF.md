# HANDOFF: GrandTheftMinecraft (Linux session → Windows agent)

You're picking this up from a Claude Code session that ran on the user's **Linux** install (same PC, dual
boot). The user rebooted into Windows so you can run GTA, OpenIV and CodeWalker natively, and drive and
screenshot the game yourself. Read this whole file, then `MODLOG.md` (the running journal: keep it updated).

The `universal-modder` plugin is installed for the user. Load its `mod-any-game` skill first, then
`game-automation` before driving the game. The skill's GTA field note is
`knowledge/games/gta-v/minecraft-passthrough.md` inside the plugin; read its gotchas list.

---

## 1. What the user wants (their words, condensed)

> "Basically mod Minecraft into GTA 5. The UI should be Minecraft, the world should be GTA, but it should
> interact with Minecraft things you place. Mobs should attack NPCs, ender pearls should work, etc. Only
> creative mode for now. ScriptHookVDotNet C# or something else if better (then tell me). When done, tell
> me how to install it into GTA 5 Legacy (Steam). Do it in stages and I'll test. If you need
> assets/textures, tell me what to get and where to put it."

- **Reference images:** `claude-refrence/gta1.png`, `gta2.png`, `gta3.png` (frames from a TikTok by
  @obradyg).
  - Textured 1 m Minecraft blocks standing on a Los Santos street, lit by GTA's lighting.
  - The Minecraft hotbar at the bottom: firework, diamond sword, ender pearl ×16, obsidian, bow, TNT ×64,
    tripwire hook, creeper spawn egg, and so on.
  - Minecraft held items (3D voxel firework, enchanted bow) at the bottom corners.
  - A crosshair.
  - A Wither boss with a purple boss bar exploding things on a GTA street.
  - Minecraft explosion/smoke particles in GTA.
- **The user's answers so far:**
  - **Runtime:** "choose what you think is best to implement as much as you can". The choice was a native
    C++ ASI, because SHVDN needs .NET 4.8, which is flaky under Proton. On Windows SHVDN would work fine,
    but the user may also play on Linux/Proton later, so **stay on the C++ ASI** unless the user says
    otherwise.
  - **OpenIV / add-on DLC pack for real textured props: "Yes, OpenIV is fine".**
  - **Launch:** on Linux they start the `GTA Modding` copy through a **non-Steam shortcut**. Ask how they
    launch it on Windows (probably `PlayGTAV.exe` in that folder, or swapping folders).
- **Stages:** the user wants staged deliveries they can test, with exact install steps each time.

---

## 2. Facts established (verified on disk)

| Thing | Value |
|---|---|
| Game | GTA V **Legacy**, Steam appid 271590, `GTA5.exe` **1.0.3889.0** (`versioninfo.txt`) |
| Steam install | `<gamedisk>\SteamLibrary\steamapps\common\Grand Theft Auto V` (vanilla, leave alone) |
| **Modding copy (target)** | `<gamedisk>\SteamLibrary\steamapps\common\GTA Modding` (vanilla, no ScriptHook yet; the user called it "GTA Modded", but it's really "GTA Modding") |
| Gamedisk | NTFS partition, mounted as `/media/gamedisk` on Linux. Find its drive letter on Windows (D:/E:?) |
| BattlEye | `GTA5_BE.exe` + `BattlEye\` present. Must run with `-nobattleye` (ScriptHookV's `args.txt`) |
| ScriptHookV | **3889.0** (`ScriptHookV_3889.0_1158.13.zip`), matches the build. SDK `ScriptHookV_SDK_1.0.617.1a.zip`. Both downloaded into `third_party/` |
| ASI loader | Legacy uses **`dinput8.dll`** from the SHV zip. The zip's `xinput1_4.dll` is the **Enhanced** ASI loader; do NOT install it on Legacy |
| Minecraft source assets | The user's own MC 1.21.11, copied for you to `<gamedisk>\GrandTheftMinecraft\mc_source\` (see §6). Never commit or redistribute them |
| Native DB | `third_party/natives.json` (alloc8or/gta5-nativedb-data, master) |

**Downloading ScriptHookV from scripts:** dev-c.com rejects requests without browser headers. Send a
Chrome User-Agent, `Accept: text/html`, and `Referer: https://www.dev-c.com/gtav/scripthookv/`. The files
are already in `third_party/` anyway.

**Linux-only note (for when the user plays on Linux again):** under Proton the launch options need
`WINEDLLOVERRIDES="dinput8=n,b" %command%`, or Wine's builtin dinput8 wins and no ASI loads.

---

## 3. Architecture decided

### Route
A ScriptHookV **native C++ ASI** (`GrandTheftMinecraft.asi`), plus a data folder
`GTA Modding\GrandTheftMinecraft\` (PNG textures, WAV sounds, `items.txt`, `config.ini`, `world.txt` save,
`gtm.log`). Stage 2 adds an add-on DLC pack through OpenIV's `mods\` folder.

### Why not the passthrough route from the plugin's example
The example runs real Minecraft next to GTA and composites it in with ReShade. The user wants Minecraft
*inside* GTA with GTA physics and interaction, like the reference. A reimplementation of creative-mode
mechanics in a GTA script fits that better and is far simpler to install. Reuse ideas and natives from
`examples/minecraft-gta5-passthrough/gta/src/` (`natives.h` there has many verified hashes).

### ScriptHookV binding (already written)
SHV exports MSVC-mangled names (e.g. `?createTexture@@YAHPEBD@Z`). `gta/src/shv.cpp` resolves them with
`GetProcAddress`, so the project builds with **mingw OR MSVC** without `ScriptHookV.lib`. On Windows you can
instead use MSVC + the SDK's lib and headers; either is fine, but keep `shv.h`'s `invoke<R>(hash, ...)`
API.
- The full export list (verified): `createTexture`, `drawTexture`, `getGameVersion`, `getGlobalPtr`,
  `getScriptHandleBaseAddress`, `keyboardHandlerRegister/Unregister`, `nativeCall`, `nativeInit`,
  `nativePush64`, `presentCallbackRegister/Unregister`, `scriptRegister`, `scriptRegisterAdditionalThread`,
  `scriptUnregister` (HMODULE and fn-ptr overloads), `scriptWait`, `scriptsAreLaunchedUsingReloading`,
  `worldGetAllObjects/Peds/Pickups/Vehicles`.
- `Vector3` returned or taken by natives is padded: `alignas(8)` per float, 24 bytes total.

### Natives
`tools/gen_natives.py` turns `third_party/natives.json` into `gta/src/natives_gen.h` (all ~6000 natives as
`NS::NAME(args)` inline wrappers, handle types → `int`). **Not yet run.** Run it, compile, and fix anything
the generator gets wrong (keyword clashes, duplicate names).

Key natives checked in the DB:
- `DRAW_POLY 0xAC26716048436851`
- `DRAW_TEXTURED_POLY 0x29280002282F1928` (build 877+; needs a streamed TXD, so usable with our own `.ytd`
  in stage 2)
- `DRAW_BOX 0xD3A9971CADAC7252`
- `DRAW_LINE 0x6B7256074AE34680`
- `SET_BACKFACECULLING 0x23BA6B0C2AD7B0D3`
- `START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE 0x377906D8A31E5586`
- `GET_SHAPE_TEST_RESULT 0x3D87450E15D98694`
- `SET_MOUSE_CURSOR_THIS_FRAME 0xAAE7CE1D63167423`
- `GET_ASPECT_RATIO 0xF1307EF624A80D87`
- `DRAW_SPRITE 0xE7FFAE5EBF23D890` (13 args in the current DB)

There is **no** runtime-TXD native in vanilla GTA. `CREATE_RUNTIME_TXD` is FiveM-only, so textured
in-world rendering needs a DLC `.ytd`/props (stage 2).

---

## 4. Stage plan and detailed design

### Stage 1: foundation and gameplay with placeholder blocks (no OpenIV needed)
Deliver it, have the user test it (or test it yourself with `um win`), then iterate.

1. **ASI skeleton**
   - `DllMain` → `shv::load()` → `scriptRegister(hModule, ScriptMain)` and `keyboardHandlerRegister`.
   - `ScriptMain` loops `tick(); WAIT(0);`.
   - Log to `GrandTheftMinecraft\gtm.log` (`log.h`/`logf` is referenced by `shv.cpp` but **not written
     yet**).
   - `config.ini` settings: `ToggleKey=F6`, `GuiScale=0` (auto), `MaxCollisionProps=350`,
     `RenderDistance=64`, `PolyDetailNear=4`, `Invincible=1`, `NoWanted=0`.
2. **Minecraft mode toggle (F6, on by default)**
   - While it's on: `HIDE_HUD_AND_RADAR_THIS_FRAME` and force unarmed.
   - Disable GTA controls every frame: attack/aim/melee `24,25,257,140-143,263,264`; weapon wheel
     `14-17,37,261,262`; weapon slots `157-165`; cover/reload `44,45,47,58`; context/talk `51,38,46` for
     `E`.
   - Enter first person (`SET_FOLLOW_PED_CAM_VIEW_MODE(4)`); V still switches view.
   - Player invincible (creative).
   - In a vehicle: keep the HUD, skip block interaction, don't block driving controls.
3. **Minecraft HUD via SHV `drawTexture`**
   - Elements:
     - `hotbar.png` 182×22 at the bottom centre;
     - `hotbar_selection.png` 24×23;
     - `crosshair.png` 15×15 (white is fine);
     - item icons with stack counts in the MC font with shadow (the reference shows 64 for blocks, 16 for
       pearls, nothing for tools; creative never decrements);
     - the item name fading in above the hotbar for about 2 s on slot change.
   - Slot keys: 1-9 and the mouse wheel (disabled controls `14/15` or `241/242`).
   - GUI scale: auto like MC, `floor(min(W/320, H/240))` (4 at 1080p).
   - **The drawTexture size maths is UNVERIFIED (verify first):**
     - expected `pixel_w = sizeX*W` and `pixel_h = sizeY*factor*H`;
     - with `factor = W/H`, pass `sizeX = w/W`, `sizeY = h/W`;
     - SHV's NativeSpeedo sample uses equal sizes with the aspect as the factor, and SHVDN's CustomSprite
       does the same.
     - Make a calibration test: a 16×16 checker drawn at 64 px must come out square and crisp.
   - **Limits:**
     - at most **64 instances per texture per frame** (the `index` argument): manage a per-texture
       counter that resets each frame;
     - `time` = how long (ms) an instance persists, so use a small value (about 30–50) or it lingers when
       scripts pause.
   - **Crispness:** drawTexture likely filters linearly. The asset tool **pre-scales PNGs ×4 with
     nearest-neighbour**, so at 1080p (scale 4) they draw 1:1.
   - **Hand item (bottom right):** isometric block icon or item sprite, drawn large, with a swing
     animation on click. Stage 2 can swap it for a real 3D prop attached to the camera.
4. **Creative inventory (`E`)**
   - Layout: the `tab_items.png` background (crop the 195×136 used region), 9×5 grid at (9,18) with
     18 px pitch, hotbar row at (9,112), scroller at (175,18) with a 112 px track, and tabs
     `tab_top_selected/unselected_N`.
   - Tabs: Building Blocks, Colored Blocks, Combat, Tools & Utilities, Spawn Eggs (stage 3). The title is
     drawn in colour 0x404040.
   - Mouse: show GTA's cursor with `SET_MOUSE_CURSOR_THIS_FRAME`, read `GET_DISABLED_CONTROL_NORMAL(0,
     239/240)`, and `DISABLE_ALL_CONTROL_ACTIONS(0)` while open.
   - Clicking a grid item puts the stack on the cursor; clicking a hotbar slot places or swaps it;
     clicking outside deletes it.
   - Hover a grid item and press 1-9 to put it straight into that hotbar slot (MC behaviour).
   - Tooltip = item name on a dark purple-bordered box.
5. **Blocks: data model** (the reasoning matters, so keep it)
   - X/Y use the global integer grid. Z uses a **per-"build" fractional offset** so blocks sit flush on
     GTA ground, which is at arbitrary heights.
   - Placing on GTA geometry: join the nearest build within 48 m (horizontal), else create a new build
     with `zOff = frac(hit.z)`.
   - Placing against an existing block: same build, `cell + faceNormal`.
   - Cells are chosen with `floor` (blocks may sink into sloped ground; never float).
   - Store `map<(build, x, y, z) → itemId>` and keep an exposed-face mask per block, recomputed on change.
   - Refuse placement inside the player's capsule.
   - Save to `world.txt` (debounced about 2 s after changes): `B <id> <zOff>` and
     `K <build> <x> <y> <z> <itemName>`.
6. **Targeting**
   - Camera ray from `GET_FINAL_RENDERED_CAM_COORD`/`ROT`. Reach = `6 m + |camera - head|`, so third
     person works.
   - **Voxel DDA** through our builds (exact), plus a GTA LOS probe (flags -1, ignore the player ped).
     Take the nearer hit; ignore probe hits on our own collision props.
   - Draw MC's black outline (12 `DRAW_LINE`s) around the targeted block.
   - LMB = break (instant in creative, with particles and sound); RMB = place or use; MMB = pick block.
     Read mouse buttons with `GetAsyncKeyState` while GTA has focus, or with disabled-control reads.
7. **Block rendering in stage 1 (placeholder)**
   - Per exposed, camera-facing face, draw `DRAW_POLY` quads coloured from the real texture, downsampled
     to N×N (4×4 near, 1×1 far, as LOD).
   - MC face shade: top 1.0, N/S 0.8, E/W 0.6, bottom 0.5. Multiply by a day/night factor from
     `GET_CLOCK_HOURS` (DRAW_POLY is unlit).
   - Call `SET_BACKFACECULLING(FALSE)` and cull faces yourself.
   - Keep a per-frame poly budget (about 6000; the engine cap for script polys is **unknown**, so measure
     it).
   - Glass and ice get alpha; glowstone adds `DRAW_LIGHT_WITH_RANGE`.
8. **Collision for blocks**
   - Invisible (`SET_ENTITY_VISIBLE false`), frozen stock props at block cells, nearest-first within
     about 40 m, capped (**GTA crashes at roughly 1500 script objects**, per the field note). Skip
     fully-enclosed blocks.
   - The passthrough used `prop_box_wood01a` (0.97×0.96×0.80 m: too short). At startup, check candidates
     with `GET_MODEL_DIMENSIONS`, log them, and pick the closest to a 1 m cube.
     - Candidates: `stt_prop_stunt_bblock_sml1` (stunt building block), `prop_box_wood02a`,
       `prop_box_wood01a`, `prop_mb_crate_01a`, `prop_ld_crate_01`.
   - Place each so its **top is flush with the cell top**.
   - Stage 2 replaces these with our own props, which have exact 1 m collision.
9. **Items with behaviour (stage 1)**
   - **Ender pearl:** RMB throws it.
     - Physics in MC units converted at 1 block = 1 m, 20 ticks/s: initial speed 1.5 b/tick = 30 m/s,
       gravity 0.03 b/tick² ≈ 12 m/s², drag 0.99 per tick.
     - Each frame, segment-test against GTA (LOS probe) and our voxels.
     - On hit: teleport the player to the hit point + normal × 0.5 (keep the heading), play the portal
       sound, and burst purple portal particles.
     - The pearl is drawn as a billboard of `item/ender_pearl.png`.
   - **TNT block + flint & steel:**
     - Flint & steel on TNT primes it: remove the block and spawn a primed-TNT entity that flashes white
       for 4 s (80 ticks) with a fuse sound.
     - Then `ADD_EXPLOSION` (GTA damage to peds and cars), remove our blocks within radius about 4 with
       falloff randomness, chain-prime nearby TNT, and spawn MC explosion particles and sound.
     - Flint & steel on another surface: `START_SCRIPT_FIRE` (real GTA fire; peds burn).
   - **Diamond sword (and any item) melee on LMB** when a ped or vehicle is targeted:
     `APPLY_DAMAGE_TO_PED` (MC damage ×10, sword 7 → 70), a knock-back force and a short ragdoll.
   - **Creative flight (double-tap Space):**
     - Freeze the ped and move it manually with `SET_ENTITY_COORDS_NO_OFFSET` from the WASD control
       normals (30/31) and the camera yaw. Space = up, Ctrl = down, sprint = faster.
     - Collision: a few LOS rays (feet and head) along the move, plus our voxels.
     - Touching the ground ends flight.
     - The velocity approach triggers falling animations, so it was rejected.
   - Gravity blocks (sand/gravel fall when unsupported) are nice-to-have.
10. **Particles:** MC particle textures (`explosion_0..15`, `big_smoke_*`, `generic_*`, `flame`, `portal`)
    drawn as screen-projected billboards (`GET_SCREEN_COORD_FROM_WORLD_COORD`; size by distance and FOV).
    Do one occlusion probe per effect, not per particle.
11. **Sounds:**
    - Use miniaudio (single header, MIT; fetch into `third_party/`) to play WAVs converted from the MC OGGs
      (ffmpeg), with simple distance attenuation and pan.
    - Sounds needed: `dig/*` (stone, grass, wood, gravel, sand, cloth, snow), `random/glass*`,
      `random/explode1-4`, `random/fuse`, `random/bow` (pearl throw), `mob/endermen/portal`,
      `fire/ignite`, `random/click`, `random/pop`.

### Stage 2: real textured blocks (add-on DLC through OpenIV's mods folder)
- One `.ydr` per block type: a 1 m cube with 6 faces and UVs (top/side/bottom textures), a box collision
  bound, and textures in a `.ytd` (from the 16×16 MC textures, point-filtered, or upscaled
  nearest-neighbour to 64–128 px so GTA's filtering stays crisp).
  - Plus a `.ytyp` with the archetypes, `dlc.rpf` (`content.xml`, `setup2.xml`), and an entry in
    `mods\update\update.rpf\common\data\dlclist.xml`.
- Tooling on Windows: CodeWalker (generate from CodeWalker XML, or a small .NET tool using
  `CodeWalker.Core`), or Blender + Sollumz. OpenIV for the `mods\` folder and `OpenIV.asi`.
  - Generate everything **programmatically** (a script per block from `items.txt`) so new blocks are cheap.
- Then swap the stage-1 DRAW_POLY + stock-prop placeholders for our props (visible, frozen, collision on).
  GTA then provides lighting, shadows and collision for free.
- The same pack carries:
  - **mob body-part props** (stage 3);
  - **held-item props**: extruded voxel meshes of item sprites, attached to the camera or ped hand bone;
  - a **primed-TNT prop**;
  - optionally a `.ytd` for the HUD (then `DRAW_SPRITE` with UVs instead of SHV `drawTexture`).
- Keep the script-object budget: props are spawned only near the player, while the world data holds all
  blocks.

### Stage 3: mobs that fight NPCs
- Each mob is an **invisible GTA ped** (combat AI, pathing and damage come from GTA) with **box-part props
  attached to its bones**, so GTA's walk animation moves the limbs. A zombie is 6 boxes:
  - head → `SKEL_Head`;
  - body → `SKEL_Spine3`;
  - arms → `SKEL_L/R_UpperArm` (zombie arms rotated forward);
  - legs → `SKEL_L/R_Thigh`.
- Behaviour:
  - Hostile mobs get their own relationship group, hostile to `CIVMALE`/`CIVFEMALE`/`COP`/`GANG`, with
    `TASK_COMBAT_PED` on the nearest NPC. Optionally they spare the player, since it's creative.
  - Creeper: walks up to a target and hisses, then `ADD_EXPLOSION` and blocks destroyed.
  - Skeleton: shoots arrows, i.e. projectile entities that damage peds through the arrow system.
  - Iron golem / wolf: allied with the player, fighting cops and gangs.
  - Wither (from the reference): a flying boss with a boss bar (`boss_bar` sprites) that shoots skull
    projectiles, i.e. explosions.
- Spawn eggs in the creative "Spawn Eggs" tab; RMB on the ground spawns.
- Field-note gotchas apply:
  - hidden frozen peds still take bullets;
  - `SET_ENTITY_VISIBLE(false)` doesn't make them immune.
  - Check that attached props stay visible when the parent ped is invisible; if not, use
    `SET_ENTITY_ALPHA` 0 on the ped instead.

### Stage 4: polish
Bow and arrows (charge, then shoot; arrows hit peds → damage, stick in walls), more items (fire charge,
snowball, egg, buckets), chat commands on `T` (`/time set day`, `/weather clear`, `/tp`, `/kill`,
`/summon`), and a showcase video if the user wants one (`showcase-video` skill).

---

## 5. Current state of the code (as handed off)

```
GrandTheftMinecraft/
  MODLOG.md                 journal (facts, route, stages); keep updating it
  HANDOFF.md                this file
  .gitignore                third_party/ build/ dist/ *.o   (claude-refrence/ was already ignored)
  claude-refrence/          the user's reference images (gitignored)
  tools/gen_natives.py      written, NOT yet run
  gta/src/shv.h             written: runtime SHV binding API, Vector3, invoke<R>()
  gta/src/shv.cpp           written: GetProcAddress binding (all mangled names verified against the DLL)
  third_party/              (gitignored)
    natives.json            alloc8or nativedb
    shv/                    SDK headers (main.h, natives.h, types.h, enums.h, nativeCaller.h)
    runtime/                ScriptHookV.dll, dinput8.dll, args.txt (3889.0); don't redistribute
```

**Not written yet:**
- `log.h`/`log.cpp`, `main.cpp` (DllMain + ScriptMain), and every gameplay module;
- `tools/extract_mc.py` (the asset extractor);
- `build.bat`/`build.sh`, `install.ps1`/`install.sh` (with `--remove`), and `README.md` with install steps.

**Suggested module split:** `log`, `config`, `input`, `render2d` (textures, text, sprites), `gui` (hotbar,
inventory, hand), `items` (registry loaded from `items.txt`), `world` (builds, blocks, save), `blockrender`,
`collision`, `interact` (raycast, place, break), `entities` (pearls, primed TNT, particles), `flight`,
`audio`.

Git: the repo has one commit (`init`). Commit each working step; end messages with the attribution line
from your system prompt.

---

## 6. Minecraft assets for the extractor (`tools/extract_mc.py`, to write)

Copied from the user's Linux Prism Launcher install to the gamedisk (their own game files; never commit or
redistribute them):

```
<gamedisk>\GrandTheftMinecraft\mc_source\minecraft-1.21.11-client.jar
<gamedisk>\GrandTheftMinecraft\mc_source\assets\indexes\29.json        (asset index for 1.21.11)
<gamedisk>\GrandTheftMinecraft\mc_source\assets\objects\xx\<sha1>      (only minecraft/sounds/{dig,random,mob,fire,step,entity,item,block,damage,liquid}/*)
```

If the user also has Minecraft on Windows (`%APPDATA%\.minecraft`), that works too.

**What the extractor should produce** in `build\GrandTheftMinecraft\` (the install copies it into the game
folder):
- **GUI** (all upscaled ×4 nearest-neighbour), from `assets/minecraft/textures/gui/`:
  - `sprites/hud/hotbar.png`, `hotbar_selection.png`, `crosshair.png`;
  - `container/creative_inventory/tab_items.png` (crop 195×136), `tab_item_search.png`;
  - `sprites/container/creative_inventory/{tab_top_selected_1..7, tab_top_unselected_1..7, scroller,
    scroller_disabled}.png`.
- **Font:** `textures/font/ascii.png` (16×16 grid of 8×8 glyphs) → one PNG per glyph + `font.txt` with
  advance widths (the rightmost non-empty column + 1, +1 spacing; space = 4).
- **Item icons:** `textures/item/<name>.png` ×4. Block icons are rendered **isometric** from
  `textures/block/*.png` with PIL affine + NEAREST at 64 px: top rhombus, left face ×0.8, right face ×0.6.
- **Tints:** `grass_block_top` and leaves are grayscale in the jar. Tint grass `#91BD59` and foliage
  `#77AB2F` (plains). `grass_block_side.png` is already coloured.
- **Animated textures** (vertical strips): take the first square frame.
- **`items.txt`:** one line per item: `name;Display Name;kind(block|item);flags;soundGroup;` followed by
  top/side/bottom N×N face colours (hex) for the stage-1 DRAW_POLY renderer.
- **Particles:** `textures/particle/explosion_0..15`, `big_smoke_0..11`, `generic_0..7`, `flame`, and the
  portal particle.
- **Sounds:** OGG → WAV with ffmpeg (or Python + `soundfile`). Look names up via `29.json`
  (`minecraft/sounds/dig/stone1.ogg` → hash → `objects/<2 chars>/<hash>`).

**Item list for stage 1:**
- **Blocks:**
  - natural and stone: grass_block, dirt, stone, cobblestone, stone_bricks, bricks, sand, gravel,
    oak_leaves, obsidian, bedrock;
  - wood: oak/spruce/birch planks, oak_log (top `oak_log_top`), bookshelf, crafting_table;
  - glass, ice, snow_block;
  - mineral blocks: diamond/gold/iron/emerald/redstone/lapis/coal blocks, quartz_block;
  - nether and end: netherrack, end_stone, glowstone;
  - sandstone (top/side/bottom), terracotta;
  - the 16 wool and 16 concrete colours;
  - pumpkin, melon, hay_block, tnt (`tnt_top`/`tnt_side`/`tnt_bottom`).
- **Items:** diamond_sword, flint_and_steel, ender_pearl. Stage 4 adds bow, fire_charge, snowball, egg.
- **Default hotbar (like the reference):** grass_block, dirt, stone, oak_planks, glass, tnt,
  flint_and_steel, ender_pearl, diamond_sword.

---

## 7. Testing on Windows (you can do this yourself now)

- Install into **`GTA Modding`** only:
  - `ScriptHookV.dll`, `dinput8.dll` and `args.txt` from `third_party\runtime\`;
  - `GrandTheftMinecraft.asi`;
  - the `GrandTheftMinecraft\` data folder.
- **Ask the user before installing anything into the game folder**, and back it up first (skill rule).
  Keep the uninstall list in `MODLOG.md`.
- Launch:
  - with BattlEye off (`args.txt` `-nobattleye`);
  - in **Story Mode only**;
  - windowed at a fixed size for stable screenshots (`game-automation` skill).
- **Never** let input reach the landing page while the user types (field note gotcha 20: it nearly
  started GTA Online on a modded game). Check `um win drive --proc GTA5 idle` before automating, or ask the
  user to click Story Mode, which takes **two clicks**.
- Oracle: `gtm.log` plus screenshots (`um win shot --scale 0.33`). Add an in-game debug overlay (F9) that
  shows the FPS, block/prop/poly counts and the targeted cell.
- Things to measure early:
  1. the drawTexture size maths;
  2. the DRAW_POLY per-frame cap and its FPS cost;
  3. which stock prop is closest to a 1 m cube;
  4. whether invisible frozen props still collide for peds and cars;
  5. drawTexture filtering (crisp or blurry).
- Circuit breaker: if the same failure happens 3 times, stop, write it in `MODLOG.md`, and change
  approach or ask.

---

## 8. Hard rules recap
- Story mode only. Never go online with the modded game. Keep BattlEye off via `args.txt` in the modding
  copy only.
- Don't touch the Steam `Grand Theft Auto V` folder. All work goes in `GTA Modding`.
- Don't commit or redistribute game files, ScriptHookV, or Minecraft assets. The extractor regenerates them
  from the user's own files.
- Ask before installing loaders, changing game files or settings, or deleting anything.
- When each stage is done, give the user exact install and uninstall steps and the test checklist.
