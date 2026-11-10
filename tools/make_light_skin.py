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

from PIL import ImageDraw as _ImageDraw
_pm = Image.new('L', (W * 4, H * 4), 0)
_pd = _ImageDraw.Draw(_pm)
def P(x, y, w, h, m=0, r=0):
    # rounded so the dark corners around rounded buttons don't survive
    _pd.rounded_rectangle([(x - m) * 4, (y - m) * 4, (x + w + m) * 4, (y + h + m) * 4], radius=max(0, r + m) * 4, fill=255)
# header art, cards, coloured buttons, roll
P(12, 6, 1424, 244)
for c in [(467,630,181,109),(661,630,181,109),(852,630,183,109),(1045,630,182,109),(1235,630,183,109),
          (467,752,181,109),(659,752,192,109),(861,752,176,109),(1045,752,182,109),(1237,752,181,109)]:
    P(*c, m=3, r=14)
for bt, rad in [((1140,268,66,30), 7), ((30,322,186,27), 6), ((222,322,219,27), 6), ((451,322,135,27), 6),
                ((30,524,70,38), 7), ((1229,527,182,38), 9), ((32,831,140,31), 7),
                ((36,952,188,75), 13), ((249,946,26,22), 4), ((249,980,26,22), 4), ((249,1014,26,22), 4),
                ((964,914,187,66), 13), ((1167,914,240,66), 13)]:
    P(*bt, m=2, r=rad)
protect = np.asarray(_pm.resize((W, H), Image.LANCZOS)) > 127

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

# ---------------------------------------------------------------------------
# Bottom stage (sky + laser-grid floor + the Detection / Export panel frames):
# per-pixel conversion leaves neon grid lines and chrome frames half-converted,
# so for the light theme this area is redrawn cleanly instead.
# ---------------------------------------------------------------------------
from PIL import ImageDraw, ImageFilter

SS = 4                                   # supersampling for smooth lines
X0, Y0, X1, Y1 = 9, 878, 1439, 1069      # the stage
HORIZON = 958
VX, VY = 721, 851                        # vanishing point of the grid

def ss(v): return int(round(v * SS))

light = Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).convert('RGBA')
orig = src.convert('RGBA')
Wd, Hd = X1 - X0, Y1 - Y0

# sky + floor gradient
stage = np.zeros((Hd, Wd, 3), np.float32)
ys = np.arange(Y0, Y1)[:, None]
xs = np.arange(X0, X1)[None, :]
skyTop, skyBot = np.array([214, 241, 255]), np.array([255, 222, 242])
floorTop, floorBot = np.array([252, 240, 255]), np.array([255, 214, 238])
t = np.clip((ys - Y0) / (HORIZON - Y0), 0, 1)[..., None]
sky = skyTop * (1 - t) + skyBot * t
t2 = np.clip((ys - HORIZON) / (Y1 - HORIZON), 0, 1)[..., None]
floor = floorTop * (1 - t2) + floorBot * t2
stage[:] = np.where((ys < HORIZON)[..., None], sky, floor) * np.ones((1, Wd, 1))
# a gentle left-right blue -> pink drift like the rest of the light theme
drift = np.clip((xs - X0) / Wd, 0, 1)[..., None]
stage = stage * (1 - 0.12 * drift) + np.array([255, 205, 235]) * 0.12 * drift
stageImg = Image.fromarray(np.clip(stage, 0, 255).astype(np.uint8)).convert('RGBA')

# neon grid (drawn big, blurred glow + crisp core, then downsampled)
grid = Image.new('RGBA', (Wd * SS, Hd * SS), (0, 0, 0, 0))
gd = ImageDraw.Draw(grid)
def gline(x0, y0, x1, y1, col, w):
    gd.line([(ss(x0 - X0), ss(y0 - Y0)), (ss(x1 - X0), ss(y1 - Y0))], fill=col, width=ss(w))
lines = []
for k in range(0, 9):
    y = HORIZON + 7 * k + 1.3 * k * k
    if y < Y1: lines.append(('h', y))
for k in range(-16, 17):
    xb = VX + k * 56                     # x where the line meets y = 1060
    lines.append(('v', xb))
def draw_lines(col, w):
    for kind, v in lines:
        if kind == 'h':
            gline(X0, v, X1, v, col, w)
        else:
            # from the horizon down to the bottom, on the line through the vanishing point
            xh = VX + (v - VX) * (HORIZON - VY) / (1060 - VY)
            xe = VX + (v - VX) * (Y1 - VY) / (1060 - VY)
            gline(xh, HORIZON, xe, Y1, col, w)
glow = Image.new('RGBA', grid.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(glow)
draw_lines((255, 80, 200, 150), 4.0)
glow = glow.filter(ImageFilter.GaussianBlur(ss(2.2)))
gd = ImageDraw.Draw(grid)
draw_lines((236, 64, 196, 235), 1.6)
gd.line([(0, ss(HORIZON - Y0)), (ss(Wd), ss(HORIZON - Y0))], fill=(255, 120, 215, 255), width=ss(2.2))
gridImg = Image.alpha_composite(glow, grid).resize((Wd, Hd), Image.LANCZOS)
# fade the grid out towards the horizon so it reads as depth
fade = np.clip((np.arange(Hd)[:, None] + Y0 - HORIZON) / 26.0, 0, 1)
ga = np.asarray(gridImg).astype(np.float32)
ga[..., 3] *= np.maximum(fade, (np.abs(np.arange(Hd)[:, None] + Y0 - HORIZON) < 2) * 1.0)
gridImg = Image.fromarray(ga.astype(np.uint8))
stageImg = Image.alpha_composite(stageImg, gridImg)

# where the stage shows: everything in the stage except the two panels, ROLL, plate and bolts
mask = Image.new('L', (Wd * SS, Hd * SS), 255)
md = ImageDraw.Draw(mask)
PANELS = [(13, 894, 503, 1052, 24), (934, 894, 1434, 1052, 24)]
for (a, b, c, d, r) in PANELS:
    md.rounded_rectangle([ss(a - X0), ss(b - Y0), ss(c - X0), ss(d - Y0)], radius=ss(r), fill=0)
md.rounded_rectangle([ss(571 - X0), ss(891 - Y0), ss(869 - X0), ss(1031 - Y0)], radius=ss(70), fill=0)   # ROLL ring
md.rounded_rectangle([ss(625 - X0), ss(1015 - Y0), ss(815 - X0), ss(1054 - Y0)], radius=ss(19), fill=0)  # INSTANT IDEA
mask = mask.resize((Wd, Hd), Image.LANCZOS)
# bolts: yellow pixels of the original plus their dark outline
o = np.asarray(orig.crop((X0, Y0, X1, Y1)).convert('RGB')).astype(np.float32) / 255
mxo, mno = o.max(-1), o.min(-1)
yellow = (o[..., 0] > 0.75) & (o[..., 1] > 0.6) & (o[..., 2] < 0.45)
boltZone = np.zeros_like(yellow)
for (a, b, c, d) in [(505, 898, 575, 978), (862, 898, 932, 978)]:
    boltZone[b - Y0:d - Y0, a - X0:c - X0] = True
bolt = Image.fromarray(((yellow & boltZone) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.GaussianBlur(0.8))
mask = Image.fromarray(np.minimum(np.asarray(mask), 255 - np.asarray(bolt)).astype(np.uint8))

region = light.crop((X0, Y0, X1, Y1))
# ROLL, its plate and the bolts come straight from the original artwork
keep = Image.fromarray((255 - np.asarray(mask)).astype(np.uint8))
panelsOnly = Image.new('L', (Wd * SS, Hd * SS), 0)
pd = ImageDraw.Draw(panelsOnly)
for (a, b, c, d, r) in PANELS:
    pd.rounded_rectangle([ss(a - X0), ss(b - Y0), ss(c - X0), ss(d - Y0)], radius=ss(r), fill=255)
panelsOnly = panelsOnly.resize((Wd, Hd), Image.LANCZOS)
artKeep = Image.fromarray(np.clip(np.asarray(keep).astype(np.int32) - np.asarray(panelsOnly).astype(np.int32), 0, 255).astype(np.uint8))
region = Image.composite(orig.crop((X0, Y0, X1, Y1)), region, artKeep)
region = Image.composite(stageImg, region, mask)

# clean neon frames for the two panels
frame = Image.new('RGBA', (Wd * SS, Hd * SS), (0, 0, 0, 0))
fglow = Image.new('RGBA', frame.size, (0, 0, 0, 0))
fd, fg = ImageDraw.Draw(frame), ImageDraw.Draw(fglow)
for (a, b, c, d, r) in PANELS:
    box = [ss(a - X0), ss(b - Y0), ss(c - X0), ss(d - Y0)]
    fg.rounded_rectangle(box, radius=ss(r), outline=(255, 90, 200, 170), width=ss(6))
    fd.rounded_rectangle(box, radius=ss(r), outline=(246, 240, 255, 255), width=ss(8))          # covers the old frame
    fd.rounded_rectangle(box, radius=ss(r), outline=(150, 98, 255, 255), width=ss(2.6))         # violet outer line
    inner = [box[0] + ss(5), box[1] + ss(5), box[2] - ss(5), box[3] - ss(5)]
    fd.rounded_rectangle(inner, radius=ss(r - 5), outline=(255, 79, 180, 255), width=ss(2.0))   # hot-pink inner line
    hl = [box[0] + ss(2.6), box[1] + ss(2.6), box[2] - ss(2.6), box[3] - ss(2.6)]
    fd.rounded_rectangle(hl, radius=ss(r - 2.6), outline=(255, 255, 255, 230), width=ss(1.2))   # chrome highlight
fglow = fglow.filter(ImageFilter.GaussianBlur(ss(2.5)))
frames = Image.alpha_composite(fglow, frame).resize((Wd, Hd), Image.LANCZOS)
region = Image.alpha_composite(region.convert('RGBA'), frames)

light.paste(region, (X0, Y0))
light.convert('RGB').save(sys.argv[2])
