#!/usr/bin/env python3
"""Build GrandTheftMinecraft's data folder from the user's own Minecraft files.

Reads the client jar (textures) and the asset index + objects (sounds) and writes everything the ASI loads into
build/GrandTheftMinecraft/. Nothing produced here is committed or redistributed.

  python tools/extract_mc.py [--jar PATH] [--assets DIR] [--index 29] [--out build/GrandTheftMinecraft]
"""
import argparse
import io
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TEX = "assets/minecraft/textures/"
SCALE = 4  # GUI pixels are pre-scaled so GUI scale 4 (1080p) draws 1:1

GRASS_TINT = (0x91, 0xBD, 0x59)
FOLIAGE_TINT = (0x77, 0xAB, 0x2F)

COLORS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan",
          "purple", "blue", "brown", "green", "red", "black"]

# name, display name, tab, sound, flags, top, side, bottom (None = same as side / top)
# flags: a = alpha (glass-like), l = light source, g = falls (gravity), t = tnt, c = cutout (leaves), r = tint top
#        with grass colour, f = tint all faces with foliage colour
BLOCKS = [
    ("grass_block", "Grass Block", "building", "grass", "r", "grass_block_top", "grass_block_side", "dirt"),
    ("dirt", "Dirt", "building", "gravel", "", "dirt", None, None),
    ("stone", "Stone", "building", "stone", "", "stone", None, None),
    ("cobblestone", "Cobblestone", "building", "stone", "", "cobblestone", None, None),
    ("stone_bricks", "Stone Bricks", "building", "stone", "", "stone_bricks", None, None),
    ("bricks", "Bricks", "building", "stone", "", "bricks", None, None),
    ("sand", "Sand", "building", "sand", "g", "sand", None, None),
    ("gravel", "Gravel", "building", "gravel", "g", "gravel", None, None),
    ("oak_log", "Oak Log", "building", "wood", "", "oak_log_top", "oak_log", "oak_log_top"),
    ("oak_planks", "Oak Planks", "building", "wood", "", "oak_planks", None, None),
    ("spruce_planks", "Spruce Planks", "building", "wood", "", "spruce_planks", None, None),
    ("birch_planks", "Birch Planks", "building", "wood", "", "birch_planks", None, None),
    ("oak_leaves", "Oak Leaves", "building", "grass", "cf", "oak_leaves", None, None),
    ("bookshelf", "Bookshelf", "building", "wood", "", "oak_planks", "bookshelf", "oak_planks"),
    ("crafting_table", "Crafting Table", "building", "wood", "", "crafting_table_top", "crafting_table_front",
     "oak_planks"),
    ("glass", "Glass", "building", "glass", "a", "glass", None, None),
    ("ice", "Ice", "building", "glass", "a", "ice", None, None),
    ("snow_block", "Snow Block", "building", "snow", "", "snow", None, None),
    ("obsidian", "Obsidian", "building", "stone", "", "obsidian", None, None),
    ("bedrock", "Bedrock", "building", "stone", "", "bedrock", None, None),
    ("sandstone", "Sandstone", "building", "stone", "", "sandstone_top", "sandstone", "sandstone_bottom"),
    ("terracotta", "Terracotta", "building", "stone", "", "terracotta", None, None),
    ("netherrack", "Netherrack", "building", "stone", "", "netherrack", None, None),
    ("end_stone", "End Stone", "building", "stone", "", "end_stone", None, None),
    ("glowstone", "Glowstone", "building", "glass", "l", "glowstone", None, None),
    ("quartz_block", "Block of Quartz", "building", "stone", "", "quartz_block_top", "quartz_block_side",
     "quartz_block_bottom"),
    ("coal_block", "Block of Coal", "building", "stone", "", "coal_block", None, None),
    ("iron_block", "Block of Iron", "building", "stone", "", "iron_block", None, None),
    ("gold_block", "Block of Gold", "building", "stone", "", "gold_block", None, None),
    ("redstone_block", "Block of Redstone", "building", "stone", "", "redstone_block", None, None),
    ("emerald_block", "Block of Emerald", "building", "stone", "", "emerald_block", None, None),
    ("lapis_block", "Block of Lapis Lazuli", "building", "stone", "", "lapis_block", None, None),
    ("diamond_block", "Block of Diamond", "building", "stone", "", "diamond_block", None, None),
    ("pumpkin", "Pumpkin", "building", "wood", "", "pumpkin_top", "pumpkin_side", None),
    ("melon", "Melon", "building", "wood", "", "melon_top", "melon_side", None),
    ("hay_block", "Hay Bale", "building", "grass", "", "hay_block_top", "hay_block_side", None),
    ("tnt", "TNT", "redstone", "grass", "t", "tnt_top", "tnt_side", "tnt_bottom"),
]
for c in COLORS:
    nice = c.replace("_", " ").title()
    BLOCKS.append((f"{c}_wool", f"{nice} Wool", "colored", "cloth", "", f"{c}_wool", None, None))
for c in COLORS:
    nice = c.replace("_", " ").title()
    BLOCKS.append((f"{c}_concrete", f"{nice} Concrete", "colored", "stone", "", f"{c}_concrete", None, None))

# name, display name, tab, flags (s = stack of 1, p = stack of 16)
ITEMS = [
    ("diamond_sword", "Diamond Sword", "combat", "s"),
    ("flint_and_steel", "Flint and Steel", "tools", "s"),
    ("ender_pearl", "Ender Pearl", "tools", "p"),
]

GUI = {
    "hotbar": "gui/sprites/hud/hotbar.png",
    "hotbar_selection": "gui/sprites/hud/hotbar_selection.png",
    "crosshair": "gui/sprites/hud/crosshair.png",
    "scroller": "gui/sprites/container/creative_inventory/scroller.png",
    "scroller_disabled": "gui/sprites/container/creative_inventory/scroller_disabled.png",
    "boss_bar_bg": "gui/sprites/boss_bar/purple_background.png",
    "boss_bar_fg": "gui/sprites/boss_bar/purple_progress.png",
}
for i in range(1, 8):
    GUI[f"tab_top_selected_{i}"] = f"gui/sprites/container/creative_inventory/tab_top_selected_{i}.png"
    GUI[f"tab_top_unselected_{i}"] = f"gui/sprites/container/creative_inventory/tab_top_unselected_{i}.png"

PARTICLES = ([f"explosion_{i}" for i in range(16)] + [f"big_smoke_{i}" for i in range(12)] +
             [f"generic_{i}" for i in range(8)] + [f"sweep_{i}" for i in range(8)] + ["flame", "critical_hit"])

SOUNDS = {
    "dig/stone": 4, "dig/grass": 4, "dig/wood": 4, "dig/gravel": 4, "dig/sand": 4, "dig/cloth": 4, "dig/snow": 4,
    "random/glass": 3, "random/explode": 4, "random/fuse": 0, "random/bow": 0, "mob/endermen/portal": 0,
    "fire/ignite": 0, "random/click": 0, "random/pop": 0, "damage/hit": 3, "random/orb": 0,
    "entity/player/attack/strong": 1, "entity/player/attack/knockback": 1, "entity/player/attack/sweep": 1,
}


class Jar:
    def __init__(self, path):
        self.z = zipfile.ZipFile(path)

    def img(self, rel):
        return Image.open(io.BytesIO(self.z.read(TEX + rel))).convert("RGBA")

    def block(self, name):
        im = self.img(f"block/{name}.png")
        if im.height > im.width:  # animated strip: first frame
            im = im.crop((0, 0, im.width, im.width))
        return im


def up(im, k=SCALE):
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def tint(im, rgb):
    a = np.asarray(im).astype(np.float32)
    a[..., :3] *= np.array(rgb, np.float32) / 255.0
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA")


def shade(im, f):
    a = np.asarray(im).astype(np.float32)
    a[..., :3] *= f
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA")


def iso_icon(top, left, right, size=64):
    """Minecraft-style inventory block icon, nearest-neighbour sampled from three 16x16 faces."""
    s = size / 64.0
    T = np.array([32, 2]) * s
    L = np.array([3, 17]) * s
    C = np.array([32, 32]) * s
    R = np.array([61, 17]) * s
    down = np.array([0, 30]) * s
    faces = [  # (texture, origin, u axis, v axis)
        (top, L, T - L, C - L),
        (left, L, C - L, down),
        (right, C, R - C, down),
    ]
    out = np.zeros((size, size, 4), np.uint8)
    for tex, o, u, v in faces:
        t = np.asarray(tex)
        n = t.shape[0]
        m = np.linalg.inv(np.array([[u[0], v[0]], [u[1], v[1]]]))
        for y in range(size):
            for x in range(size):
                uv = m @ (np.array([x + 0.5, y + 0.5]) - o)
                if -1e-6 <= uv[0] < 1 and -1e-6 <= uv[1] < 1:
                    px = t[min(n - 1, int(uv[1] * n)), min(n - 1, int(uv[0] * n))]
                    if px[3] > 0:
                        out[y, x] = px
    return Image.fromarray(out, "RGBA")


def hexface(im):
    a = np.asarray(im.resize((16, 16), Image.NEAREST))
    return "".join(f"{r:02x}{g:02x}{b:02x}{al:02x}" for r, g, b, al in a.reshape(-1, 4))


def font(jar, out):
    im = jar.img("font/ascii.png")
    a = np.asarray(im)
    cell = im.width // 16
    (out / "font").mkdir(parents=True, exist_ok=True)
    widths = []
    for c in range(256):
        gx, gy = (c % 16) * cell, (c // 16) * cell
        g = a[gy:gy + cell, gx:gx + cell]
        cols = np.where(g[..., 3].max(axis=0) > 0)[0]
        if c == 32:
            adv = 4
        elif len(cols) == 0:
            adv = 0
        else:
            adv = int(cols.max()) + 2  # rightmost column + 1, + 1 spacing
        if 32 < c < 127 and adv:
            up(Image.fromarray(g.copy(), "RGBA")).save(out / "font" / f"{c}.png")
        widths.append(adv)
    (out / "font" / "font.txt").write_text(" ".join(map(str, widths)) + "\n")


def gui(jar, out):
    d = out / "gui"
    d.mkdir(parents=True, exist_ok=True)
    for name, rel in GUI.items():
        up(jar.img(rel)).save(d / f"{name}.png")
    up(jar.img("gui/container/creative_inventory/tab_items.png").crop((0, 0, 195, 136))).save(d / "tab_items.png")
    Image.new("RGBA", (4, 4), (255, 255, 255, 255)).save(d / "white.png")
    # mouse cursor (own pixel art, not from Minecraft): GTA's cursor is hidden under our overlay
    arrow = ["X...........", "XX..........", "XWX.........", "XWWX........", "XWWWX.......", "XWWWWX......",
             "XWWWWWX.....", "XWWWWWWX....", "XWWWWWWWX...", "XWWWWWWWWX..", "XWWWWWWWWWX.", "XWWWWWWXXXXX",
             "XWWWXWWX....", "XWWXXWWX....", "XWX..XWWX...", "XX...XWWX...", "X.....XWWX..", "......XWWX..",
             ".......XX..."]
    cur = np.zeros((19, 12, 4), np.uint8)
    for y, row in enumerate(arrow):
        for x, ch in enumerate(row):
            if ch == "X":
                cur[y, x] = (0, 0, 0, 255)
            elif ch == "W":
                cur[y, x] = (255, 255, 255, 255)
    up(Image.fromarray(cur, "RGBA")).save(d / "cursor.png")
    chk = np.zeros((64, 64, 4), np.uint8)
    for y in range(64):
        for x in range(64):
            chk[y, x] = (255, 255, 255, 255) if ((x // 4) + (y // 4)) % 2 else (255, 0, 255, 255)
    Image.fromarray(chk, "RGBA").save(d / "calib.png")  # 16x16 checker x4: must draw square and crisp


def blocks_and_items(jar, out):
    d = out / "items"
    d.mkdir(parents=True, exist_ok=True)
    lines = ["# name;Display Name;kind;tab;flags;sound;top 16x16 RGBA hex;side;bottom"]
    for name, disp, tab, sound, flags, top, side, bottom in BLOCKS:
        side = side or top
        bottom = bottom or top
        t, s, b = jar.block(top), jar.block(side), jar.block(bottom)
        if "r" in flags:
            t = tint(t, GRASS_TINT)
        if "f" in flags:
            t, s, b = (tint(x, FOLIAGE_TINT) for x in (t, s, b))
        iso_icon(t, shade(s, 0.8), shade(s, 0.6)).save(d / f"{name}.png")
        lines.append(";".join([name, disp, "block", tab, flags, sound, hexface(t), hexface(s), hexface(b)]))
    for name, disp, tab, flags in ITEMS:
        up(jar.img(f"item/{name}.png")).save(d / f"{name}.png")
        lines.append(";".join([name, disp, "item", tab, flags, "", "", "", ""]))
    # primed TNT flashes white; the runtime lerps, it only needs the TNT colours above
    (out / "items.txt").write_text("\n".join(lines) + "\n")
    return len(BLOCKS), len(ITEMS)


def particles(jar, out):
    d = out / "particles"
    d.mkdir(parents=True, exist_ok=True)
    for p in PARTICLES:
        up(jar.img(f"particle/{p}.png")).save(d / f"{p}.png")


def sounds(assets, index, out):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("! ffmpeg not on PATH: sounds skipped")
        return 0
    objs = json.loads((assets / "indexes" / f"{index}.json").read_text())["objects"]
    n = 0
    for key, count in SOUNDS.items():
        names = [f"{key}{i}" for i in range(1, count + 1)] if count else [key]
        for nm in names:
            k = f"minecraft/sounds/{nm}.ogg"
            if k not in objs:
                print(f"! missing sound {k}")
                continue
            h = objs[k]["hash"]
            src = assets / "objects" / h[:2] / h
            dst = out / "sounds" / f"{nm}.wav"
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists():
                n += 1
                continue
            subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(src), "-ac", "1", "-ar", "44100",
                            "-c:a", "pcm_s16le", str(dst)], check=True)
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jar", default=str(ROOT / "mc_source" / "minecraft-1.21.11-client.jar"))
    ap.add_argument("--assets", default=str(ROOT / "mc_source" / "assets"))
    ap.add_argument("--index", default="29")
    ap.add_argument("--out", default=str(ROOT / "build" / "GrandTheftMinecraft"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    jar = Jar(a.jar)
    gui(jar, out)
    font(jar, out)
    nb, ni = blocks_and_items(jar, out)
    particles(jar, out)
    ns = sounds(Path(a.assets), a.index, out)
    print(f"wrote {out}: {nb} blocks, {ni} items, {len(PARTICLES)} particles, {ns} sounds")


if __name__ == "__main__":
    sys.exit(main())
