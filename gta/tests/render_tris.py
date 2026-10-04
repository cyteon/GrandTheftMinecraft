"""Rasterize hand_test output (world triangles, camera at origin looking +Y, FOV 50) to a PNG."""
import math, sys
from PIL import Image, ImageDraw
W, H, fov = 960, 540, float(sys.argv[3]) if len(sys.argv) > 3 else 50
t = math.tan(math.radians(fov / 2))
img = Image.new("RGB", (W, H), (110, 150, 200))
d = ImageDraw.Draw(img)
for line in open(sys.argv[1]):
    v = line.split()
    if len(v) < 12: continue
    p = [tuple(map(float, v[i:i + 3])) for i in (0, 3, 6)]
    pts = [((x / y) / (t * W / H) * W / 2 + W / 2, -(z / y) / t * H / 2 + H / 2) for x, y, z in p]
    d.polygon(pts, fill=tuple(int(c) for c in v[9:12]))
img.save(sys.argv[2])
