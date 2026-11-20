/* ORBIT NEXT: strict GIF reader, see include/gif.h */
#include "gif.h"

#include <stdlib.h>
#include <string.h>

#include "image.h"

typedef struct {
    const uint8_t *p, *end;
} Rd;

static int rd_byte(Rd *r, uint8_t *v) {
    if (r->p >= r->end)
        return -1;
    *v = *r->p++;
    return 0;
}
static int rd_u16(Rd *r, unsigned *v) {
    if (r->end - r->p < 2)
        return -1;
    *v = (unsigned)r->p[0] | (unsigned)r->p[1] << 8;
    r->p += 2;
    return 0;
}
static int rd_skip_blocks(Rd *r) {
    for (;;) {
        uint8_t n;
        if (rd_byte(r, &n))
            return -1;
        if (!n)
            return 0;
        if (r->end - r->p < n)
            return -1;
        r->p += n;
    }
}

int gif_sniff(const uint8_t *d, size_t len) {
    return len >= 6 && !memcmp(d, "GIF", 3) && (!memcmp(d + 3, "87a", 3) || !memcmp(d + 3, "89a", 3));
}

/* LZW decode of one image's sub-blocks into out[0..need). Returns 0 when exactly `need`
 * pixels were produced (extra codes are ignored), -1 on any error or short data. */
typedef struct {
    uint16_t prefix[4096];
    uint8_t suffix[4096], first[4096];
    uint8_t stack[4097];
} Lzw;

static int lzw_decode(Rd *r, int min_size, uint8_t *out, size_t need, Lzw *z) {
    if (min_size < 2 || min_size > 8)
        return -1;
    const int clear = 1 << min_size, eoi = clear + 1;
    int size = min_size + 1, next = eoi + 1, old = -1, done = 0;
    uint32_t bits = 0;
    int nbits = 0;
    size_t pos = 0;
    for (int i = 0; i < clear; i++) {
        z->prefix[i] = 0xFFFF;
        z->suffix[i] = z->first[i] = (uint8_t)i;
    }
    for (;;) {
        uint8_t n;
        if (rd_byte(r, &n))
            return -1;
        if (!n)
            break;
        if (r->end - r->p < n)
            return -1;
        const uint8_t *b = r->p;
        r->p += n;
        for (int k = 0; k < n && !done; k++) {
            bits |= (uint32_t)b[k] << nbits;
            nbits += 8;
            while (nbits >= size && !done) {
                int code = (int)(bits & ((1u << size) - 1));
                bits >>= size;
                nbits -= size;
                if (code == clear) {
                    size = min_size + 1;
                    next = eoi + 1;
                    old = -1;
                    continue;
                }
                if (code == eoi) {
                    done = 1;
                    break;
                }
                int in = code, sp = 0;
                if (old < 0) {
                    if (code >= clear)
                        return -1; /* first code after a clear must be a literal */
                } else if (code > next || (code == next && next >= 4096))
                    return -1;
                if (old >= 0 && code == next) { /* KwKwK case */
                    z->stack[sp++] = z->first[old];
                    in = old;
                }
                while (in >= clear) { /* the chain only ever points to smaller codes */
                    if (sp >= 4096)
                        return -1;
                    z->stack[sp++] = z->suffix[in];
                    in = z->prefix[in];
                }
                z->stack[sp++] = (uint8_t)in;
                uint8_t lead = (uint8_t)in;
                while (sp > 0) {
                    uint8_t v = z->stack[--sp];
                    if (pos < need)
                        out[pos++] = v;
                }
                if (old >= 0 && next < 4096) {
                    z->prefix[next] = (uint16_t)old;
                    z->suffix[next] = lead;
                    z->first[next] = z->first[old];
                    next++;
                    if (next == 1 << size && size < 12)
                        size++;
                }
                old = code;
            }
        }
        if (done) /* skip whatever sub-blocks follow the end code */
            return rd_skip_blocks(r) || pos < need ? -1 : 0;
    }
    return pos < need ? -1 : 0;
}

static void read_palette(const uint8_t *src, int n, uint32_t pal[256]) {
    for (int i = 0; i < 256; i++)
        pal[i] = 0xFF000000u; /* indices past the table: opaque black */
    for (int i = 0; i < n; i++)
        pal[i] = 0xFF000000u | (uint32_t)src[3 * i] << 16 | (uint32_t)src[3 * i + 1] << 8 | src[3 * i + 2];
}

int gif_decode(const uint8_t *data, size_t len, gif_frame_fn fn, void *ctx) {
    if (!data || !fn)
        return IMG_E_ARG;
    if (len > GIF_MAX_FILE)
        return IMG_E_LIMIT;
    if (len < 13 || !gif_sniff(data, len))
        return IMG_E_FORMAT;
    Rd r = {data + 6, data + len};
    unsigned W, H;
    uint8_t flags, bg, aspect;
    if (rd_u16(&r, &W) || rd_u16(&r, &H) || rd_byte(&r, &flags) || rd_byte(&r, &bg) || rd_byte(&r, &aspect))
        return IMG_E_FORMAT;
    (void)bg; /* disposal 2 clears to transparent, as browsers do */
    (void)aspect;
    if (!W || !H)
        return IMG_E_FORMAT;
    if (W > GIF_MAX_SIDE || H > GIF_MAX_SIDE || W * H > GIF_MAX_AREA)
        return IMG_E_LIMIT;
    uint32_t gpal[256], lpal[256];
    int have_global = flags & 0x80;
    if (have_global) {
        int n = 2 << (flags & 7);
        if (r.end - r.p < 3 * n)
            return IMG_E_FORMAT;
        read_palette(r.p, n, gpal);
        r.p += 3 * n;
    }
    size_t area = (size_t)W * H;
    uint32_t *canvas = calloc(area, 4), *saved = NULL;
    uint8_t *idx = malloc(area);
    Lzw *z = malloc(sizeof(Lzw));
    int rc = IMG_E_MEMORY, frames = 0, stop = 0;
    if (!canvas || !idx || !z)
        goto out;
    int disposal = 0, delay = 0, transparent = -1;
    uint64_t work = 0;
    rc = IMG_E_FORMAT;
    while (!stop) {
        uint8_t tag;
        if (rd_byte(&r, &tag))
            goto out; /* no trailer: truncated */
        if (tag == 0x3B)
            break;
        if (tag == 0x21) {
            uint8_t label;
            if (rd_byte(&r, &label))
                goto out;
            if (label == 0xF9) {
                uint8_t sz, pk, ti, term;
                unsigned d;
                if (rd_byte(&r, &sz) || sz != 4 || rd_byte(&r, &pk) || rd_u16(&r, &d) || rd_byte(&r, &ti) ||
                    rd_byte(&r, &term) || term)
                    goto out;
                disposal = (pk >> 2) & 7;
                if (disposal > 3)
                    disposal = 0; /* reserved values: no disposal */
                transparent = pk & 1 ? ti : -1;
                delay = (int)d * 10;
            } else if (rd_skip_blocks(&r))
                goto out;
            continue;
        }
        if (tag != 0x2C)
            goto out;
        unsigned x, y, w, h;
        uint8_t f;
        if (rd_u16(&r, &x) || rd_u16(&r, &y) || rd_u16(&r, &w) || rd_u16(&r, &h) || rd_byte(&r, &f))
            goto out;
        if (!w || !h || x + w > W || y + h > H)
            goto out; /* frame rectangle outside the canvas */
        if (++frames > GIF_MAX_SRC_FRAMES || (work += (uint64_t)w * h) > GIF_MAX_WORK) {
            rc = IMG_E_LIMIT;
            goto out;
        }
        const uint32_t *pal = gpal;
        if (f & 0x80) {
            int n = 2 << (f & 7);
            if (r.end - r.p < 3 * n)
                goto out;
            read_palette(r.p, n, lpal);
            r.p += 3 * n;
            pal = lpal;
        } else if (!have_global)
            goto out;
        uint8_t mcs;
        if (rd_byte(&r, &mcs) || lzw_decode(&r, mcs, idx, (size_t)w * h, z)) {
            rc = IMG_E_DATA;
            goto out;
        }
        if (disposal == 3) { /* keep what is under the frame for "restore previous" */
            if (!saved && !(saved = malloc(area * 4))) {
                rc = IMG_E_MEMORY;
                goto out;
            }
            memcpy(saved, canvas, area * 4);
        }
        int interlaced = f & 0x40;
        static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        int pass = 0, row = 0;
        for (unsigned j = 0; j < h; j++) {
            unsigned ty;
            if (interlaced) {
                while (pass < 4 && start[pass] + row * step[pass] >= (int)h) {
                    pass++;
                    row = 0;
                }
                if (pass >= 4)
                    goto out;
                ty = (unsigned)(start[pass] + row * step[pass]);
                row++;
            } else
                ty = j;
            const uint8_t *src = idx + (size_t)j * w;
            uint32_t *dst = canvas + (size_t)(y + ty) * W + x;
            for (unsigned i = 0; i < w; i++)
                if (src[i] != transparent)
                    dst[i] = pal[src[i]];
        }
        int shown = delay < 20 ? 100 : delay;
        stop = fn(ctx, canvas, (int)W, (int)H, frames - 1, shown);
        /* disposal before the next frame */
        if (disposal == 2)
            for (unsigned j = 0; j < h; j++)
                memset(canvas + (size_t)(y + j) * W + x, 0, (size_t)w * 4);
        else if (disposal == 3 && saved)
            memcpy(canvas, saved, area * 4);
        disposal = 0;
        delay = 0;
        transparent = -1;
    }
    rc = frames ? frames : IMG_E_FORMAT;
out:
    free(canvas);
    free(saved);
    free(idx);
    free(z);
    return rc;
}
