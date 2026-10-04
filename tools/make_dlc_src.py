#!/usr/bin/env python3
"""Sources for the GrandTheftMinecraft DLC, with NO Minecraft content (tools/gtmpack builds dlc.rpf from them).

Everything here is ours: generic meshes, and blank placeholder textures. Each placeholder starts with a marker so
gtmpack can record where its pixels sit inside the texture dictionary; on first launch the ASI fills them from the
player's own Minecraft (or one fetched from Mojang's servers) using the recipes in tex_recipes.txt.

Writes build/dlc_src/:
  <texture>.dds       placeholder textures (A8R8G8B8, full mip chain, marker in the first bytes)
  tex_recipes.txt     texture;width;height;recipe   (recipe: "sheet <block name>" | "sprite <sprite>" | "arrow")
  <model>.geo         "v x y z nx ny nz u v" lines, then "i a b c ..." (triangles, counter-clockwise from outside)
  models.txt          model;texture;shader;material;collision ("-" or half-extents "hx,hy,hz")

Models
  gtm_<block>      1 m world block, centred, box collision (GTA space: z up)
  gtm_<block>_h    the block held in first person (Minecraft item space: y up, z towards the viewer; 0.25 m)
  gtm_<block>_p    a 0.12 m break chip (a 4x4 texel window of the side texture), small box collision
  gtm_i_<sprite>   held item: a generic 16x16 extrusion; the cutout shader trims it to the sprite's shape
  gtm_arrow        flying/stuck arrow: crossed planes along +Y (GTA space)
"""
import random
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
HAND_SIZE = 0.25  # hand models are this big; the ASI rescales distance so they look Minecraft-sized
CHIP = 0.12

# unit-cube faces (GTA z-up): origin = top-left seen from outside, right axis, down axis, normal, sheet column
FACES_Z = [((0, 1, 1), (1, 0, 0), (0, -1, 0), (0, 0, 1), 0), ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, -1), 2),
           ((1, 1, 1), (-1, 0, 0), (0, 0, -1), (0, 1, 0), 1), ((0, 0, 1), (1, 0, 0), (0, 0, -1), (0, -1, 0), 1),
           ((1, 0, 1), (0, 1, 0), (0, 0, -1), (1, 0, 0), 1), ((0, 1, 1), (0, -1, 0), (0, 0, -1), (-1, 0, 0), 1)]
# the same in Minecraft item space (y up, +z = south / towards the viewer)
FACES_Y = [((0, 1, 0), (1, 0, 0), (0, 0, 1), (0, 1, 0), 0), ((0, 0, 1), (1, 0, 0), (0, 0, -1), (0, -1, 0), 2),
           ((0, 1, 1), (1, 0, 0), (0, -1, 0), (0, 0, 1), 1), ((1, 1, 0), (-1, 0, 0), (0, -1, 0), (0, 0, -1), 1),
           ((1, 1, 1), (0, 0, -1), (0, -1, 0), (1, 0, 0), 1), ((0, 1, 0), (0, 0, 1), (0, -1, 0), (-1, 0, 0), 1)]


def read_defs():
    blocks, items, sprites = [], [], []
    for line in (ROOT / "data" / "defs.txt").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split(";")
        if f[0] == "block":
            blocks.append(dict(name=f[1], flags=f[4], sound=f[5]))
        elif f[0] == "item":
            items.append(f[1])
            sprites.append(f[5])
        elif f[0] == "sprite":
            sprites.append(f[1])
    return blocks, items, sprites


class Geo:
    def __init__(self):
        self.v, self.i = [], []

    def quad(self, p00, p10, p11, p01, n, uv00, uv11):
        """p00 top-left, p10 top-right, p11 bottom-right, p01 bottom-left (seen from the front)."""
        # winding must agree with the normal (GTA culls clockwise-from-outside triangles)
        e1, e2 = np.subtract(p01, p00), np.subtract(p11, p00)
        assert np.dot(np.cross(e1, e2), n) > 0, f"quad winding disagrees with normal {n}"
        b = len(self.v)
        (u0, v0), (u1, v1) = uv00, uv11
        for p, (u, v) in zip((p00, p10, p11, p01), ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
            self.v.append((*p, *n, u, v))
        self.i += [b, b + 3, b + 2, b + 2, b + 1, b]

    def write(self, path):
        lines = [f"v {' '.join(f'{c:.6g}' for c in v)}" for v in self.v]
        lines.append("i " + " ".join(map(str, self.i)))
        path.write_text("\n".join(lines) + "\n")


def add(a, b, s=1.0):
    return tuple(x + y * s for x, y in zip(a, b))


def cube(faces, size, uv_for):
    g = Geo()
    for o, u, v, n, col in faces:
        p00 = tuple((c - 0.5) * size for c in o)
        p10, p01 = add(p00, u, size), add(p00, v, size)
        g.quad(p00, p10, add(p10, v, size), p01, n, *uv_for(col))
    return g


def sheet_uv(col):
    iu, iv = 0.5 / 512, 0.5 / 128
    return (col * 0.25 + iu, iv), ((col + 1) * 0.25 - iu, 1 - iv)


def sprite_geo(size):
    """A 16x16 item extrusion that fits ANY sprite: whole front/back faces, plus a wall on every pixel border
    facing each way, coloured from the pixel it belongs to. The cutout shader drops transparent texels, so only the
    sprite's silhouette and its outer edges remain (inner walls are hidden between the front and back faces)."""
    g = Geo()
    s = size / 16
    h = size / 2
    z0, z1 = -size / 32, size / 32
    g.quad((-h, h, z1), (h, h, z1), (h, -h, z1), (-h, -h, z1), (0, 0, 1), (0.001, 0.001), (0.999, 0.999))
    g.quad((h, h, z0), (-h, h, z0), (-h, -h, z0), (h, -h, z0), (0, 0, -1), (0.999, 0.001), (0.001, 0.999))

    def pc(x, y):  # sample a pixel's centre
        c = ((x + 0.5) / 16, (y + 0.5) / 16)
        return c, c

    for y in range(16):
        top, bot = h - y * s, h - (y + 1) * s
        for x in range(17):  # vertical borders between columns x-1 and x
            xx = -h + x * s
            if x < 16:  # wall facing -x, belongs to pixel x
                g.quad((xx, top, z0), (xx, top, z1), (xx, bot, z1), (xx, bot, z0), (-1, 0, 0), *pc(x, y))
            if x > 0:  # wall facing +x, belongs to pixel x-1
                g.quad((xx, top, z1), (xx, top, z0), (xx, bot, z0), (xx, bot, z1), (1, 0, 0), *pc(x - 1, y))
    for x in range(16):
        l, r = -h + x * s, -h + (x + 1) * s
        for y in range(17):  # horizontal borders between rows y-1 and y
            yy = h - y * s
            if y < 16:  # wall facing +y (up), belongs to pixel row y
                g.quad((l, yy, z0), (r, yy, z0), (r, yy, z1), (l, yy, z1), (0, 1, 0), *pc(x, y))
            if y > 0:  # wall facing -y (down), belongs to pixel row y-1
                g.quad((l, yy, z1), (r, yy, z1), (r, yy, z0), (l, yy, z0), (0, -1, 0), *pc(x, y - 1))
    return g


def arrow_geo():
    """Minecraft-style arrow: two crossed 16x5 px planes, 0.7 m along +Y (z up), both sides."""
    g = Geo()
    L, W = 0.7, 0.7 * 5 / 16
    y0, y1 = -L / 2, L / 2
    uv_side = ((0 / 32, 0 / 32), (16 / 32, 5 / 32))
    for n_a, n_b, axis in (((0, 0, 1), (0, 0, -1), "h"), ((1, 0, 0), (-1, 0, 0), "v")):
        if axis == "h":
            a, b, c, d = (-W / 2, y1, 0), (W / 2, y1, 0), (W / 2, y0, 0), (-W / 2, y0, 0)
        else:
            a, b, c, d = (0, y1, W / 2), (0, y1, -W / 2), (0, y0, -W / 2), (0, y0, W / 2)
        for (p00, p10, p11, p01), n in (((d, a, b, c), n_a), ((a, d, c, b), n_b)):
            g.quad(p00, p10, p11, p01, n, *uv_side)
    return g


def write_placeholder_dds(path, w, h, index):
    """Blank A8R8G8B8 DDS with a full mip chain; the first 16 bytes are a marker gtmpack searches for."""
    mips, mw, mh = [], w, h
    while True:
        mips.append(mw * mh * 4)
        if mw == 1 and mh == 1:
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
    hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, flags, h, w, w * 4, 0, len(mips))
    hdr += struct.pack("<II4sIIIII", 32, 0x41, bytes(4), 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack("<IIII4x", 0x1000 | 0x400000 | 0x8, 0, 0, 0)
    body = bytearray(sum(mips))
    marker = f"GTMTEX{index:06d}#".encode()
    body[:len(marker)] = marker
    path.write_bytes(hdr + bytes(body))


def main():
    blocks, items, sprites = read_defs()
    out = ROOT / "build" / "dlc_src"
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("*"):
        f.unlink()
    rows, recipes = [], []

    def texture(name, w, h, recipe):
        write_placeholder_dds(out / f"{name}.dds", w, h, len(recipes))
        recipes.append(f"{name};{w};{h};{recipe}")

    rnd = random.Random(7)
    for b in blocks:
        name, flags = b["name"], b["flags"]
        tex = f"gtm_{name}"
        texture(tex, 512, 128, f"sheet {name}")
        shader = "alpha" if "a" in flags else "cutout" if "c" in flags else "default"
        material = {"wood": 70, "cloth": 104, "glass": 69}.get(b["sound"], 1)
        cube(FACES_Z, 1.0, sheet_uv).write(out / f"gtm_{name}.geo")
        rows.append(f"gtm_{name};{tex};{shader};{material};0.5,0.5,0.5")
        cube(FACES_Y, HAND_SIZE, sheet_uv).write(out / f"gtm_{name}_h.geo")
        rows.append(f"gtm_{name}_h;{tex};{shader};{material};-")
        ox, oy = rnd.randrange(0, 13), rnd.randrange(0, 13)
        chip_uv = lambda col: ((0.25 + (ox + 0.1) / 64, (oy + 0.1) / 16), (0.25 + (ox + 3.9) / 64, (oy + 3.9) / 16))  # noqa: E731
        cube(FACES_Z, CHIP, chip_uv).write(out / f"gtm_{name}_p.geo")
        rows.append(f"gtm_{name}_p;{tex};{shader};{material};{CHIP / 2},{CHIP / 2},{CHIP / 2}")
    sprite_geo(HAND_SIZE).write(out / "gtm_sprite.geo")
    for sp in dict.fromkeys(sprites):
        tex = f"gtm_i_{sp}"
        texture(tex, 128, 128, f"sprite {sp}")
        (out / f"{tex}.geo").write_bytes((out / "gtm_sprite.geo").read_bytes())
        rows.append(f"{tex};{tex};cutout;1;-")
    (out / "gtm_sprite.geo").unlink()
    texture("gtm_arrow_e", 256, 256, "arrow")
    arrow_geo().write(out / "gtm_arrow.geo")
    rows.append("gtm_arrow;gtm_arrow_e;cutout;1;-")
    (out / "models.txt").write_text("\n".join(rows) + "\n")
    (out / "tex_recipes.txt").write_text("\n".join(recipes) + "\n")
    print(f"wrote {out}: {len(rows)} models, {len(recipes)} placeholder textures")


if __name__ == "__main__":
    main()
