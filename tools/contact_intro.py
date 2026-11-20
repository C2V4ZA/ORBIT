"""Contact sheet of intro key frames: build/intro/f*.ppm -> build/intro-contact.png (3x2, half size)."""
import pathlib, struct, sys, zlib
def read(p):
    with open(p, 'rb') as f:
        assert f.readline() == b'P6\n'
        w, h = map(int, f.readline().split()); f.readline()
        return w, h, f.read()
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
frames = [int(x) for x in (sys.argv[1:] or ['12', '30', '45', '58', '70', '92'])]
W, H = 640, 360
sheet = bytearray(W * 3 * H * 2 * 3)
for k, n in enumerate(frames):
    w, h, px = read(f'build/intro/f{n:03d}.ppm')
    ox, oy = (k % 3) * W, (k // 3) * H
    for y in range(H):
        for x in range(W):
            s = ((y * 2) * w + x * 2) * 3
            d = ((oy + y) * W * 3 + ox + x) * 3
            sheet[d:d + 3] = px[s:s + 3]
rows = b''.join(b'\0' + bytes(sheet[y * W * 9:(y + 1) * W * 9]) for y in range(H * 2))
pathlib.Path('build/intro-contact.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * 3, H * 2, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
