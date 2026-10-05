#!/usr/bin/env python3
"""Sources for the GrandTheftMinecraft DLC, with NO Minecraft content (tools/gtmpack builds dlc.rpf from them).

Everything here is ours: generic meshes, and blank placeholder textures. Each placeholder starts with a marker so
gtmpack can record where its pixels sit inside the texture dictionary; on first launch the ASI fills them from the
player's own Minecraft (or one fetched from Mojang's servers) using the recipes in tex_recipes.txt.

Writes build/dlc_src/:
  <texture>.dds       placeholder textures (A8R8G8B8, full mip chain, marker in the first bytes)
  tex_recipes.txt     texture;width;height;recipe;format   (recipe: "sheet <block name>" | "sprite <sprite>" | "arrow";
                      format: rgba | dxt1 | dxt5. Block sheets are 8x pixel art, so every 4x4 DXT block is one colour)
  <model>.geo         "v x y z nx ny nz u v" lines, then "i a b c ..." (triangles, counter-clockwise from outside)
  models.txt          model;texture;shader;material;collision ("-" or half-extents "hx,hy,hz")

Models
  gtm_<block>      1 m world block, centred, box collision (GTA space: z up)
  gtm_<block>_h    the block held in first person (Minecraft item space: y up, z towards the viewer; 0.25 m)
  gtm_<block>_p    a 0.12 m break chip (a 4x4 texel window of the side texture), small box collision
  gtm_i_<sprite>   held item: a generic 16x16 extrusion; the cutout shader trims it to the sprite's shape
  gtm_arrow        flying/stuck arrow: crossed planes along +Y (GTA space)
  gtm_i_<sprite>_tp  held item in third person (0.5 m)
  gtm_steve_<part> Steve's head/body/arms/legs from a 64x64 skin (base + outer layer), origin at each part's
                   pivot (neck, shoulders, hips); x = character's right, y = forward, z = up (GTA space)
"""
import random
import struct
from pathlib import Path

import numpy as np

import shapes

ROOT = Path(__file__).resolve().parent.parent
HAND_SIZE = 0.25  # hand models are this big; the ASI rescales distance so they look Minecraft-sized
TP_SIZE = 0.5     # held items in third person
PX = 0.9375 / 16  # Steve: one skin pixel in metres (Minecraft renders players at 15/16 scale: 1.875 m tall)
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
            # flags: letters, then optionally :<shape>[@<base block whose textures it uses>]
            letters, _, spec = f[4].partition(":")
            shape, _, base = spec.partition("@")
            b = dict(name=f[1], flags=letters, shape=shape, base=base, sound=f[5], top=f[6], side=f[7] or f[6],
                     front=len(f) > 9 and f[9])
            blocks.append(b)
            sp = shapes.held_sprite(shape, b["name"], b["top"], b["side"]) if shape else None
            if sp:
                sprites.append(sp)
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

    def double_sided(self):
        """Add every quad again facing the other way (Minecraft's no-cull render types, e.g. elytra wings)."""
        for k in range(0, len(self.i), 3):
            a, b, c = self.i[k:k + 3]
            base = len(self.v)
            for idx in (a, c, b):
                x, y, z, nx, ny, nz, u, v = self.v[idx]
                self.v.append((x, y, z, -nx, -ny, -nz, u, v))
            self.i += [base, base + 1, base + 2]

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


def mc_box(g, scale, box, uv, tex, inflate=0.0, mirror=False):
    """A cuboid from a Minecraft entity model: box = (x, y, z, w, h, d) in Minecraft model space relative to the
    part's pivot (y down, the model faces -z, -x is its right), uv = texture offset, tex = texture size. Converted to
    GTA space (x = the character's right, y = forward, z = up). Standard Minecraft cube UV layout: top (u+d, v),
    bottom (u+d+w, v), then the strip at v+d: right side, front, left side, back. mirror flips it like Minecraft's
    mirrored limbs (left arms/legs that reuse the right ones' texture)."""
    bx, by, bz, w, h, d = box
    u, v = uv
    tw, th = tex
    x0, x1 = -(bx + w) - inflate, -bx + inflate  # Minecraft -x = right = our +x
    y0, y1 = -(bz + d) - inflate, -bz + inflate  # Minecraft -z = front = our +y
    z0, z1 = -(by + h) - inflate, -by + inflate  # Minecraft -y = up = our +z
    P = lambda x, y, z: (x * scale, y * scale, z * scale)  # noqa: E731
    e = 0.01

    def region(a, b, cw, ch, flip=False):
        u0, v0, u1, v1 = (a + e) / tw, (b + e) / th, (a + cw - e) / tw, (b + ch - e) / th
        return ((u1, v0), (u0, v1)) if flip else ((u0, v0), (u1, v1))

    right, left = region(u, v + d, d, h), region(u + d + w, v + d, d, h)
    if mirror:  # mirrored limbs swap the side faces and flip every face horizontally
        right, left = region(u + d + w, v + d, d, h, True), region(u, v + d, d, h, True)
    front = region(u + d, v + d, w, h, mirror)
    back = region(u + 2 * d + w, v + d, w, h, mirror)
    top = region(u + d, v, w, d, mirror)
    bottom = region(u + d + w, v, w, d, mirror)
    faces = [
        ((x1, y1, z1), (-1, 0, 0), (0, 0, -1), (0, 1, 0), front),
        ((x0, y0, z1), (1, 0, 0), (0, 0, -1), (0, -1, 0), back),
        ((x1, y0, z1), (0, 1, 0), (0, 0, -1), (1, 0, 0), right),
        ((x0, y1, z1), (0, -1, 0), (0, 0, -1), (-1, 0, 0), left),
        ((x1, y0, z1), (-1, 0, 0), (0, 1, 0), (0, 0, 1), top),
        ((x1, y1, z0), (-1, 0, 0), (0, -1, 0), (0, 0, -1), bottom),
    ]
    sizes = {(1, 0, 0): x1 - x0, (0, 1, 0): y1 - y0, (0, 0, 1): z1 - z0}
    for o, du, dv, n, (uv0, uv1) in faces:
        lu = sizes[tuple(abs(c) for c in du)]
        lv = sizes[tuple(abs(c) for c in dv)]
        p00 = P(*o)
        p10 = P(*(o[k] + du[k] * lu for k in range(3)))
        p01 = P(*(o[k] + dv[k] * lv for k in range(3)))
        p11 = P(*(o[k] + du[k] * lu + dv[k] * lv for k in range(3)))
        g.quad(p00, p10, p11, p01, n, uv0, uv1)


# Entity rigs, copied from Minecraft's model classes. Each part: name, pivot (Minecraft model space: y = 24 is the
# ground), how the mod poses it, the GTA bones it follows, and its boxes: (box, uv, inflate, mirror).
#   kinds: body (upright, faces the ped's heading), head (looks at the target / camera), limb (follows bone b0->b1),
#          fwdarm (held straight out in front, like a zombie's)
HUMANOID_BONES = {"rarm": (40269, 57005), "larm": (45509, 18905), "rleg": (51826, 52301), "lleg": (58271, 14201)}


def humanoid(arm_w, layers, uvs, arm_kind="limb"):
    """Player / zombie / skeleton layout. uvs: head, body, rarm, larm, rleg, lleg (larm/lleg None = mirror right)."""
    aw = arm_w
    parts = [
        ("head", (0, 0, 0), "head", (0, 0), [((-4, -8, -4, 8, 8, 8), uvs["head"])]),
        ("body", (0, 0, 0), "body", (0, 0), [((-4, 0, -2, 8, 12, 4), uvs["body"])]),
        ("rarm", (-5, 2, 0), arm_kind, HUMANOID_BONES["rarm"], [((-3 if aw == 4 else -1, -2, -aw / 2, aw, 12, aw), uvs["rarm"])]),
        ("larm", (5, 2, 0), arm_kind, HUMANOID_BONES["larm"], [((-1, -2, -aw / 2, aw, 12, aw), uvs["larm"] or uvs["rarm"], uvs["larm"] is None)]),
        ("rleg", (-1.9 if aw == 4 else -2, 12, 0), "limb", HUMANOID_BONES["rleg"], [((-aw / 2, 0, -aw / 2, aw, 12, aw), uvs["rleg"])]),
        ("lleg", (1.9 if aw == 4 else 2, 12, 0), "limb", HUMANOID_BONES["lleg"], [((-aw / 2, 0, -aw / 2, aw, 12, aw), uvs["lleg"] or uvs["rleg"], uvs["lleg"] is None)]),
    ]
    out = []
    for name, pivot, kind, bones, boxes in parts:
        bl = []
        for bx in boxes:
            box, uv = bx[0], bx[1]
            mirror = bx[2] if len(bx) > 2 else False
            bl.append((box, uv, 0.0, mirror))
            if name in layers:  # the outer layer (hat, jacket, sleeves, pants)
                luv, infl = layers[name]
                bl.append((box, luv, infl, mirror and luv == uv))
        out.append((name, pivot, kind, bones, bl))
    return out


PLAYER_UV = {"head": (0, 0), "body": (16, 16), "rarm": (40, 16), "larm": (32, 48), "rleg": (0, 16), "lleg": (16, 48)}
PLAYER_LAYERS = {"head": ((32, 0), 0.5), "body": ((16, 32), 0.25), "rarm": ((40, 32), 0.25),
                 "larm": ((48, 48), 0.25), "rleg": ((0, 32), 0.25), "lleg": ((0, 48), 0.25)}
OLD_UV = {"head": (0, 0), "body": (16, 16), "rarm": (40, 16), "larm": None, "rleg": (0, 16), "lleg": None}

RIGS = {
    # name: (texture recipe, texture size, scale m/px, parts)
    "steve": ("skin", (64, 64), 0.9375 / 16, humanoid(4, PLAYER_LAYERS, PLAYER_UV)),
    # zombie.png is 64x64 but only uses the old 64x32 layout: left limbs mirror the right ones, no outer layers
    "zombie": ("entity entity/zombie/zombie.png", (64, 64), 1 / 16,
               humanoid(4, {"head": ((32, 0), 0.5)}, OLD_UV, arm_kind="fwdarm")),
    "skeleton": ("entity entity/skeleton/skeleton.png", (64, 32), 1 / 16,
                 humanoid(2, {"head": ((32, 0), 0.5)}, OLD_UV)),
    "creeper": ("entity entity/creeper/creeper.png", (64, 32), 1 / 16, [
        ("head", (0, 6, 0), "head", (0, 0), [((-4, -8, -4, 8, 8, 8), (0, 0), 0.0, False)]),
        ("body", (0, 6, 0), "body", (0, 0), [((-4, 0, -2, 8, 12, 4), (16, 16), 0.0, False)]),
        # front legs follow the thighs, back legs the opposite thighs: a trot
        ("rfleg", (-2, 18, -4), "limb", (51826, 52301), [((-2, 0, -2, 4, 6, 4), (0, 16), 0.0, False)]),
        ("lfleg", (2, 18, -4), "limb", (58271, 14201), [((-2, 0, -2, 4, 6, 4), (0, 16), 0.0, False)]),
        ("rhleg", (-2, 18, 4), "limb", (58271, 14201), [((-2, 0, -2, 4, 6, 4), (0, 16), 0.0, False)]),
        ("lhleg", (2, 18, 4), "limb", (51826, 52301), [((-2, 0, -2, 4, 6, 4), (0, 16), 0.0, False)]),
    ]),
    # ElytraModel: two 10x20x2 wings (inflated by 1), posed by the ASI from Steve's body; not a ped rig
    "elytra": ("entity entity/equipment/wings/elytra.png", (64, 32), 0.9375 / 16, [
        ("lwing", (5, 0, 2), "wing", (0, 0), [((-10, 0, 0, 10, 20, 2), (22, 0), 1.0, False)]),
        ("rwing", (-5, 0, 2), "wing", (0, 0), [((0, 0, 0, 10, 20, 2), (22, 0), 1.0, True)]),
    ]),
    "golem": ("entity entity/iron_golem/iron_golem.png", (128, 128), 1 / 16, [
        ("head", (0, -7, -2), "head", (0, 0), [((-4, -12, -5.5, 8, 10, 8), (0, 0), 0.0, False),
                                              ((-1, -5, -7.5, 2, 4, 2), (24, 0), 0.0, False)]),
        ("body", (0, -7, 0), "body", (0, 0), [((-9, -2, -6, 18, 12, 11), (0, 40), 0.0, False),
                                             ((-4.5, 10, -3, 9, 5, 6), (0, 70), 0.5, False)]),
        # Minecraft pivots the arms at the body's centre line; pivot them at the shoulders (same shape at rest) so
        # sideways swings from the GTA bones don't orbit the chest
        ("rarm", (-11, -7, 0), "limb", (40269, 57005), [((-2, -2.5, -3, 4, 30, 6), (60, 21), 0.0, False)]),
        ("larm", (11, -7, 0), "limb", (45509, 18905), [((-2, -2.5, -3, 4, 30, 6), (60, 58), 0.0, False)]),
        ("rleg", (-4, 11, 0), "limb", (51826, 52301), [((-3.5, -3, -3, 6, 16, 5), (37, 0), 0.0, False)]),
        ("lleg", (5, 11, 0), "limb", (58271, 14201), [((-3.5, -3, -3, 6, 16, 5), (60, 0), 0.0, True)]),
    ]),
}


def mc_rotated(g, scale, pivot, rot, boxes, tex):
    """Boxes of a Minecraft sub-part that sits at `pivot` (Minecraft space, relative to the model's origin) and is
    rotated by `rot` = (xRot, yRot, zRot) radians (PartPose: X then Y then Z), baked into g in our space."""
    import math
    tmp = Geo()
    for box, uv, infl, mirror in boxes:
        mc_box(tmp, scale, box, uv, tex, infl, mirror)
    xr, yr, zr = rot

    def rx(a, v): return (v[0], v[1] * math.cos(a) - v[2] * math.sin(a), v[1] * math.sin(a) + v[2] * math.cos(a))
    def ry(a, v): return (v[0] * math.cos(a) + v[2] * math.sin(a), v[1], -v[0] * math.sin(a) + v[2] * math.cos(a))
    def rz(a, v): return (v[0] * math.cos(a) - v[1] * math.sin(a), v[0] * math.sin(a) + v[1] * math.cos(a), v[2])
    to_mc = lambda o: (-o[0], -o[2], -o[1])  # noqa: E731  ours -> Minecraft
    from_mc = lambda m: (-m[0], -m[2], -m[1])  # noqa: E731
    rot3 = lambda v: from_mc(rx(xr, ry(yr, rz(zr, to_mc(v)))))  # noqa: E731
    off = from_mc(tuple(c * scale for c in pivot))
    base = len(g.v)
    for x, y, z, nx, ny, nz, u, v in tmp.v:
        px, py, pz = rot3((x, y, z))
        qx, qy, qz = rot3((nx, ny, nz))
        g.v.append((px + off[0], py + off[1], pz + off[2], qx, qy, qz, u, v))
    g.i += [base + k for k in tmp.i]


# The Wither (WitherBossModel, drawn at 2x): posed by the ASI (it flies; no ped skeleton drives it).
# Model origin = Minecraft (0, 0, 0) of the model; the ASI puts that 24 px above the Wither's "feet".
WITHER_SCALE = 2 / 16
WITHER_TEX = (64, 64)


def wither_models(out, rows, texture):
    tex = "gtm_tex_wither"
    texture(tex, 512, 512, "entity entity/wither/wither.png")
    g = Geo()  # body: shoulders, the tilted ribcage and the tail
    mc_box(g, WITHER_SCALE, (-10, 3.9, -0.5, 20, 3, 3), (0, 16), WITHER_TEX)
    rib = (-2, 6.9, -0.5)
    mc_rotated(g, WITHER_SCALE, rib, (0.20420352, 0, 0), [
        ((0, 0, 0, 3, 10, 3), (0, 22), 0.0, False),
        ((-4, 1.5, 0.5, 11, 2, 2), (24, 22), 0.0, False),
        ((-4, 4, 0.5, 11, 2, 2), (24, 22), 0.0, False),
        ((-4, 6.5, 0.5, 11, 2, 2), (24, 22), 0.0, False)], WITHER_TEX)
    import math
    tail = (-2, 6.9 + math.cos(0.20420352) * 10, -0.5 + math.sin(0.20420352) * 10)
    mc_rotated(g, WITHER_SCALE, tail, (0.83252203, 0, 0), [((0, 0, 0, 3, 6, 3), (12, 22), 0.0, False)], WITHER_TEX)
    g.write(out / "gtm_wither_body.geo")
    rows.append(f"gtm_wither_body;{tex};cutout;1;-")
    g = Geo()
    mc_box(g, WITHER_SCALE, (-4, -4, -4, 8, 8, 8), (0, 0), WITHER_TEX)
    g.write(out / "gtm_wither_head.geo")
    rows.append(f"gtm_wither_head;{tex};cutout;1;-")
    g = Geo()
    mc_box(g, WITHER_SCALE, (-4, -4, -4, 6, 6, 6), (32, 0), WITHER_TEX)
    g.write(out / "gtm_wither_shead.geo")
    rows.append(f"gtm_wither_shead;{tex};cutout;1;-")
    g = Geo()  # the wither skull projectile (WitherSkullRenderer: an 8x8x8 head at 1x)
    mc_box(g, 1 / 16, (-4, -4, -4, 8, 8, 8), (0, 35), WITHER_TEX)
    g.write(out / "gtm_wither_skull.geo")
    rows.append(f"gtm_wither_skull;{tex};cutout;1;-")


# ---- furniture with Minecraft entity textures: beds, chests, signs, banners (block-centred metres, front +Y) ----
ENTITY_SHAPES = ("bed", "chest", "sign", "banner")


def xf(g, fp, fn=None):
    """A Geo with every vertex position (and normal) mapped."""
    out = Geo()
    for x, y, z, nx, ny, nz, u, v in g.v:
        px, py, pz = fp(x, y, z)
        qx, qy, qz = (fn or fp)(nx, ny, nz) if fn else (nx, ny, nz)
        out.v.append((px, py, pz, qx, qy, qz, u, v))
    out.i = list(g.i)
    return out


def join(*gs):
    out = Geo()
    for g in gs:
        base = len(out.v)
        out.v += g.v
        out.i += [base + k for k in g.i]
    return out


def bed_half(head):
    """BedRenderer's head / foot: the 16x16x6 mattress laid flat (the 'front' face up, pillow towards -Y) on legs."""
    g = Geo()
    mc_box(g, 1 / 16, (0, 0, 0, 16, 16, 6), (0, 0) if head else (0, 22), (64, 64))
    for lx, ly in ((13, 0), (0, 0)) if head else ((13, 13), (0, 13)):
        mc_box(g, 1 / 16, (lx, ly, 6, 3, 3, 3), (50, 6), (64, 64))
    # mc_box: (-mcx, -mcz, -mcy)/16; flat: x + 1, -z (pillow end = -Y), y + 9/16 (height); centred on the block
    return xf(g, lambda x, y, z: (x + 1 - 0.5, -z - 0.5, y + 9 / 16 - 0.5), lambda x, y, z: (x, -z, y))


def chest_model(opened):
    """ChestModel (single): bottom, lid, lock; Minecraft draws it y-up, so mc_box's output turns 180 degrees about X."""
    base, lid = Geo(), Geo()
    mc_box(base, 1 / 16, (1, 0, 1, 14, 10, 14), (0, 19), (64, 64))
    mc_box(lid, 1 / 16, (1, 9, 1, 14, 5, 14), (0, 0), (64, 64))
    mc_box(lid, 1 / 16, (7, 6, 15, 2, 4, 1), (0, 0), (64, 64))
    flip = lambda x, y, z: (x + 1 - 0.5, -y - 0.5, -z - 0.5)  # noqa: E731
    flipn = lambda x, y, z: (x, -y, -z)  # noqa: E731
    base, lid = xf(base, flip, flipn), xf(lid, flip, flipn)
    if opened:  # the lid swings up about its back edge (Minecraft opens it 90 degrees)
        import math
        a = math.radians(85)
        hy, hz = 1 / 16 - 0.5, 9 / 16 - 0.5

        def r(x, y, z, pivot=True):
            y0, z0 = (y - hy, z - hz) if pivot else (y, z)
            y1, z1 = y0 * math.cos(a) - z0 * math.sin(a), y0 * math.sin(a) + z0 * math.cos(a)
            return (x, y1 + hy, z1 + hz) if pivot else (x, y1, z1)
        lid = xf(lid, r, lambda x, y, z: r(x, y, z, False))
    return join(base, lid)


SIGN_S = 0.6666667 / 16


def sign_model(wall):
    """SignRenderer: board (+ stick) at 2/3 scale; on a wall the board moves back to it and down."""
    g = Geo()
    mc_box(g, SIGN_S, (-12, -14, -1, 24, 12, 2), (0, 0), (64, 32))
    if not wall:
        mc_box(g, SIGN_S, (-1, -2, -1, 2, 14, 2), (0, 14), (64, 32))
        return g
    return xf(g, lambda x, y, z: (x, y - 0.4375, z - 0.3125))


def banner_model(wall):
    """BannerRenderer: flag (40 px tall, hanging from the bar), pole and bar at 2/3 scale."""
    g = Geo()
    mc_box(g, SIGN_S, (-10, -32, -2, 20, 40, 1), (0, 0), (64, 64))
    mc_box(g, SIGN_S, (-10, -32, -1, 20, 2, 2), (0, 42), (64, 64))
    if not wall:
        mc_box(g, SIGN_S, (-1, -30, -1, 2, 42, 2), (44, 0), (64, 64))
        return g
    return xf(g, lambda x, y, z: (x, y - 0.4375, z - 0.3125))


def entity_furniture(b):
    """(texture name, size, recipe), {suffix: (geo, collision boxes in pixels)}, hand geo or None."""
    shape, top = b["shape"], b["top"]
    if shape == "banner":
        rgb = top.split(":")[1]
        tex = (f"gtm_tex_banner_{rgb}", (512, 512), f"banner {rgb}")
    else:
        tex = (f"gtm_tex_e_{top.replace('/', '_')}", (512, 256) if shape == "sign" else (512, 512),
               f"entity entity/{top}.png")
    if shape == "bed":
        head, foot = bed_half(True), bed_half(False)
        both = join(xf(head, lambda x, y, z: (x * 0.5, y * 0.5 - 0.25, z * 0.5)),
                    xf(foot, lambda x, y, z: (x * 0.5, y * 0.5 + 0.25, z * 0.5)))
        return tex, {"_head": (head, [((0, 0, 0), (16, 16, 9))]), "_foot": (foot, [((0, 0, 0), (16, 16, 9))])}, both
    if shape == "chest":
        closed = chest_model(False)
        return tex, {"": (closed, [((1, 1, 0), (15, 15, 14))]), "_open": (chest_model(True), [((1, 1, 0), (15, 15, 10))])}, closed
    if shape == "sign":
        return tex, {"": (sign_model(False), []), "_wall": (sign_model(True), [])}, None
    standing = banner_model(False)
    small = xf(standing, lambda x, y, z: (x * 0.5, y * 0.5, (z - 0.42) * 0.5))
    return tex, {"": (standing, []), "_wall": (banner_model(True), [])}, small


def geo_tris(g):
    """A Geo's triangles in block pixels (for inventory icons): (normal, (x, y, z, u, v) * 3)."""
    out = []
    for k in range(0, len(g.i), 3):
        vs = [g.v[i] for i in g.i[k:k + 3]]
        out.append((vs[0][3:6], *[((x + 0.5) * 16, (y + 0.5) * 16, (z + 0.5) * 16, u, v) for x, y, z, _, _, _, u, v in vs]))
    return out


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


def mip_sizes(w, h, fmt):
    sizes, mw, mh = [], w, h
    while True:
        if fmt == "rgba":
            sizes.append(mw * mh * 4)
        else:
            sizes.append(max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * (8 if fmt == "dxt1" else 16))
        if mw == 1 and mh == 1:
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)
    return sizes


def write_placeholder_dds(path, w, h, index, fmt="rgba"):
    """Blank DDS (A8R8G8B8, DXT1 or DXT5) with a full mip chain; the first 16 bytes are a marker gtmpack searches for."""
    mips = mip_sizes(w, h, fmt)
    if fmt == "rgba":
        flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
        hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, flags, h, w, w * 4, 0, len(mips))
        hdr += struct.pack("<II4sIIIII", 32, 0x41, bytes(4), 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    else:
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
        hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, flags, h, w, mips[0], 0, len(mips))
        hdr += struct.pack("<II4sIIIII", 32, 0x4, fmt.upper().encode(), 0, 0, 0, 0, 0)
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

    def texture(name, w, h, recipe, fmt="dxt5"):  # everything is 8x pixel art: DXT is exact at the top mips
        write_placeholder_dds(out / f"{name}.dds", w, h, len(recipes), fmt)
        recipes.append(f"{name};{w};{h};{recipe};{fmt}")

    rnd = random.Random(7)
    made_tex, icons, icon_done = set(), [], set()

    def tri_line(t):
        n, *vs = t
        return "t;" + ",".join(f"{c:.4g}" for c in n) + ";" + ";".join(",".join(f"{c:.5g}" for c in v) for v in vs)
    for b in blocks:
        name, flags = b["name"], b["flags"]
        tex = f"gtm_{b['base'] or name}"
        if not b["base"]:
            texture(tex, 512, 128, f"sheet {name}", "dxt5" if "a" in flags else "dxt1")
        shader = "alpha" if "a" in flags else "cutout" if "c" in flags else "default"
        material = {"wood": 70, "cloth": 104, "glass": 69}.get(b["sound"], 1)
        if b["shape"] in ENTITY_SHAPES:  # beds, chests, signs, banners: Minecraft's entity models and textures
            (etex, (tw, th), recipe), variants, hand = entity_furniture(b)
            if etex not in made_tex:
                made_tex.add(etex)
                texture(etex, tw, th, recipe)
            for suffix, (g, coll) in variants.items():
                g.write(out / f"gtm_{name}{suffix}.geo")
                rows.append(f"gtm_{name}{suffix};{etex};cutout;{material};{shapes.coll_spec(coll)}")
            if hand:
                k = HAND_SIZE
                xf(hand, lambda x, y, z: (-x * k, z * k, y * k), lambda x, y, z: (-x, z, y)).write(out / f"gtm_{name}_h.geo")
                rows.append(f"gtm_{name}_h;{etex};cutout;{material};-")
                icons.append(f"tpl;{b['shape']};entity")
                icons += [tri_line(t) for t in geo_tris(hand)] if b["shape"] not in icon_done else []
                icon_done.add(b["shape"])
                icons.append(f"blk;{name};{b['shape']};entity")
        elif b["shape"]:  # slabs, stairs, fences... (tools/shapes.py): a model per variant, the ASI picks one
            variants, hand = shapes.build(b["shape"], front=bool(b["front"]))
            for suffix, (quads, coll) in variants.items():
                g = Geo()
                shapes.emit(quads, g, "world")
                g.write(out / f"gtm_{name}{suffix}.geo")
                rows.append(f"gtm_{name}{suffix};{tex};{shader};{material};{shapes.coll_spec(coll)}")
            if hand:
                g = Geo()
                shapes.emit(hand, g, "hand", HAND_SIZE)
                g.write(out / f"gtm_{name}_h.geo")
                rows.append(f"gtm_{name}_h;{tex};{shader};{material};-")
                key = b["shape"] + ("_f" if b["front"] else "")
                if key not in icon_done:
                    icon_done.add(key)
                    icons.append(f"tpl;{key};sheet")
                    icons += [tri_line(t) for t in shapes.icon_tris(hand)]
                icons.append(f"blk;{name};{key};sheet")
        elif "k" in flags:  # mob head: SkullModel's 8x8x8 head (+ hat layer) on the floor of its cell, facing +y
            stex = f"gtm_tex_{name}"
            big = b["top"].startswith(("zombie/", "player/"))  # 64x64 textures with a hat layer
            texture(stex, 512, 512 if big else 256, f"entity entity/{b['top']}.png")
            head = Geo()
            mc_box(head, 1 / 16, (-4, -8, -4, 8, 8, 8), (0, 0), (64, 64 if big else 32))
            if big:
                mc_box(head, 1 / 16, (-4, -8, -4, 8, 8, 8), (32, 0), (64, 64), 0.25)
            g = Geo()
            g.v = [(x, y, z - 0.5, nx, ny, nz, u, v) for x, y, z, nx, ny, nz, u, v in head.v]
            g.i = list(head.i)
            g.write(out / f"gtm_{name}.geo")
            rows.append(f"gtm_{name};{stex};cutout;{material};0.25,0.25,0.25,0,0,-0.25")  # the head only
            k = HAND_SIZE  # in the hand: half a held block, face towards the viewer (ours -> item space)
            g = Geo()
            g.v = [(-x * k, (z - 0.25) * k, y * k, -nx, nz, ny, u, v) for x, y, z, nx, ny, nz, u, v in head.v]
            g.i = list(head.i)
            g.write(out / f"gtm_{name}_h.geo")
            rows.append(f"gtm_{name}_h;{stex};cutout;{material};-")
        else:
            # a front (sheet column 3) on the +Y side in the world (the ASI turns the prop towards you) and on the
            # side facing the viewer in the hand
            fz = [(o, u, v, n, 3 if b["front"] and n == (0, 1, 0) else c) for o, u, v, n, c in FACES_Z]
            fy = [(o, u, v, n, 3 if b["front"] and n == (0, 0, 1) else c) for o, u, v, n, c in FACES_Y]
            cube(fz, 1.0, sheet_uv).write(out / f"gtm_{name}.geo")
            rows.append(f"gtm_{name};{tex};{shader};{material};0.5,0.5,0.5")
            cube(fy, HAND_SIZE, sheet_uv).write(out / f"gtm_{name}_h.geo")
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
    sprite_geo(TP_SIZE).write(out / "gtm_sprite_tp.geo")
    for sp in dict.fromkeys(sprites):
        (out / f"gtm_i_{sp}_tp.geo").write_bytes((out / "gtm_sprite_tp.geo").read_bytes())
        rows.append(f"gtm_i_{sp}_tp;gtm_i_{sp};cutout;1;-")
    (out / "gtm_sprite.geo").unlink()
    (out / "gtm_sprite_tp.geo").unlink()
    # entity rigs (Steve, mobs): one model per part, origin at the part's pivot; rigs.txt tells the ASI how to pose them
    rig_lines = ["# rig;name;scale (m per model pixel)",
                 "# part;rig;model;kind;pivot x;pivot y;pivot z (GTA space, pixels above the ground);bone0;bone1"]
    for rig, (recipe, (tw, th), scale, parts) in RIGS.items():
        tex = "gtm_skin" if rig == "steve" else f"gtm_tex_{rig}"
        texture(tex, tw * 8, th * 8, recipe)
        rig_lines.append(f"rig;{rig};{scale:.6f}")
        for name, (px, py, pz), kind, (b0, b1), boxes in parts:
            g = Geo()
            for box, uv, infl, mirror in boxes:
                mc_box(g, scale, box, uv, (tw, th), infl, mirror)
            if kind == "wing":
                g.double_sided()
            model = f"gtm_r_{rig}_{name}"  # r_: rig parts (zombie_head is also a block)
            g.write(out / f"{model}.geo")
            rows.append(f"{model};{tex};cutout;1;-")
            # Minecraft pivot -> GTA space above the ground
            rig_lines.append(f"part;{rig};{model};{kind};{-px:g};{-pz:g};{24 - py:g};{b0};{b1}")
    (out / "rigs.txt").write_text("\n".join(rig_lines) + "\n")
    wither_models(out, rows, texture)
    texture("gtm_arrow_e", 256, 256, "arrow")
    arrow_geo().write(out / "gtm_arrow.geo")
    rows.append("gtm_arrow;gtm_arrow_e;cutout;1;-")
    dup = [n for n, c in __import__("collections").Counter(r.split(";")[0] for r in rows).items() if c > 1]
    assert not dup, f"duplicate model names: {dup}"
    (out / "models.txt").write_text("\n".join(rows) + "\n")
    # inventory icons of 3D shaped blocks: geometry only (the ASI draws them with the player's own textures)
    tpl_seen, lines = set(), ["# tpl;<template>;sheet|entity, then t;<normal>;<x,y,z,u,v> x3 (block pixels); blk;<block>;<template>;<kind>"]
    for l in icons:
        if l.startswith("tpl;"):
            key = l.split(";")[1]
            if key in tpl_seen:
                continue
            tpl_seen.add(key)
        lines.append(l)
    (out / "icons.txt").write_text("\n".join(lines) + "\n")
    (out / "tex_recipes.txt").write_text("\n".join(recipes) + "\n")
    print(f"wrote {out}: {len(rows)} models, {len(recipes)} placeholder textures")


if __name__ == "__main__":
    main()
