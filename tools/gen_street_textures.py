# Generates the extra street textures into assets/textures (run: python3 tools/gen_street_textures.py)
import numpy as np, random, os
from PIL import Image, ImageDraw, ImageFilter
random.seed(11); np.random.seed(11)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "textures")
def save(im, name): im.save(os.path.join(OUT, name + ".png")); print(name, im.size)
def noise_img(w, h, base, sigma):
    a = np.clip(np.array(base, float)[None, None, :] + np.random.normal(0, sigma, (h, w, 1)), 0, 255).astype(np.uint8)
    return Image.fromarray(a, "RGB")
def tile_blur(im, r):
    w, h = im.size; big = Image.new("RGB", (w * 3, h * 3))
    for i in range(3):
        for j in range(3): big.paste(im, (i * w, j * h))
    return big.filter(ImageFilter.GaussianBlur(r)).crop((w, h, 2 * w, 2 * h))

# ---- dirt road (tileable) ----
im = tile_blur(noise_img(256, 256, (128, 98, 66), 12), 0.8); d = ImageDraw.Draw(im)
for _ in range(220):
    x, y = random.randint(0, 255), random.randint(0, 255); r = random.choice([1, 1, 2, 3]); c = random.randint(85, 165)
    d.ellipse((x - r, y - r, x + r, y + r), fill=(c, int(c * .8), int(c * .58)))
for _ in range(40):
    x, y = random.randint(0, 255), random.randint(0, 255); d.line((x, y, x + 1, y - random.randint(3, 6)), fill=(70, 110, 50), width=1)
save(im, "dirt")

# ---- cobblestone / pavers ----
im = Image.new("RGB", (256, 256), (70, 66, 62)); d = ImageDraw.Draw(im)
rows, bw = 8, 64
for r in range(rows):
    off = (r % 2) * (bw // 2); y0 = r * 32
    for c in range(-1, 5):
        x0 = c * bw + off; t = random.randint(150, 198)
        col = (t, int(t * .95), int(t * .88)); d.rounded_rectangle((x0 + 2, y0 + 2, x0 + bw - 2, y0 + 30), radius=5, fill=col)
im = Image.fromarray(np.clip(np.array(im, float) + np.random.normal(0, 6, (256, 256, 1)), 0, 255).astype(np.uint8), "RGB")
save(im.filter(ImageFilter.GaussianBlur(0.5)), "paver")

# ---- concrete sidewalk ----
im = tile_blur(noise_img(256, 256, (196, 194, 188), 6), 0.7); d = ImageDraw.Draw(im)
for k in (0, 128): d.line((k, 0, k, 255), fill=(140, 138, 132), width=3); d.line((0, k, 255, k), fill=(140, 138, 132), width=3)
save(im, "sidewalk")

# ---- plain brick ----
def brick_img():
    im = Image.new("RGB", (256, 256), (178, 170, 160)); d = ImageDraw.Draw(im)
    for r in range(16):
        off = (r % 2) * 20; y0 = r * 16
        for c in range(-1, 8):
            x0 = c * 40 + off; t = random.randint(-22, 22)
            d.rectangle((x0 + 1, y0 + 1, x0 + 38, y0 + 14), fill=(150 + t, 68 + t // 2, 52 + t // 3))
    return im
save(brick_img(), "brick")

# ---- brick with windows (tile = 2x2 windows) ----
im = brick_img(); d = ImageDraw.Draw(im)
for cx in (64, 192):
    for cy in (64, 192):
        d.rectangle((cx - 30, cy - 38, cx + 30, cy + 38), fill=(235, 232, 222))
        for k in range(60):
            t = 70 + k; d.line((cx - 25, cy - 33 + k * 66 // 60, cx + 25, cy - 33 + k * 66 // 60), fill=(t // 2 + 20, t // 2 + 40, t // 2 + 55))
        d.line((cx, cy - 33, cx, cy + 33), fill=(235, 232, 222), width=3); d.line((cx - 25, cy, cx + 25, cy), fill=(235, 232, 222), width=3)
        d.rectangle((cx - 34, cy + 36, cx + 34, cy + 44), fill=(200, 196, 186))
save(im, "brickwin")

# ---- modern glass facade (tile = 4x4 windows) ----
im = Image.new("RGB", (256, 256), (214, 212, 206)); d = ImageDraw.Draw(im)
for i in range(4):
    for j in range(4):
        x0, y0 = i * 64 + 8, j * 64 + 10
        for k in range(44):
            t = 80 + k * 2; d.line((x0, y0 + k, x0 + 48, y0 + k), fill=(t // 2, t // 2 + 25, t // 2 + 55))
        d.line((x0 + 24, y0, x0 + 24, y0 + 44), fill=(220, 220, 215), width=2)
        d.polygon([(x0 + 4, y0 + 40), (x0 + 22, y0 + 2), (x0 + 30, y0 + 2), (x0 + 12, y0 + 40)], fill=(150, 175, 200))
        d.rectangle((x0 - 2, y0 + 44, x0 + 50, y0 + 49), fill=(170, 168, 160))
save(im, "facade")

# ---- shopfront (tile = one 6m x 3m shop) ----
im = Image.new("RGB", (256, 128), (228, 224, 214)); d = ImageDraw.Draw(im)
d.rectangle((0, 0, 255, 24), fill=(235, 235, 235))                                   # sign band (tinted in game)
d.rectangle((14, 34, 170, 112), fill=(40, 60, 80))
for k in range(78):
    t = 90 + k; d.line((16, 36 + k, 168, 36 + k), fill=(t // 2, t // 2 + 35, t // 2 + 65))
d.polygon([(30, 110), (70, 36), (90, 36), (50, 110)], fill=(170, 195, 215))
d.rectangle((14, 112, 170, 120), fill=(120, 116, 108))
d.rectangle((188, 34, 238, 120), fill=(95, 62, 42)); d.rectangle((196, 42, 230, 84), fill=(150, 190, 210)); d.ellipse((222, 92, 230, 100), fill=(220, 190, 80))
save(im, "shopfront")

# ---- jungle trail (mud, leaf litter, roots) ----
im = tile_blur(noise_img(256, 256, (92, 70, 46), 10), 0.9); d = ImageDraw.Draw(im)
for _ in range(14):                                                   # damp dark patches
    x, y = random.randint(0, 255), random.randint(0, 255); r = random.randint(14, 34)
    d.ellipse((x - r, y - r // 2, x + r, y + r // 2), fill=(66, 50, 34))
for _ in range(7):                                                    # roots
    x, y = random.randint(0, 255), random.randint(0, 255); pts = [(x, y)]
    for k in range(5): x += random.randint(-22, 22); y += random.randint(10, 26); pts.append((x % 256, y % 256))
    for a, b in zip(pts, pts[1:]):
        if abs(a[0] - b[0]) < 60 and abs(a[1] - b[1]) < 60: d.line((a, b), fill=(58, 42, 30), width=random.randint(3, 6))
for _ in range(180):                                                  # leaf litter
    x, y = random.randint(0, 255), random.randint(0, 255); w = random.randint(4, 9)
    col = random.choice([(70, 100, 40), (110, 120, 50), (130, 90, 40), (60, 80, 36)])
    d.ellipse((x, y, x + w, y + w // 2 + 1), fill=col)
save(im, "jungle_path")

# ---- jungle floor (mossy, leafy) ----
im = tile_blur(noise_img(256, 256, (32, 72, 38), 9), 0.8); d = ImageDraw.Draw(im)
for _ in range(420):
    x, y = random.randint(0, 255), random.randint(0, 255); w = random.randint(5, 14)
    col = random.choice([(24, 58, 30), (46, 96, 44), (60, 110, 50), (38, 80, 40), (86, 120, 46)])
    d.ellipse((x, y, x + w, y + w // 2 + 2), fill=col)
for _ in range(70):
    x, y = random.randint(0, 255), random.randint(0, 255); d.line((x, y, x + random.randint(-8, 8), y - random.randint(5, 12)), fill=(110, 150, 60), width=1)
save(im, "jungle_floor")
