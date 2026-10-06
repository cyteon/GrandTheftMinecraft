"""Shaped block models for tools/make_dlc_src.py (slabs, stairs, walls, fences, gates, panes, carpets, torches,
lanterns, plants, ladders, doors, trapdoors). No Minecraft content: just boxes and planes, written from scratch.

Pixel space: 0..16 per block, x east, y north, z up. A block's "canonical" orientation has its front (or whoever
placed it) towards +Y; the ASI turns props by the block's facing. Faces sample the block's sheet (columns: 0 top,
1 side, 2 bottom, 3 front) by position, the way Minecraft's automatic UVs work, unless a face brings its own uv
(u0, v0, u1, v1 in the column's 16x16 pixels).

Variant names (model gtm_<block><suffix>) and connection masks must match gta/src/shapes.cpp:
  arms mask: 1 = +Y, 2 = +X, 4 = -Y, 8 = -X; canonical variants post 0, n 1, ns 5, ne 3, nes 7, nesw 15 (rotated by
  multiples of 90 degrees anticlockwise at runtime).
"""
import math

# normal: (origin, u axis, v axis, column) of the matching unit-cube face (same frames as FACES_Z)
FRAMES = {
    (0, 0, 1): ((0, 1, 1), (1, 0, 0), (0, -1, 0), 0),
    (0, 0, -1): ((0, 0, 0), (1, 0, 0), (0, 1, 0), 2),
    (0, 1, 0): ((1, 1, 1), (-1, 0, 0), (0, 0, -1), 1),
    (0, -1, 0): ((0, 0, 1), (1, 0, 0), (0, 0, -1), 1),
    (1, 0, 0): ((1, 0, 1), (0, 1, 0), (0, 0, -1), 1),
    (-1, 0, 0): ((0, 1, 1), (0, -1, 0), (0, 0, -1), 1),
}
UP, DOWN, N, S, E, W = (0, 0, 1), (0, 0, -1), (0, 1, 0), (0, -1, 0), (1, 0, 0), (-1, 0, 0)
ARMS = {1: 0, 2: -90, 4: 180, 8: 90}  # arm bit -> rotation (degrees anticlockwise) of the +Y arm
CANON = {"_post": 0, "_n": 1, "_ns": 5, "_ne": 3, "_nes": 7, "_nesw": 15}


class Quad:
    def __init__(self, p, n, col, uv):
        self.p, self.n, self.col, self.uv = [tuple(map(float, c)) for c in p], tuple(map(float, n)), col, uv


def box(lo, hi, cols=None, uv=None, skip=(), front=False):
    """Quads of an axis-aligned box (pixels). cols / uv: per-normal column / uv overrides; skip: normals left out;
    front: the +Y face uses column 3."""
    out = []
    for n, (o, u, v, col) in FRAMES.items():
        if n in skip:
            continue
        k = [abs(c) for c in n].index(1)
        plane = hi[k] if n[k] > 0 else lo[k]
        a, b = [i for i in range(3) if i != k]
        corners = []
        for ca in (lo[a], hi[a]):
            for cb in (lo[b], hi[b]):
                c = [0, 0, 0]
                c[k], c[a], c[b] = plane, ca, cb
                s = sum((c[i] / 16 - o[i]) * u[i] for i in range(3))
                t = sum((c[i] / 16 - o[i]) * v[i] for i in range(3))
                corners.append((s, t, tuple(c)))
        smin, smax = min(c[0] for c in corners), max(c[0] for c in corners)
        tmin, tmax = min(c[1] for c in corners), max(c[1] for c in corners)
        if smax - smin < 1e-6 or tmax - tmin < 1e-6:
            continue
        pick = lambda s, t: next(c[2] for c in corners if abs(c[0] - s) < 1e-6 and abs(c[1] - t) < 1e-6)  # noqa: E731
        p = [pick(smin, tmin), pick(smax, tmin), pick(smax, tmax), pick(smin, tmax)]
        if cols and n in cols:
            col = cols[n]
        elif front and n == N:
            col = 3
        r = uv[n] if uv and n in uv else (smin * 16, tmin * 16, smax * 16, tmax * 16)
        out.append(Quad(p, n, col, r))
    return out


def plane(p00, p10, p11, p01, col=1, uv=(0, 0, 16, 16), double=True):
    """A flat quad (pixels), corners clockwise from the top-left as seen from its front; double = both sides."""
    e1 = [p01[i] - p00[i] for i in range(3)]
    e2 = [p11[i] - p00[i] for i in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    ln = math.sqrt(sum(c * c for c in n))
    n = tuple(c / ln for c in n)
    out = [Quad([p00, p10, p11, p01], n, col, uv)]
    if double:
        u0, v0, u1, v1 = uv
        out.append(Quad([p10, p00, p01, p11], tuple(-c for c in n), col, (u1, v0, u0, v1)))
    return out


def rotate(quads, axis, deg, pivot):
    """Rotate quads about an axis ('x' / 'z') through pivot (pixels), anticlockwise looking down the axis."""
    a = math.radians(deg)
    ca, sa = math.cos(a), math.sin(a)

    def r(v, piv):
        x, y, z = (v[i] - piv[i] for i in range(3))
        if axis == "z":
            x, y = x * ca - y * sa, x * sa + y * ca
        else:
            y, z = y * ca - z * sa, y * sa + z * ca
        return (x + piv[0], y + piv[1], z + piv[2])

    for q in quads:
        q.p = [r(c, pivot) for c in q.p]
        q.n = r(q.n, (0, 0, 0))
    return quads


def translate(quads, d):
    for q in quads:
        q.p = [tuple(c[i] + d[i] for i in range(3)) for c in q.p]
    return quads


def on_wall(quads):
    """A floor model (lying on z = 0) turned onto the wall at -Y: (x, y, z) -> (x, z, 16 - y)."""
    return translate(rotate(quads, "x", -90, (0, 0, 0)), (0, 0, 16))


def rot_box(lo, hi, deg):
    """An axis-aligned box turned about the block's vertical centre line by a multiple of 90 degrees."""
    pts = rotate([Quad([lo, hi, lo, hi], (0, 0, 1), 0, None)], "z", deg, (8, 8, 0))[0].p[:2]
    return (tuple(round(min(p[i] for p in pts), 4) for i in range(3)),
            tuple(round(max(p[i] for p in pts), 4) for i in range(3)))


def emit(quads, geo, mode, size=1.0):
    """Quads -> Geo. mode 'world': centred on the block (metres, GTA space); 'hand': Minecraft item space (y up,
    z towards the viewer = the block's +Y), scaled to `size`."""
    e = 0.5 / 128
    for q in quads:
        u0, v0, u1, v1 = q.uv[:4]
        rot = q.uv[4] if len(q.uv) > 4 else 0

        def U(u):
            return q.col * 0.25 + min(max(u / 16, e), 1 - e) * 0.25

        def V(v):
            return min(max(v / 16, e), 1 - e)

        if mode == "world":
            pts = [tuple(c / 16 - 0.5 for c in p) for p in q.p]
            n = q.n
        else:
            pts = [(-(p[0] / 16 - 0.5) * size, (p[2] / 16 - 0.5) * size, (p[1] / 16 - 0.5) * size) for p in q.p]
            n = (-q.n[0], q.n[2], q.n[1])
        geo.quad(pts[0], pts[1], pts[2], pts[3], n, (U(u0), V(v0)), (U(u1), V(v1)))
        if rot:  # the corners take the next corner's uv, `rot` times
            corners = [geo.v[-4 + k][6:8] for k in range(4)]
            for k in range(4):
                geo.v[-4 + k] = geo.v[-4 + k][:6] + tuple(corners[(k + rot) % 4])


# ---- the shapes: each returns {suffix: (quads, collision boxes)}, plus the hand model's quads (or None = sprite) ----
def slab(front):
    variants = {"_bottom": [((0, 0, 0), (16, 16, 8))], "_top": [((0, 0, 8), (16, 16, 16))],
                "_double": [((0, 0, 0), (16, 16, 16))]}
    out = {k: ([q for lo, hi in v for q in box(lo, hi, front=front)], v) for k, v in variants.items()}
    return out, out["_bottom"][0]


def stairs(front):
    tall = {"_s": [((0, 0, 8), (16, 8, 16))], "_ol": [((8, 0, 8), (16, 8, 16))], "_or": [((0, 0, 8), (8, 8, 16))],
            "_il": [((0, 0, 8), (16, 8, 16)), ((8, 8, 8), (16, 16, 16))],
            "_ir": [((0, 0, 8), (16, 8, 16)), ((0, 8, 8), (8, 16, 16))]}
    out = {}
    for k, parts in tall.items():
        for top in (False, True):
            boxes = [((0, 0, 0), (16, 16, 8))] + parts
            if top:  # upside down: mirror the heights
                boxes = [((lo[0], lo[1], 16 - hi[2]), (hi[0], hi[1], 16 - lo[2])) for lo, hi in boxes]
            out[k + ("_t" if top else "")] = ([q for lo, hi in boxes for q in box(lo, hi, front=front)], boxes)
    return out, out["_s"][0]


def connected(post, arm, arm_coll, post_coll, extra=None):
    """Fence / wall / pane variants from a post and a +Y arm."""
    out = {}
    for suffix, mask in CANON.items():
        quads, coll = list(post()), list(post_coll)
        for bit, deg in ARMS.items():
            if mask & bit:
                quads += rotate(arm(), "z", deg, (8, 8, 0))
                coll += [rot_box(lo, hi, deg) for lo, hi in arm_coll]
        out[suffix] = (quads, coll)
    if extra:
        out.update(extra)
    return out


def fence():
    post = lambda: box((6, 6, 0), (10, 10, 16))  # noqa: E731
    arm = lambda: box((7, 10, 12), (9, 16, 15), skip=(S,)) + box((7, 10, 6), (9, 16, 9), skip=(S,))  # noqa: E731
    out = connected(post, arm, [((7, 10, 0), (9, 16, 24))], [((6, 6, 0), (10, 10, 24))])
    hand = (box((6, 0, 0), (10, 4, 16)) + box((6, 12, 0), (10, 16, 16)) +
            box((7, -2, 13), (9, 18, 15)) + box((7, -2, 5), (9, 18, 7)))
    return out, hand


def wall():
    post = lambda: box((4, 4, 0), (12, 12, 16))  # noqa: E731
    arm = lambda: box((5, 12, 0), (11, 16, 14), skip=(S,))  # noqa: E731
    straight = box((5, 0, 0), (11, 16, 14))
    out = connected(post, arm, [((5, 12, 0), (11, 16, 24))], [((4, 4, 0), (12, 12, 24))],
                    {"_ns_np": (straight, [((5, 0, 0), (11, 16, 24))])})
    hand = box((4, 4, 0), (12, 12, 16)) + box((5, 0, 0), (11, 16, 13))
    return out, hand


def pane():
    edge = {UP: 0, DOWN: 0, N: 0, S: 0}
    post = lambda: box((7, 7, 0), (9, 9, 16), cols={UP: 0, DOWN: 0})  # noqa: E731
    arm = lambda: box((7, 9, 0), (9, 16, 16), cols=edge, skip=(S,))  # noqa: E731
    out = connected(post, arm, [((7, 9, 0), (9, 16, 16))], [((7, 7, 0), (9, 9, 16))])
    return out, None


def gate():
    closed = (box((0, 7, 5), (2, 9, 16)) + box((14, 7, 5), (16, 9, 16)) + box((6, 7, 6), (8, 9, 15)) +
              box((8, 7, 6), (10, 9, 15)) + box((2, 7, 6), (6, 9, 9)) + box((2, 7, 12), (6, 9, 15)) +
              box((10, 7, 6), (14, 9, 9)) + box((10, 7, 12), (14, 9, 15)))
    opened = (box((0, 7, 5), (2, 9, 16)) + box((14, 7, 5), (16, 9, 16)) + box((0, 1, 6), (2, 3, 15)) +
              box((14, 1, 6), (16, 3, 15)) + box((0, 3, 6), (2, 7, 9)) + box((0, 3, 12), (2, 7, 15)) +
              box((14, 3, 6), (16, 7, 9)) + box((14, 3, 12), (16, 7, 15)))
    return {"_closed": (closed, [((0, 6, 0), (16, 10, 24))]), "_open": (opened, [])}, closed


def carpet():
    q = box((0, 0, 0), (16, 16, 1))
    return {"": (q, [((0, 0, 0), (16, 16, 1))])}, q


def torch():
    uv = {UP: (7, 6, 9, 8), DOWN: (7, 13, 9, 15), N: (7, 6, 9, 16), S: (7, 6, 9, 16), E: (7, 6, 9, 16),
          W: (7, 6, 9, 16)}
    floor = box((7, 7, 0), (9, 9, 10), uv=uv)
    # on a wall (the wall at -Y): leaning out by 22.5 degrees, like Minecraft's wall torch
    walled = rotate(box((7, -1, 3.5), (9, 1, 13.5), uv=uv), "x", -22.5, (8, 0, 3.5))
    return {"": (floor, []), "_wall": (walled, [])}, None


def lantern():
    def parts(z0, chain_top):
        q = box((5, 5, z0), (11, 11, z0 + 7), uv={UP: (0, 9, 6, 15), DOWN: (0, 9, 6, 15), N: (0, 2, 6, 9),
                                                    S: (0, 2, 6, 9), E: (0, 2, 6, 9), W: (0, 2, 6, 9)})
        q += box((6, 6, z0 + 7), (10, 10, z0 + 9), uv={UP: (1, 10, 5, 14), DOWN: (1, 10, 5, 14), N: (1, 0, 5, 2),
                                                        S: (1, 0, 5, 2), E: (1, 0, 5, 2), W: (1, 0, 5, 2)})
        zc = z0 + 9
        h = chain_top - zc
        q += rotate(plane((6.5, 8, chain_top), (9.5, 8, chain_top), (9.5, 8, zc), (6.5, 8, zc),
                          uv=(11, 1, 14, 1 + h)), "z", 45, (8, 8, 0))
        q += rotate(plane((8, 6.5, chain_top), (8, 9.5, chain_top), (8, 9.5, zc), (8, 6.5, zc),
                          uv=(11, 10, 14, 10 + min(h, 6))), "z", 45, (8, 8, 0))
        return q
    return {"": (parts(0, 11), [((5, 5, 0), (11, 11, 9))]),
            "_hanging": (parts(1, 16), [((5, 5, 1), (11, 11, 10))])}, None


def cross():
    q = plane((0, 0, 16), (16, 16, 16), (16, 16, 0), (0, 0, 0)) + plane((16, 0, 16), (0, 16, 16), (0, 16, 0), (16, 0, 0))
    return {"": (q, [])}, None


def ladder():
    q = plane((16, 0.8, 16), (0, 0.8, 16), (0, 0.8, 0), (16, 0.8, 0))
    return {"": (q, [((0, 0, 0), (16, 2, 16))])}, None


def door():
    out = {}
    for half, col in (("_lower", 1), ("_upper", 0)):
        cols = {n: col for n in FRAMES}
        for suffix, lo, hi in (("", (0, 13, 0), (16, 16, 16)), ("_open", (13, 0, 0), (16, 16, 16)),
                               ("_open_r", (0, 0, 0), (3, 16, 16))):
            out[half + suffix] = (box(lo, hi, cols=cols), [(lo, hi)])
    return out, None


def trapdoor():
    out = {}
    for suffix, lo, hi in (("_bottom", (0, 0, 0), (16, 16, 3)), ("_top", (0, 0, 13), (16, 16, 16)),
                           ("_open", (0, 0, 0), (16, 3, 16))):
        out[suffix] = (box(lo, hi), [(lo, hi)])
    return out, out["_bottom"][0]


def button():
    out = {}
    for suffix, d in (("", 2), ("_on", 1)):
        out["_floor" + suffix] = (box((5, 6, 0), (11, 10, d)), [])
        out["_wall" + suffix] = (on_wall(box((5, 6, 0), (11, 10, d))), [])
    return out, box((5, 6, 6), (11, 10, 10))


def lever():
    def model(deg):
        base = box((5, 4, 0), (11, 12, 3), cols={n: 1 for n in FRAMES})
        uv = {UP: (7, 6, 9, 8), DOWN: (7, 6, 9, 8), N: (7, 6, 9, 16), S: (7, 6, 9, 16), E: (7, 6, 9, 16), W: (7, 6, 9, 16)}
        handle = rotate(box((7, 7, 1), (9, 9, 11), uv=uv, cols={n: 0 for n in FRAMES}), "x", deg, (8, 8, 1))
        return base + handle
    out = {}
    for suffix, deg in (("", 45), ("_on", -45)):
        out["_floor" + suffix] = (model(deg), [])
        out["_wall" + suffix] = (on_wall(model(deg)), [])
    return out, None


def plate():
    up = box((1, 1, 0), (15, 15, 1))
    return {"": (up, []), "_down": (box((1, 1, 0), (15, 15, 0.5)), [])}, box((1, 1, 0), (15, 15, 1))


def rail():
    flat = lambda col: plane((0, 16, 1), (16, 16, 1), (16, 0, 1), (0, 0, 1), col=col)  # noqa: E731
    return {"_ns": (flat(0), []), "_corner": (flat(3), [])}, None


def anvil():
    side = {n: 1 for n in FRAMES}
    q = (box((2, 2, 0), (14, 14, 4), cols=side) + box((4, 3, 4), (12, 13, 5), cols=side) +
         box((6, 4, 5), (10, 12, 10), cols=side) + box((3, 0, 10), (13, 16, 16), cols={**side, UP: 0}))
    coll = [((2, 2, 0), (14, 14, 10)), ((3, 0, 10), (13, 16, 16))]
    return {"": (q, coll)}, q


def enchanting():
    q = box((0, 0, 0), (16, 16, 12))
    return {"": (q, [((0, 0, 0), (16, 16, 12))])}, q


def brewing():
    rod = box((7, 7, 0), (9, 9, 14), uv={UP: (7, 2, 9, 4), DOWN: (7, 2, 9, 4), N: (7, 2, 9, 16), S: (7, 2, 9, 16),
                                         E: (7, 2, 9, 16), W: (7, 2, 9, 16)}, cols={n: 0 for n in FRAMES})
    base = box((9, 5, 0), (15, 11, 2)) + box((2, 1, 0), (8, 7, 2)) + box((2, 9, 0), (8, 15, 2))
    for q in base:
        q.col = 1
    arms = plane((0, 8, 16), (16, 8, 16), (16, 8, 0), (0, 8, 0), col=0)
    arms += rotate(plane((0, 8, 16), (16, 8, 16), (16, 8, 0), (0, 8, 0), col=0), "z", 90, (8, 8, 0))
    return {"": (rod + base + arms, [((1, 1, 0), (15, 15, 2)), ((7, 7, 0), (9, 9, 14))])}, None


def cauldron():
    q = []
    inner = {N: 3, S: 3, E: 3, W: 3}
    for lo, hi, facing_in in (((0, 0, 3), (2, 16, 16), E), ((14, 0, 3), (16, 16, 16), W),
                              ((2, 0, 3), (14, 2, 16), N), ((2, 14, 3), (14, 16, 16), S)):
        q += box(lo, hi, cols={facing_in: 3})
    q += box((2, 2, 3), (14, 14, 4), cols={UP: 3})
    for lo, hi in (((0, 0, 0), (4, 2, 3)), ((0, 2, 0), (2, 4, 3)), ((12, 0, 0), (16, 2, 3)), ((14, 2, 0), (16, 4, 3)),
                   ((0, 14, 0), (4, 16, 3)), ((0, 12, 0), (2, 14, 3)), ((12, 14, 0), (16, 16, 3)),
                   ((14, 12, 0), (16, 14, 3))):
        q += box(lo, hi)
    coll = [((0, 0, 0), (16, 2, 16)), ((0, 14, 0), (16, 16, 16)), ((0, 2, 0), (2, 14, 16)), ((14, 2, 0), (16, 14, 16)),
            ((2, 2, 0), (14, 14, 4))]
    return {"": (q, coll)}, None


def campfire():
    """Minecraft's campfire: the log texture holds bark (rows 0-4), the log end (0,4)-(4,8) and coals (rows 8-14)."""
    bark, end = (0, 0, 16, 4), (0, 4, 4, 8)
    along_y = dict(cols={UP: 0, DOWN: 1, N: 1, S: 1, E: 1, W: 1},
                   uv={E: bark, W: bark, N: end, S: end, UP: (0, 0, 16, 4, 1), DOWN: (0, 0, 16, 4, 1)})
    along_x = dict(cols={UP: 0, DOWN: 1, N: 1, S: 1, E: 1, W: 1},
                   uv={N: bark, S: bark, E: end, W: end, UP: bark, DOWN: bark})
    q = (box((1, 0, 0), (5, 16, 4), **along_y) + box((11, 0, 0), (15, 16, 4), **along_y) +
         box((0, 1, 3), (16, 5, 7), **along_x) + box((0, 11, 3), (16, 15, 7), **along_x) +
         box((5, 0, 0), (11, 16, 1), cols={n: 1 for n in FRAMES}, uv={UP: (0, 8, 16, 14, 1)}, skip=(N, S, E, W, DOWN)))
    fire = plane((0, 0, 17), (16, 16, 17), (16, 16, 1), (0, 0, 1), col=3)
    fire += plane((16, 0, 17), (0, 16, 17), (0, 16, 1), (16, 0, 1), col=3)
    return {"": (q + fire, [((0, 0, 0), (16, 16, 7))])}, None


def pot():
    q = []
    for lo, hi in (((5, 5, 0), (6, 11, 6)), ((10, 5, 0), (11, 11, 6)), ((6, 5, 0), (10, 6, 6)), ((6, 10, 0), (10, 11, 6))):
        q += box(lo, hi, cols={n: 1 for n in FRAMES})
    q += box((6, 6, 0), (10, 10, 4), cols={UP: 0, DOWN: 1, N: 1, S: 1, E: 1, W: 1})
    return {"": (q, [((5, 5, 0), (11, 11, 6))])}, None


def fluid():
    """Water / lava: a surface at the flow level's height (Minecraft: 8/9 of a block for a source, less as it runs
    out; full while falling), sides only where the next cell isn't the same fluid: _<level 0-7 | f>_<side mask>."""
    out = {}
    for lvl in [str(k) for k in range(8)] + ["f"]:
        h = 16 if lvl == "f" else 14.2 * (8 - int(lvl)) / 8
        for mask in range(16):
            skip = [DOWN] + [n for bit, n in ((1, N), (2, E), (4, S), (8, W)) if not mask & bit]
            out[f"_{lvl}_{mask}"] = (box((0, 0, 0), (16, 16, h), skip=tuple(skip)), [])
    return out, None


SHAPES = {"slab": slab, "stairs": stairs, "wall": wall, "fence": fence, "gate": gate, "pane": pane,
          "carpet": carpet, "torch": torch, "lantern": lantern, "cross": cross, "ladder": ladder, "door": door,
          "trapdoor": trapdoor, "button": button, "lever": lever, "plate": plate, "rail": rail, "anvil": anvil,
          "enchanting": enchanting, "brewing": brewing, "cauldron": cauldron, "campfire": campfire, "pot": pot, "fluid": fluid}


def build(shape, front=False):
    fn = SHAPES[shape]
    return fn(front) if shape in ("slab", "stairs") else fn()


def held_sprite(shape, name, top, side):
    """The 2D sprite a shaped block is held / shown as (None = a 3D model). 'b_<texture>' = block/<texture>.png."""
    if shape == "pane":
        return "b_" + side
    if shape in ("torch", "cross", "ladder"):
        return "b_" + top
    if shape in ("lantern", "door", "sign", "brewing", "cauldron", "campfire", "pot"):
        return name
    if shape in ("lever", "rail"):
        return "b_" + top
    return None


def icon_tris(quads):
    """Inventory icon geometry (block pixels, sheet uvs 0..1) as triangles: (normal, (x, y, z, u, v) * 3)."""
    e = 0.5 / 128
    out = []
    for q in quads:
        u0, v0, u1, v1 = q.uv[:4]
        rot = q.uv[4] if len(q.uv) > 4 else 0
        U = lambda u: q.col * 0.25 + min(max(u / 16, e), 1 - e) * 0.25  # noqa: E731
        V = lambda v: min(max(v / 16, e), 1 - e)  # noqa: E731
        uvs = [(U(u0), V(v0)), (U(u1), V(v0)), (U(u1), V(v1)), (U(u0), V(v1))]
        uvs = uvs[rot:] + uvs[:rot]
        c = [(*q.p[k], *uvs[k]) for k in range(4)]
        out += [(q.n, c[0], c[1], c[2]), (q.n, c[0], c[2], c[3])]
    return out


def coll_spec(boxes):
    """models.txt collision: boxes 'hx,hy,hz,cx,cy,cz' (metres, relative to the block centre) joined by '|'."""
    if not boxes:
        return "-"
    parts = []
    for lo, hi in boxes:
        h = [(hi[i] - lo[i]) / 32 for i in range(3)]
        c = [(lo[i] + hi[i]) / 32 - 0.5 for i in range(3)]
        parts.append(",".join(f"{v:.5g}" for v in h + c))
    return "|".join(parts)
