/* ORBIT NEXT: animated pictures, see include/anim.h */
#include "anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "avatar.h"
#include "gif.h"

/* ---------- making an animation from a GIF ---------- */
typedef struct {
    int n;               /* kept frames */
    int start[ANIM_MAX_FRAMES];
    int t;               /* start of the current source frame (ms) */
    int end;             /* end of the last source frame (ms, capped) */
    uint8_t *rgb;        /* n x 96 x 96 x 3 */
    int err, anchor;
} Collect;

static int collect(void *ctx, const uint32_t *canvas, int w, int h, int index, int delay) {
    Collect *c = ctx;
    (void)index;
    int t = c->t;
    c->t += delay;
    if (t >= ANIM_MAX_MS)
        return 1;
    c->end = c->t < ANIM_MAX_MS ? c->t : ANIM_MAX_MS;
    /* keep this frame only when the previous kept one has been shown >= 100 ms */
    if (c->n && t - c->start[c->n - 1] < ANIM_MIN_DELAY)
        return 0;
    if (c->n == ANIM_MAX_FRAMES)
        return 1;
    OrbitImage src = {w, h, (uint32_t *)canvas}, sq;
    if (image_cover_at(&src, &sq, ANIM_SIDE, ANIM_SIDE, c->anchor) != IMG_OK) {
        c->err = IMG_E_MEMORY;
        return 1;
    }
    uint8_t *o = c->rgb + (size_t)c->n * ANIM_FRAME_PX * 3;
    static const unsigned bg[3] = {0x17, 0x1a, 0x20}; /* same flattening as static pictures */
    for (int i = 0; i < ANIM_FRAME_PX; i++) {
        uint32_t p = sq.px[i];
        unsigned a = p >> 24;
        for (int k = 0; k < 3; k++)
            o[3 * i + k] = (uint8_t)(((p >> (16 - 8 * k) & 255) * a + bg[k] * (255 - a)) / 255);
    }
    image_free(&sq);
    c->start[c->n++] = t;
    return 0;
}

/* median cut on a 15-bit histogram: deterministic, bounded (32768 bins, <= 256 boxes) */
typedef struct {
    int lo[3], hi[3];
    uint32_t count;
} Box;

static void box_shrink(Box *b, const uint32_t *hist) {
    int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
    uint32_t count = 0;
    for (int r = b->lo[0]; r <= b->hi[0]; r++)
        for (int g = b->lo[1]; g <= b->hi[1]; g++)
            for (int bl = b->lo[2]; bl <= b->hi[2]; bl++) {
                uint32_t v = hist[r << 10 | g << 5 | bl];
                if (!v)
                    continue;
                count += v;
                int c[3] = {r, g, bl};
                for (int k = 0; k < 3; k++) {
                    if (c[k] < lo[k])
                        lo[k] = c[k];
                    if (c[k] > hi[k])
                        hi[k] = c[k];
                }
            }
    b->count = count;
    if (count)
        for (int k = 0; k < 3; k++)
            b->lo[k] = lo[k], b->hi[k] = hi[k];
}

static int quantize(const uint8_t *rgb, size_t px, uint32_t pal[256], uint8_t *idx) {
    uint32_t *hist = calloc(32768, 4);
    uint16_t *map = malloc(32768 * 2);
    Box *box = malloc(sizeof(Box) * 256);
    if (!hist || !map || !box) {
        free(hist);
        free(map);
        free(box);
        return IMG_E_MEMORY;
    }
    for (size_t i = 0; i < px; i++)
        hist[(rgb[3 * i] >> 3) << 10 | (rgb[3 * i + 1] >> 3) << 5 | rgb[3 * i + 2] >> 3]++;
    int nb = 1;
    box[0] = (Box){{0, 0, 0}, {31, 31, 31}, 0};
    box_shrink(&box[0], hist);
    while (nb < 256) {
        int best = -1, axis = 0, span = 0;
        for (int i = 0; i < nb; i++)
            for (int k = 0; k < 3; k++) {
                int s = box[i].hi[k] - box[i].lo[k];
                if (s > span || (s == span && s > 0 && best >= 0 && box[i].count > box[best].count)) {
                    span = s;
                    best = i;
                    axis = k;
                }
            }
        if (best < 0 || span == 0)
            break;
        /* split at the median along the widest axis */
        Box *b = &box[best];
        uint32_t half = b->count / 2, acc = 0, along[32] = {0};
        for (int r = b->lo[0]; r <= b->hi[0]; r++) /* one pass: counts per value on the axis */
            for (int g = b->lo[1]; g <= b->hi[1]; g++)
                for (int bl = b->lo[2]; bl <= b->hi[2]; bl++) {
                    int c[3] = {r, g, bl};
                    along[c[axis]] += hist[r << 10 | g << 5 | bl];
                }
        int cut = b->lo[axis];
        for (int v = b->lo[axis]; v < b->hi[axis]; v++) {
            acc += along[v];
            cut = v;
            if (acc >= half)
                break;
        }
        Box nbx = *b;
        b->hi[axis] = cut;
        nbx.lo[axis] = cut + 1;
        box_shrink(b, hist);
        box_shrink(&nbx, hist);
        box[nb++] = nbx;
    }
    for (int i = 0; i < 256; i++)
        pal[i] = 0xFF000000u;
    for (int i = 0; i < nb; i++) {
        uint64_t sum[3] = {0, 0, 0}, cnt = 0;
        for (int r = box[i].lo[0]; r <= box[i].hi[0]; r++)
            for (int g = box[i].lo[1]; g <= box[i].hi[1]; g++)
                for (int bl = box[i].lo[2]; bl <= box[i].hi[2]; bl++) {
                    uint32_t v = hist[r << 10 | g << 5 | bl];
                    map[r << 10 | g << 5 | bl] = (uint16_t)i;
                    sum[0] += (uint64_t)v * (r * 8 + 4);
                    sum[1] += (uint64_t)v * (g * 8 + 4);
                    sum[2] += (uint64_t)v * (bl * 8 + 4);
                    cnt += v;
                }
        if (cnt)
            pal[i] = 0xFF000000u | (uint32_t)(sum[0] / cnt) << 16 | (uint32_t)(sum[1] / cnt) << 8 | (uint32_t)(sum[2] / cnt);
    }
    for (size_t i = 0; i < px; i++)
        idx[i] = (uint8_t)map[(rgb[3 * i] >> 3) << 10 | (rgb[3 * i + 1] >> 3) << 5 | rgb[3 * i + 2] >> 3];
    free(hist);
    free(map);
    free(box);
    return IMG_OK;
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

int anim_make_gif(const uint8_t *gif, size_t len, uint8_t **blob, size_t *n, char hash[33], OrbitImage *first) {
    return anim_make_gif_at(gif, len, 0, blob, n, hash, first);
}
int anim_make_gif_at(const uint8_t *gif, size_t len, int anchor, uint8_t **blob, size_t *n, char hash[33],
                     OrbitImage *first) {
    *blob = NULL;
    *n = 0;
    memset(first, 0, sizeof(*first));
    Collect c;
    memset(&c, 0, sizeof c);
    c.anchor = anchor;
    c.rgb = malloc((size_t)ANIM_MAX_FRAMES * ANIM_FRAME_PX * 3);
    uint8_t *raw = malloc(ANIM_RAW_MAX);
    int rc = IMG_E_MEMORY;
    if (!c.rgb || !raw)
        goto out;
    rc = gif_decode(gif, len, collect, &c);
    if (rc < 0)
        goto out;
    if (c.err) {
        rc = c.err;
        goto out;
    }
    if (!c.n) {
        rc = IMG_E_FORMAT;
        goto out;
    }
    /* first frame for the static fallback */
    first->px = malloc(ANIM_FRAME_PX * 4);
    if (!first->px) {
        rc = IMG_E_MEMORY;
        goto out;
    }
    first->w = first->h = ANIM_SIDE;
    for (int i = 0; i < ANIM_FRAME_PX; i++)
        first->px[i] = 0xFF000000u | (uint32_t)c.rgb[3 * i] << 16 | (uint32_t)c.rgb[3 * i + 1] << 8 | c.rgb[3 * i + 2];
    if (c.n < 2) {
        rc = IMG_E_UNSUPPORTED; /* a still picture */
        goto out;
    }
    /* keep halving the frame rate until the compressed animation fits the budget */
    for (int step = 1; step <= 8; step *= 2) {
        int frames = (c.n + step - 1) / step;
        if (frames < 2)
            break;
        uint8_t *h = raw;
        memcpy(h, "OANI", 4);
        h[4] = 1;
        h[5] = ANIM_SIDE;
        put16(h + 6, (unsigned)frames);
        uint8_t *d = h + ANIM_HEAD, *p = d + 2 * frames, *ix = p + 768;
        int total = 0;
        for (int f = 0; f < frames; f++) {
            int s = c.start[f * step], e = f * step + step < c.n ? c.start[f * step + step] : c.end;
            int ms = e - s;
            if (ms < ANIM_MIN_DELAY)
                ms = ANIM_MIN_DELAY;
            if (total + ms > ANIM_MAX_MS)
                ms = ANIM_MAX_MS - total;
            if (ms < ANIM_MIN_DELAY) { /* no time left: drop the rest */
                frames = f;
                put16(h + 6, (unsigned)frames);
                break;
            }
            total += ms;
            put16(d + 2 * f, (unsigned)ms);
        }
        if (frames < 2)
            break;
        /* compact: delays were written for `frames` slots; move palette/indices right after */
        p = d + 2 * frames;
        ix = p + 768;
        uint8_t *sel = malloc((size_t)frames * ANIM_FRAME_PX * 3);
        if (!sel) {
            rc = IMG_E_MEMORY;
            goto out;
        }
        for (int f = 0; f < frames; f++)
            memcpy(sel + (size_t)f * ANIM_FRAME_PX * 3, c.rgb + (size_t)f * step * ANIM_FRAME_PX * 3, ANIM_FRAME_PX * 3);
        uint32_t pal[256];
        rc = quantize(sel, (size_t)frames * ANIM_FRAME_PX, pal, ix);
        free(sel);
        if (rc)
            goto out;
        for (int i = 0; i < 256; i++) {
            p[3 * i] = (uint8_t)(pal[i] >> 16);
            p[3 * i + 1] = (uint8_t)(pal[i] >> 8);
            p[3 * i + 2] = (uint8_t)pal[i];
        }
        size_t rawn = (size_t)(ix - raw) + (size_t)frames * ANIM_FRAME_PX;
        uLongf cap = compressBound((uLong)rawn);
        uint8_t *z = malloc(cap);
        if (!z) {
            rc = IMG_E_MEMORY;
            goto out;
        }
        if (compress2(z, &cap, raw, (uLong)rawn, 9) != Z_OK) {
            free(z);
            rc = IMG_E_MEMORY;
            goto out;
        }
        if (cap <= ANIM_BLOB_MAX) {
            *blob = z;
            *n = cap;
            avatar_hash(z, cap, hash);
            rc = IMG_OK;
            goto out;
        }
        free(z);
    }
    rc = IMG_E_LIMIT; /* too detailed even at a quarter of the frames */
out:
    if (rc != IMG_OK && rc != IMG_E_UNSUPPORTED)
        image_free(first);
    free(c.rgb);
    free(raw);
    return rc;
}

/* ---------- reading ---------- */
int anim_decode(const uint8_t *blob, size_t n, const char *hash, OrbitAnim *a) {
    memset(a, 0, sizeof(*a));
    if (!blob || n < 16 || n > ANIM_BLOB_MAX)
        return -1;
    if (hash) {
        char h[33];
        avatar_hash(blob, n, h);
        if (strcmp(h, hash))
            return -1;
    }
    /* the header first: it decides the exact size the rest must have */
    uint8_t head[ANIM_HEAD];
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK)
        return -1;
    z.next_in = (Bytef *)blob;
    z.avail_in = (uInt)n;
    z.next_out = head;
    z.avail_out = ANIM_HEAD;
    int rc = inflate(&z, Z_SYNC_FLUSH);
    if ((rc != Z_OK && rc != Z_STREAM_END) || z.avail_out || memcmp(head, "OANI", 4) || head[4] != 1 ||
        head[5] != ANIM_SIDE) {
        inflateEnd(&z);
        return -1;
    }
    int frames = head[6] << 8 | head[7];
    if (frames < 2 || frames > ANIM_MAX_FRAMES) {
        inflateEnd(&z);
        return -1;
    }
    size_t rest = (size_t)2 * frames + 768 + (size_t)frames * ANIM_FRAME_PX;
    uint8_t *body = malloc(rest + 1);
    if (!body) {
        inflateEnd(&z);
        return -1;
    }
    z.next_out = body;
    z.avail_out = (uInt)(rest + 1); /* one extra byte: more data than announced is an error */
    rc = rc == Z_STREAM_END ? rc : inflate(&z, Z_FINISH);
    size_t got = rest + 1 - z.avail_out;
    int left = z.avail_in;
    inflateEnd(&z);
    if (rc != Z_STREAM_END || got != rest || left) {
        free(body);
        return -1;
    }
    int total = 0;
    for (int f = 0; f < frames; f++) {
        int ms = body[2 * f] << 8 | body[2 * f + 1];
        if (ms < ANIM_MIN_DELAY || ms > ANIM_MAX_MS) {
            free(body);
            return -1;
        }
        a->delay[f] = (uint16_t)ms;
        total += ms;
    }
    if (total > ANIM_MAX_MS) {
        free(body);
        return -1;
    }
    const uint8_t *p = body + 2 * frames;
    for (int i = 0; i < 256; i++)
        a->pal[i] = 0xFF000000u | (uint32_t)p[3 * i] << 16 | (uint32_t)p[3 * i + 1] << 8 | p[3 * i + 2];
    /* keep only the indices */
    size_t idx_n = (size_t)frames * ANIM_FRAME_PX;
    memmove(body, p + 768, idx_n);
    uint8_t *shrunk = realloc(body, idx_n);
    a->idx = shrunk ? shrunk : body;
    a->frames = frames;
    a->total_ms = total;
    return 0;
}

void anim_free(OrbitAnim *a) {
    free(a->idx);
    memset(a, 0, sizeof(*a));
}

int anim_frame_at(const OrbitAnim *a, uint32_t t) {
    if (a->frames < 1 || a->total_ms <= 0)
        return 0;
    t %= (uint32_t)a->total_ms;
    for (int f = 0; f < a->frames; f++) {
        if (t < a->delay[f])
            return f;
        t -= a->delay[f];
    }
    return a->frames - 1;
}

void anim_render(const OrbitAnim *a, int k, uint32_t *px) {
    if (k < 0 || k >= a->frames)
        k = 0;
    const uint8_t *s = a->idx + (size_t)k * ANIM_FRAME_PX;
    for (int i = 0; i < ANIM_FRAME_PX; i++)
        px[i] = a->pal[s[i]];
}

size_t anim_bytes(const OrbitAnim *a) {
    return sizeof(*a) + (size_t)a->frames * ANIM_FRAME_PX;
}

int anim_save(const char *dir, const char *hash, const uint8_t *blob, size_t n) {
    if (!avatar_hash_valid(hash) || !n || n > ANIM_BLOB_MAX)
        return -1;
    char path[600], tmp[610];
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/%s.ani", dir, hash);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int ok = fwrite(blob, 1, n, f) == n;
    if (fclose(f) || !ok) {
        remove(tmp);
        return -1;
    }
    remove(path);
    return rename(tmp, path) ? -1 : 0;
}

int anim_load(const char *dir, const char *hash, uint8_t **blob, size_t *n) {
    *blob = NULL;
    if (!avatar_hash_valid(hash))
        return -1;
    char path[600];
    snprintf(path, sizeof(path), "%s/%s.ani", dir, hash);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    uint8_t *b = malloc(ANIM_BLOB_MAX + 1);
    size_t got = b ? fread(b, 1, ANIM_BLOB_MAX + 1, f) : 0;
    fclose(f);
    char h[33];
    if (!got || got > ANIM_BLOB_MAX) {
        free(b);
        return -1;
    }
    avatar_hash(b, got, h);
    if (strcmp(h, hash)) { /* damaged file */
        free(b);
        return -1;
    }
    *blob = b;
    *n = got;
    return 0;
}
