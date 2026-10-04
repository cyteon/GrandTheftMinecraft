#!/usr/bin/env python3
"""Sources for the GrandTheftMinecraft DLC (tools/gtmpack turns them into .ydr/.ytd/.ytyp/dlc.rpf).

Writes build/dlc_src/:
  gtm_*.dds           textures (one shared texture dictionary, gtm_tex)
  <model>.geo         geometry: "v x y z nx ny nz u v" lines, then "i a b c ..." (triangles, CCW from outside)
  models.txt          model;texture;shader;material;collision ("-" or half-extents "hx,hy,hz")

Models
  gtm_<block>      1 m world block, centred, box collision (GTA space: z up)
  gtm_<block>_h    the block held in first person (Minecraft item space: y up, z towards the viewer; 0.25 m)
  gtm_<block>_p    a 0.12 m break chip (4x4 texels of the side texture), small box collision
  gtm_i_<sprite>   held items: the 16x16 sprite extruded one pixel thick (Minecraft item space, 0.25 m)
  gtm_arrow        flying/stuck arrow: Minecraft's crossed planes, along +Y (GTA space)
"""
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_mc as mc  # noqa: E402

HAND_SIZE = 0.25  # hand models are this big; the ASI rescales distance so they look Minecraft-sized
CHIP = 0.12
SPRITES = ["diamond_sword", "flint_and_steel", "ender_pearl", "bow", "bow_pulling_0", "bow_pulling_1",
           "bow_pulling_2", "crossbow_standby", "crossbow_pulling_0", "crossbow_pulling_1", "crossbow_pulling_2",
           "crossbow_arrow"]

# unit-cube faces (GTA z-up): origin = top-left seen from outside, right axis, down axis, normal, sheet column
FACES_Z = [((0, 1, 1), (1, 0, 0), (0, -1, 0), (0, 0, 1), 0), ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, -1), 2),
           ((1, 1, 1), (-1, 0, 0), (0, 0, -1), (0, 1, 0), 1), ((0, 0, 1), (1, 0, 0), (0, 0, -1), (0, -1, 0), 1),
           ((1, 0, 1), (0, 1, 0), (0, 0, -1), (1, 0, 0), 1), ((0, 1, 1), (0, -1, 0), (0, 0, -1), (-1, 0, 0), 1)]
# the same in Minecraft item space (y up, +z = south / towards the viewer)
FACES_Y = [((0, 1, 0), (1, 0, 0), (0, 0, 1), (0, 1, 0), 0), ((0, 0, 1), (1, 0, 0), (0, 0, -1), (0, -1, 0), 2),
           ((0, 1, 1), (1, 0, 0), (0, -1, 0), (0, 0, 1), 1), ((1, 1, 0), (-1, 0, 0), (0, -1, 0), (0, 0, -1), 1),
           ((1, 1, 1), (0, 0, -1), (0, -1, 0), (1, 0, 0), 1), ((0, 1, 0), (0, 0, 1), (0, -1, 0), (-1, 0, 0), 1)]


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
        self.i += [b, b + 3, b + 2, b + 2, b + 1, b]  # GTA: counter-clockwise from outside

    def write(self, path):
        lines = [f"v {' '.join(f'{c:.6g}' for c in v)}" for v in self.v]
        lines.append("i " + " ".join(map(str, self.i)))
        path.write_text("\n".join(lines) + "\n")


def add(a, b, s=1.0):
    return tuple(x + y * s for x, y in zip(a, b))


def cube(faces, size, centre, uv_for):
    """uv_for(col) -> ((u0, v0), (u1, v1)) for the face's sheet column."""
    g = Geo()
    for o, u, v, n, col in faces:
        p00 = tuple((c - 0.5) * size + centre[k] for k, c in enumerate(o))
        p10, p01 = add(p00, u, size), add(p00, v, size)
        p11 = add(p10, v, size)
        g.quad(p00, p10, p11, p01, n, *uv_for(col))
    return g


def sheet_uv(col):
    iu, iv = 0.5 / 512, 0.5 / 128
    return (col * 0.25 + iu, iv), ((col + 1) * 0.25 - iu, 1 - iv)


def sprite_geo(px, size):
    """Minecraft's generated item model: front/back + an edge quad wherever opaque meets clear (y up, z out)."""
    g = Geo()
    s = size / 16
    z0, z1 = -size / 32, size / 32
    op = lambda x, y: 0 <= x < 16 and 0 <= y < 16 and px[y, x, 3] >= 128  # noqa: E731
    h = size / 2
    for y in range(16):
        top, bot = h - y * s, h - (y + 1) * s
        for x in range(16):
            if not op(x, y):
                continue
            l, r = -h + x * s, -h + (x + 1) * s
            # sample the pixel's centre so edges get its colour
            uc = ((x + 0.5) / 16, (y + 0.5) / 16)
            uv = ((x + 0.02) / 16, (y + 0.02) / 16), ((x + 0.98) / 16, (y + 0.98) / 16)
            g.quad((l, top, z1), (r, top, z1), (r, bot, z1), (l, bot, z1), (0, 0, 1), *uv)
            g.quad((r, top, z0), (l, top, z0), (l, bot, z0), (r, bot, z0), (0, 0, -1),
                   ((x + 0.98) / 16, (y + 0.02) / 16), ((x + 0.02) / 16, (y + 0.98) / 16))
            e = (uc, uc)
            if not op(x, y - 1):
                g.quad((l, top, z0), (r, top, z0), (r, top, z1), (l, top, z1), (0, 1, 0), *e)
            if not op(x, y + 1):
                g.quad((l, bot, z1), (r, bot, z1), (r, bot, z0), (l, bot, z0), (0, -1, 0), *e)
            if not op(x - 1, y):
                g.quad((l, top, z0), (l, top, z1), (l, bot, z1), (l, bot, z0), (-1, 0, 0), *e)
            if not op(x + 1, y):
                g.quad((r, top, z1), (r, top, z0), (r, bot, z0), (r, bot, z1), (1, 0, 0), *e)
    return g


def arrow_geo():
    """Minecraft's arrow entity: two crossed 16x5 px planes + the 5x5 fletching cross, 0.7 m along +Y (z up)."""
    g = Geo()
    L, W = 0.7, 0.7 * 5 / 16
    y0, y1 = -L / 2, L / 2
    uv_side = ((0 / 32, 0 / 32), (16 / 32, 5 / 32))
    for n_a, n_b, axis in (((0, 0, 1), (0, 0, -1), "h"), ((1, 0, 0), (-1, 0, 0), "v")):
        if axis == "h":  # horizontal plane (spans x), seen from above and below
            a, b = (-W / 2, y1, 0), (W / 2, y1, 0)
            c, d = (W / 2, y0, 0), (-W / 2, y0, 0)
        else:  # vertical plane (spans z)
            a, b = (0, y1, W / 2), (0, y1, -W / 2)
            c, d = (0, y0, -W / 2), (0, y0, W / 2)
        # texture runs along the shaft: rotate uv so u follows +Y (tip at u=1)
        for (p00, p10, p11, p01), n in (((d, a, b, c), n_a), ((a, d, c, b), n_b)):
            g.quad(p00, p10, p11, p01, n, *uv_side)
    return g


def main():
    jar = mc.Jar(str(mc.ROOT / "mc_source" / "minecraft-1.21.11-client.jar"))
    out = mc.ROOT / "build" / "dlc_src"
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("*"):
        f.unlink()
    rows = []
    rnd = random.Random(7)
    for name, disp, tab, sound, flags, top, side, bottom in mc.BLOCKS:
        side = side or top
        bottom = bottom or top
        t, s, b = jar.block(top), jar.block(side), jar.block(bottom)
        if "r" in flags:
            t = mc.tint(t, mc.GRASS_TINT)
        if "f" in flags:
            t, s, b = (mc.tint(x, mc.FOLIAGE_TINT) for x in (t, s, b))
        sheet = Image.new("RGBA", (64, 16), (0, 0, 0, 0))
        for k, face in enumerate((t, s, b)):
            sheet.paste(face.resize((16, 16), Image.NEAREST), (k * 16, 0))
        if "a" not in flags and "c" not in flags:
            arr = np.asarray(sheet).copy()
            arr[:, :48, 3] = 255
            sheet = Image.fromarray(arr, "RGBA")
        tex = f"gtm_{name}"
        mc.write_dds(out / f"{tex}.dds", sheet.resize((512, 128), Image.NEAREST))
        shader = "alpha" if "a" in flags else "cutout" if "c" in flags else "default"
        material = {"wood": 70, "cloth": 104, "glass": 69}.get(sound, 1)
        cube(FACES_Z, 1.0, (0, 0, 0), sheet_uv).write(out / f"gtm_{name}.geo")
        rows.append(f"gtm_{name};{tex};{shader};{material};0.5,0.5,0.5")
        cube(FACES_Y, HAND_SIZE, (0, 0, 0), sheet_uv).write(out / f"gtm_{name}_h.geo")
        rows.append(f"gtm_{name}_h;{tex};{shader};{material};-")
        # break chip: one 4x4 texel window of the side texture on every face
        ox, oy = rnd.randrange(0, 13), rnd.randrange(0, 13)
        chip_uv = lambda col: ((0.25 + (ox + 0.1) / 64, (oy + 0.1) / 16), (0.25 + (ox + 3.9) / 64, (oy + 3.9) / 16))  # noqa: E731
        cube(FACES_Z, CHIP, (0, 0, 0), chip_uv).write(out / f"gtm_{name}_p.geo")
        h = CHIP / 2
        rows.append(f"gtm_{name}_p;{tex};{shader};{material};{h},{h},{h}")
    for sp in SPRITES:
        im = jar.img(f"item/{sp}.png").resize((16, 16), Image.NEAREST)
        tex = f"gtm_i_{sp}"
        mc.write_dds(out / f"{tex}.dds", im.resize((128, 128), Image.NEAREST))
        sprite_geo(np.asarray(im), HAND_SIZE).write(out / f"{tex}.geo")
        rows.append(f"{tex};{tex};cutout;1;-")
    arrow = jar.img("entity/projectiles/arrow.png")
    mc.write_dds(out / "gtm_arrow_e.dds", arrow.resize((arrow.width * 8, arrow.height * 8), Image.NEAREST))
    arrow_geo().write(out / "gtm_arrow.geo")
    rows.append("gtm_arrow;gtm_arrow_e;cutout;1;-")
    (out / "models.txt").write_text("\n".join(rows) + "\n")
    print(f"wrote {out}: {len(rows)} models, {len(list(out.glob('*.dds')))} textures")


if __name__ == "__main__":
    main()
