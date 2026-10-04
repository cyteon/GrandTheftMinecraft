# GrandTheftMinecraft

Minecraft creative mode inside **GTA V story mode**. Minecraft's hotbar, creative inventory, blocks and items, in
GTA's world: blocks are real GTA objects with GTA's lighting, shadows and collision, TNT blows up cars, ender pearls
teleport you across Los Santos, arrows hit pedestrians.

> No Minecraft files are included in this mod. On first launch it builds its textures and sounds from **your own**
> Minecraft Java 1.21.11 (or downloads the official 1.21.11 files from Mojang's servers). See
> [How it works](#how-it-works).

## Features

- **Minecraft HUD**: hotbar (1-9, mouse wheel), crosshair, item names, the 3D held item with Minecraft's own
  first-person animations, the Minecraft font.
- **Creative inventory** (`E`): Building Blocks, Colored Blocks, Redstone, Combat, Tools tabs, tooltips, drag
  and drop, number keys to fill hotbar slots.
- **69 blocks**: grass, stone, wood, glass, ice, leaves, ores, all 16 wool and concrete colours, TNT and more. Place
  them on any GTA surface; they stand flush on the ground, cars and people collide with them, and placing one inside
  a car pushes the car out of the way. Your builds are saved.
- **TNT**: light it with flint and steel; it hops, flashes, explodes with GTA damage, and chain-reacts.
- **Ender pearls**: throw and teleport, with Minecraft's physics.
- **Flint and steel**: sets GTA fires; lights TNT.
- **Diamond sword**: ragdolls people and knocks cars around.
- **Bow and crossbow**: draw / load like Minecraft; arrows fly with Minecraft's arc, hit people and cars, and stick
  in walls and blocks.
- **Play as Steve**: in third person you're Steve (or your own skin), animated by GTA's own walk, run and jump
  animations, looking where you look and holding your item.
- **Mobs that fight GTA's people** (Spawn Eggs tab): zombies chase and hit pedestrians, skeletons shoot arrows,
  creepers hiss and explode, and iron golems fight on your side against monsters, cops and gangs. They're real GTA
  characters underneath (GTA does their walking and pathing, and cops shoot at them) wearing Minecraft bodies.
- **Elytra and firework rockets**: right-click the elytra to put it on, jump off something and press Space to glide
  with Minecraft's own flight physics; firework rockets boost you while gliding, or fly up and burst into colour.
- **Creative flight**: double-tap Space.
- **Minecraft sounds** for blocks, explosions, bows and more, positioned in 3D.

## Requirements

- **GTA V Legacy** (the original PC version, Steam / Rockstar / Epic), **story mode only**. GTA V Enhanced isn't
  supported.
- **[ScriptHookV](https://www.dev-c.com/gtav/scripthookv/)** matching your game version (includes the ASI loader,
  `dinput8.dll`).
- **[OpenIV](https://openiv.com/)** with **OpenIV.asi** installed (OpenIV > Tools > ASI Manager > OpenIV.asi >
  Install). It loads the block pack from the `mods` folder.
- An internet connection on the first launch, unless Minecraft Java 1.21.11 is installed on the PC.

> **Never use mods in GTA Online.** The mod switches itself off if an online session starts, but play story mode
> with BattlEye disabled (add `-nobattleye` to `args.txt` in the GTA folder).

## Install

1. Install ScriptHookV and OpenIV.asi (see above).
2. Download `GrandTheftMinecraft-<version>.oiv` from [Releases](https://github.com/cyteon/GrandTheftMinecraft/releases).
3. In OpenIV: **Tools > Package Installer**, open the `.oiv`, and install it (to the `mods` folder).
4. Start GTA V and load story mode. The first time, the top-left corner shows the setup progress for a few seconds
   ("Downloading Minecraft 1.21.11 from Mojang...", "Building block textures..."), then the Minecraft HUD appears.
5. **Restart GTA once** when it says so: the textured blocks load from the second launch on. (Until then blocks are
   drawn in a simpler, unlit way.)

A manual-install zip is also on the Releases page (see `INSTALL.txt` inside).

**Uninstall:** delete `GrandTheftMinecraft.asi`, the `GrandTheftMinecraft` folder and `mods\update\x64\dlcpacks\gtm`
from the GTA folder, and remove the `dlcpacks:/gtm/` line from `mods\update\update.rpf\common\data\dlclist.xml` in
OpenIV.

## Controls

| Key | Action |
|---|---|
| F6 | Minecraft mode on / off (back to normal GTA) |
| 1-9, mouse wheel | Select a hotbar slot |
| Left mouse | Break a block / hit (sword: ragdoll and knock-back) |
| Right mouse | Place a block / use the item (hold to draw a bow or load a crossbow) |
| Middle mouse | Pick the block you're looking at |
| E | Creative inventory (1-9 over an item puts it in that slot; Esc closes) |
| Space twice | Creative flight: Space up, Ctrl down, Shift faster; land to stop |
| F9 | Debug overlay (FPS, block counts, target) |

In a vehicle the hotbar stays but GTA's driving controls are untouched.

## Settings

`GrandTheftMinecraft\config.ini` (created on first launch; edit while the game is closed):

| Setting | Default | Meaning |
|---|---|---|
| `ToggleKey`, `DebugKey` | `0x75` (F6), `0x78` (F9) | Virtual-key codes |
| `StartEnabled` | 1 | Minecraft mode on when the game starts |
| `GuiScale` | 0 | 0 = automatic like Minecraft (4 at 1080p), or 1-6 |
| `Invincible`, `NoWanted` | 1, 0 | Creative-style god mode; no wanted level |
| `Volume` | 0.8 | Minecraft sound volume |
| `PlayAsSteve` | 1 | Third person shows Steve instead of the GTA character (on foot) |
| `SkinFile` | empty | Path to your own 64x64 Minecraft skin PNG (classic arms); applied at the next launch |
| `PropRadius`, `MaxBlockProps` | 150, 900 | Blocks within this distance become real GTA objects (nearest first, capped: GTA gets unstable past ~1500 script objects); farther ones use the fallback renderer |
| `RenderDistance`, `FullDetailDistance`, `PolyBudget` | 64, 24, 30000 | The fallback renderer's distance, detail and triangle budget |
| `MinecraftJar`, `MinecraftAssets` | empty | Use a specific Minecraft 1.21.11 jar / assets folder instead of searching |

## How it works

GrandTheftMinecraft is a native [ScriptHookV](https://www.dev-c.com/gtav/scripthookv/) script (`.asi`, C++), plus an
add-on DLC pack for the block models.

**Minecraft's look, without shipping Minecraft.** Mojang's textures and sounds can't be redistributed, so the mod
contains none. On first launch, a background thread:
1. looks for Minecraft Java 1.21.11 from the official launcher, Prism, PolyMC, CurseForge or the Modrinth app,
   checking the client jar's SHA1 against Mojang's;
2. if there isn't one, downloads the official 1.21.11 client jar and the needed sounds from Mojang's own servers
   (`piston-data.mojang.com`, `resources.download.minecraft.net`), verifying each file's SHA1;
3. builds the HUD, font, icons, particles and `.ogg` sounds into `GrandTheftMinecraft\`, and the block pack's
   textures into `textures.bin`.

**Real block objects.** The DLC pack (`dlc.rpf`, built with [CodeWalker](https://github.com/dexyfex/CodeWalker)'s
library) holds a 1 m cube model per block with box collision, small "held in hand" and "break chip" versions, the
held-item models and an arrow. It ships with blank placeholder textures. At the next game start, before GTA loads
DLC packs, the mod writes the textures it built into the pack's texture dictionary (gtm_tex.ytd), which is why one
restart is needed. From then on, blocks within `PropRadius` are spawned as frozen GTA objects: GTA lights them,
shadows them and collides with them. Blocks beyond that (or before the restart) are drawn with GTA's polygon
drawing, using the same textures as colour grids.

**Building.** X/Y use a global 1 m grid; Z is offset per "build" so blocks sit flush on GTA's ground at whatever
height it has. Targeting combines an exact voxel ray through your blocks with a GTA line-of-sight probe.

**Steve and mobs.** Each is an invisible GTA ped wearing Minecraft box parts from the block pack, built from
Minecraft's own model layouts (`rigs.txt`). Every frame each part follows the matching GTA bone, so GTA's walk, run
and ragdoll animations move Minecraft-style limbs. Mob behaviour (targets, attacks, the creeper fuse) is the mod's;
moving around is GTA's.

**Items.** Arrows and ender pearls are simulated with Minecraft's numbers (speed, gravity, drag per tick) and tested
against GTA's world each frame; an arrow hitting a person or car becomes an invisible GTA bullet so GTA handles damage
and reactions. TNT is a physics object; its explosion is a GTA explosion plus Minecraft's block destruction. The held
item is a model placed in front of the camera every frame with Minecraft's `ItemInHandRenderer` transforms.

**Files it touches:** `GrandTheftMinecraft.asi` and `GrandTheftMinecraft\` in the GTA folder, and (via OpenIV) the
`mods` folder. It never modifies the original game archives.

## Troubleshooting

- **Nothing happens in game:** check `asiloader.log` in the GTA folder lists `GrandTheftMinecraft.asi` and
  `OpenIV.asi`, and that ScriptHookV matches your game version.
- **Blocks look flat / unlit, F9 says the collision model isn't `gtm_* (DLC)`:** restart GTA once after the first
  setup. If it persists, OpenIV.asi isn't loading the `mods` folder, or the `.oiv` wasn't installed to `mods`.
- **"Couldn't get Minecraft 1.21.11":** no local install and the download failed (offline, firewall). Install
  Minecraft 1.21.11 with any launcher, or set `MinecraftJar` in `config.ini`.
- **Everything else:** `GrandTheftMinecraft\gtm.log` explains what the mod found and did.

## Building from source

- C++: mingw-w64 (`scoop install mingw` on Windows; `x86_64-w64-mingw32-g++` on Linux) and Python 3. `./build.sh`
  generates the native wrappers from `third_party/natives.json`
  ([alloc8or's native DB](https://github.com/alloc8or/gta5-nativedb-data)) and builds `build/GrandTheftMinecraft.asi`.
- Block pack: .NET 8 SDK, Python 3 with numpy, and CodeWalker's source in `third_party/CodeWalker`
  (`git clone https://github.com/dexyfex/CodeWalker third_party/CodeWalker`):
  ```
  python tools/make_dlc_src.py
  dotnet build -c Release tools/gtmpack
  tools/gtmpack/bin/Release/net8.0-windows/gtmpack.exe build - build/dlc_src build/dlc/gtm/dlc.rpf
  python tools/package.py      # dist/GrandTheftMinecraft-<version>.oiv and the manual zip
  ```
- Releases are built by GitHub Actions (`.github/workflows/release.yml`) from this source.

## Credits

- Built by cyteon with [Claude Code](https://claude.com/claude-code) (Anthropic): most of the code was written by an
  AI agent under the author's direction and testing.
- [ScriptHookV](https://www.dev-c.com/gtav/scripthookv/) by Alexander Blade (not included).
- [CodeWalker](https://github.com/dexyfex/CodeWalker) by dexyfex (MIT), used to build the DLC pack.
- [miniaudio](https://miniaud.io) by David Reid, [stb_image, stb_image_write, stb_vorbis](https://github.com/nothings/stb)
  by Sean Barrett, [miniz](https://github.com/richgel999/miniz) by Rich Geldreich: see [NOTICE.md](NOTICE.md).
- Native hashes from [alloc8or's GTA V native DB](https://github.com/alloc8or/gta5-nativedb-data).

Minecraft is a trademark of Mojang Synergies AB; Grand Theft Auto is a trademark of Take-Two Interactive. This project
isn't affiliated with or endorsed by either. Minecraft's assets are © Mojang and are never distributed by this
project; the mod reads them from the player's own copy or Mojang's servers.

## License

The code is [MIT](LICENSE). Third-party components keep their own licenses ([NOTICE.md](NOTICE.md)).
