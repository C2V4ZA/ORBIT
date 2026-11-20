/* JPEG reader: baseline 4:4:4 / 4:2:0 with restarts, grey, progressive (1/8), big photo (1/8),
 * EXIF rotation, truncation and mutation robustness, format sniffing */
#include "avatar.h"
#include "i18n.h"
#include "image.h"
#include "orbit.h"
#include <zlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load(const char *name, size_t *n) {
    char path[256];
    snprintf(path, sizeof(path), "tests/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    assert(fread(b, 1, *n, f) == *n);
    fclose(f);
    return b;
}
static int near(uint32_t px, int r, int g, int b, int tol) {
    int pr = px >> 16 & 255, pg = px >> 8 & 255, pb = px & 255;
    return abs(pr - r) <= tol && abs(pg - g) <= tol && abs(pb - b) <= tol;
}
/* the fixtures' pattern: r = 4x, g = 6y, b = 128 */
static double pattern_error(const OrbitImage *img, int scale) {
    double e = 0;
    for (int y = 0; y < img->h; y++)
        for (int x = 0; x < img->w; x++) {
            uint32_t p = img->px[y * img->w + x];
            int cx = x * scale + scale / 2, cy = y * scale + scale / 2;
            e += abs((int)(p >> 16 & 255) - cx * 4 % 256) + abs((int)(p >> 8 & 255) - cy * 6 % 256) +
                 abs((int)(p & 255) - 128);
        }
    return e / (img->w * img->h * 3.0);
}
static int fuzz(const uint8_t *src, size_t n, int rounds, unsigned seed) {
    uint8_t *m = malloc(n);
    int ok = 0;
    for (int r = 0; r < rounds; r++) {
        memcpy(m, src, n);
        int flips = 1 + (int)(seed % 6);
        for (int k = 0; k < flips; k++) {
            seed = seed * 1103515245u + 12345u;
            m[(seed >> 8) % n] = (uint8_t)(seed >> 3);
        }
        OrbitImage img;
        if (image_jpeg_decode(m, n, &img, 0) == IMG_OK) {
            assert(img.w > 0 && img.h > 0 && img.w <= 16384 && img.px);
            ok++;
            image_free(&img);
        } else
            assert(!img.px);
    }
    free(m);
    return ok;
}

int main(void) {
    OrbitImage img;
    size_t n;
    uint8_t *b = load("base444.jpg", &n);
    assert(image_jpeg_decode(b, n, &img, 0) == IMG_OK && img.w == 61 && img.h == 37);
    assert(pattern_error(&img, 1) < 3.0);
    assert(near(img.px[10 * 61 + 20], 80, 60, 128, 10));
    image_free(&img);
    for (size_t cut = 0; cut < n; cut += 7) /* every truncation is refused or bounded */
        if (image_jpeg_decode(b, cut, &img, 0) == IMG_OK)
            image_free(&img);
    int ok1 = fuzz(b, n, 6000, 7);
    free(b);

    b = load("base420.jpg", &n); /* 4:2:0 with restart markers */
    assert(image_jpeg_decode(b, n, &img, 0) == IMG_OK && img.w == 100 && img.h == 75);
    assert(pattern_error(&img, 1) < 6.0);
    image_free(&img);
    int ok2 = fuzz(b, n, 6000, 99);
    free(b);

    b = load("gray.jpg", &n);
    assert(image_decode_any(b, n, &img) == IMG_OK && img.w == 40 && img.h == 30);
    uint32_t p = img.px[5 * 40 + 5];
    assert((p >> 16 & 255) == (p & 255) && (p >> 8 & 255) == (p & 255));
    image_free(&img);
    free(b);

    b = load("prog.jpg", &n); /* progressive: 1/8 picture from the DC scans */
    assert(image_jpeg_decode(b, n, &img, 0) == IMG_OK && img.w == 40 && img.h == 30);
    /* chroma is 16x16-averaged at 1/8, so compare brightness with the block average */
    static const int rows[] = {0, 3, 7, 12, 18, 24, 28}; /* block rows where 6y does not wrap */
    for (int i = 0; i < 7; i++)
        for (int x = 0; x < 40; x += 7) {
            int y = rows[i];
            uint32_t q = img.px[y * 40 + x];
            double got = 0.299 * (q >> 16 & 255) + 0.587 * (q >> 8 & 255) + 0.114 * (q & 255);
            double r = 0, g = 0;
            for (int k = 0; k < 8; k++) {
                r += (x * 8 + k) * 4 % 256 / 8.0;
                g += (y * 8 + k) * 6 % 256 / 8.0;
            }
            double want = 0.299 * r + 0.587 * g + 0.114 * 128;
            assert(got > want - 10 && got < want + 10);
        }
    image_free(&img);
    int ok3 = fuzz(b, n, 6000, 4242);
    free(b);

    b = load("big.jpg", &n); /* 2600x1800 > 4 Mpx: decoded at 1/8 without the full picture */
    assert(image_jpeg_decode(b, n, &img, 0) == IMG_OK && img.w == 325 && img.h == 225);
    assert(near(img.px[100 * 325 + 100], 40, 160, 220, 8));
    image_free(&img);
    assert(image_jpeg_decode(b, n, &img, 1000) == IMG_E_LIMIT && !img.px); /* caller limit */
    free(b);

    b = load("exif6.jpg", &n); /* stored 40x20, EXIF says rotate 90 degrees */
    assert(image_jpeg_decode(b, n, &img, 0) == IMG_OK && img.w == 20 && img.h == 40);
    /* stored (0,0) (black-ish red 0, green 0) ends up top right */
    assert(near(img.px[0 * 20 + 19], 0, 0, 128, 24));
    image_free(&img);
    free(b);

    /* sniffing and refusals */
    static const uint8_t elf[] = {0x7f, 'E', 'L', 'F', 1, 2, 1, 0};
    assert(image_decode_any(elf, sizeof(elf), &img) == IMG_E_FORMAT && !img.px);
    static const uint8_t soi_only[] = {0xFF, 0xD8, 0xFF, 0xD9};
    assert(image_jpeg_decode(soi_only, sizeof(soi_only), &img, 0) == IMG_E_FORMAT);
    assert(image_load_any("tests/fixtures/base444.jpg", &img) == IMG_OK && img.w == 61);
    image_free(&img);
    assert(image_load_any("tests/fixtures/missing.jpg", &img) == IMG_E_IO);
    printf("PASS: JPEG baseline 4:4:4/4:2:0 + restarts, grey, progressive and >4 Mpx at 1/8, EXIF "
           "rotation, truncations, 18000 mutations (%d decodable, all bounded)\n",
           ok1 + ok2 + ok3);

    /* profile pictures: crop + scale, round trip, name check, refusals */
    b = load("big.jpg", &n);
    assert(image_decode_any(b, n, &img) == IMG_OK);
    free(b);
    static uint8_t blob[AVATAR_BLOB_MAX];
    size_t bn = 0;
    char hash[33];
    assert(!avatar_make(&img, blob, &bn, hash) && avatar_hash_valid(hash) && bn < AVATAR_BLOB_MAX);
    image_free(&img);
    OrbitImage av;
    assert(!avatar_decode(blob, bn, hash, &av) && av.w == 96 && av.h == 96);
    assert(near(av.px[48 * 96 + 48], 40, 160, 220, 8));
    image_free(&av);
    char wrong[33];
    snprintf(wrong, sizeof(wrong), "%s", hash);
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';
    assert(avatar_decode(blob, bn, wrong, &av) && !av.px); /* name does not match */
    blob[bn / 2] ^= 0x55;
    assert(avatar_decode(blob, bn, NULL, &av) && !av.px); /* damaged */
    {
        /* a valid zlib stream that inflates to more than one picture is refused (bomb) */
        static uint8_t big[AVATAR_RAW * 4];
        uLongf zn = AVATAR_BLOB_MAX;
        assert(compress2(blob, &zn, big, sizeof(big), 9) == Z_OK);
        assert(avatar_decode(blob, zn, NULL, &av) && !av.px);
        zn = AVATAR_BLOB_MAX;
        assert(compress2(blob, &zn, big, AVATAR_RAW - 1, 9) == Z_OK);
        assert(avatar_decode(blob, zn, NULL, &av) && !av.px); /* one byte short */
    }
    char text[64];
    size_t tn;
    uint8_t raw[16];
    assert(!avatar_b64_encode(text, sizeof(text), (const uint8_t *)"ORBIT!", 6) && !strcmp(text, "T1JCSVQh"));
    assert(!avatar_b64_decode(raw, sizeof(raw), &tn, "T1JCSVQh") && tn == 6 && !memcmp(raw, "ORBIT!", 6));
    assert(!avatar_b64_decode(raw, sizeof(raw), &tn, "T1I=") && tn == 2);
    assert(avatar_b64_decode(raw, sizeof(raw), &tn, "T1I") && avatar_b64_decode(raw, sizeof(raw), &tn, "T=I=") &&
           avatar_b64_decode(raw, 2, &tn, "T1JCSVQh") && avatar_b64_decode(raw, sizeof(raw), &tn, "T1J*"));
    assert(!avatar_hash_valid("0123") && !avatar_hash_valid("0123456789ABCDEF0123456789abcdef") &&
           !avatar_hash_valid("../../../../etc/passwd0123456789"));
    printf("PASS: profile pictures: 96x96 crop, zlib blob named by BLAKE2b, wrong name/damage/bomb "
           "refused, base64, safe file names\n");

    /* name rules */
    assert(orbit_name_check("Mika") == 0 && orbit_name_check("Jo") == 0);
    assert(orbit_name_check("Zoë Müller") == 0 && orbit_name_check("田中") == 0);
    assert(orbit_name_check("A") == E_NAME_RULES_LEN);
    assert(orbit_name_check("abcdefghijklmnopqrstuvwxy") == E_NAME_RULES_LEN);
    assert(orbit_name_check(" Mika") == E_NAME_CHARS && orbit_name_check("Mika ") == E_NAME_CHARS &&
           orbit_name_check("Mi  ka") == E_NAME_CHARS);
    assert(orbit_name_check("Mi\xE2\x80\x8B" "ka") == E_NAME_CHARS); /* zero-width space */
    assert(orbit_name_check("Mi\xE2\x80\xAE" "ka") == E_NAME_CHARS); /* right-to-left override */
    assert(orbit_name_check("ADMIN") == E_NAME_RESERVED && orbit_name_check("0rbit") == E_NAME_RESERVED &&
           orbit_name_check("Mod_erator") == E_NAME_RESERVED && orbit_name_check("Orbit Team") == E_NAME_RESERVED);
    assert(orbit_name_check("Orbiter") == 0);
    /* offensive words: substrings for unambiguous ones, whole words for short ones */
    assert(orbit_name_check("Scunthorpe") == 0 && orbit_name_check("Computadora") == 0 &&
           orbit_name_check("Grape Juice") == 0 && orbit_name_check("Sussex Fan") == 0);
    assert(orbit_name_check("xXfuckXx") == E_NAME_BLOCKED && orbit_name_check("F.U.C.K") == E_NAME_BLOCKED &&
           orbit_name_check("Puta Madre") == E_NAME_BLOCKED && orbit_name_check("h1tl3r fan") == E_NAME_BLOCKED);
    assert(orbit_name_check("a\x01") == E_NAME_LEN);
    printf("PASS: name rules: 2-24 characters, invisible/direction characters and odd spaces refused, "
           "reserved staff names (case, digits, separators folded)\n");
    return 0;
}
