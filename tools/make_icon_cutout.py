# Cuts the Koolaid Man mascot out of resources/skin.png (transparent background):
#   python3 tools/make_icon_cutout.py resources/skin.png mascot.png preview.png
# then squared/resized into resources/icon.png (1024 px) and resources/icon.ico.
import sys, numpy as np
from PIL import Image, ImageFilter, ImageDraw
src = Image.open(sys.argv[1]).convert('RGB')
X0, Y0, X1, Y1 = 90, 0, 450, 252
im = src.crop((X0, Y0, X1, Y1))
a = np.asarray(im).astype(np.float32) / 255
W, H = im.size
SS = 4
def poly_mask(pts, blur=0.0):
    m = Image.new('L', (W * SS, H * SS), 0)
    ImageDraw.Draw(m).polygon([((x - X0) * SS, (y - Y0) * SS) for x, y in pts], fill=255)
    m = m.resize((W, H), Image.LANCZOS)
    return m.filter(ImageFilter.GaussianBlur(blur)) if blur else m
def ellipse_mask(cx, cy, rx, ry):
    m = Image.new('L', (W * SS, H * SS), 0)
    ImageDraw.Draw(m).ellipse([((cx - rx) - X0) * SS, ((cy - ry) - Y0) * SS, ((cx + rx) - X0) * SS, ((cy + ry) - Y0) * SS], fill=255)
    return m.resize((W, H), Image.LANCZOS)

# Hand-traced silhouette of the character (skin coordinates), following the outside of his black outline.
pitcher = [(214,108),(222,82),(236,62),(250,48),(270,40),(300,38),(330,42),(352,52),(370,66),(380,78),(380,88),
           (370,96),(370,122),(376,132),(372,142),(362,150),(360,166),(354,184),(344,198),(328,210),(306,220),
           (296,226),(282,226),(258,218),(238,206),(222,190),(212,172),(206,150),(208,128)]
fistL  = [(124,90),(134,80),(150,77),(168,78),(184,84),(196,94),(206,104),(212,118),(212,132),(204,146),(190,154),
          (170,157),(150,152),(134,144),(122,134),(116,120),(117,102)]
fistR  = [(371,166),(380,158),(396,157),(410,162),(418,174),(419,190),(412,204),(398,210),(382,208),(372,200),(367,188),(366,176)]
splash = [(141,52),(140,40),(148,30),(158,20),(166,10),(178,6),(190,8),(198,16),(206,8),(218,8),(232,12),
          (250,10),(270,12),(290,16),(300,10),(306,4),(320,2),(330,8),(332,20),(346,16),(362,14),(376,20),
          (382,32),(378,44),(366,52),(350,54),(330,52),(300,48),(276,44),(252,48),(234,58),(216,66),(196,72),
          (176,70),(158,64)]
shoeL  = [(160,212),(170,198),(186,186),(206,180),(226,180),(240,186),(250,196),(250,212),(242,226),(232,240),
          (224,252),(158,252),(156,232)]
shoeR  = [(298,232),(312,226),(326,228),(332,238),(334,252),(298,252)]
dropL  = ellipse_mask(212, 24, 9, 8)
rimSplashR = [(368,94),(382,92),(394,98),(398,108),(390,118),(376,118),(368,110)]
parts = [poly_mask(p) for p in (pitcher, fistL, fistR, splash, shoeL, shoeR, rimSplashR)]
m = np.zeros((H, W), np.float32)
for p in parts:
    m = np.maximum(m, np.asarray(p).astype(np.float32) / 255)

# Snap the hand-traced edge onto the real outline: inside a thin band around the traced edge,
# keep pixels that are part of the character (dark outline or character colours), drop
# background (bricks: orange-brown, sky: cyan/teal).
r, g, b = a[..., 0], a[..., 1], a[..., 2]
mx, mn = a.max(-1), a.min(-1); d = mx - mn
hue = np.where(mx == r, ((g - b) / (d + 1e-6)) % 6, np.where(mx == g, (b - r) / (d + 1e-6) + 2, (r - g) / (d + 1e-6) + 4)) * 60
sat = d / (mx + 1e-6)
yy = np.arange(H)[:, None] + Y0
brick = (hue > 13) & (hue < 42) & (sat > 0.35) & (mx > 0.25)
sky = (hue > 170) & (hue < 215) & (sat > 0.5) & (mx < 0.72)        # dark teal sky (the splash's bright cyan outline stays)
banner = (yy < 7) & (hue > 170) & (hue < 215)                       # the light strip at the very top
background = brick | sky | banner
hard = m > 0.5
# only clean up near the outer edge, so dark parts inside him (sunglasses) are never touched
inner = np.asarray(Image.fromarray((hard * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(15))) > 0
alpha = hard & ~(background & ~inner)
A = Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
A = A.filter(ImageFilter.GaussianBlur(0.6))
out = im.convert('RGBA'); out.putalpha(A)
bbox = out.getbbox(); out = out.crop(bbox)
out.save(sys.argv[2])
w, h = out.size
ck = Image.new('RGB', (w, h)); cd = ImageDraw.Draw(ck)
for y in range(0, h, 8):
    for x in range(0, w, 8):
        cd.rectangle([x, y, x + 7, y + 7], fill=(200, 200, 200) if (x // 8 + y // 8) % 2 else (255, 255, 255))
ck.paste(out, (0, 0), out)
ck.resize((w * 2, h * 2), Image.LANCZOS).save(sys.argv[3])
