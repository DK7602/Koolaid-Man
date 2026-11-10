# Generates resources/skin_light.png from resources/skin.png:
#   python3 tools/make_light_skin.py resources/skin.png resources/skin_light.png
# Purple/blue/grey background areas are lightness-inverted into a white + pastel
# light-blue -> light-pink wash; header art, cards, coloured buttons and ROLL are kept.
import sys
from PIL import Image
import numpy as np
src = Image.open(sys.argv[1]).convert('RGB')
a = np.asarray(src).astype(np.float32) / 255.0
H, W = a.shape[:2]
r, g, b = a[..., 0], a[..., 1], a[..., 2]
mx = a.max(-1); mn = a.min(-1); d = mx - mn
L = (mx + mn) / 2
S = np.where(d < 1e-6, 0, d / (1 - np.abs(2 * L - 1) + 1e-6))
hue = np.where(mx == r, ((g - b) / (d + 1e-6)) % 6, np.where(mx == g, (b - r) / (d + 1e-6) + 2, (r - g) / (d + 1e-6) + 4)) * 60
hue = np.where(d < 1e-6, 0, hue)

protect = np.zeros((H, W), bool)
def P(x, y, w, h, m=0):
    protect[max(0, y - m):y + h + m, max(0, x - m):x + w + m] = True
# header art, cards, coloured buttons, roll
P(12, 6, 1424, 244)
for c in [(467,630,181,109),(661,630,181,109),(852,630,183,109),(1045,630,182,109),(1235,630,183,109),
          (467,752,181,109),(659,752,192,109),(861,752,176,109),(1045,752,182,109),(1237,752,181,109)]:
    P(*c, m=3)
for bt in [(1140,268,66,30),(30,322,556,27),(30,524,70,38),(1229,527,182,38),(32,831,140,31),
           (36,952,188,75),(249,946,26,90),(964,914,187,66),(1167,914,240,66),(582,890,280,128),
           (512,900,64,90),(866,900,64,90)]:
    P(*bt, m=2)

# where to transform: purples / blues / greys, but not vivid neon accents
purpleBlue = (hue >= 200) & (hue <= 318)
grey = (S < 0.22) | (d < 0.16)
neon = (d > 0.38) & (L > 0.36) & (L < 0.82)
sel = (purpleBlue | grey) & ~neon & ~protect

# new lightness: invert (dark panels -> light, white text -> dark)
Ln = 0.975 - 0.83 * L
# pastel wash: light blue (top-left) -> light pink (bottom-right)
yy, xx = np.mgrid[0:H, 0:W]
f = np.clip(0.55 * xx / W + 0.45 * (yy - 250) / (H - 250), 0, 1)
washHue = 200 + f * 128          # 200 = light blue ... 328 = light pink
Hn = np.where(Ln > 0.55, washHue, 262)                       # dark results become navy-purple text
Sn = np.where(Ln > 0.55, np.clip(0.25 + 0.6 * S, 0, 0.75) * np.clip((1.0 - Ln) * 6.0, 0.25, 1.0), 0.55)

def hls2rgb(h, l, s):
    c = (1 - np.abs(2 * l - 1)) * s
    hp = (h % 360) / 60
    x = c * (1 - np.abs(hp % 2 - 1))
    i = hp.astype(int) % 6
    z = np.zeros_like(c)
    rr = np.choose(i, [c, x, z, z, x, c]); gg = np.choose(i, [x, c, c, x, z, z]); bb = np.choose(i, [z, z, x, c, c, x])
    m = l - c / 2
    return np.stack([rr + m, gg + m, bb + m], -1)

out = a.copy()
conv = hls2rgb(Hn, Ln, Sn)
out[sel] = conv[sel]
# neon purple accents outside protected areas: keep hue, make them a touch deeper so they read on white
acc = neon & purpleBlue & ~protect
out[acc] = out[acc] * 0.85
Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(sys.argv[2])
