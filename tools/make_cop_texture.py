# Repaints the character's base-colour texture into a police uniform.
# Uses the mesh's UV layout + skin weights to know which texture areas are head / shoes / body.
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from pygltflib import GLTF2

HERE = os.path.dirname(os.path.abspath(__file__))
GLB  = os.path.join(HERE, "..", "assets", "models", "player.glb")
SRC  = os.path.join(HERE, "Boy_4_basecolor.JPEG")
OUT  = os.path.join(HERE, "..", "assets", "textures", "cop_uniform.jpg")

g = GLTF2().load(GLB); blob = g.binary_blob()
def acc(i):
    a = g.accessors[i]; bv = g.bufferViews[a.bufferView]
    n = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4}[a.type]
    fmt = {5121:"B",5123:"H",5125:"I",5126:"f"}[a.componentType]
    off = (bv.byteOffset or 0) + (a.byteOffset or 0)
    return np.array(struct.unpack_from(f"<{a.count*n}{fmt}", blob, off)).reshape(a.count, n)
prim = g.meshes[0].primitives[0]
uv = acc(prim.attributes.TEXCOORD_0); jn = acc(prim.attributes.JOINTS_0); wt = acc(prim.attributes.WEIGHTS_0); idx = acc(prim.indices).reshape(-1, 3)
names = [g.nodes[j].name for j in g.skins[0].joints]
best = jn[np.arange(len(jn)), wt.argmax(axis=1)]
vgroup = np.array(["head" if "Head" in names[j] or "Eye" in names[j] or "Jaw" in names[j] else
                   "shoe" if ("Foot" in names[j] or "Toe" in names[j]) else "other" for j in best])

img = Image.open(SRC).convert("RGB"); W, H = img.size
def raster(group):
    m = Image.new("L", (W, H), 0); d = ImageDraw.Draw(m)
    for tri in idx:
        if (vgroup[tri] == group).sum() >= 2:
            d.polygon([(uv[v][0] * W, uv[v][1] * H) for v in tri], fill=255)
    return np.array(m.filter(ImageFilter.MaxFilter(5))) > 0
head = raster("head"); shoe = raster("shoe") & ~head
print("head px", head.sum(), "shoe px", shoe.sum())

rgb = np.array(img).astype(np.float32) / 255.0
mx = rgb.max(2); mn = rgb.min(2); sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
r, gg, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
blueish = (b > r + 0.04) & (b >= gg - 0.02) & (sat > 0.18)          # jeans
dark    = (mx < 0.38) & (sat < 0.35)                                 # black hoodie
light   = (mx > 0.55) & (sat < 0.22)                                 # white tee / drawstrings
out = rgb.copy()
navy_leg = np.array([0.10, 0.14, 0.26]); navy_top = np.array([0.12, 0.17, 0.33]); shirt = np.array([0.70, 0.81, 0.93])
out[blueish] = navy_leg * (0.55 + 1.1 * mx[blueish, None])
out[dark]    = navy_top * (0.65 + 1.5 * mx[dark, None])
out[light]   = shirt * (0.55 + 0.45 * mx[light, None])
out[shoe]    = (0.04 + 0.20 * mx[shoe, None]) * np.array([1.0, 1.0, 1.05])      # black boots
out[head]    = rgb[head]                                                          # face / hair untouched
Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(OUT, quality=92)
print("saved", OUT)
