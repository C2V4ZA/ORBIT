#!/usr/bin/env python3
"""Abstract stand-in art (320x176 PNG) for PC preview renders only.
NOT game artwork: gradients and orbit arcs generated from the Title ID so screenshots show
how the real ICON0.PNG is placed. Never packaged."""
import math, os, struct, sys, zlib

def png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def art(seed, w=320, h=176):
    hsh = zlib.crc32(seed.encode())
    hue = (hsh % 360) / 360.0
    def hsv(hh, s, v):
        i = int(hh * 6) % 6; f = hh * 6 - int(hh * 6)
        p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
        return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
    a, b = hsv(hue, 0.65, 0.55), hsv((hue + 0.12) % 1, 0.75, 0.22)
    cx, cy = w * (0.3 + (hsh >> 8) % 40 / 100), h * (0.4 + (hsh >> 16) % 30 / 100)
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            t = min(1.0, math.hypot(x - cx, (y - cy) * 1.4) / (w * 0.7))
            r, g, bb = [a[i] * (1 - t) + b[i] * t for i in range(3)]
            d = abs(math.hypot(x - cx, (y - cy) * 2.6) - w * 0.33)
            if d < 2.5:
                k = (2.5 - d) / 2.5 * 0.5
                r, g, bb = r + (1 - r) * k, g + (1 - g) * k, bb + (1 - bb) * k
            o = (y * w + x) * 4
            out[o:o + 4] = bytes((int(r * 255), int(g * 255), int(bb * 255), 255))
    return out

if __name__ == "__main__":
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    for tid in sys.argv[2:]:
        png(os.path.join(d, tid + ".png"), 320, 176, art(tid))
