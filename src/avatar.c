/* ORBIT profile pictures, see include/avatar.h */
#include "avatar.h"
#include "monocypher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

int avatar_hash_valid(const char *h) {
    if (!h || strlen(h) != 32)
        return 0;
    for (int i = 0; i < 32; i++)
        if (!((h[i] >= '0' && h[i] <= '9') || (h[i] >= 'a' && h[i] <= 'f')))
            return 0;
    return 1;
}
void avatar_hash(const uint8_t *blob, size_t n, char hash[33]) {
    uint8_t h[16];
    crypto_blake2b(h, 16, blob, n);
    for (int i = 0; i < 16; i++)
        snprintf(hash + 2 * i, 3, "%02x", h[i]);
}
int avatar_make(const OrbitImage *img, uint8_t blob[AVATAR_BLOB_MAX], size_t *n, char hash[33]) {
    OrbitImage sq;
    if (!img || !img->px || image_cover(img, &sq, AVATAR_SIDE, AVATAR_SIDE) != IMG_OK)
        return -1;
    uint8_t *raw = malloc(AVATAR_RAW);
    if (!raw) {
        image_free(&sq);
        return -1;
    }
    for (int i = 0; i < AVATAR_SIDE * AVATAR_SIDE; i++) {
        uint32_t p = sq.px[i];
        unsigned a = p >> 24; /* flatten transparency onto the dark UI background */
        unsigned bg[3] = {0x17, 0x1a, 0x20};
        for (int c = 0; c < 3; c++) {
            unsigned v = p >> (16 - 8 * c) & 255;
            raw[3 * i + c] = (uint8_t)((v * a + bg[c] * (255 - a)) / 255);
        }
    }
    image_free(&sq);
    uLongf len = AVATAR_BLOB_MAX;
    int rc = compress2(blob, &len, raw, AVATAR_RAW, 9);
    free(raw);
    if (rc != Z_OK)
        return -1;
    *n = len;
    avatar_hash(blob, len, hash);
    return 0;
}
int avatar_decode(const uint8_t *blob, size_t n, const char *hash, OrbitImage *out) {
    memset(out, 0, sizeof(*out));
    if (!blob || !n || n > AVATAR_BLOB_MAX)
        return -1;
    if (hash) {
        char h[33];
        avatar_hash(blob, n, h);
        if (strcmp(h, hash))
            return -1;
    }
    uint8_t *raw = malloc(AVATAR_RAW + 1);
    if (!raw)
        return -1;
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK) {
        free(raw);
        return -1;
    }
    z.next_in = (Bytef *)blob;
    z.avail_in = (uInt)n;
    z.next_out = raw;
    z.avail_out = AVATAR_RAW + 1; /* one extra byte: more data than a picture is an error */
    int rc = inflate(&z, Z_FINISH);
    size_t got = AVATAR_RAW + 1 - z.avail_out;
    inflateEnd(&z);
    if (rc != Z_STREAM_END || got != AVATAR_RAW || z.avail_in) {
        free(raw);
        return -1;
    }
    out->px = malloc(AVATAR_SIDE * AVATAR_SIDE * 4);
    if (!out->px) {
        free(raw);
        return -1;
    }
    out->w = out->h = AVATAR_SIDE;
    for (int i = 0; i < AVATAR_SIDE * AVATAR_SIDE; i++)
        out->px[i] = 0xFF000000u | (uint32_t)raw[3 * i] << 16 | (uint32_t)raw[3 * i + 1] << 8 | raw[3 * i + 2];
    free(raw);
    return 0;
}
int avatar_save(const char *dir, const char *hash, const uint8_t *blob, size_t n) {
    if (!avatar_hash_valid(hash) || !n || n > AVATAR_BLOB_MAX)
        return -1;
    char path[600], tmp[610];
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/%s.pic", dir, hash);
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
int avatar_load(const char *dir, const char *hash, uint8_t blob[AVATAR_BLOB_MAX], size_t *n) {
    if (!avatar_hash_valid(hash))
        return -1;
    char path[600];
    snprintf(path, sizeof(path), "%s/%s.pic", dir, hash);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t got = fread(blob, 1, AVATAR_BLOB_MAX, f);
    int more = fgetc(f) != EOF;
    fclose(f);
    if (!got || more)
        return -1;
    char h[33];
    avatar_hash(blob, got, h);
    if (strcmp(h, hash))
        return -1; /* damaged file */
    *n = got;
    return 0;
}
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int avatar_b64_encode(char *out, size_t cap, const uint8_t *in, size_t n) {
    if ((n + 2) / 3 * 4 + 1 > cap)
        return -1;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) |
                     (i + 2 < n ? in[i + 2] : 0);
        out[o++] = b64[v >> 18 & 63];
        out[o++] = b64[v >> 12 & 63];
        out[o++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? b64[v & 63] : '=';
    }
    out[o] = 0;
    return 0;
}
int avatar_b64_decode(uint8_t *out, size_t cap, size_t *n, const char *in) {
    if (!in)
        return -1;
    size_t len = strlen(in), o = 0;
    if (len % 4)
        return -1;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t v = 0;
        int pad = 0;
        for (int k = 0; k < 4; k++) {
            char c = in[i + k];
            const char *p = c ? strchr(b64, c) : NULL;
            if (c == '=' && i + 4 == len && k >= 2) {
                pad++;
                v <<= 6;
                continue;
            }
            if (!p || pad)
                return -1;
            v = v << 6 | (uint32_t)(p - b64);
        }
        for (int k = 0; k < 3 - pad; k++) {
            if (o >= cap)
                return -1;
            out[o++] = (uint8_t)(v >> (16 - 8 * k));
        }
    }
    *n = o;
    return 0;
}
