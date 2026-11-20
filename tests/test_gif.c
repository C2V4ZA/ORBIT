/* GIF reader and ORBIT animations: reference decoding (Pillow), budgets, malicious files, fuzzing.
 * Built with AddressSanitizer + UBSan (see Makefile). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "anim.h"
#include "avatar.h"
#include "gif.h"
#include "image.h"

static uint8_t *load(const char *name, size_t *n) {
    char path[256];
    snprintf(path, sizeof(path), "tests/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 1);
    assert(fread(b, 1, *n, f) == *n);
    fclose(f);
    b[*n] = 0;
    return b;
}

/* ---------- reference comparison ---------- */
typedef struct {
    unsigned crc[400];
    int n, w, h, delay[400];
} Seen;
static int see(void *ctx, const uint32_t *c, int w, int h, int index, int delay) {
    Seen *s = ctx;
    assert(index == s->n);
    uint8_t *rgba = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint32_t p = c[i];
        uint8_t a = p >> 24;
        rgba[4 * i] = a ? (uint8_t)(p >> 16) : 0;
        rgba[4 * i + 1] = a ? (uint8_t)(p >> 8) : 0;
        rgba[4 * i + 2] = a ? (uint8_t)p : 0;
        rgba[4 * i + 3] = a;
    }
    if (s->n < 400) {
        s->crc[s->n] = (unsigned)crc32(0, rgba, (uInt)(w * h * 4));
        s->delay[s->n] = delay;
    }
    s->n++;
    s->w = w;
    s->h = h;
    free(rgba);
    return 0;
}
static void compare_ref(const char *gif, const char *refname, int w, int h) {
    size_t n, rn;
    uint8_t *g = load(gif, &n);
    char *r = (char *)load(refname, &rn);
    Seen s;
    memset(&s, 0, sizeof s);
    int frames = gif_decode(g, n, see, &s);
    assert(frames == s.n && s.w == w && s.h == h);
    int k = 0;
    for (char *line = strtok(r, "\n"); line; line = strtok(NULL, "\n"), k++) {
        assert(k < s.n);
        unsigned want = (unsigned)strtoul(line, NULL, 16);
        if (s.crc[k] != want) {
            fprintf(stderr, "%s frame %d differs from Pillow\n", gif, k);
            assert(0);
        }
    }
    assert(k == s.n);
    free(g);
    free(r);
}

/* ---------- a small LZW encoder for hand-made files ---------- */
typedef struct {
    uint8_t *b;
    size_t n, cap;
} Buf;
static void put(Buf *b, const void *p, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->b = realloc(b->b, b->cap);
    }
    memcpy(b->b + b->n, p, n);
    b->n += n;
}
static void put8(Buf *b, int v) {
    uint8_t c = (uint8_t)v;
    put(b, &c, 1);
}
static void put16le(Buf *b, int v) {
    put8(b, v & 255);
    put8(b, v >> 8 & 255);
}
static void lzw_encode(Buf *out, const uint8_t *px, size_t n, int m) {
    static int16_t dict[4096][256];
    memset(dict, -1, sizeof dict);
    int clear = 1 << m, size = m + 1, next = clear + 2;
    uint32_t acc = 0;
    int bits = 0;
    uint8_t blk[256];
    int bl = 0;
#define EMIT(code)                                                                                 \
    do {                                                                                           \
        acc |= (uint32_t)(code) << bits;                                                           \
        bits += size;                                                                              \
        while (bits >= 8) {                                                                        \
            blk[bl++] = (uint8_t)acc;                                                              \
            acc >>= 8;                                                                             \
            bits -= 8;                                                                             \
            if (bl == 255) {                                                                       \
                put8(out, 255);                                                                    \
                put(out, blk, 255);                                                                \
                bl = 0;                                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)
    put8(out, m);
    EMIT(clear);
    int cur = px[0];
    for (size_t i = 1; i < n; i++) {
        int c = px[i];
        if (dict[cur][c] >= 0) {
            cur = dict[cur][c];
            continue;
        }
        EMIT(cur);
        if (next < 4096) {
            dict[cur][c] = (int16_t)next++;
            if (next > (1 << size) && size < 12)
                size++;
        } else { /* table full: clear */
            EMIT(clear);
            memset(dict, -1, sizeof dict);
            size = m + 1;
            next = clear + 2;
        }
        cur = c;
    }
    EMIT(cur);
    EMIT(clear + 1);
    if (bits)
        blk[bl++] = (uint8_t)acc;
    if (bl) {
        put8(out, bl);
        put(out, blk, (size_t)bl);
    }
    put8(out, 0);
#undef EMIT
}
static void gif_head(Buf *b, int w, int h) {
    put(b, "GIF89a", 6);
    put16le(b, w);
    put16le(b, h);
    put8(b, 0x80 | 1); /* global table, 4 colours */
    put8(b, 0);
    put8(b, 0);
    static const uint8_t pal[12] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    put(b, pal, 12);
}
static void gif_frame(Buf *b, int x, int y, int w, int h, int colour, int delay_cs) {
    put8(b, 0x21);
    put8(b, 0xF9);
    put8(b, 4);
    put8(b, 0);
    put16le(b, delay_cs);
    put8(b, 0);
    put8(b, 0);
    put8(b, 0x2C);
    put16le(b, x);
    put16le(b, y);
    put16le(b, w);
    put16le(b, h);
    put8(b, 0);
    size_t n = (size_t)w * h;
    uint8_t *px = malloc(n);
    for (size_t i = 0; i < n; i++)
        px[i] = (uint8_t)((colour + (i / 7)) & 3);
    lzw_encode(b, px, n, 2);
    free(px);
}
static int count(void *ctx, const uint32_t *c, int w, int h, int i, int d) {
    (void)c, (void)w, (void)h, (void)i, (void)d;
    (*(int *)ctx)++;
    return 0;
}
static int decode(const Buf *b) {
    int frames = 0;
    return gif_decode(b->b, b->n, count, &frames);
}

static void test_reference(void) {
    compare_ref("anim.gif", "anim.ref", 200, 150);
    compare_ref("interlaced.gif", "interlaced.ref", 64, 48);
    compare_ref("transparent.gif", "transparent.ref", 80, 60);
    /* hand-made file through our encoder: exact pixels */
    Buf b = {0};
    gif_head(&b, 30, 20);
    gif_frame(&b, 0, 0, 30, 20, 1, 10);
    gif_frame(&b, 5, 5, 10, 10, 2, 0); /* delay 0 counts as 100 ms */
    put8(&b, 0x3B);
    Seen s;
    memset(&s, 0, sizeof s);
    assert(gif_decode(b.b, b.n, see, &s) == 2 && s.delay[0] == 100 && s.delay[1] == 100);
    OrbitImage img;
    assert(image_decode_any(b.b, b.n, &img) == IMG_OK && img.w == 30 && img.h == 20);
    assert(img.px[0] == 0xFFFF0000u); /* colour 1 = red */
    image_free(&img);
    free(b.b);
    printf("PASS: GIF reader matches Pillow frame by frame (animation, interlaced, transparency + disposal), "
           "first frame as still picture\n");
}

static void test_malicious(void) {
    Buf b = {0};
    /* canvas too big */
    gif_head(&b, 2000, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_LIMIT);
    b.n = 0;
    gif_head(&b, 1024, 1025);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_LIMIT);
    /* zero size, no frames, no trailer */
    b.n = 0;
    gif_head(&b, 0, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    b.n = 0;
    gif_head(&b, 10, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    b.n = 0;
    gif_head(&b, 10, 10);
    gif_frame(&b, 0, 0, 10, 10, 1, 10);
    assert(decode(&b) < 0); /* truncated: no trailer */
    /* frame rectangle outside the canvas */
    b.n = 0;
    gif_head(&b, 10, 10);
    gif_frame(&b, 5, 5, 10, 10, 1, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    b.n = 0;
    gif_head(&b, 10, 10);
    gif_frame(&b, 65535, 0, 2, 2, 1, 10); /* x + w overflows 16 bits but not unsigned */
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    /* decompression bomb: 60 full 1024 x 1024 frames of one colour compress to a small file but
     * exceed the decoded-pixel budget */
    b.n = 0;
    gif_head(&b, 1024, 1024);
    uint8_t *flat = calloc(1024 * 1024, 1);
    for (int f = 0; f < 60; f++) {
        put8(&b, 0x2C);
        put16le(&b, 0);
        put16le(&b, 0);
        put16le(&b, 1024);
        put16le(&b, 1024);
        put8(&b, 0);
        lzw_encode(&b, flat, 1024 * 1024, 2);
    }
    put8(&b, 0x3B);
    assert(b.n < GIF_MAX_FILE);
    assert(decode(&b) == IMG_E_LIMIT);
    free(flat);
    /* too many frames */
    b.n = 0;
    gif_head(&b, 4, 4);
    for (int f = 0; f <= GIF_MAX_SRC_FRAMES; f++)
        gif_frame(&b, 0, 0, 1, 1, f, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_LIMIT);
    /* file over the size limit */
    uint8_t *big = calloc(GIF_MAX_FILE + 1, 1);
    memcpy(big, "GIF89a", 6);
    int frames = 0;
    assert(gif_decode(big, GIF_MAX_FILE + 1, count, &frames) == IMG_E_LIMIT);
    free(big);
    /* short image data: the frame claims 10 x 10 but the LZW stream holds 4 pixels */
    b.n = 0;
    gif_head(&b, 10, 10);
    put8(&b, 0x2C);
    put16le(&b, 0);
    put16le(&b, 0);
    put16le(&b, 10);
    put16le(&b, 10);
    put8(&b, 0);
    uint8_t four[4] = {1, 2, 3, 1};
    lzw_encode(&b, four, 4, 2);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_DATA);
    /* invalid code: a code above the next free entry right after a clear */
    b.n = 0;
    gif_head(&b, 4, 4);
    put8(&b, 0x2C);
    put16le(&b, 0);
    put16le(&b, 0);
    put16le(&b, 4);
    put16le(&b, 4);
    put8(&b, 0);
    put8(&b, 2);         /* min code size 2: clear 4, end 5, codes of 3 bits */
    put8(&b, 1);         /* one sub-block of 1 byte */
    put8(&b, 4 | 7 << 3); /* clear, then code 7 (undefined) */
    put8(&b, 0);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_DATA);
    /* bad min code size, bad extension, no colour table */
    b.n = 0;
    gif_head(&b, 4, 4);
    put8(&b, 0x2C);
    put16le(&b, 0);
    put16le(&b, 0);
    put16le(&b, 4);
    put16le(&b, 4);
    put8(&b, 0);
    put8(&b, 12);
    put8(&b, 0);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_DATA);
    b.n = 0;
    gif_head(&b, 4, 4);
    put8(&b, 0x21);
    put8(&b, 0xF9);
    put8(&b, 9); /* graphic control block must be 4 bytes */
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    b.n = 0;
    put(&b, "GIF89a", 6);
    put16le(&b, 4);
    put16le(&b, 4);
    put8(&b, 0);
    put8(&b, 0);
    put8(&b, 0);
    gif_frame(&b, 0, 0, 4, 4, 1, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == IMG_E_FORMAT);
    /* not a GIF at all / wrong version */
    assert(gif_decode((const uint8_t *)"GIF90a\0\0\0\0\0\0\0", 13, count, &frames) == IMG_E_FORMAT);
    /* every truncation of a valid file fails cleanly */
    b.n = 0;
    gif_head(&b, 16, 16);
    gif_frame(&b, 0, 0, 16, 16, 1, 10);
    gif_frame(&b, 2, 2, 8, 8, 3, 10);
    put8(&b, 0x3B);
    assert(decode(&b) == 2);
    for (size_t cut = 0; cut < b.n; cut++) {
        frames = 0;
        assert(gif_decode(b.b, cut, count, &frames) < 0);
    }
    free(b.b);
    printf("PASS: GIF limits: canvas size, decompression bomb (pixel budget), frame count, file size, "
           "frame outside the canvas, short data, bad LZW codes, bad blocks, every truncation\n");
}

static void test_anim(void) {
    size_t n;
    uint8_t *g = load("anim.gif", &n), *blob;
    size_t bn;
    char hash[33];
    OrbitImage first;
    /* 40 frames at 50 ms (20 fps) -> 20 frames of 100 ms */
    assert(anim_make_gif(g, n, &blob, &bn, hash, &first) == IMG_OK);
    assert(first.w == 96 && first.h == 96 && bn <= ANIM_BLOB_MAX);
    OrbitAnim a;
    assert(anim_decode(blob, bn, hash, &a) == 0);
    assert(a.frames == 20 && a.total_ms == 2000);
    for (int f = 0; f < a.frames; f++)
        assert(a.delay[f] >= ANIM_MIN_DELAY);
    assert(anim_frame_at(&a, 0) == 0 && anim_frame_at(&a, 150) == 1 && anim_frame_at(&a, 2050) == 0);
    /* palette reduction stays close to the source (flat colours) */
    uint32_t px[ANIM_FRAME_PX];
    anim_render(&a, 0, px);
    long err = 0;
    for (int i = 0; i < ANIM_FRAME_PX; i++)
        for (int k = 0; k < 3; k++)
            err += labs((long)(px[i] >> (8 * k) & 255) - (long)(first.px[i] >> (8 * k) & 255));
    assert(err / (ANIM_FRAME_PX * 3) < 6);
    /* a damaged or renamed blob is refused */
    OrbitAnim bad;
    blob[bn / 2] ^= 1;
    assert(anim_decode(blob, bn, hash, &bad) == -1);
    blob[bn / 2] ^= 1;
    hash[0] = hash[0] == 'a' ? 'b' : 'a';
    assert(anim_decode(blob, bn, hash, &bad) == -1);
    anim_free(&a);
    free(blob);
    image_free(&first);
    free(g);

    /* 30 s at 10 fps -> cut to 150 frames / 15 s */
    g = load("long.gif", &n);
    assert(anim_make_gif(g, n, &blob, &bn, hash, &first) == IMG_OK);
    assert(anim_decode(blob, bn, NULL, &a) == 0);
    assert(a.frames <= ANIM_MAX_FRAMES && a.total_ms <= ANIM_MAX_MS && a.total_ms >= ANIM_MAX_MS - 400);
    assert(anim_bytes(&a) <= sizeof(OrbitAnim) + (size_t)ANIM_MAX_FRAMES * ANIM_FRAME_PX);
    anim_free(&a);
    free(blob);
    image_free(&first);
    free(g);

    /* one frame: not an animation, but the still picture comes back */
    g = load("interlaced.gif", &n);
    assert(anim_make_gif(g, n, &blob, &bn, hash, &first) == IMG_E_UNSUPPORTED && first.px && !blob);
    image_free(&first);
    free(g);

    /* hand-made blobs that break the rules */
    static uint8_t raw[ANIM_RAW_MAX + 64];
    uint8_t *z = malloc(compressBound(sizeof raw));
    struct {
        int frames, delay, total_extra, side, extra_bytes;
        int ok;
    } cases[] = {
        {3, 100, 0, 96, 0, 1},  /* valid */
        {1, 100, 0, 96, 0, 0},  /* one frame */
        {151, 100, 0, 96, 0, 0}, /* too many frames */
        {3, 50, 0, 96, 0, 0},   /* faster than 10 fps */
        {3, 6000, 0, 96, 0, 0}, /* longer than 15 s in total */
        {3, 100, 0, 64, 0, 0},  /* wrong side */
        {3, 100, 0, 96, 1, 0},  /* trailing data */
        {3, 100, 0, 96, -1, 0}, /* short data */
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        int fr = cases[c].frames;
        memcpy(raw, "OANI", 4);
        raw[4] = 1;
        raw[5] = (uint8_t)cases[c].side;
        raw[6] = (uint8_t)(fr >> 8);
        raw[7] = (uint8_t)fr;
        int nfr = fr > ANIM_MAX_FRAMES ? 2 : fr;
        for (int f = 0; f < nfr; f++) {
            raw[8 + 2 * f] = (uint8_t)(cases[c].delay >> 8);
            raw[9 + 2 * f] = (uint8_t)cases[c].delay;
        }
        size_t len = 8 + 2 * (size_t)nfr + 768 + (size_t)nfr * ANIM_FRAME_PX + cases[c].extra_bytes;
        memset(raw + 8 + 2 * nfr, 7, len - 8 - 2 * nfr);
        uLongf zn = compressBound(sizeof raw);
        assert(compress2(z, &zn, raw, len, 6) == Z_OK);
        int r = anim_decode(z, zn, NULL, &a);
        assert((r == 0) == cases[c].ok);
        if (!r)
            anim_free(&a);
    }
    /* zlib bomb: 64 MB of zeros behind a valid header never gets inflated past the budget */
    size_t bomb_n = 64u << 20;
    uint8_t *bomb = calloc(bomb_n, 1);
    memcpy(bomb, "OANI\1\140\0\2", 8);
    uLongf zn = compressBound(bomb_n);
    uint8_t *zb = malloc(zn);
    assert(compress2(zb, &zn, bomb, bomb_n, 9) == Z_OK && zn <= ANIM_BLOB_MAX);
    assert(anim_decode(zb, zn, NULL, &a) == -1);
    free(bomb);
    free(zb);
    free(z);
    printf("PASS: animations: 20 fps -> 10 fps, 30 s -> 15 s / 150 frames, palette close to the source, "
           "damaged/renamed blobs, frame/delay/size rules and zlib bomb refused, still GIF falls back\n");
}

static void fuzz_one(const char *name, int rounds, unsigned seed) {
    size_t n;
    uint8_t *src = load(name, &n), *m = malloc(n);
    int ok = 0;
    for (int r = 0; r < rounds; r++) {
        memcpy(m, src, n);
        int flips = 1 + (int)(seed % 8);
        for (int k = 0; k < flips; k++) {
            seed = seed * 1103515245u + 12345u;
            m[(seed >> 8) % n] = (uint8_t)(seed >> 3);
        }
        size_t len = n;
        if (r % 7 == 3)
            len = (seed >> 4) % n; /* truncation */
        int frames = 0;
        if (gif_decode(m, len, count, &frames) > 0)
            ok++;
        if (r % 50 == 0) { /* the full pipeline now and then */
            uint8_t *blob;
            size_t bn;
            char hash[33];
            OrbitImage first;
            int rc = anim_make_gif(m, len, &blob, &bn, hash, &first);
            if (rc == IMG_OK) {
                OrbitAnim a;
                assert(anim_decode(blob, bn, hash, &a) == 0);
                anim_free(&a);
                free(blob);
            }
            image_free(&first);
        }
    }
    printf("  fuzz %s: %d of %d mutated files still decoded\n", name, ok, rounds);
    free(m);
    free(src);
}

int main(void) {
    test_reference();
    test_malicious();
    test_anim();
    fuzz_one("anim.gif", 1500, 7);
    fuzz_one("transparent.gif", 3000, 11);
    fuzz_one("interlaced.gif", 3000, 13);
    /* mutated animation blobs */
    size_t n;
    uint8_t *g = load("transparent.gif", &n), *blob;
    size_t bn;
    char hash[33];
    OrbitImage first;
    assert(anim_make_gif(g, n, &blob, &bn, hash, &first) == IMG_OK);
    unsigned seed = 99;
    for (int r = 0; r < 3000; r++) {
        uint8_t *m = malloc(bn);
        memcpy(m, blob, bn);
        seed = seed * 1103515245u + 12345u;
        m[(seed >> 8) % bn] ^= (uint8_t)(1 + (seed >> 3) % 255);
        OrbitAnim a;
        if (!anim_decode(m, bn, NULL, &a)) {
            assert(a.frames >= 2 && a.frames <= ANIM_MAX_FRAMES && a.total_ms <= ANIM_MAX_MS);
            uint32_t px[ANIM_FRAME_PX];
            anim_render(&a, anim_frame_at(&a, seed), px);
            anim_free(&a);
        }
        free(m);
    }
    free(blob);
    image_free(&first);
    free(g);
    printf("PASS: GIF and animation fuzzing (mutations + truncations, AddressSanitizer/UBSan build)\n");
    return 0;
}
