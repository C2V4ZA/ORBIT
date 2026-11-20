/* Strict PNG decoder (8-bit grey, RGB, palette, grey+alpha, RGBA; 1/2/4-bit palette and grey;
 * no interlace, no 16-bit). Untrusted input: every length is checked before use. */
#include "image.h"
#include "gif.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
void image_free(OrbitImage *img) {
    if (!img)
        return;
    free(img->px);
    img->px = NULL;
    img->w = img->h = 0;
}
static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
int image_png_decode(const uint8_t *data, size_t len, OrbitImage *out, int max_w, int max_h) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!data || !out)
        return IMG_E_ARG;
    memset(out, 0, sizeof(*out));
    if (max_w <= 0 || max_w > IMG_MAX_SIDE)
        max_w = IMG_MAX_SIDE;
    if (max_h <= 0 || max_h > IMG_MAX_SIDE)
        max_h = IMG_MAX_SIDE;
    if (len > IMG_MAX_FILE)
        return IMG_E_LIMIT;
    if (len < 8 + 25 || memcmp(data, sig, 8))
        return IMG_E_FORMAT;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, have_ihdr = 0, have_end = 0, idat_done = 0, idat_seen = 0;
    uint32_t pal[256];
    int npal = 0;
    for (int i = 0; i < 256; i++)
        pal[i] = 0xff000000u;
    int trns_grey = -1, trns_r = -1, trns_g = -1, trns_b = -1;
    uint8_t *raw = NULL;
    size_t raw_len = 0, stride = 0;
    int channels = 0, bpp = 0, rc = IMG_E_DATA;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int z_open = 0, z_end = 0;
    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t clen = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        if (clen > len - pos - 12) {
            rc = IMG_E_FORMAT;
            goto fail;
        }
        const uint8_t *body = data + pos + 8;
        uint32_t crc = (uint32_t)crc32(0L, type, 4 + clen);
        if (crc != be32(body + clen)) {
            rc = IMG_E_DATA;
            goto fail;
        }
        pos += 12 + (size_t)clen;
        if (!have_ihdr && memcmp(type, "IHDR", 4)) {
            rc = IMG_E_FORMAT;
            goto fail;
        }
        if (!memcmp(type, "IHDR", 4)) {
            if (have_ihdr || clen != 13) {
                rc = IMG_E_FORMAT;
                goto fail;
            }
            have_ihdr = 1;
            w = be32(body);
            h = be32(body + 4);
            depth = body[8];
            ctype = body[9];
            if (body[10] || body[11]) {
                rc = IMG_E_FORMAT;
                goto fail;
            }
            if (body[12]) { /* interlaced */
                rc = IMG_E_UNSUPPORTED;
                goto fail;
            }
            if (!w || !h || w > (uint32_t)max_w || h > (uint32_t)max_h ||
                (uint64_t)w * h > IMG_MAX_PIXELS) {
                rc = IMG_E_LIMIT;
                goto fail;
            }
            channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
            int ok_depth = depth == 8 || ((ctype == 0 || ctype == 3) && (depth == 1 || depth == 2 || depth == 4));
            if (!channels || !ok_depth) {
                rc = IMG_E_UNSUPPORTED;
                goto fail;
            }
            bpp = (channels * depth + 7) / 8;
            stride = ((size_t)w * channels * depth + 7) / 8;
            raw_len = (stride + 1) * h;
            raw = malloc(raw_len);
            if (!raw) {
                rc = IMG_E_MEMORY;
                goto fail;
            }
            if (inflateInit(&zs) != Z_OK) {
                rc = IMG_E_MEMORY;
                goto fail;
            }
            z_open = 1;
            zs.next_out = raw;
            zs.avail_out = (uInt)raw_len;
        } else if (!memcmp(type, "PLTE", 4)) {
            if (idat_seen || clen % 3 || clen / 3 > 256 || !clen) {
                rc = IMG_E_FORMAT;
                goto fail;
            }
            npal = (int)(clen / 3);
            for (int i = 0; i < npal; i++)
                pal[i] = 0xff000000u | (uint32_t)body[i * 3] << 16 | (uint32_t)body[i * 3 + 1] << 8 | body[i * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            if (ctype == 3) {
                if (clen > (uint32_t)npal) {
                    rc = IMG_E_FORMAT;
                    goto fail;
                }
                for (uint32_t i = 0; i < clen; i++)
                    pal[i] = (pal[i] & 0xffffffu) | (uint32_t)body[i] << 24;
            } else if (ctype == 0 && clen == 2)
                trns_grey = body[1];
            else if (ctype == 2 && clen == 6)
                trns_r = body[1], trns_g = body[3], trns_b = body[5];
        } else if (!memcmp(type, "IDAT", 4)) {
            if (idat_done || z_end) {
                if (z_end && clen == 0)
                    continue;
                rc = idat_done ? IMG_E_FORMAT : IMG_E_DATA;
                goto fail;
            }
            if (ctype == 3 && !npal) {
                rc = IMG_E_FORMAT;
                goto fail;
            }
            idat_seen = 1;
            zs.next_in = (Bytef *)body;
            zs.avail_in = clen;
            while (zs.avail_in) {
                int r = inflate(&zs, Z_NO_FLUSH);
                if (r == Z_STREAM_END) {
                    z_end = 1;
                    break;
                }
                if (r != Z_OK) { /* Z_BUF_ERROR: more data than the image can hold - refuse */
                    rc = IMG_E_DATA;
                    goto fail;
                }
            }
        } else if (!memcmp(type, "IEND", 4)) {
            have_end = 1;
            break;
        } else {
            if (idat_seen && !idat_done)
                idat_done = 1;
            if (!(type[0] & 0x20)) { /* unknown critical chunk */
                rc = IMG_E_UNSUPPORTED;
                goto fail;
            }
            continue;
        }
        if (idat_seen && memcmp(type, "IDAT", 4))
            idat_done = 1;
    }
    if (!have_end || !idat_seen || !z_end || zs.total_out != raw_len) {
        rc = IMG_E_DATA;
        goto fail;
    }
    inflateEnd(&zs);
    z_open = 0;
    /* unfilter in place */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1), *cur = row + 1;
        const uint8_t *prev = y ? raw + (y - 1) * (stride + 1) + 1 : NULL;
        int f = row[0];
        if (f > 4) {
            rc = IMG_E_DATA;
            goto fail;
        }
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0,
                c = prev && x >= (size_t)bpp ? prev[x - bpp] : 0;
            int v = cur[x];
            switch (f) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: break;
            }
            cur[x] = (uint8_t)v;
        }
    }
    out->px = malloc((size_t)w * h * 4);
    if (!out->px) {
        rc = IMG_E_MEMORY;
        goto fail;
    }
    out->w = (int)w;
    out->h = (int)h;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = raw + y * (stride + 1) + 1;
        uint32_t *d = out->px + (size_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t r, g, b, a = 255;
            if (depth < 8) {
                int per = 8 / depth, shift = (per - 1 - (int)(x % per)) * depth;
                int v = (s[x / per] >> shift) & ((1 << depth) - 1);
                if (ctype == 3) {
                    d[x] = v < npal ? pal[v] : 0xff000000u;
                    continue;
                }
                int grey = v * 255 / ((1 << depth) - 1);
                d[x] = 0xff000000u | (uint32_t)grey * 0x010101u;
                if (v == trns_grey)
                    d[x] &= 0xffffffu;
                continue;
            }
            switch (ctype) {
            case 0: r = g = b = s[x]; a = s[x] == trns_grey ? 0 : 255; break;
            case 2:
                r = s[x * 3], g = s[x * 3 + 1], b = s[x * 3 + 2];
                if ((int)r == trns_r && (int)g == trns_g && (int)b == trns_b)
                    a = 0;
                break;
            case 3: d[x] = s[x] < npal ? pal[s[x]] : 0xff000000u; continue;
            case 4: r = g = b = s[x * 2], a = s[x * 2 + 1]; break;
            default: r = s[x * 4], g = s[x * 4 + 1], b = s[x * 4 + 2], a = s[x * 4 + 3]; break;
            }
            d[x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    free(raw);
    return IMG_OK;
fail:
    if (z_open)
        inflateEnd(&zs);
    free(raw);
    image_free(out);
    return rc;
}
int image_load_png(const char *path, OrbitImage *out, int max_w, int max_h) {
    if (!path || !out)
        return IMG_E_ARG;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f)
        return IMG_E_IO;
    uint8_t *buf = NULL;
    size_t cap = 0, n = 0;
    int rc = IMG_OK;
    for (;;) {
        if (n == cap) {
            if (cap >= IMG_MAX_FILE) {
                rc = IMG_E_LIMIT;
                break;
            }
            size_t ncap = cap ? cap * 2 : 65536;
            if (ncap > IMG_MAX_FILE + 1)
                ncap = IMG_MAX_FILE + 1;
            uint8_t *nb = realloc(buf, ncap);
            if (!nb) {
                rc = IMG_E_MEMORY;
                break;
            }
            buf = nb;
            cap = ncap;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        if (!got)
            break;
        n += got;
    }
    fclose(f);
    if (rc == IMG_OK)
        rc = n > IMG_MAX_FILE ? IMG_E_LIMIT : image_png_decode(buf, n, out, max_w, max_h);
    free(buf);
    return rc;
}
int image_scale(const OrbitImage *src, OrbitImage *dst, int w, int h) {
    if (!src || !src->px || !dst || w <= 0 || h <= 0 || w > IMG_MAX_SIDE || h > IMG_MAX_SIDE)
        return IMG_E_ARG;
    memset(dst, 0, sizeof(*dst));
    dst->px = malloc((size_t)w * h * 4);
    if (!dst->px)
        return IMG_E_MEMORY;
    dst->w = w;
    dst->h = h;
    for (int y = 0; y < h; y++) {
        int y0 = (int)((int64_t)y * src->h / h), y1 = (int)((int64_t)(y + 1) * src->h / h);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < w; x++) {
            int x0 = (int)((int64_t)x * src->w / w), x1 = (int)((int64_t)(x + 1) * src->w / w);
            if (x1 <= x0)
                x1 = x0 + 1;
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
            for (int j = y0; j < y1 && j < src->h; j++)
                for (int i = x0; i < x1 && i < src->w; i++) {
                    uint32_t p = src->px[(size_t)j * src->w + i], a = p >> 24;
                    sa += a;
                    sr += (p >> 16 & 255) * a;
                    sg += (p >> 8 & 255) * a;
                    sb += (p & 255) * a;
                    n++;
                }
            uint32_t r = sa ? sr / sa : 0, g = sa ? sg / sa : 0, b = sa ? sb / sa : 0, a = n ? sa / n : 0;
            dst->px[(size_t)y * w + x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    return IMG_OK;
}
int image_cover(const OrbitImage *src, OrbitImage *dst, int w, int h) {
    return image_cover_at(src, dst, w, h, 0);
}
int image_cover_at(const OrbitImage *src, OrbitImage *dst, int w, int h, int anchor) {
    if (!src || !src->px || !dst || w <= 0 || h <= 0)
        return IMG_E_ARG;
    /* crop the source to the target aspect, centred, then scale */
    int cw = src->w, ch = src->h;
    if ((int64_t)src->w * h > (int64_t)src->h * w)
        cw = (int)((int64_t)src->h * w / h);
    else
        ch = (int)((int64_t)src->w * h / w);
    if (cw < 1)
        cw = 1;
    if (ch < 1)
        ch = 1;
    OrbitImage crop = {cw, ch, malloc((size_t)cw * ch * 4)};
    if (!crop.px)
        return IMG_E_MEMORY;
    int ox = anchor == 1 ? 0 : anchor == 2 ? src->w - cw : (src->w - cw) / 2;
    int oy = anchor == 1 ? 0 : anchor == 2 ? src->h - ch : (src->h - ch) / 2;
    for (int y = 0; y < ch; y++)
        memcpy(crop.px + (size_t)y * cw, src->px + (size_t)(y + oy) * src->w + ox, (size_t)cw * 4);
    int rc = image_scale(&crop, dst, w, h);
    image_free(&crop);
    return rc;
}
/* GIF: the first frame only (animations go through anim.h) */
static int first_frame(void *ctx, const uint32_t *canvas, int w, int h, int index, int delay) {
    OrbitImage *o = ctx;
    (void)index;
    (void)delay;
    o->px = malloc((size_t)w * h * 4);
    if (o->px) {
        memcpy(o->px, canvas, (size_t)w * h * 4);
        o->w = w;
        o->h = h;
    }
    return 1;
}
int image_gif_first(const uint8_t *data, size_t len, OrbitImage *out) {
    memset(out, 0, sizeof(*out));
    int rc = gif_decode(data, len, first_frame, out);
    if (rc < 0)
        return rc;
    return out->px ? IMG_OK : IMG_E_MEMORY;
}
/* 1.2: pictures the user brings (USB stick, phone upload): PNG, JPEG or GIF, sniffed by content */
int image_decode_any(const uint8_t *data, size_t len, OrbitImage *out) {
    if (!data || !out)
        return IMG_E_ARG;
    memset(out, 0, sizeof(*out));
    if (len >= 8 && data[0] == 137 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return image_png_decode(data, len, out, 0, 0);
    if (len >= 4 && data[0] == 0xFF && data[1] == 0xD8)
        return image_jpeg_decode(data, len, out, 0);
    if (gif_sniff(data, len))
        return image_gif_first(data, len, out);
    return IMG_E_FORMAT;
}
int image_load_any(const char *path, OrbitImage *out) {
    if (!path || !out)
        return IMG_E_ARG;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f)
        return IMG_E_IO;
    int rc = IMG_E_LIMIT;
    uint8_t *buf = NULL;
    long size = -1;
    if (!fseek(f, 0, SEEK_END))
        size = ftell(f);
    if (size > 0 && (unsigned long)size <= IMG_MAX_UPLOAD && !fseek(f, 0, SEEK_SET)) {
        buf = malloc((size_t)size);
        rc = !buf ? IMG_E_MEMORY
             : fread(buf, 1, (size_t)size, f) != (size_t)size ? IMG_E_IO
                                                               : image_decode_any(buf, (size_t)size, out);
    } else if (size <= 0)
        rc = IMG_E_IO;
    fclose(f);
    free(buf);
    return rc;
}
