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
        u0, v0, u1, v1 = q.uv

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


SHAPES = {"slab": slab, "stairs": stairs, "wall": wall, "fence": fence, "gate": gate, "pane": pane,
          "carpet": carpet, "torch": torch, "lantern": lantern, "cross": cross, "ladder": ladder, "door": door,
          "trapdoor": trapdoor}


def build(shape, front=False):
    fn = SHAPES[shape]
    return fn(front) if shape in ("slab", "stairs") else fn()


def held_sprite(shape, name, top, side):
    """The 2D sprite a shaped block is held / shown as (None = a 3D model). 'b_<texture>' = block/<texture>.png."""
    if shape == "pane":
        return "b_" + side
    if shape in ("torch", "cross", "ladder"):
        return "b_" + top
    if shape in ("lantern", "door"):
        return name
    return None


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
