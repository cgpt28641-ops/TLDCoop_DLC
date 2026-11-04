#!/usr/bin/env python3
"""TLDCoop_DLC: tone down the snow on Dynamic Trees' textures (Karol: "wyglądają na takie bardzo białe").

Every snowy pine/cedar set is blended back towards its Clear set: new = clear + K * (snowy - clear), mip by mip (the
original mips decoded, blended and encoded again as DXT5), so the needles keep their green under the snow and the snow
reads as grey-white instead of glowing white. Alpha (the needles' cut-out) stays the snowy texture's own.

usage: tone_snow.py <tldcoop_trees.bundle in> <bundle out> [K]
Needs: numpy, Pillow (the bundle's LZMA via Python's lzma, its block info via tools/trees/lz4d or uncompressed).
"""
import io
import lzma
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sf import SF  # noqa: E402

PINE = ["Lowest", "Low", "LowMedium", "Medium", "HighMedium", "High", "Highest"]
CEDAR = ["Low", "LowMedium", "Medium", "HighMedium", "High", "Highest"]


# --- the bundle ------------------------------------------------------------------------------------------------------
def rstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode(), e + 1


def read_bundle(path):
    b = open(path, "rb").read()
    o = 0
    sig, o = rstr(b, o)
    ver = struct.unpack(">I", b[o:o + 4])[0]
    o += 4
    uv, o = rstr(b, o)
    rev, o = rstr(b, o)
    size, csz, usz, flags = struct.unpack(">qIII", b[o:o + 20])
    o += 20
    o = (o + 15) // 16 * 16
    info = b[o:o + csz]
    o += csz
    comp = flags & 0x3F
    if comp in (2, 3):
        d = tempfile.mkdtemp()
        open(d + "/i", "wb").write(info)
        subprocess.run([os.path.join(HERE, "lz4d"), d + "/i", d + "/o", str(usz)], check=True)
        info = open(d + "/o", "rb").read()
    p = 16
    n = struct.unpack(">i", info[p:p + 4])[0]
    p += 4
    blocks = []
    for _ in range(n):
        blocks.append(struct.unpack(">IIH", info[p:p + 10]))
        p += 10
    nn = struct.unpack(">i", info[p:p + 4])[0]
    p += 4
    nodes = []
    for _ in range(nn):
        off, sz, fl = struct.unpack(">qqI", info[p:p + 20])
        p += 20
        name, p = rstr(info, p)
        nodes.append((off, sz, fl, name))
    if flags & 0x200:
        o = (o + 15) // 16 * 16
    data = bytearray()
    for u, c, fl in blocks:
        raw = b[o:o + c]
        o += c
        cm = fl & 0x3F
        if cm == 0:
            data += raw
        elif cm == 1:
            props = raw[:5]
            lc = props[0] % 9
            r = props[0] // 9
            dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[{
                "id": lzma.FILTER_LZMA1, "dict_size": struct.unpack("<I", props[1:5])[0],
                "lc": lc, "lp": r % 5, "pb": r // 5}])
            data += dec.decompress(raw[5:], u)
        else:
            raise SystemExit("block compression %d not handled" % cm)
    return (ver, uv, rev), {name: bytes(data[off:off + sz]) for off, sz, fl, name in nodes}, nodes


def write_bundle(path, head, files, nodes):
    ver, uv, rev = head
    data = bytearray()
    new_nodes = []
    for off, sz, fl, name in nodes:
        new_nodes.append((len(data), len(files[name]), fl, name))
        data += files[name]
    dict_size = 1 << 23
    filt = {"id": lzma.FILTER_LZMA1, "dict_size": dict_size, "lc": 3, "lp": 0, "pb": 2, "preset": 9}
    comp = lzma.compress(bytes(data), format=lzma.FORMAT_RAW, filters=[filt])
    props = bytes([(2 * 5 + 0) * 9 + 3]) + struct.pack("<I", dict_size)
    block = props + comp
    info = bytearray(b"\0" * 16)
    info += struct.pack(">i", 1)
    info += struct.pack(">IIH", len(data), len(block), 0x41)
    info += struct.pack(">i", len(new_nodes))
    for off, sz, fl, name in new_nodes:
        info += struct.pack(">qqI", off, sz, fl) + name.encode() + b"\0"
    hdr = b"UnityFS\0" + struct.pack(">I", ver) + uv.encode() + b"\0" + rev.encode() + b"\0"
    flags = 0x40 | 0x200  # info uncompressed, blocks and directory together, padded after the info
    pre = len(hdr) + 20
    pad1 = (pre + 15) // 16 * 16 - pre
    after_info = pre + pad1 + len(info)
    pad2 = (after_info + 15) // 16 * 16 - after_info
    total = after_info + pad2 + len(block)
    out = hdr + struct.pack(">qIII", total, len(info), len(info), flags) + b"\0" * pad1 + info + b"\0" * pad2 + block
    open(path, "wb").write(out)


# --- DXT5 ------------------------------------------------------------------------------------------------------------
def dxt5_decode(w, h, data):
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    dds = b"DDS " + struct.pack("<7I", 124, 0x1007, bh * 4, bw * 4, len(data), 0, 1) + b"\0" * 44
    dds += struct.pack("<2I4s5I", 32, 4, b"DXT5", 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    img = Image.open(io.BytesIO(dds + data)).convert("RGBA")
    return np.asarray(img).astype(np.float32)[:h, :w]


def to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.uint32)
    return ((c[..., 0] * 31 + 127) // 255 << 11) | ((c[..., 1] * 63 + 127) // 255 << 5) | ((c[..., 2] * 31 + 127) // 255)


def from565(v):
    r = ((v >> 11) & 31) * 255.0 / 31
    g = ((v >> 5) & 63) * 255.0 / 63
    b = (v & 31) * 255.0 / 31
    return np.stack([r, g, b], -1)


def dxt5_encode(img):
    h, w = img.shape[:2]
    bh, bw = h // 4, w // 4
    blk = img.reshape(bh, 4, bw, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * bw, 16, 4)
    rgb, a = blk[..., :3], blk[..., 3]
    # colour: endpoints on the block's main axis (mean + the extremes projected on the bounding box diagonal)
    mean = rgb.mean(1, keepdims=True)
    cen = rgb - mean
    cov = np.einsum("nki,nkj->nij", cen, cen)
    axis = np.ones((len(blk), 3), np.float32)
    for _ in range(6):
        axis = np.einsum("nij,nj->ni", cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-6)
    t = np.einsum("nki,ni->nk", cen, axis)
    lo = mean[:, 0] + axis * t.min(1, keepdims=True)
    hi = mean[:, 0] + axis * t.max(1, keepdims=True)
    c0, c1 = to565(hi), to565(lo)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = from565(c0), from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)  # n,4,3
    d = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)
    idx = d.argmin(-1).astype(np.uint32)
    idx = np.where((c0 == c1)[:, None], 0, idx)
    cbits = (idx << (2 * np.arange(16, dtype=np.uint32))).sum(1).astype(np.uint32)
    # alpha: 8-value mode between the extremes
    a0, a1 = np.rint(a.max(1)).astype(np.int32), np.rint(a.min(1)).astype(np.int32)
    apal = np.stack([a0, a1] + [((7 - i) * a0 + i * a1) / 7 for i in range(1, 7)], 1)
    ai = np.abs(a[:, :, None] - apal[:, None, :]).argmin(-1).astype(np.uint64)
    ai = np.where((a0 == a1)[:, None], 0, ai)
    abits = (ai << (3 * np.arange(16, dtype=np.uint64))).sum(1).astype(np.uint64)
    out = np.zeros((len(blk), 16), np.uint8)
    out[:, 0] = a0
    out[:, 1] = a1
    for i in range(6):
        out[:, 2 + i] = (abits >> np.uint64(8 * i)) & np.uint64(255)
    out[:, 8] = c0 & 255
    out[:, 9] = c0 >> 8
    out[:, 10] = c1 & 255
    out[:, 11] = c1 >> 8
    for i in range(4):
        out[:, 12 + i] = (cbits >> (8 * i)) & 255
    return out.tobytes()


def mip_sizes(w, h, n):
    out = []
    for i in range(n):
        mw, mh = max(1, w >> i), max(1, h >> i)
        out.append((mw, mh, max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * 16))
    return out


def blend(snowy, clear, w, h, mips, k):
    out = bytearray()
    o = 0
    for mw, mh, sz in mip_sizes(w, h, mips):
        s, c = snowy[o:o + sz], clear[o:o + sz]
        o += sz
        if mw < 4 or mh < 4:
            out += s
            continue
        S, C = dxt5_decode(mw, mh, s), dxt5_decode(mw, mh, c)
        N = S.copy()
        N[..., :3] = C[..., :3] + k * (S[..., :3] - C[..., :3])
        out += dxt5_encode(np.clip(N, 0, 255))
    assert len(out) == len(snowy)
    return bytes(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    k = float(sys.argv[3]) if len(sys.argv) > 3 else 0.55
    head, files, nodes = read_bundle(src)
    cab = next(n for n in files if not n.endswith(".resS"))
    res = bytearray(files[cab + ".resS"])
    s = SF(files[cab])
    tex = {}
    for o in s.objects:
        if o[3] != 28:
            continue
        d = s.read(o)
        tex[d["m_Name"]] = d
    def raw(d):
        sd = d["m_StreamData"]
        return bytes(res[sd["offset"]:sd["offset"] + sd["size"]])
    for kind, names in (("Pine", PINE), ("Cedar", CEDAR)):
        clear = tex["TRN_TreeBark%s_Clear" % kind]
        for n in names:
            d = tex["TRN_TreeBark%s_%s" % (kind, n)]
            assert d["m_TextureFormat"] == 12 and d["m_Width"] == clear["m_Width"]
            new = blend(raw(d), raw(clear), d["m_Width"], d["m_Height"], d["m_MipCount"], k)
            sd = d["m_StreamData"]
            res[sd["offset"]:sd["offset"] + sd["size"]] = new
            print("toned", d["m_Name"], flush=True)
    files[cab + ".resS"] = bytes(res)
    write_bundle(dst, head, files, nodes)
    print("written", dst, os.path.getsize(dst))


if __name__ == "__main__":
    main()
