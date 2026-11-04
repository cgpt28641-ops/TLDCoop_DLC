"""TLDCoop_DLC: realistic textures for Dynamic Trees (tldcoop_trees.bundle).

Bark (the whole atlas but the branch cards): TLD's blue-grey recoloured to bark colours from its own light and shade
(pine: grey-brown scaly plates, dark fissures; cedar: red-brown fibrous strips), its plates' relief brought out, the
snow on it kept. Branch cards: each snow state's card made from the clear one, snow in clumps on top of the needles
with green between (not the original's flat white).

    python3 realistic.py orig.bundle out.bundle
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tone_snow import read_bundle, write_bundle, dxt5_decode, dxt5_encode, mip_sizes  # noqa: E402
from sf import SF  # noqa: E402

STATES = ["Clear", "Lowest", "Low", "LowMedium", "Medium", "HighMedium", "High", "Highest"]
COVER = {"Clear": 0.0, "Lowest": 0.08, "Low": 0.15, "LowMedium": 0.22, "Medium": 0.3, "HighMedium": 0.38,
         "High": 0.46, "Highest": 0.55}
CARDS = {"Pine": (1792, 2048, 0, 1024), "Cedar": (768, 1024, 0, 1024)}  # x0, x1, y0, y1
# bark colour ramps: dark (fissures), mid, light (plates' tops)
RAMP = {
    "Pine": np.array([[36, 31, 28], [92, 79, 68], [148, 134, 120], [184, 172, 160]], np.float32),
    "Cedar": np.array([[40, 24, 19], [108, 66, 48], [156, 112, 86], [186, 160, 140]], np.float32),
}


def blur(x, r):
    im = Image.fromarray(np.clip(x * 255, 0, 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r))).astype(np.float32) / 255


def noise(h, w, scale, seed):
    r = np.random.default_rng(seed)
    small = r.random((max(2, h // scale), max(2, w // scale))).astype(np.float32)
    im = Image.fromarray((small * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(im.filter(ImageFilter.GaussianBlur(scale / 4))).astype(np.float32) / 255


def ramp(t, cols):
    t = np.clip(t, 0, 1) * (len(cols) - 1)
    i = np.minimum(t.astype(np.int32), len(cols) - 2)
    f = (t - i)[..., None]
    return cols[i] * (1 - f) + cols[i + 1] * f


def bark(img, kind, seed=3):
    """img: HxWx4 float 0..255 (the clear texture). Returns the bark recoloured (cards untouched by the caller)."""
    rgb = img[..., :3] / 255
    h, w = rgb.shape[:2]
    lum = rgb @ np.array([0.3, 0.59, 0.11], np.float32)
    sat = rgb.max(-1) - rgb.min(-1)
    # snow on the bark: bright and colourless
    snow = np.clip((lum - 0.62) / 0.12, 0, 1) * np.clip((0.16 - sat) / 0.08, 0, 1)
    snow = blur(snow, 1.5)
    # relief: the plates against their surroundings, stronger
    base = blur(lum, 6)
    detail = lum - base
    lo, hi = np.percentile(lum[snow < 0.1], [3, 97]) if (snow < 0.1).any() else (0.2, 0.6)
    t = np.clip((base - lo) / (hi - lo + 1e-4), 0, 1) * 0.75 + 0.12 + detail * 2.6
    # some colour variety along the trunk (patches a little redder or greyer), as on real bark
    var = noise(h, w, 96, seed) - 0.5
    col = ramp(t, RAMP[kind])
    grey = col.mean(-1, keepdims=True)
    col = col + (col - grey) * var[..., None] * 0.9
    col *= 1 + 0.12 * (noise(h, w, 24, seed + 1)[..., None] - 0.5)
    # lichen hints on pine (grey-green), sparse
    if kind == "Pine":
        lich = np.clip((noise(h, w, 40, seed + 2) - 0.7) / 0.15, 0, 1) * np.clip(t - 0.35, 0, 1)
        col = col * (1 - 0.35 * lich[..., None]) + np.array([120, 128, 104], np.float32) * 0.35 * lich[..., None]
    out = img.copy()
    o = rgb * 255
    mixed = col * 0.88 + o * 0.12
    out[..., :3] = mixed * (1 - snow[..., None]) + o * snow[..., None]
    return np.clip(out, 0, 255)


def card_snow(clear, cover, seed=1):
    """A branch card (HxWx4 float) with snow in clumps on top of the needles, green between."""
    if cover <= 0:
        return clear.copy()
    h, w = clear.shape[:2]
    rgb, a = clear[..., :3], clear[..., 3]
    inside = a > 128
    luma = rgb @ np.array([0.3, 0.59, 0.11], np.float32)
    lum_n = np.clip((luma - np.percentile(luma[inside], 5)) / (np.ptp(luma[inside]) + 1e-3), 0, 1)
    clumps = 0.6 * noise(h, w, 48, seed) + 0.4 * noise(h, w, 14, seed + 1)
    dense = blur(a / 255, 10)
    # snow rests on the upper side: a needle with needles below it holds more than one at the bottom edge
    up = np.zeros_like(dense)
    up[6:] = dense[:-6]
    field = 0.5 * clumps + 0.25 * lum_n + 0.15 * dense + 0.1 * (dense - up + 0.5)
    thr = np.quantile(field[inside], 1 - cover)
    m = blur(np.clip((field - thr) / 0.06, 0, 1), 1.2)
    snow = np.array([222, 228, 236], np.float32) * (0.72 + 0.28 * lum_n[..., None])
    out = clear.copy()
    out[..., :3] = rgb * (1 - m[..., None]) + snow * m[..., None]
    out[..., :3] = out[..., :3] * (1 - 0.08 * cover) + 230 * 0.08 * cover
    return np.clip(out, 0, 255)


def build(kind, state, clear_img, bark_img):
    x0, x1, y0, y1 = CARDS[kind]
    out = bark_img.copy()
    out[y0:y1, x0:x1] = card_snow(clear_img[y0:y1, x0:x1], COVER[state], seed=11)
    return out


def mips_of(img, n):
    """DXT5 bytes of every mip level (box-filtered, alpha kept as coverage)."""
    h, w = img.shape[:2]
    data = bytearray()
    cur = Image.fromarray(np.clip(np.rint(img), 0, 255).astype(np.uint8), "RGBA")
    for mw, mh, sz in mip_sizes(w, h, n):
        lvl = np.asarray(cur.resize((mw, mh), Image.BOX)).astype(np.float32) if (mw, mh) != (w, h) else \
            np.asarray(cur).astype(np.float32)
        if mw < 4 or mh < 4:
            pw, ph = max(4, mw), max(4, mh)
            pad = np.zeros((ph, pw, 4), np.float32)
            pad[:mh, :mw] = lvl
            pad[mh:, :] = pad[mh - 1:mh, :] if mh < ph else pad[mh:, :]
            pad[:, mw:] = pad[:, mw - 1:mw] if mw < pw else pad[:, mw:]
            lvl = pad
        enc = dxt5_encode(lvl)
        assert len(enc) == sz, (mw, mh, len(enc), sz)
        data += enc
    return bytes(data)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    preview = sys.argv[3] if len(sys.argv) > 3 else None
    head, files, nodes = read_bundle(src)
    cab = next(n for n in files if not n.endswith(".resS"))
    res = bytearray(files[cab + ".resS"])
    s = SF(files[cab])
    tex = {}
    for o in s.objects:
        if o[3] == 28:
            d = s.read(o)
            tex[d["m_Name"]] = d

    def img_of(d):
        sd, w, h = d["m_StreamData"], d["m_Width"], d["m_Height"]
        return dxt5_decode(w, h, bytes(res[sd["offset"]:sd["offset"] + w * h]))

    for kind in ("Pine", "Cedar"):
        clear = img_of(tex["TRN_TreeBark%s_Clear" % kind])
        barked = bark(clear, kind)
        for state in STATES:
            d = tex["TRN_TreeBark%s_%s" % (kind, state)]
            assert d["m_TextureFormat"] == 12
            img = build(kind, state, clear, barked)
            if preview:
                Image.fromarray(np.rint(img).astype(np.uint8), "RGBA").save("%s/%s_%s.png" % (preview, kind, state))
            new = mips_of(img, d["m_MipCount"])
            sd = d["m_StreamData"]
            assert len(new) == sd["size"], (len(new), sd["size"])
            res[sd["offset"]:sd["offset"] + sd["size"]] = new
            print("made", d["m_Name"], flush=True)
    files[cab + ".resS"] = bytes(res)
    write_bundle(dst, head, files, nodes)
    print("written", dst, os.path.getsize(dst))


if __name__ == "__main__":
    main()
