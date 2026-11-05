"""TLDCoop_DLC: the slot machine's model, embedded: assets/automat/automat_model.glb (Karol's, made with ChatGPT) and
a reel strip of our 12 symbols cut from assets/automat/automat_przod_do_modelu.png -> src/slot_machine_model.inc.

Unity's axes: x mirrored (glTF is right-handed), triangles' winding reversed, v = 1 - v. Vertex colours become a
small palette texture (each triangle flat on one texel), so any diffuse shader shows them.

    python3 tools/automat/gen_model.py
"""
import io
import json
import pathlib
import struct

import numpy as np
from PIL import Image, ImageFilter

root = pathlib.Path(__file__).resolve().parents[2]
GLB = root / "assets/automat/automat_model.glb"
FRONT = root / "assets/automat/automat_przod_do_modelu.png"
OUT = root / "src/slot_machine_model.inc"

# the strip, top to bottom (glTF v 0 -> 1); 0 skull, 1 wolf, 2 meat, 3 match, 4 fire, 5 gold
SYMBOLS = ["skull", "wolf", "meat", "match", "fire", "gold"]
STRIP = [0, 1, 2, 3, 0, 4, 5, 1, 0, 2, 4, 3]
# where each symbol is in the front picture (x0, y0, x1, y1)
CROPS = {"meat": (380, 360, 510, 450), "gold": (380, 500, 500, 590), "skull": (560, 355, 660, 445),
         "wolf": (545, 470, 670, 605), "match": (725, 365, 825, 450), "fire": (715, 470, 835, 600)}
CELL, TEX_W = 128, 128

b = GLB.read_bytes()
off, ch = 12, []
while off < len(b):
    ln, _ = struct.unpack("<I4s", b[off:off + 8])
    ch.append(b[off + 8:off + 8 + ln])
    off += 8 + ln
j, bin_ = json.loads(ch[0]), ch[1]
CT = {5126: ("f", 4), 5123: ("H", 2), 5125: ("I", 4), 5121: ("B", 1)}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def acc(i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    f, s = CT[a["componentType"]]
    n = NC[a["type"]]
    st = bv.get("byteStride", s * n)
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    arr = np.ndarray((a["count"], n), dtype=np.dtype("<" + f), buffer=bin_, offset=start, strides=(st, s)).astype(np.float64)
    if a["componentType"] == 5121: arr /= 255
    if a["componentType"] == 5123 and "COLOR" in json.dumps(a): arr /= 65535
    return arr


def image(i):
    bv = j["bufferViews"][j["images"][i]["bufferView"]]
    o = bv.get("byteOffset", 0)
    return Image.open(io.BytesIO(bin_[o:o + bv["byteLength"]])).convert("RGB")


# --- the reel strip ---
front = Image.open(FRONT).convert("RGB")
paper = np.array([228, 216, 190], np.float32)
rng = np.random.default_rng(5)
strip = np.zeros((CELL * len(STRIP), TEX_W, 3), np.float32)
noise = np.asarray(Image.fromarray((rng.random((CELL * len(STRIP) // 8, TEX_W // 8)) * 255).astype(np.uint8))
                   .resize((TEX_W, CELL * len(STRIP)), Image.BICUBIC)).astype(np.float32) / 255
strip[:] = paper * (0.9 + 0.12 * noise[..., None])
for k, s in enumerate(STRIP):
    crop = front.crop(CROPS[SYMBOLS[s]])
    w, h = crop.size
    sc = 0.82 * CELL / max(w, h)
    crop = crop.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.LANCZOS)
    c = np.asarray(crop).astype(np.float32)
    bg = np.median(np.concatenate([c[0], c[-1], c[:, 0], c[:, -1]]), 0)
    c = np.clip(c * (paper / np.maximum(bg, 1)), 0, 255)  # its paper the strip's colour
    hh, ww = c.shape[:2]
    ry = np.minimum(np.arange(hh), np.arange(hh)[::-1]) / 10
    rx = np.minimum(np.arange(ww), np.arange(ww)[::-1]) / 10
    m = np.clip(np.minimum(ry[:, None], rx[None, :]), 0, 1)  # soft edges
    y0 = k * CELL + (CELL - c.shape[0]) // 2
    x0 = (TEX_W - c.shape[1]) // 2
    reg = strip[y0:y0 + c.shape[0], x0:x0 + c.shape[1]]
    reg[:] = reg * (1 - m[..., None]) + c * m[..., None]
    strip[k * CELL:k * CELL + 2] *= 0.55  # a thin line between cells
strip = np.clip(strip, 0, 255).astype(np.uint8)
Image.fromarray(strip).save(root / "assets/automat/reel_strip.png")

# --- parts ---
palette, pal_index = [], {}
def pal(c):
    key = tuple(int(round(x * 31)) for x in c[:3])
    if key not in pal_index:
        pal_index[key] = len(palette)
        palette.append([x / 31 for x in key])
    return pal_index[key]

TEX_PALETTE, TEX_WOLF, TEX_REEL = 0, 1, 2
parts = []
for n in j["nodes"]:
    if "mesh" not in n:
        continue
    M = np.array(n.get("matrix", np.eye(4).flatten().tolist())).reshape(4, 4).T
    t = M[:3, 3]
    for p in j["meshes"][n["mesh"]]["primitives"]:
        P = acc(p["attributes"]["POSITION"])
        idx = acc(p["indices"]).astype(int).reshape(-1, 3)
        mat = j["materials"][p["material"]] if "material" in p else None
        if "TEXCOORD_0" in p["attributes"] and mat and "baseColorTexture" in mat["pbrMetallicRoughness"]:
            UV = acc(p["attributes"]["TEXCOORD_0"])
            tex = TEX_REEL if n["name"].startswith("Reel_") else TEX_WOLF
            V, U = P[idx].reshape(-1, 3), UV[idx].reshape(-1, 2)
        else:
            if "COLOR_0" in p["attributes"]:
                C = acc(p["attributes"]["COLOR_0"])[:, :3]
                fc = C[idx].mean(1)
            else:
                e = mat.get("emissiveFactor", [1, 1, 1]) if mat else [0.8, 0.8, 0.8]
                fc = np.tile(np.array(e) * 0.5 + 0.5, (len(idx), 1))
            tex = TEX_PALETTE
            V = P[idx].reshape(-1, 3)
            U = np.repeat(np.array([pal(c) for c in fc]), 3)
            U = np.stack([U.astype(float), np.zeros_like(U, float)], 1)  # palette slot (fixed below)
        parts.append(dict(name=n["name"], t=t, V=V, U=U, tex=tex))

PAL_W = 16
pal_img = np.zeros((PAL_W * 4, PAL_W * 4, 3), np.uint8)
assert len(palette) <= PAL_W * PAL_W, len(palette)
for i, c in enumerate(palette):
    x, y = i % PAL_W, i // PAL_W
    pal_img[y * 4:y * 4 + 4, x * 4:x * 4 + 4] = (np.array(c) * 255).astype(np.uint8)
for pt in parts:
    if pt["tex"] == TEX_PALETTE:
        slot = pt["U"][:, 0].astype(int)
        u = (slot % PAL_W * 4 + 2) / (PAL_W * 4)
        v = (slot // PAL_W * 4 + 2) / (PAL_W * 4)
        pt["U"] = np.stack([u, v], 1)

wolf = image(0).resize((512, 216), Image.LANCZOS)

# to Unity's axes
pos, uv, ind, rows = [], [], [], []
for pt in parts:
    V = pt["V"].copy()
    V[:, 0] *= -1
    U = pt["U"].copy()
    U[:, 1] = 1 - U[:, 1]
    n = len(V)
    tris = np.arange(n).reshape(-1, 3)[:, ::-1].flatten()
    rows.append((pt["name"], -pt["t"][0], pt["t"][1], pt["t"][2], pt["tex"], len(pos) // 3, n, len(ind), len(tris)))
    pos += V.flatten().tolist()
    uv += U.flatten().tolist()
    ind += (tris).tolist()


def fl(x):
    t = f"{x:.5g}"
    return (t if ("." in t or "e" in t) else t + ".0") + "f"


def floats(name, a):
    return f"const float {name}[{len(a)}] = {{\n" + ",".join(fl(x) for x in a) + "\n};"


def bytes_(name, data):
    lines = [f"alignas(16) const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 48):
        lines.append("".join(f"{x}," for x in data[i:i + 48]))
    lines.append("};")
    return "\n".join(lines)


out = ["// Generated by tools/automat/gen_model.py from assets/automat (Karol's model and pictures); do not edit.",
       "struct AutPart { const char* name; float x, y, z; int tex; int v0, vn, i0, in; };",
       "const AutPart kAutParts[] = {"]
for r in rows:
    out.append(f'    {{"{r[0]}", {fl(r[1])}, {fl(r[2])}, {fl(r[3])}, {r[4]}, {r[5]}, {r[6]}, {r[7]}, {r[8]}}},')
out.append("};")
out.append(floats("kAutPos", pos))
out.append(floats("kAutUV", uv))
out.append(f"const int kAutIdx[{len(ind)}] = {{\n" + ",".join(str(x) for x in ind) + "\n};")
out.append(f"constexpr int kAutPalW = {pal_img.shape[1]}, kAutPalH = {pal_img.shape[0]};")
out.append(bytes_("kAutPal", np.asarray(Image.fromarray(pal_img)).tobytes()))
out.append(f"constexpr int kAutWolfW = {wolf.width}, kAutWolfH = {wolf.height};")
out.append(bytes_("kAutWolf", np.asarray(wolf).tobytes()))
out.append(f"constexpr int kAutReelW = {TEX_W}, kAutReelH = {strip.shape[0]}, kAutReelCells = {len(STRIP)};")
out.append(bytes_("kAutReel", strip.tobytes()))
out.append("// the strip's cells, top to bottom: 0 skull, 1 wolf, 2 meat, 3 match, 4 fire, 5 gold")
out.append("const int kAutStrip[" + str(len(STRIP)) + "] = {" + ",".join(map(str, STRIP)) + "};")
OUT.write_text("\n".join(out) + "\n")
print(len(rows), "parts,", len(pos) // 3, "vertices,", len(ind) // 3, "triangles,", len(palette), "colours,",
      OUT.stat().st_size // 1024, "KB")
