/* Strict JPEG reader for untrusted pictures (avatars from USB sticks and phones).
 *
 * Supported: 8-bit Huffman baseline/extended (SOF0/SOF1) and progressive (SOF2), grey or YCbCr
 * (Adobe RGB too), sampling factors 1 or 2, restart intervals, EXIF orientation.
 * Refused: arithmetic coding, 12-bit, lossless, CMYK, anything malformed.
 *
 * Large pictures are not decoded in full: when the picture is bigger than IMG_MAX_PIXELS (or
 * always for progressive files) only the DC coefficient of each 8x8 block is used, which gives
 * the picture at 1/8 size directly (a 4000x3000 phone photo becomes 500x375) without ever holding
 * the full picture in memory. Every table, index, count and size is checked before use and the
 * output buffer is allocated once from the checked frame size. */
#include "image.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_MAX_SIDE 16384

static const uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

typedef struct {
    int defined;
    uint8_t vals[256];
    int mincode[17], maxcode[18], valptr[17];
    uint16_t fast[512]; /* 9-bit lookup: (length << 8) | value, 0 = slow path */
} Huff;

typedef struct {
    int id, h, v, tq;
    int td, ta;      /* current scan's DC/AC table */
    int bw, bh;      /* blocks across / down (padded to whole MCUs) */
    int pred;        /* DC predictor */
    uint8_t *plane;  /* full: (bw*8) x (bh*8); dc mode: bw x bh */
    int *dc;         /* progressive: DC value per block */
} Comp;

typedef struct {
    const uint8_t *p, *end;
    uint32_t acc;
    int n, marker;
} Bits;

typedef struct {
    uint16_t q[4][64];
    int qdef[4];
    Huff dc[4], ac[4];
    Comp c[3];
    int nc, w, h, hmax, vmax, mcux, mcuy, progressive, dconly, restart, frame, adobe, transform;
    int orientation;
} Jpeg;

static int build_huff(Huff *t, const uint8_t counts[16], const uint8_t *vals, int total) {
    memset(t, 0, sizeof(*t));
    memcpy(t->vals, vals, (size_t)total);
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        t->valptr[l] = k;
        t->mincode[l] = code;
        code += counts[l - 1];
        if (code > (1 << l))
            return -1; /* over-subscribed code */
        t->maxcode[l] = counts[l - 1] ? code - 1 : -1;
        if (l <= 9)
            for (int i = 0; i < counts[l - 1]; i++) {
                int c = t->mincode[l] + i, shift = 9 - l;
                for (int f = 0; f < (1 << shift); f++)
                    t->fast[(c << shift) | f] = (uint16_t)((l << 8) | t->vals[k + i]);
            }
        k += counts[l - 1];
        code <<= 1;
    }
    t->maxcode[17] = 0x7fffffff;
    t->defined = 1;
    return 0;
}

static void fill(Bits *b) {
    while (b->n <= 24) {
        uint32_t c = 0;
        if (!b->marker && b->p < b->end) {
            c = *b->p;
            if (c == 0xFF) {
                if (b->p + 1 < b->end && b->p[1] == 0)
                    b->p += 2;
                else {
                    b->marker = 1; /* stop at the marker, feed zeros */
                    c = 0;
                }
            } else
                b->p++;
        }
        b->acc |= c << (24 - b->n);
        b->n += 8;
    }
}
static int getbits(Bits *b, int n) {
    if (!n)
        return 0;
    fill(b);
    int v = (int)(b->acc >> (32 - n));
    b->acc <<= n;
    b->n -= n;
    return v;
}
static int decode(Bits *b, const Huff *t) {
    fill(b);
    int f = t->fast[b->acc >> 23];
    if (f) {
        int l = f >> 8;
        b->acc <<= l;
        b->n -= l;
        return f & 255;
    }
    for (int l = 10; l <= 16; l++) {
        int code = (int)(b->acc >> (32 - l));
        if (t->maxcode[l] >= 0 && code <= t->maxcode[l] && code >= t->mincode[l]) {
            b->acc <<= l;
            b->n -= l;
            int idx = t->valptr[l] + code - t->mincode[l];
            return idx < 256 ? t->vals[idx] : -1;
        }
    }
    return -1;
}
static int extend(int v, int s) {
    return s && v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}
static uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static float idct_cos[8][8];
static void idct_init(void) {
    static int done;
    if (done)
        return;
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            idct_cos[x][u] = (float)((u ? 1.0 : sqrt(0.5)) * cos((2 * x + 1) * u * 3.14159265358979323846 / 16) / 2);
    done = 1;
}
static void idct(const int in[64], uint8_t *out, int stride) {
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) { /* columns first: tmp[y][u] */
            float s = 0;
            for (int v = 0; v < 8; v++)
                s += idct_cos[y][v] * (float)in[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++)
                s += idct_cos[x][u] * tmp[y * 8 + u];
            out[y * stride + x] = clamp8((int)floorf(s + 0.5f) + 128);
        }
}

/* one block of a sequential scan */
static int block_seq(Jpeg *j, Bits *b, Comp *c, int bx, int by) {
    int coef[64] = {0};
    int t = decode(b, &j->dc[c->td]);
    if (t < 0 || t > 11)
        return -1;
    c->pred += extend(getbits(b, t), t);
    coef[0] = c->pred * j->q[c->tq][0];
    for (int k = 1; k < 64;) {
        int rs = decode(b, &j->ac[c->ta]);
        if (rs < 0)
            return -1;
        int r = rs >> 4, s = rs & 15;
        if (!s) {
            if (r != 15)
                break;
            k += 16;
            continue;
        }
        k += r;
        if (k > 63 || s > 10)
            return -1;
        if (!j->dconly)
            coef[zigzag[k]] = extend(getbits(b, s), s) * j->q[c->tq][k];
        else
            getbits(b, s);
        k++;
    }
    if (j->dconly)
        c->plane[by * c->bw + bx] = clamp8((coef[0] >> 3) + 128);
    else
        idct(coef, c->plane + (size_t)by * 8 * (c->bw * 8) + bx * 8, c->bw * 8);
    return 0;
}
/* one block of a progressive DC scan */
static int block_dc(Jpeg *j, Bits *b, Comp *c, int bx, int by, int ah, int al) {
    int *dc = &c->dc[by * c->bw + bx];
    if (!ah) {
        int t = decode(b, &j->dc[c->td]);
        if (t < 0 || t > 11)
            return -1;
        c->pred += extend(getbits(b, t), t);
        *dc = c->pred * (1 << al);
    } else if (getbits(b, 1))
        *dc |= 1 << al;
    return 0;
}
static const uint8_t *skip_entropy(const uint8_t *p, const uint8_t *end) {
    while (p + 1 < end && !(p[0] == 0xFF && p[1] != 0 && !(p[1] >= 0xD0 && p[1] <= 0xD7)))
        p++;
    return p;
}
static int restart(Bits *b, Jpeg *j) {
    /* the bit reader stopped at the marker; find and consume RSTn */
    const uint8_t *p = b->p;
    while (p + 1 < b->end && !(p[0] == 0xFF && p[1] >= 0xD0 && p[1] <= 0xD7)) {
        if (p[0] == 0xFF && p[1] != 0 && p[1] != 0xFF)
            return -1; /* some other marker: data ended early */
        p++;
    }
    if (p + 1 >= b->end)
        return -1;
    b->p = p + 2;
    b->acc = 0;
    b->n = 0;
    b->marker = 0;
    for (int i = 0; i < j->nc; i++)
        j->c[i].pred = 0;
    return 0;
}
static const uint8_t *scan(Jpeg *j, const uint8_t *p, const uint8_t *end) {
    if (end - p < 3)
        return NULL;
    int len = p[0] << 8 | p[1], ns = p[2];
    if (len != 6 + 2 * ns || ns < 1 || ns > j->nc || end - p < len)
        return NULL;
    Comp *sc[3];
    for (int i = 0; i < ns; i++) {
        int id = p[3 + 2 * i], tab = p[4 + 2 * i];
        sc[i] = NULL;
        for (int k = 0; k < j->nc; k++)
            if (j->c[k].id == id)
                sc[i] = &j->c[k];
        if (!sc[i] || (tab >> 4) > 3 || (tab & 15) > 3)
            return NULL;
        for (int k = 0; k < i; k++)
            if (sc[k] == sc[i])
                return NULL;
        sc[i]->td = tab >> 4;
        sc[i]->ta = tab & 15;
        sc[i]->pred = 0;
    }
    int ss = p[3 + 2 * ns], se = p[4 + 2 * ns], ah = p[5 + 2 * ns] >> 4, al = p[5 + 2 * ns] & 15;
    p += len;
    if (j->progressive) {
        if (ss > se || se > 63 || ah > 13 || al > 13 || (ss == 0 && se != 0) || (ss > 0 && ns != 1))
            return NULL;
        if (ss > 0) /* AC scan: not needed for the 1/8 picture */
            return skip_entropy(p, end);
    } else if (ss != 0 || se != 63 || ah || al)
        return NULL;
    for (int i = 0; i < ns; i++)
        if ((!(j->progressive && ah) && !j->dc[sc[i]->td].defined) ||
            (!j->progressive && !j->ac[sc[i]->ta].defined) || !j->qdef[sc[i]->tq])
            return NULL;
    Bits b = {p, end, 0, 0, 0};
    int count = 0;
    if (ns == 1) { /* non-interleaved: the component's own block grid */
        Comp *c = sc[0];
        int bw = (j->w * c->h / j->hmax + 7) / 8, bh = (j->h * c->v / j->vmax + 7) / 8;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                if (j->restart && count && count % j->restart == 0 && restart(&b, j))
                    return NULL;
                count++;
                if (j->progressive ? block_dc(j, &b, c, bx, by, ah, al) : block_seq(j, &b, c, bx, by))
                    return NULL;
            }
    } else
        for (int my = 0; my < j->mcuy; my++)
            for (int mx = 0; mx < j->mcux; mx++) {
                if (j->restart && count && count % j->restart == 0 && restart(&b, j))
                    return NULL;
                count++;
                for (int i = 0; i < ns; i++) {
                    Comp *c = sc[i];
                    for (int v = 0; v < c->v; v++)
                        for (int h = 0; h < c->h; h++) {
                            int bx = mx * c->h + h, by = my * c->v + v;
                            if (j->progressive ? block_dc(j, &b, c, bx, by, ah, al)
                                               : block_seq(j, &b, c, bx, by))
                                return NULL;
                        }
                }
            }
    /* continue at the next marker */
    const uint8_t *q = b.p;
    return skip_entropy(q, end);
}

static int frame(Jpeg *j, const uint8_t *p, int len, int max_pixels) {
    if (j->frame || len < 8)
        return IMG_E_FORMAT;
    int bits = p[0];
    j->h = p[1] << 8 | p[2];
    j->w = p[3] << 8 | p[4];
    j->nc = p[5];
    if (bits != 8)
        return IMG_E_UNSUPPORTED;
    if (!j->w || !j->h || j->w > JPEG_MAX_SIDE || j->h > JPEG_MAX_SIDE)
        return IMG_E_LIMIT;
    if ((j->nc != 1 && j->nc != 3) || len != 6 + 3 * j->nc)
        return j->nc == 4 ? IMG_E_UNSUPPORTED : IMG_E_FORMAT;
    j->hmax = j->vmax = 1;
    for (int i = 0; i < j->nc; i++) {
        Comp *c = &j->c[i];
        c->id = p[6 + 3 * i];
        c->h = p[7 + 3 * i] >> 4;
        c->v = p[7 + 3 * i] & 15;
        c->tq = p[8 + 3 * i];
        if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 || c->tq > 3)
            return IMG_E_UNSUPPORTED;
        for (int k = 0; k < i; k++)
            if (j->c[k].id == c->id)
                return IMG_E_FORMAT;
        j->hmax = c->h > j->hmax ? c->h : j->hmax;
        j->vmax = c->v > j->vmax ? c->v : j->vmax;
    }
    if (j->nc == 1) /* a single component is never subsampled */
        j->c[0].h = j->c[0].v = j->hmax = j->vmax = 1;
    j->mcux = (j->w + 8 * j->hmax - 1) / (8 * j->hmax);
    j->mcuy = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
    long full = (long)j->w * j->h, small = (long)((j->w + 7) / 8) * ((j->h + 7) / 8);
    j->dconly = j->progressive || full > max_pixels || j->w > IMG_MAX_SIDE || j->h > IMG_MAX_SIDE;
    if (j->dconly && small > max_pixels)
        return IMG_E_LIMIT;
    for (int i = 0; i < j->nc; i++) {
        Comp *c = &j->c[i];
        c->bw = j->mcux * c->h;
        c->bh = j->mcuy * c->v;
        size_t n = (size_t)c->bw * c->bh * (j->dconly ? 1 : 64);
        c->plane = calloc(n, 1);
        if (j->progressive)
            c->dc = calloc((size_t)c->bw * c->bh, sizeof(int));
        if (!c->plane || (j->progressive && !c->dc))
            return IMG_E_MEMORY;
    }
    j->frame = 1;
    return IMG_OK;
}

/* EXIF orientation (APP1 "Exif\0\0" + TIFF IFD0 tag 0x0112) */
static void exif(Jpeg *j, const uint8_t *p, int len) {
    if (len < 14 || memcmp(p, "Exif\0\0", 6))
        return;
    const uint8_t *t = p + 6;
    int n = len - 6, le;
    if (!memcmp(t, "II*\0", 4))
        le = 1;
    else if (!memcmp(t, "MM\0*", 4))
        le = 0;
    else
        return;
#define U16(q) (le ? (q)[0] | (q)[1] << 8 : (q)[0] << 8 | (q)[1])
#define U32(q) (le ? (uint32_t)(q)[0] | (uint32_t)(q)[1] << 8 | (uint32_t)(q)[2] << 16 | (uint32_t)(q)[3] << 24 \
                   : (uint32_t)(q)[0] << 24 | (uint32_t)(q)[1] << 16 | (uint32_t)(q)[2] << 8 | (uint32_t)(q)[3])
    uint32_t ifd = U32(t + 4);
    if (ifd > (uint32_t)n - 2)
        return;
    int count = U16(t + ifd);
    for (int i = 0; i < count; i++) {
        uint32_t e = ifd + 2 + 12 * (uint32_t)i;
        if (e + 12 > (uint32_t)n)
            return;
        if (U16(t + e) == 0x0112 && U16(t + e + 2) == 3) {
            int v = U16(t + e + 8);
            if (v >= 1 && v <= 8)
                j->orientation = v;
            return;
        }
    }
#undef U16
#undef U32
}

static int orient(OrbitImage *img, int o) {
    if (o <= 1)
        return IMG_OK;
    int w = img->w, h = img->h, swap = o >= 5;
    int nw = swap ? h : w, nh = swap ? w : h;
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px)
        return IMG_E_MEMORY;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx, dy;
            switch (o) {
            case 2: dx = w - 1 - x, dy = y; break;
            case 3: dx = w - 1 - x, dy = h - 1 - y; break;
            case 4: dx = x, dy = h - 1 - y; break;
            case 5: dx = y, dy = x; break;
            case 6: dx = h - 1 - y, dy = x; break;
            case 7: dx = h - 1 - y, dy = w - 1 - x; break;
            default: dx = y, dy = w - 1 - x; break; /* 8 */
            }
            px[(size_t)dy * nw + dx] = img->px[(size_t)y * w + x];
        }
    free(img->px);
    img->px = px;
    img->w = nw;
    img->h = nh;
    return IMG_OK;
}

static int output(Jpeg *j, OrbitImage *out) {
    int ow = j->dconly ? (j->w + 7) / 8 : j->w, oh = j->dconly ? (j->h + 7) / 8 : j->h;
    if (j->progressive)
        for (int i = 0; i < j->nc; i++) {
            Comp *c = &j->c[i];
            for (int k = 0; k < c->bw * c->bh; k++)
                c->plane[k] = clamp8(((c->dc[k] * j->q[c->tq][0]) >> 3) + 128);
        }
    out->px = malloc((size_t)ow * oh * 4);
    if (!out->px)
        return IMG_E_MEMORY;
    out->w = ow;
    out->h = oh;
    int rgb = j->nc == 3 && j->adobe && j->transform == 0;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            int v[3];
            for (int i = 0; i < j->nc; i++) {
                const Comp *c = &j->c[i];
                int stride = j->dconly ? c->bw : c->bw * 8;
                int sx = x * c->h / j->hmax, sy = y * c->v / j->vmax;
                v[i] = c->plane[(size_t)sy * stride + sx];
            }
            uint32_t r, g, b;
            if (j->nc == 1)
                r = g = b = (uint32_t)v[0];
            else if (rgb)
                r = (uint32_t)v[0], g = (uint32_t)v[1], b = (uint32_t)v[2];
            else {
                int Y = v[0] << 8, cb = v[1] - 128, cr = v[2] - 128;
                r = clamp8((Y + 359 * cr + 128) >> 8);
                g = clamp8((Y - 88 * cb - 183 * cr + 128) >> 8);
                b = clamp8((Y + 454 * cb + 128) >> 8);
            }
            out->px[(size_t)y * ow + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    return orient(out, j->orientation);
}

int image_jpeg_decode(const uint8_t *data, size_t len, OrbitImage *out, int max_pixels) {
    if (!out)
        return IMG_E_ARG;
    memset(out, 0, sizeof(*out));
    if (!data || len < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return IMG_E_FORMAT;
    if (len > IMG_MAX_UPLOAD)
        return IMG_E_LIMIT;
    if (max_pixels <= 0 || max_pixels > (int)IMG_MAX_PIXELS)
        max_pixels = (int)IMG_MAX_PIXELS;
    idct_init();
    Jpeg *j = calloc(1, sizeof(Jpeg));
    if (!j)
        return IMG_E_MEMORY;
    const uint8_t *p = data + 2, *end = data + len;
    int rc = IMG_E_FORMAT, scans = 0, done = 0;
    while (!done) {
        while (p < end && *p == 0xFF && p + 1 < end && p[1] == 0xFF)
            p++; /* fill bytes */
        if (end - p < 2 || p[0] != 0xFF) {
            if (j->frame && scans)
                break; /* data cut after the last scan: keep what was decoded */
            goto fail;
        }
        int m = p[1];
        p += 2;
        if (m == 0xD9) { /* EOI */
            done = 1;
            break;
        }
        if (m >= 0xD0 && m <= 0xD7)
            continue;
        if (end - p < 2)
            goto fail;
        int l = p[0] << 8 | p[1];
        if (l < 2 || end - p < l)
            goto fail;
        const uint8_t *s = p + 2;
        int n = l - 2;
        switch (m) {
        case 0xC0: case 0xC1: case 0xC2:
            j->progressive = m == 0xC2;
            if ((rc = frame(j, s, n, max_pixels)) != IMG_OK)
                goto fail;
            rc = IMG_E_FORMAT;
            break;
        case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB:
        case 0xCD: case 0xCE: case 0xCF:
            rc = IMG_E_UNSUPPORTED;
            goto fail;
        case 0xC4: /* DHT */
            while (n > 0) {
                if (n < 17)
                    goto fail;
                int tc = s[0] >> 4, th = s[0] & 15, total = 0;
                if (tc > 1 || th > 3)
                    goto fail;
                for (int i = 0; i < 16; i++)
                    total += s[1 + i];
                if (total > 256 || n < 17 + total ||
                    build_huff(tc ? &j->ac[th] : &j->dc[th], s + 1, s + 17, total))
                    goto fail;
                s += 17 + total;
                n -= 17 + total;
            }
            break;
        case 0xDB: /* DQT */
            while (n > 0) {
                int pq = s[0] >> 4, tq = s[0] & 15, size = pq ? 129 : 65;
                if (pq > 1 || tq > 3 || n < size)
                    goto fail;
                for (int k = 0; k < 64; k++)
                    j->q[tq][k] = pq ? (uint16_t)(s[1 + 2 * k] << 8 | s[2 + 2 * k]) : s[1 + k];
                j->qdef[tq] = 1;
                s += size;
                n -= size;
            }
            break;
        case 0xDD: /* DRI */
            if (n != 2)
                goto fail;
            j->restart = s[0] << 8 | s[1];
            break;
        case 0xDA: /* SOS */
            if (!j->frame || ++scans > 64)
                goto fail;
            p = scan(j, p, end);
            if (!p)
                goto fail;
            continue;
        case 0xE1:
            exif(j, s, n);
            break;
        case 0xEE: /* Adobe */
            if (n >= 12 && !memcmp(s, "Adobe", 5)) {
                j->adobe = 1;
                j->transform = s[11];
            }
            break;
        default:
            if (m < 0xC0)
                goto fail;
            break; /* APPn, COM, ... */
        }
        p += l;
    }
    if (!j->frame || !scans)
        goto fail;
    rc = output(j, out);
fail:
    for (int i = 0; i < 3; i++) {
        free(j->c[i].plane);
        free(j->c[i].dc);
    }
    free(j);
    if (rc != IMG_OK)
        image_free(out);
    return rc;
}
