"""Checks that the bundled font can draw every character of every language file.
Usage: python3 tools/font_coverage.py assets/OrbitSans.ttf lang/*.lang
Reads the font's character map (cmap formats 4 and 12) directly - no extra libraries.
Exit status 1 if a language file uses a character the font does not have."""
import struct
import sys


def cmap_codepoints(path):
    data = open(path, 'rb').read()
    num = struct.unpack('>H', data[4:6])[0]
    cmap = None
    for i in range(num):
        tag, _, off, _ = struct.unpack('>4sIII', data[12 + 16 * i:28 + 16 * i])
        if tag == b'cmap':
            cmap = off
    if cmap is None:
        raise SystemExit('no cmap table')
    cps = set()
    n = struct.unpack('>H', data[cmap + 2:cmap + 4])[0]
    for i in range(n):
        pid, eid, off = struct.unpack('>HHI', data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        t = cmap + off
        fmt = struct.unpack('>H', data[t:t + 2])[0]
        if fmt == 4:
            segx2 = struct.unpack('>H', data[t + 6:t + 8])[0]
            seg = segx2 // 2
            ends = struct.unpack('>%dH' % seg, data[t + 14:t + 14 + segx2])
            starts = struct.unpack('>%dH' % seg, data[t + 16 + segx2:t + 16 + 2 * segx2])
            deltas = struct.unpack('>%dh' % seg, data[t + 16 + 2 * segx2:t + 16 + 3 * segx2])
            ro_pos = t + 16 + 3 * segx2
            ranges = struct.unpack('>%dH' % seg, data[ro_pos:ro_pos + segx2])
            for k in range(seg):
                for c in range(starts[k], ends[k] + 1):
                    if c == 0xFFFF:
                        continue
                    if ranges[k] == 0:
                        g = (c + deltas[k]) & 0xFFFF
                    else:
                        p = ro_pos + 2 * k + ranges[k] + 2 * (c - starts[k])
                        g = struct.unpack('>H', data[p:p + 2])[0]
                        if g:
                            g = (g + deltas[k]) & 0xFFFF
                    if g:
                        cps.add(c)
        elif fmt == 12:
            groups = struct.unpack('>I', data[t + 12:t + 16])[0]
            for k in range(groups):
                s, e, g = struct.unpack('>III', data[t + 16 + 12 * k:t + 28 + 12 * k])
                for c in range(s, e + 1):
                    if g + (c - s):
                        cps.add(c)
    return cps


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    have = cmap_codepoints(sys.argv[1])
    bad = 0
    for path in sys.argv[2:]:
        text = open(path, encoding='utf-8').read()
        used = sorted({ord(ch) for line in text.splitlines() if not line.lstrip().startswith('#')
                       for ch in line.split('=', 1)[-1] if ord(ch) >= 0x20})
        missing = [c for c in used if c not in have]
        if missing:
            bad = 1
            print('%s: %d characters, MISSING in the font: %s' % (
                path, len(used), ' '.join('U+%04X(%s)' % (c, chr(c)) for c in missing[:20])))
        else:
            print('%s: all %d characters are in the font' % (path, len(used)))
    sys.exit(bad)


if __name__ == '__main__':
    main()
