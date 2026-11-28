"""PS4 package icon (sce_sys/icon0.png, 512x512) drawn with the same planet-and-ring mark as the
app's renderer (orbit_mark in orbit-ps3/src/ui.c), on the ORBIT dark-navy gradient. stdlib only.
Usage: python3 make_icon.py out.png"""
import math, struct, sys, zlib

W = H = 512
ACCENT = (0x4d, 0x8d, 0xff)

def blend(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

px = [[blend((0x0d, 0x0f, 0x13), (0x16, 0x22, 0x3a), (x + y) / (W + H)) for x in range(W)] for y in range(H)]

def plot(x, y, col, a):
    a = max(0.0, min(1.0, a))
    px[y][x] = blend(px[y][x], col, a)

X, Y, R = W / 2, H / 2 + 6, 150.0
c, s = 0.866, -0.5
for j in range(H):
    for i in range(W):
        dx, dy = i + 0.5 - X, j + 0.5 - Y
        ex = (dx * c - dy * s) / (R * 1.55)
        ey = (dx * s + dy * c) / (R * 0.48)
        e = math.sqrt(ex * ex + ey * ey)
        ring = 1.0 - abs(e - 1.0) * R * 0.48 / (R * 0.09 + 0.8)
        d = math.sqrt(dx * dx + dy * dy)
        planet = R * 0.62 - d + 0.5
        front = (dx * s + dy * c) > 0
        if not front and ring > 0:
            plot(i, j, ACCENT, ring)
        if planet > 0:
            shade = 0.75 + 0.25 * (-dx - dy) / (R * 0.9)
            plot(i, j, blend((0x2c, 0x63, 0xd8), (0x8f, 0xb5, 0xff), max(0, min(1, shade))), planet)
        if front and ring > 0 and d > R * 0.5:
            plot(i, j, ACCENT, ring)

rows = b''.join(b'\x00' + bytes(v for p in row for v in p) for row in px)
open(sys.argv[1], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) +
                              chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))
