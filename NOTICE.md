# Third-party notices

GrandTheftMinecraft's own code is MIT licensed (see LICENSE). It includes or uses:

| Component | Author | License | Where |
|---|---|---|---|
| miniaudio | David Reid | Public domain (Unlicense) or MIT No Attribution | `gta/vendor/miniaudio.h` |
| stb_image, stb_image_write, stb_vorbis | Sean Barrett and contributors | Public domain (Unlicense) or MIT | `gta/vendor/stb_*` |
| miniz | Rich Geldreich, Tenacious Software LLC and contributors | MIT (`gta/vendor/miniz.LICENSE`) | `gta/vendor/miniz.*` |
| CodeWalker.Core | dexyfex | MIT | build tool only (`tools/gtmpack`), not shipped |
| GTA V native DB | alloc8or | see its repository | generated `natives_gen.h` (build time) |

Not included, installed by the player: ScriptHookV (Alexander Blade), OpenIV / OpenIV.asi (NTAuthority team).

Not included: any Minecraft asset. Textures and sounds are read on the player's PC from their own Minecraft Java
1.21.11 or downloaded by the player's game from Mojang's servers. Minecraft assets are © Mojang AB.
