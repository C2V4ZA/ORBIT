"""Builds the GIF test fixtures with Pillow (an independent encoder/decoder) and writes Pillow's
own decoding of every frame as reference data (<name>.ref: one CRC-32 per frame).
Run from the repository root: python tests/fixtures/make_gifs.py"""
import zlib
from PIL import Image, ImageDraw

D = 'tests/fixtures/'


def ref(name):
    """one line per frame: CRC-32 of the RGBA canvas, fully transparent pixels as 0,0,0,0"""
    im = Image.open(D + name)
    lines = []
    try:
        while True:
            px = bytearray(im.convert('RGBA').tobytes())
            for i in range(0, len(px), 4):
                if px[i + 3] == 0:
                    px[i:i + 4] = bytes(4)
            lines.append('%08x' % zlib.crc32(bytes(px)))
            im.seek(im.tell() + 1)
    except EOFError:
        pass
    with open(D + name.replace('.gif', '.ref'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return len(lines)


def palette_frames(w, h, n, step):
    frames = []
    for i in range(n):
        im = Image.new('P', (w, h), 0)
        pal = []
        for k in range(256):
            pal += [(k * 37) % 256, (k * 91) % 256, (k * 13) % 256]
        im.putpalette(pal)
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, w - 1, h - 1], fill=1 + i % 3)
        x = (i * step) % (w - 20)
        d.rectangle([x, 10, x + 19, 29], fill=10 + i)
        d.ellipse([w // 2 - 15, h // 2 - 15, w // 2 + 15, h // 2 + 15], fill=200)
        frames.append(im)
    return frames


# 1. animation: 40 frames at 50 ms (20 fps) -> must be reduced to <= 10 fps
f = palette_frames(200, 150, 40, 7)
f[0].save(D + 'anim.gif', save_all=True, append_images=f[1:], duration=50, loop=0, optimize=False,
          disposal=1)
# 2. interlaced still picture
f = palette_frames(64, 48, 1, 3)
f[0].save(D + 'interlaced.gif', interlace=True)
# 3. transparency + disposal 2 (restore background) with small frame rectangles (optimize=True)
fr = []
for i in range(6):
    im = Image.new('RGBA', (80, 60), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle([i * 10, 5, i * 10 + 15, 25], fill=(255, 40 * i, 0, 255))
    fr.append(im)
fr[0].save(D + 'transparent.gif', save_all=True, append_images=fr[1:], duration=120, loop=0,
           disposal=2, transparency=0, optimize=True)
# 4. long animation: 300 frames at 100 ms (30 s) -> cut to 15 s / 150 frames
f = palette_frames(120, 120, 300, 3)
f[0].save(D + 'long.gif', save_all=True, append_images=f[1:], duration=100, loop=0, optimize=False)
for n in ('anim.gif', 'interlaced.gif', 'transparent.gif'):
    print(n, ref(n), 'frames')
# 5. noise: hardly compressible, so its animation needs several 32 KB pieces (chunked transfer test)
import random
rnd = random.Random(7)
fr = []
for i in range(30):
    im = Image.new('P', (96, 96))
    im.putpalette([rnd.randrange(256) for _ in range(768)])
    im.putdata([rnd.randrange(256) for _ in range(96 * 96)])
    fr.append(im)
fr[0].save(D + 'noise.gif', save_all=True, append_images=fr[1:], duration=100, loop=0, optimize=False)
