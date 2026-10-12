#!/usr/bin/env python3
"""
Software preview of the generated Blockland level - no game, no GPU needed.

Reads levels/blockland/areas/1/model.inc.c and the block PNGs, rasterises them with a z-buffer and the same
back-face culling the N64 uses (front face = counter-clockwise), and writes PNG screenshots.
Needs:  pip install numpy pillow   (Debian: sudo apt install python3-numpy python3-pil)

  python3 tools/blockland/preview.py --sm64 ~/sm64ex-alo --out preview
writes preview_ground.png and preview_overview.png
"""
import argparse, math, os, re, sys
import numpy as np
from PIL import Image

TEX = 32


def load_level(sm64):
    lvl = os.path.join(sm64, "levels", "blockland")
    src = open(os.path.join(lvl, "areas", "1", "model.inc.c")).read()
    vtx = {}
    for m in re.finditer(r"static const Vtx (\w+)\[\] = \{(.*?)\n\};", src, re.S):
        rows = re.findall(r"\{\{\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\},\s*0,\s*\{\s*(-?\d+),\s*(-?\d+)\},\s*\{0x(\w+), 0x(\w+), 0x(\w+), 0x(\w+)\}\}\}", m.group(2))
        vtx[m.group(1)] = [(int(a), int(b), int(c), int(u) / 32.0, int(v) / 32.0, int(r, 16), int(g, 16), int(bb, 16), int(al, 16)) for a, b, c, u, v, r, g, bb, al in rows]
    tris = []   # (texture, layer, [3 vertices])
    for m in re.finditer(r"const Gfx (\w+)\[\] = \{(.*?)\n\};", src, re.S):
        layer = m.group(1).split("_")[-1]
        tex, buf = None, []
        for line in m.group(2).splitlines():
            line = line.strip()
            t = re.match(r"gsDPLoadTextureBlock\(blockland_texture_(\w+),", line)
            if t: tex = t.group(1); continue
            v = re.match(r"gsSPVertex\((\w+), (\d+), 0\)", line)
            if v: buf = vtx[v.group(1)][:int(v.group(2))]; continue
            q = re.match(r"gsSP2Triangles\((\d+), (\d+), (\d+), 0x0, (\d+), (\d+), (\d+), 0x0\)", line)
            if q:
                i = list(map(int, q.groups()))
                tris.append((tex, layer, [buf[i[0]], buf[i[1]], buf[i[2]]]))
                tris.append((tex, layer, [buf[i[3]], buf[i[4]], buf[i[5]]]))
    textures = {}
    for f in os.listdir(lvl):
        if f.endswith(".rgba16.png"):
            textures[f[:-len(".rgba16.png")]] = np.asarray(Image.open(os.path.join(lvl, f)).convert("RGBA"))
    return tris, textures


def look_at(eye, target):
    f = np.array(target, float) - np.array(eye, float); f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return np.array(eye, float), r, u, f


def render(tris, textures, eye, target, w=960, h=540, fov=60.0, far=14000.0):
    e, r, u, f = look_at(eye, target)
    focal = 0.5 * h / math.tan(math.radians(fov) / 2)
    img = np.zeros((h, w, 3), np.float32)
    # sky gradient
    for y in range(h): img[y, :, :] = np.array([120, 170, 235], np.float32) * (0.75 + 0.25 * (1 - y / h))
    zbuf = np.full((h, w), np.inf, np.float32)
    order = sorted(range(len(tris)), key=lambda i: 1 if tris[i][1] == "water" else 0)   # water last (blended)
    drawn = culled = 0
    for i in order:
        tex, layer, vs = tris[i]
        P = []
        for v in vs:
            d = np.array(v[:3], float) - e
            x, y, z = d @ r, d @ u, d @ f
            P.append((x, y, z))
        if min(p[2] for p in P) < 60: continue                     # behind / too close: skip (no clipping)
        sc = [(w / 2 + focal * p[0] / p[2], h / 2 - focal * p[1] / p[2]) for p in P]
        area = (sc[1][0] - sc[0][0]) * (sc[2][1] - sc[0][1]) - (sc[2][0] - sc[0][0]) * (sc[1][1] - sc[0][1])
        # screen y points down, so a counter-clockwise (front) triangle has NEGATIVE area here
        if area >= 0: culled += 1; continue
        x0, x1 = int(max(0, min(s[0] for s in sc))), int(min(w - 1, max(s[0] for s in sc)))
        y0, y1 = int(max(0, min(s[1] for s in sc))), int(min(h - 1, max(s[1] for s in sc)))
        if x1 < x0 or y1 < y0: continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        den = area
        l0 = ((sc[1][0] - xs) * (sc[2][1] - ys) - (sc[2][0] - xs) * (sc[1][1] - ys)) / den
        l1 = ((sc[2][0] - xs) * (sc[0][1] - ys) - (sc[0][0] - xs) * (sc[2][1] - ys)) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any(): continue
        iz = l0 / P[0][2] + l1 / P[1][2] + l2 / P[2][2]           # perspective-correct interpolation
        z = 1.0 / np.maximum(iz, 1e-9)
        b0, b1, b2 = l0 / P[0][2] * z, l1 / P[1][2] * z, l2 / P[2][2] * z
        uu = b0 * vs[0][3] + b1 * vs[1][3] + b2 * vs[2][3]
        vv = b0 * vs[0][4] + b1 * vs[1][4] + b2 * vs[2][4]
        tx = (np.floor(uu).astype(int) * 0 + np.mod(np.floor(uu), TEX).astype(int))
        ty = np.mod(np.floor(vv), TEX).astype(int)
        texel = textures[tex][ty, tx].astype(np.float32)
        shade = (b0 * vs[0][5] + b1 * vs[1][5] + b2 * vs[2][5]) / 255.0
        alpha = (b0 * vs[0][8] + b1 * vs[1][8] + b2 * vs[2][8]) / 255.0 * texel[..., 3] / 255.0
        zs = zbuf[y0:y1 + 1, x0:x1 + 1]
        ok = inside & (z < zs) & ((alpha > 0.5) if layer != "water" else (alpha > 0.05))
        if not ok.any(): continue
        col = texel[..., :3] * shade[..., None]
        sub = img[y0:y1 + 1, x0:x1 + 1]
        if layer == "water":
            sub[ok] = sub[ok] * (1 - alpha[ok][:, None]) + col[ok] * alpha[ok][:, None]
        else:
            sub[ok] = col[ok]; zs[ok] = z[ok]
        drawn += 1
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)), drawn, culled


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sm64", required=True)
    ap.add_argument("--out", default="preview")
    a = ap.parse_args()
    tris, textures = load_level(a.sm64)
    print("loaded %d triangles, %d textures" % (len(tris), len(textures)))
    script = open(os.path.join(a.sm64, "levels", "blockland", "script.c")).read()
    sx, sy, sz = map(int, re.search(r"MARIO_POS\(/\*area\*/ 1, /\*yaw\*/ 0, /\*pos\*/ (-?\d+), (-?\d+), (-?\d+)\)", script).groups())
    img, drawn, culled = render(tris, textures, (sx - 900, sy + 650, sz - 1100), (sx + 300, sy, sz + 600))
    img.save(a.out + "_ground.png"); print("ground view: drew %d, back-face culled %d" % (drawn, culled))
    img, drawn, culled = render(tris, textures, (-5600, 6200, -5600), (0, 1500, 0), fov=62)
    img.save(a.out + "_overview.png"); print("overview: drew %d, back-face culled %d" % (drawn, culled))


if __name__ == "__main__":
    main()
