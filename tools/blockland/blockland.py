#!/usr/bin/env python3
"""
Blockland generator for sm64ex-alo (or the n64decomp/sm64 tree).

Builds a Minecraft-style block world and writes it as a normal SM64 level:

    levels/blockland/{header.h,script.c,geo.c,leveldata.c,texture.inc.c}
    levels/blockland/areas/1/{geo.inc.c,model.inc.c,collision.inc.c,macro.inc.c}
    levels/blockland/<block>.rgba16.png          (32x32 block textures)

and registers it in levels/level_defines.h (takes over the unused LEVEL_UNKNOWN_1 slot as LEVEL_BLOCKLAND).

Textures: by default simple ORIGINAL procedural textures are written. For the real Minecraft look pass --mc-jar with
YOUR OWN Minecraft client jar (e.g. ~/.minecraft/versions/1.21.1/1.21.1.jar): the block textures are read from it
locally and only written into your own build tree.

  python3 tools/blockland/blockland.py --sm64 ~/sm64ex-alo [--seed 7] [--size 96] [--mc-jar JAR] [--start-here]
"""
import argparse, math, os, random, re, struct, sys, zipfile, zlib

S = 100       # SM64 units per block (Mario is ~160 units tall = 1.6 blocks)
TEX = 32      # texture size in texels (power of two; 16x16 Minecraft textures are scaled up 2x)
MAXSPAN = 31  # blocks a merged face may span: UV is s10.5 fixed point, 31 * 32 texels * 32 = 31744 < 32767
WATER_ALPHA = 0xB0

AIR, STONE, DIRT, GRASS, SAND, GRAVEL, LOG, LEAVES, SNOW = range(9)
TEXNAMES = ["stone", "dirt", "grass_top", "grass_side", "sand", "gravel", "log_side", "log_top", "leaves", "snow", "water"]
T = {n: i for i, n in enumerate(TEXNAMES)}
ALPHA_TEX = {T["leaves"]}
WATER_TEX = T["water"]


def face_texture(block, axis, sign):
    if block == GRASS:
        return T["grass_top"] if (axis == 1 and sign > 0) else (T["dirt"] if axis == 1 else T["grass_side"])
    if block == LOG:
        return T["log_top"] if axis == 1 else T["log_side"]
    return {STONE: T["stone"], DIRT: T["dirt"], SAND: T["sand"], GRAVEL: T["gravel"], LEAVES: T["leaves"], SNOW: T["snow"]}[block]


# Minecraft-style face shading (applied as vertex colour; lighting is switched off in the display lists)
SHADE = {(1, 1): 255, (2, 1): 204, (2, -1): 204, (0, 1): 153, (0, -1): 153, (1, -1): 127}


# ----------------------------------------------------------------------------------------------- world
def hash2(ix, iz, seed):
    n = (ix * 374761393 + iz * 668265263 + seed * 144665) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, z, seed):
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    fx, fz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    a, b = hash2(ix, iz, seed), hash2(ix + 1, iz, seed)
    c, d = hash2(ix, iz + 1, seed), hash2(ix + 1, iz + 1, seed)
    top = a + (b - a) * fx
    bot = c + (d - c) * fx
    return top + (bot - top) * fz


def fbm(x, z, seed, octaves=4):
    v, amp, f, tot = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        v += vnoise(x * f, z * f, seed + o * 101) * amp
        tot += amp; amp *= 0.5; f *= 2.0
    return v / tot


class World:
    def __init__(self, n, seed):
        self.n, self.seed, self.sea = n, seed, 20
        c = n / 2.0
        self.h = []
        for z in range(n):
            row = []
            for x in range(n):
                d = math.hypot(x - c, z - c) / c
                land = fbm(x / 28.0, z / 28.0, seed)
                hills = fbm(x / 11.0 + 40, z / 11.0 + 40, seed + 7)
                height = 14 + land * 22 + (hills - 0.5) * 8 - (max(0.0, d - 0.55) ** 2) * 60
                row.append(max(4, min(60, int(round(height)))))
            self.h.append(row)
        self.H = max(max(r) for r in self.h) + 9           # tallest column + room for trees
        self.vox = bytearray(n * self.H * n)               # index (z * H + y) * n + x
        self._fill(random.Random(seed))

    def idx(self, x, y, z): return (z * self.H + y) * self.n + x

    def get(self, x, y, z):
        if 0 <= x < self.n and 0 <= y < self.H and 0 <= z < self.n: return self.vox[(z * self.H + y) * self.n + x]
        return AIR

    def set(self, x, y, z, b):
        if 0 <= x < self.n and 0 <= y < self.H and 0 <= z < self.n: self.vox[(z * self.H + y) * self.n + x] = b

    def _fill(self, rnd):
        n, sea = self.n, self.sea
        for z in range(n):
            for x in range(n):
                top = self.h[z][x]
                if top > sea + 26: surf = SNOW
                elif top > sea + 20: surf = STONE
                elif top <= sea + 1: surf = SAND if top >= sea - 4 else GRAVEL
                else: surf = GRASS
                for y in range(top):
                    if y == top - 1: b = surf
                    elif y >= top - 4: b = DIRT if surf == GRASS else (SAND if surf == SAND else STONE)
                    else: b = STONE
                    self.set(x, y, z, b)
        taken = set()
        for _ in range(n * n // 85):  # trees on grass, spaced apart (sparse enough to run between)
            x, z = rnd.randrange(2, n - 2), rnd.randrange(2, n - 2)
            top = self.h[z][x]
            if self.get(x, top - 1, z) != GRASS or top > sea + 17: continue
            if any((x + dx, z + dz) in taken for dx in range(-3, 4) for dz in range(-3, 4)): continue
            taken.add((x, z))
            th = rnd.randint(4, 6)
            for y in range(th): self.set(x, top + y, z, LOG)
            for dy in range(th - 3, th + 2):
                r = 2 if dy < th - 1 else 1
                for dx in range(-r, r + 1):
                    for dz in range(-r, r + 1):
                        if abs(dx) == r and abs(dz) == r and rnd.random() < 0.5: continue
                        if self.get(x + dx, top + dy, z + dz) == AIR: self.set(x + dx, top + dy, z + dz, LEAVES)

    def spawn(self):
        """Nearest column to the centre that is clearly above the sea and free above: (x, top, z) in blocks."""
        c = self.n // 2
        for r in range(0, self.n // 2):
            for dz in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    x, z = c + dx, c + dz
                    if 0 <= x < self.n and 0 <= z < self.n and self.h[z][x] >= self.sea + 2 and all(self.get(x, self.h[z][x] + k, z) == AIR for k in range(3)):
                        return x, self.h[z][x], z
        return c, self.h[c][c], c


# ----------------------------------------------------------------------------------------------- meshing
def hidden(b, nb):
    """Is a face of block b covered by neighbour nb? (leaves are only hidden by other leaves)"""
    if nb == AIR: return False
    if b == LEAVES: return nb == LEAVES
    return nb != LEAVES


def greedy(world):
    """Greedy-merged quads of all visible solid faces."""
    n, H = world.n, world.H
    dims = (n, H, n)
    strides = (1, n, H * n)
    vox = world.vox
    quads = []
    for axis in range(3):
        u_ax, v_ax = [a for a in range(3) if a != axis]
        du, dv = dims[u_ax], dims[v_ax]
        su, sv, sa = strides[u_ax], strides[v_ax], strides[axis]
        for sign in (1, -1):
            for plane in range(dims[axis]):
                nbplane = plane + sign
                edge = nbplane < 0 or nbplane >= dims[axis]
                base = plane * sa
                mask = [0] * (du * dv)
                any_face = False
                for v in range(dv):
                    row = base + v * sv
                    for u in range(du):
                        b = vox[row + u * su]
                        if b == AIR: continue
                        nb = AIR if edge else vox[row + u * su + sign * sa]
                        if hidden(b, nb): continue
                        mask[v * du + u] = face_texture(b, axis, sign) + 1
                        any_face = True
                if not any_face: continue
                for v in range(dv):
                    u = 0
                    while u < du:
                        t = mask[v * du + u]
                        if not t: u += 1; continue
                        w = 1
                        while u + w < du and w < MAXSPAN and mask[v * du + u + w] == t: w += 1
                        hh = 1
                        while v + hh < dv and hh < MAXSPAN and all(mask[(v + hh) * du + u + k] == t for k in range(w)): hh += 1
                        for dvv in range(hh):
                            for duu in range(w): mask[(v + dvv) * du + u + duu] = 0
                        quads.append(dict(axis=axis, sign=sign, plane=plane, ua=u_ax, va=v_ax, u0=u, v0=v, w=w, h=hh, tex=t - 1))
                        u += w
    return quads


def water_rects(world):
    n, sea = world.n, world.sea
    mask = [[1 if world.h[z][x] < sea else 0 for x in range(n)] for z in range(n)]
    out = []
    for z in range(n):
        x = 0
        while x < n:
            if not mask[z][x]: x += 1; continue
            w = 1
            while x + w < n and w < MAXSPAN and mask[z][x + w]: w += 1
            h = 1
            while z + h < n and h < MAXSPAN and all(mask[z + h][x + k] for k in range(w)): h += 1
            for dz in range(h):
                for dx in range(w): mask[z + dz][x + dx] = 0
            out.append((x, z, w, h)); x += w
    return out


def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def make_quad(world, q):
    """-> list of 4 vertices [(x,y,z),(u,v) texels] wound counter-clockwise seen from outside."""
    half = world.n * S // 2
    a, s = q["axis"], q["sign"]
    plane = q["plane"] + (1 if s > 0 else 0)
    ua, va = q["ua"], q["va"]
    cs = []
    for du_, dv_ in ((0, 0), (q["w"], 0), (q["w"], q["h"]), (0, q["h"])):
        c = [0, 0, 0]
        c[a], c[ua], c[va] = plane, q["u0"] + du_, q["v0"] + dv_
        cs.append(c)
    lo = [min(c[i] for c in cs) for i in range(3)]
    hi = [max(c[i] for c in cs) for i in range(3)]
    verts = []
    for c in cs:
        pos = (c[0] * S - half, c[1] * S, c[2] * S - half)
        if a == 1:                                       # top / bottom: u along x, v along z
            u, v = c[0] - lo[0], c[2] - lo[2]
        else:                                            # side: v runs downward, u runs left->right seen from outside
            h_ax = 0 if a == 2 else 2                    # horizontal axis lying in the face
            right_is_plus = (a == 2 and s > 0) or (a == 0 and s < 0)
            u = (c[h_ax] - lo[h_ax]) if right_is_plus else (hi[h_ax] - c[h_ax])
            v = hi[1] - c[1]
        verts.append((pos, (u * TEX, v * TEX)))
    out = [0, 0, 0]; out[a] = s
    n = cross(sub(verts[1][0], verts[0][0]), sub(verts[2][0], verts[0][0]))
    if n[0] * out[0] + n[1] * out[1] + n[2] * out[2] < 0:
        verts.reverse()
    return verts


def make_water_quad(world, r):
    x, z, w, h = r
    half = world.n * S // 2
    y = world.sea * S - 12                               # Minecraft water sits slightly below the block top
    pts = [(x, z), (x + w, z), (x + w, z + h), (x, z + h)]
    verts = [((px * S - half, y, pz * S - half), ((px - x) * TEX, (pz - z) * TEX)) for px, pz in pts]
    n = cross(sub(verts[1][0], verts[0][0]), sub(verts[2][0], verts[0][0]))
    if n[1] < 0: verts.reverse()
    return verts


# ----------------------------------------------------------------------------------------------- files: model
def fx(t): return int(round(t * 32))                    # texels -> s10.5 fixed point


def vtx_line(v, shade, alpha):
    (x, y, z), (u, vv) = v
    return "    {{{%d, %d, %d}, 0, {%d, %d}, {0x%02x, 0x%02x, 0x%02x, 0x%02x}}},\n" % (x, y, z, fx(u), fx(vv), shade, shade, shade, alpha)


def write_model(path, layers):
    """layers: list of (dl_name, combine, [ (tex_id, [ (verts, shade, alpha), ... ]) ])"""
    out = ["// Generated by tools/blockland/blockland.py - do not edit by hand\n\n"]
    dls = []
    counter = 0
    for dl_name, combine, groups in layers:
        body = ["const Gfx %s[] = {\n" % dl_name,
                "    gsDPPipeSync(),\n",
                "    gsDPSetCombineMode(%s, %s),\n" % (combine, combine),
                "    gsSPClearGeometryMode(G_LIGHTING),\n",
                "    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),\n"]
        for tex_id, quads in groups:
            body.append("    gsDPLoadTextureBlock(blockland_texture_%s, G_IM_FMT_RGBA, G_IM_SIZ_16b, %d, %d, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 5, 5, G_TX_NOLOD, G_TX_NOLOD),\n" % (TEXNAMES[tex_id], TEX, TEX))
            for i in range(0, len(quads), 4):
                batch = quads[i:i + 4]
                name = "blockland_seg7_vertex_%05d" % counter; counter += 1
                out.append("static const Vtx %s[] = {\n" % name)
                for verts, shade, alpha in batch:
                    for v in verts: out.append(vtx_line(v, shade, alpha))
                out.append("};\n\n")
                body.append("    gsSPVertex(%s, %d, 0),\n" % (name, 4 * len(batch)))
                for k in range(len(batch)):
                    b = 4 * k
                    body.append("    gsSP2Triangles(%d, %d, %d, 0x0, %d, %d, %d, 0x0),\n" % (b, b + 1, b + 2, b, b + 2, b + 3))
        body += ["    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),\n",
                 "    gsDPPipeSync(),\n",
                 "    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),\n",
                 "    gsSPSetGeometryMode(G_LIGHTING),\n",
                 "    gsSPEndDisplayList(),\n", "};\n\n"]
        dls.append("".join(body))
    out += dls
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("".join(out))


# ----------------------------------------------------------------------------------------------- files: collision
def write_collision(path, tri_quads, half, sea_y, top_y):
    verts, index, tris = [], {}, []

    def vi(p):
        if p not in index: index[p] = len(verts); verts.append(p)
        return index[p]

    for q in tri_quads:
        a, b, c, d = [vi(v[0]) for v in q]
        tris.append((a, b, c)); tris.append((a, c, d))
    # invisible world border (facing inwards) so Mario cannot walk off the edge
    lo, hi, y0, y1 = -half, half, -400, top_y
    border = [((lo, y0, lo), (lo, y1, lo), (hi, y1, lo), (hi, y0, lo)),     # z = lo wall, normal +z
              ((hi, y0, lo), (hi, y1, lo), (hi, y1, hi), (hi, y0, hi)),     # x = hi wall, normal -x
              ((hi, y0, hi), (hi, y1, hi), (lo, y1, hi), (lo, y0, hi)),     # z = hi wall, normal -z
              ((lo, y0, hi), (lo, y1, hi), (lo, y1, lo), (lo, y0, lo))]     # x = lo wall, normal +x
    inward = [(0, 0, 1), (-1, 0, 0), (0, 0, -1), (1, 0, 0)]
    for quad, nrm in zip(border, inward):
        n = cross(sub(quad[1], quad[0]), sub(quad[2], quad[0]))
        if n[0] * nrm[0] + n[1] * nrm[1] + n[2] * nrm[2] < 0: quad = tuple(reversed(quad))
        a, b, c, d = [vi(p) for p in quad]
        tris.append((a, b, c)); tris.append((a, c, d))
    if len(verts) > 32000: sys.exit("too many collision vertices (%d); use a smaller --size" % len(verts))
    o = ["// Generated by tools/blockland/blockland.py - do not edit by hand\n",
         "const Collision blockland_seg7_collision[] = {\n", "    COL_INIT(),\n", "    COL_VERTEX_INIT(%d),\n" % len(verts)]
    o += ["    COL_VERTEX(%d, %d, %d),\n" % v for v in verts]
    for i in range(0, len(tris), 6000):          # several groups: each COL_TRI_INIT count stays small (s16)
        chunk = tris[i:i + 6000]
        o += ["    COL_TRI_INIT(SURFACE_DEFAULT, %d),\n" % len(chunk)]
        o += ["    COL_TRI(%d, %d, %d),\n" % t for t in chunk]
    o += ["    COL_TRI_STOP(),\n", "    COL_WATER_BOX_INIT(1),\n",
          "    COL_WATER_BOX(0, %d, %d, %d, %d, %d),\n" % (lo, lo, hi, hi, sea_y), "    COL_END(),\n", "};\n"]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("".join(o))
    return len(verts), len(tris)


# ----------------------------------------------------------------------------------------------- textures
def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_png(data):
    """Minimal PNG reader (8-bit gray / gray+alpha / RGB / RGBA / palette, not interlaced) -> (w, h, rgba bytes)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, pal, trns = 8, b"", None, None
    while pos < len(data):
        ln, = struct.unpack(">I", data[pos:pos + 4]); typ = data[pos + 4:pos + 8]; body = data[pos + 8:pos + 8 + ln]; pos += 12 + ln
        if typ == b"IHDR": w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE": pal = body
        elif typ == b"tRNS": trns = body
        elif typ == b"IDAT": idat += body
    if bd != 8 or il != 0: raise ValueError("unsupported PNG (bit depth %d, interlace %d)" % (bd, il))
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat); stride = w * ch; prev = bytearray(stride); p = 0; rows = []
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0; b = prev[i]; c = prev[i - ch] if i >= ch else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(bytes(line)); prev = line
    out = bytearray()
    for r in rows:
        for x in range(w):
            if ct == 6: out += r[x * 4:x * 4 + 4]
            elif ct == 2: out += r[x * 3:x * 3 + 3] + b"\xff"
            elif ct == 0: out += bytes((r[x],) * 3) + b"\xff"
            elif ct == 4: out += bytes((r[x * 2],) * 3) + bytes((r[x * 2 + 1],))
            else:
                i = r[x]; out += pal[i * 3:i * 3 + 3] + bytes((trns[i] if trns and i < len(trns) else 255,))
    return w, h, bytes(out)


def scale_to(w, h, px, size=TEX):
    """Nearest-neighbour resize of the first square (w x w) frame to size x size."""
    side = min(w, h)
    out = bytearray()
    for y in range(size):
        for x in range(size):
            sx, sy = x * side // size, y * side // size
            out += px[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
    return bytes(out)


def tinted(px, rgb):
    o = bytearray(px)
    for i in range(0, len(o), 4):
        o[i] = o[i] * rgb[0] // 255; o[i + 1] = o[i + 1] * rgb[1] // 255; o[i + 2] = o[i + 2] * rgb[2] // 255
    return bytes(o)


def procedural_textures(seed):
    """Original, simple stand-ins (no Minecraft assets)."""
    rnd = random.Random(seed + 99)

    def noise(base, var, alpha=255):
        o = bytearray()
        for _ in range(TEX * TEX):
            d = rnd.randint(-var, var)
            o += bytes((max(0, min(255, base[0] + d)), max(0, min(255, base[1] + d)), max(0, min(255, base[2] + d)), alpha))
        return o

    tex = {}
    tex["stone"] = bytes(noise((125, 125, 128), 14))
    tex["dirt"] = bytes(noise((134, 96, 67), 14))
    tex["grass_top"] = bytes(noise((95, 159, 53), 14))
    g = bytearray(noise((134, 96, 67), 14))
    for y in range(6):
        for x in range(TEX):
            d = rnd.randint(-10, 10); i = (y * TEX + x) * 4
            g[i:i + 4] = bytes((95 + d, 159 + d, 53 + d, 255))
    tex["grass_side"] = bytes(g)
    tex["sand"] = bytes(noise((219, 207, 163), 9))
    tex["gravel"] = bytes(noise((131, 127, 126), 26))
    ls = bytearray(noise((102, 81, 49), 8))
    for x in range(0, TEX, 6):
        for y in range(TEX): i = (y * TEX + x) * 4; ls[i:i + 3] = bytes((74, 58, 33))
    tex["log_side"] = bytes(ls)
    lt = bytearray(noise((170, 140, 90), 8))
    for y in range(TEX):
        for x in range(TEX):
            r = int(math.hypot(x - 15.5, y - 15.5))
            if r % 5 == 0: i = (y * TEX + x) * 4; lt[i:i + 3] = bytes((130, 100, 60))
    tex["log_top"] = bytes(lt)
    lv = bytearray(noise((58, 130, 40), 22))
    for i in range(0, len(lv), 4):
        if rnd.random() < 0.18: lv[i + 3] = 0
    tex["leaves"] = bytes(lv)
    tex["snow"] = bytes(noise((245, 248, 250), 5))
    w = bytearray(noise((52, 100, 220), 10))
    tex["water"] = bytes(w)
    return tex


def minecraft_textures(jar_path):
    """Block textures from the user's own Minecraft jar, tinted the way the game tints them in a plains biome."""
    z = zipfile.ZipFile(jar_path)

    def load(name):
        w, h, px = read_png(z.read("assets/minecraft/textures/block/%s.png" % name))
        return w, h, px

    def get(name, tint=None):
        w, h, px = load(name)
        px = scale_to(w, h, px)
        return tinted(px, tint) if tint else px

    GRASS_T, FOLIAGE_T, WATER_T = (0x91, 0xBD, 0x59), (0x77, 0xAB, 0x2F), (0x3F, 0x76, 0xE4)
    side = bytearray(get("grass_block_side"))
    over = get("grass_block_side_overlay", GRASS_T)
    for i in range(0, len(side), 4):
        if over[i + 3] > 127: side[i:i + 4] = over[i:i + 4]
    tex = {"stone": get("stone"), "dirt": get("dirt"), "grass_top": get("grass_block_top", GRASS_T), "grass_side": bytes(side),
           "sand": get("sand"), "gravel": get("gravel"), "log_side": get("oak_log"), "log_top": get("oak_log_top"),
           "leaves": get("oak_leaves", FOLIAGE_T), "snow": get("snow"), "water": get("water_still", WATER_T)}
    return tex


# ----------------------------------------------------------------------------------------------- files: level scaffolding
def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(text)


HEADER = """#ifndef BLOCKLAND_HEADER_H
#define BLOCKLAND_HEADER_H

#include "types.h"
#include "game/moving_texture.h"

// geo
extern const GeoLayout blockland_geo_area_1[];

// leveldata
extern const Gfx blockland_seg7_dl_opaque[];
extern const Gfx blockland_seg7_dl_alpha[];
extern const Gfx blockland_seg7_dl_water[];
extern const Collision blockland_seg7_collision[];
extern const MacroObject blockland_seg7_macro_objs[];

// script
extern const LevelScript level_blockland_entry[];

#endif
"""

GEO_C = """#include <ultra64.h>
#include "sm64.h"
#include "geo_commands.h"

#include "game/level_geo.h"
#include "game/geo_misc.h"
#include "game/camera.h"
#include "game/moving_texture.h"
#include "game/screen_transition.h"
#include "game/paintings.h"

#include "make_const_nonconst.h"

#include "levels/blockland/header.h"

#include "levels/blockland/areas/1/geo.inc.c"
"""

LEVELDATA_C = """#include <ultra64.h>
#include "sm64.h"
#include "surface_terrains.h"
#include "moving_texture_macros.h"
#include "level_misc_macros.h"
#include "macro_presets.h"
#include "special_presets.h"
#include "textures.h"
#include "dialog_ids.h"

#include "make_const_nonconst.h"
#include "levels/blockland/texture.inc.c"
#include "levels/blockland/areas/1/model.inc.c"
#include "levels/blockland/areas/1/collision.inc.c"
#include "levels/blockland/areas/1/macro.inc.c"
"""

AREA_GEO = """// Generated by tools/blockland/blockland.py
const GeoLayout blockland_geo_area_1[] = {
    GEO_NODE_SCREEN_AREA(10, SCREEN_WIDTH/2, SCREEN_HEIGHT/2, SCREEN_WIDTH/2, SCREEN_HEIGHT/2),
    GEO_OPEN_NODE(),
        GEO_ZBUFFER(0),
        GEO_OPEN_NODE(),
            GEO_NODE_ORTHO(100),
            GEO_OPEN_NODE(),
                GEO_BACKGROUND(BACKGROUND_OCEAN_SKY, geo_skybox_main),
            GEO_CLOSE_NODE(),
        GEO_CLOSE_NODE(),
        GEO_ZBUFFER(1),
        GEO_OPEN_NODE(),
            GEO_CAMERA_FRUSTUM_WITH_FUNC(45, 100, 12800, geo_camera_fov),
            GEO_OPEN_NODE(),
                GEO_CAMERA(CAMERA_MODE_8_DIRECTIONS, 0, 2000, 6000, 0, 0, 0, geo_camera_main),
                GEO_OPEN_NODE(),
                    GEO_DISPLAY_LIST(LAYER_OPAQUE, blockland_seg7_dl_opaque),
                    GEO_DISPLAY_LIST(LAYER_ALPHA, blockland_seg7_dl_alpha),
                    GEO_DISPLAY_LIST(LAYER_TRANSPARENT, blockland_seg7_dl_water),
                    GEO_RENDER_OBJ(),
                    GEO_ASM(0, geo_envfx_main),
                GEO_CLOSE_NODE(),
            GEO_CLOSE_NODE(),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
"""

MACRO_C = """// Generated by tools/blockland/blockland.py
const MacroObject blockland_seg7_macro_objs[] = {
    MACRO_OBJECT_END(),
};
"""

SCRIPT_C = """#include <ultra64.h>
#include "sm64.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "seq_ids.h"
#include "segment_symbols.h"
#include "level_commands.h"

#include "game/level_update.h"

#include "levels/scripts.h"

#include "make_const_nonconst.h"
#include "levels/blockland/header.h"

// Generated by tools/blockland/blockland.py
static const LevelScript script_func_local_1[] = {
%(coins)s    RETURN(),
};

const LevelScript level_blockland_entry[] = {
    INIT_LEVEL(),
    LOAD_MIO0        (/*seg*/ 0x07, _blockland_segment_7SegmentRomStart, _blockland_segment_7SegmentRomEnd),
    LOAD_MIO0_TEXTURE(/*seg*/ 0x09, _generic_mio0SegmentRomStart, _generic_mio0SegmentRomEnd),
    LOAD_MIO0        (/*seg*/ 0x0A, _water_skybox_mio0SegmentRomStart, _water_skybox_mio0SegmentRomEnd),
    LOAD_MIO0        (/*seg*/ 0x08, _common0_mio0SegmentRomStart, _common0_mio0SegmentRomEnd),
    LOAD_RAW         (/*seg*/ 0x0F, _common0_geoSegmentRomStart,  _common0_geoSegmentRomEnd),
    ALLOC_LEVEL_POOL(),
    MARIO(/*model*/ MODEL_MARIO, /*bhvParam*/ BPARAM4(0x01), /*bhv*/ bhvMario),
    JUMP_LINK(script_func_global_1),

    AREA(/*index*/ 1, blockland_geo_area_1),
        OBJECT(/*model*/ MODEL_NONE, /*pos*/ %(sx)d, %(sy_air)d, %(sz)d, /*angle*/ 0, 0, 0, /*bhvParam*/ BPARAM2(WARP_NODE_0A), /*bhv*/ bhvSpinAirborneWarp),
        WARP_NODE(/*id*/ WARP_NODE_0A,      /*destLevel*/ LEVEL_BLOCKLAND, /*destArea*/ 1, /*destNode*/ WARP_NODE_0A, /*flags*/ WARP_NO_CHECKPOINT),
        WARP_NODE(/*id*/ WARP_NODE_SUCCESS, /*destLevel*/ LEVEL_BLOCKLAND, /*destArea*/ 1, /*destNode*/ WARP_NODE_0A, /*flags*/ WARP_NO_CHECKPOINT),
        WARP_NODE(/*id*/ WARP_NODE_DEATH,   /*destLevel*/ LEVEL_BLOCKLAND, /*destArea*/ 1, /*destNode*/ WARP_NODE_0A, /*flags*/ WARP_NO_CHECKPOINT),
        JUMP_LINK(script_func_local_1),
        TERRAIN(/*terrainData*/ blockland_seg7_collision),
        MACRO_OBJECTS(/*objList*/ blockland_seg7_macro_objs),
        SET_BACKGROUND_MUSIC(/*settingsPreset*/ 0x0000, /*seq*/ SEQ_LEVEL_GRASS),
        TERRAIN_TYPE(/*terrainType*/ TERRAIN_GRASS),
    END_AREA(),

    FREE_LEVEL_POOL(),
    MARIO_POS(/*area*/ 1, /*yaw*/ 0, /*pos*/ %(sx)d, %(sy)d, %(sz)d),
    CALL(/*arg*/ 0, /*func*/ lvl_init_or_update),
    CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update),
    CLEAR_LEVEL(),
    SLEEP_BEFORE_EXIT(/*frames*/ 1),
    EXIT(),
};
"""


def patch_level_defines(sm64):
    p = os.path.join(sm64, "levels", "level_defines.h")
    s = open(p).read()
    if "LEVEL_BLOCKLAND" in s: return "already registered"
    pat = re.compile(r'^STUB_LEVEL\(\s*"",\s*LEVEL_UNKNOWN_1,.*$', re.M)
    if not pat.search(s): sys.exit("could not find the LEVEL_UNKNOWN_1 slot in levels/level_defines.h")
    line = 'DEFINE_LEVEL("BLOCKLAND",     LEVEL_BLOCKLAND,        COURSE_NONE,     blockland,        generic,  20000,    0x08, 0x08, 0x08, _,         _)'
    open(p, "w").write(pat.sub(line, s, count=1))
    return "registered as LEVEL_BLOCKLAND"


def drop_stale_build_files(sm64):
    """make generates these from levels/level_defines.h but does not track that dependency, so a build made before
    the level was registered keeps old copies (symptom: 'level_blockland_entry undeclared'). They are safe to delete."""
    import glob
    gone = []
    for pat in ("build/*/include/level_headers.h", "build/*/level_rules.mk"):
        for p in glob.glob(os.path.join(sm64, pat)):
            os.remove(p); gone.append(os.path.relpath(p, sm64))
    return gone


def patch_start_here(sm64):
    tw = os.path.join(sm64, "include", "tweaks.h")
    s = open(tw).read()
    s = re.sub(r"#define START_LEVEL\s+\w+", "#define START_LEVEL LEVEL_BLOCKLAND", s)
    open(tw, "w").write(s)
    ms = os.path.join(sm64, "levels", "menu", "script.c")
    s = open(ms).read()
    s = re.sub(r"#ifdef RM2C\n(\s*SET_REG\(/\*value\*/ START_LEVEL\),)\n#else\n\s*SET_REG\(/\*value\*/ LEVEL_CASTLE_GROUNDS\),\n#endif", r"\1", s)
    open(ms, "w").write(s)


def main():
    ap = argparse.ArgumentParser(description="Generate the Blockland level for sm64ex-alo")
    ap.add_argument("--sm64", nargs="?", const=".", default=".",
                    help="path to your sm64ex-alo (or sm64 decomp) folder; defaults to the current folder")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--size", type=int, default=96, help="world size in blocks (default 96, max 160)")
    ap.add_argument("--mc-jar", help="your own Minecraft client jar, for the real block textures")
    ap.add_argument("--start-here", action="store_true", help="make a new game start in Blockland (patches tweaks.h and levels/menu/script.c)")
    ap.add_argument("--no-register", action="store_true", help="do not touch levels/level_defines.h")
    a = ap.parse_args()
    if not os.path.isdir(os.path.join(a.sm64, "levels")): sys.exit("%s does not look like an sm64 source tree" % a.sm64)
    n = max(16, min(160, a.size))
    if n * S > 16000: sys.exit("size too large for the 16384-unit level bounds")

    print("generating %dx%d world (seed %d) ..." % (n, n, a.seed))
    world = World(n, a.seed)
    quads = [(make_quad(world, q), q) for q in greedy(world)]
    wrects = [make_water_quad(world, r) for r in water_rects(world)]

    # visual layers
    opaque, alpha = {}, {}
    for verts, q in quads:
        entry = (verts, SHADE[(q["axis"], q["sign"])], 255)
        (alpha if q["tex"] in ALPHA_TEX else opaque).setdefault(q["tex"], []).append(entry)
    water = [(v, 255, WATER_ALPHA) for v in wrects]
    layers = [("blockland_seg7_dl_opaque", "G_CC_MODULATERGB", sorted(opaque.items())),
              ("blockland_seg7_dl_alpha", "G_CC_MODULATERGBA", sorted(alpha.items())),
              ("blockland_seg7_dl_water", "G_CC_MODULATERGBA", [(WATER_TEX, water)] if water else [])]

    lvl = os.path.join(a.sm64, "levels", "blockland")
    write_model(os.path.join(lvl, "areas", "1", "model.inc.c"), layers)
    half = n * S // 2
    nv, nt = write_collision(os.path.join(lvl, "areas", "1", "collision.inc.c"), [verts for verts, q in quads], half, world.sea * S - 12, 7000)

    sx, sy, sz = world.spawn()
    spx, spz = sx * S - half + S // 2, sz * S - half + S // 2
    spy = sy * S
    coins = ""
    rnd = random.Random(a.seed + 5)
    for k in range(16):
        ang = k / 16.0 * 2 * math.pi
        cx, cz = int(spx + math.cos(ang) * 600), int(spz + math.sin(ang) * 600)
        gx, gz = int((cx + half) // S), int((cz + half) // S)
        if 0 <= gx < n and 0 <= gz < n:
            coins += "    OBJECT(/*model*/ MODEL_YELLOW_COIN, /*pos*/ %d, %d, %d, /*angle*/ 0, 0, 0, /*bhvParam*/ 0, /*bhv*/ bhvYellowCoin),\n" % (cx, world.h[gz][gx] * S + 80, cz)
    write(os.path.join(lvl, "script.c"), SCRIPT_C % dict(sx=spx, sy=spy, sz=spz, sy_air=spy + 1000, coins=coins))
    write(os.path.join(lvl, "header.h"), HEADER)
    write(os.path.join(lvl, "geo.c"), GEO_C)
    write(os.path.join(lvl, "leveldata.c"), LEVELDATA_C)
    write(os.path.join(lvl, "areas", "1", "geo.inc.c"), AREA_GEO)
    write(os.path.join(lvl, "areas", "1", "macro.inc.c"), MACRO_C)

    # textures
    if a.mc_jar:
        print("reading block textures from", a.mc_jar)
        tex = minecraft_textures(os.path.expanduser(a.mc_jar))
    else:
        tex = procedural_textures(a.seed)
    lines = ["// Generated by tools/blockland/blockland.py\n"]
    for name in TEXNAMES:
        write_png(os.path.join(lvl, "%s.rgba16.png" % name), TEX, TEX, tex[name])
        lines.append('ALIGNED8 static const Texture blockland_texture_%s[] = {\n#include "levels/blockland/%s.rgba16.inc.c"\n};\n\n' % (name, name))
    write(os.path.join(lvl, "texture.inc.c"), "".join(lines))

    if not a.no_register:
        print(patch_level_defines(a.sm64))
        stale = drop_stale_build_files(a.sm64)
        if stale: print("removed stale generated files so make rebuilds the level table:", ", ".join(stale))
    if a.start_here: patch_start_here(a.sm64); print("new games now start in Blockland (revert START_LEVEL in include/tweaks.h)")
    print("quads: %d solid + %d water; collision: %d vertices, %d triangles; spawn (%d, %d, %d)" % (len(quads), len(wrects), nv, nt, spx, spy, spz))
    print("done. Now build with  make -j4  and run  ./build/us_pc/sm64.us.f3dex2e --level 1 --skip-intro  (see tools/blockland/README.md)")


if __name__ == "__main__":
    main()
