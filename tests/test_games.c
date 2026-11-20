/* ORBIT NEXT: game detection (PARAM.SFO) and the strict PNG decoder, incl. malformed input. */
#include "games.h"
#include "image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
static size_t put_sfo(uint8_t *out, const char *const keys[], const char *const vals[], int n) {
    /* header 20 + entries 16*n + key table + data table */
    size_t kt = 20 + 16 * (size_t)n, ko = 0, vo = 0;
    char ktab[512];
    uint8_t vtab[1024];
    memset(out, 0, 2048);
    memcpy(out, "\0PSF", 4);
    out[4] = 1;
    for (int i = 0; i < n; i++) {
        uint8_t *e = out + 20 + i * 16;
        e[0] = (uint8_t)ko, e[1] = (uint8_t)(ko >> 8);
        e[2] = 0x04, e[3] = 0x02;
        size_t l = strlen(vals[i]) + 1;
        e[4] = (uint8_t)l;
        e[8] = (uint8_t)((l + 3) & ~3u);
        e[12] = (uint8_t)vo, e[13] = (uint8_t)(vo >> 8);
        memcpy(ktab + ko, keys[i], strlen(keys[i]) + 1);
        ko += strlen(keys[i]) + 1;
        memcpy(vtab + vo, vals[i], l);
        vo += (l + 3) & ~3u;
    }
    ko = (ko + 3) & ~3u;
    size_t vt = kt + ko;
    out[8] = (uint8_t)kt, out[9] = (uint8_t)(kt >> 8);
    out[12] = (uint8_t)vt, out[13] = (uint8_t)(vt >> 8);
    out[16] = (uint8_t)n;
    memcpy(out + kt, ktab, ko);
    memcpy(out + vt, vtab, vo);
    return vt + vo;
}
static void wfile(const char *path, const void *d, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(d, 1, n, f);
    fclose(f);
}
static void be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static size_t chunk(uint8_t *o, const char *type, const uint8_t *d, uint32_t n) {
    be(o, n);
    memcpy(o + 4, type, 4);
    if (n)
        memcpy(o + 8, d, n);
    be(o + 8 + n, (uint32_t)crc32(0, o + 4, 4 + n));
    return 12 + n;
}
/* RGBA PNG with filter type f on every row */
/* recompute every chunk CRC so mutations reach the inflate and unfilter code */
static void fix_crcs(uint8_t *p, size_t n) {
    size_t pos = 8;
    while (pos + 12 <= n) {
        uint32_t l = (uint32_t)p[pos] << 24 | (uint32_t)p[pos + 1] << 16 | (uint32_t)p[pos + 2] << 8 | p[pos + 3];
        if (l > n - pos - 12)
            return;
        be(p + pos + 8 + l, (uint32_t)crc32(0, p + pos + 4, 4 + l));
        pos += 12 + l;
    }
}
static size_t make_png(uint8_t *o, int w, int h, int ctype, int f, size_t extra_raw) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    int ch = ctype == 6 ? 4 : ctype == 2 ? 3 : 1;
    size_t stride = (size_t)w * ch, rawn = (stride + 1) * h + extra_raw;
    uint8_t *raw = calloc(1, rawn), *z = malloc(compressBound(rawn));
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = (uint8_t)f;
        for (size_t x = 0; x < stride; x++)
            raw[y * (stride + 1) + 1 + x] = (uint8_t)(x * 7 + y * 3);
    }
    uLongf zn = compressBound(rawn);
    compress(z, &zn, raw, rawn);
    size_t n = 8;
    memcpy(o, sig, 8);
    uint8_t ih[13] = {0};
    be(ih, (uint32_t)w);
    be(ih + 4, (uint32_t)h);
    ih[8] = 8;
    ih[9] = (uint8_t)ctype;
    n += chunk(o + n, "IHDR", ih, 13);
    if (ctype == 3) {
        uint8_t pal[768];
        for (int i = 0; i < 768; i++)
            pal[i] = (uint8_t)i;
        n += chunk(o + n, "PLTE", pal, 768);
    }
    n += chunk(o + n, "IDAT", z, (uint32_t)zn);
    n += chunk(o + n, "IEND", NULL, 0);
    free(raw);
    free(z);
    return n;
}
int main(void) {
    /* --- PARAM.SFO --- */
    uint8_t sfo[2048];
    const char *k[] = {"CATEGORY", "TITLE", "TITLE_ID"};
    const char *v[] = {"HG", "LittleBigPlanet\nGame of the Year", "BCES00141"};
    size_t n = put_sfo(sfo, k, v, 3);
    char title[64], id[10], cat[3];
    assert(!sfo_parse(sfo, n, title, sizeof(title), id, sizeof(id), cat, sizeof(cat)));
    assert(!strcmp(title, "LittleBigPlanet Game of the Year") && !strcmp(id, "BCES00141") && !strcmp(cat, "HG"));
    /* truncated / garbage / huge counts must fail cleanly */
    for (size_t cut = 0; cut < n; cut += 7)
        sfo_parse(sfo, cut, title, sizeof(title), id, sizeof(id), cat, sizeof(cat));
    uint8_t bad[2048];
    memcpy(bad, sfo, n);
    bad[16] = 0xff, bad[17] = 0xff;
    assert(sfo_parse(bad, n, title, sizeof(title), id, sizeof(id), cat, sizeof(cat)));
    srand(7);
    for (int it = 0; it < 20000; it++) {
        memcpy(bad, sfo, n);
        for (int m = 0; m < 4; m++)
            bad[rand() % n] = (uint8_t)rand();
        if (!sfo_parse(bad, n, title, sizeof(title), id, sizeof(id), cat, sizeof(cat)))
            assert(strlen(title) < sizeof(title) && strlen(id) < sizeof(id));
    }
    /* title buffer smaller than the value: cut on a UTF-8 boundary */
    const char *v2[] = {"HG", "ÄÄÄÄÄÄÄÄÄÄ", "BLES00680"};
    n = put_sfo(sfo, k, v2, 3);
    char small[6];
    assert(!sfo_parse(sfo, n, small, sizeof(small), id, sizeof(id), cat, sizeof(cat)));
    assert(!strcmp(small, "ÄÄ"));
    puts("PASS: PARAM.SFO parsed (title, Title ID, category), line breaks, UTF-8 cut, 20000 mutated files refused or bounded");
    assert(games_is_game_id("BLES00680") && games_is_game_id("NPEB01234") && games_is_game_id("BCUS98174"));
    assert(!games_is_game_id("ORB000001") && !games_is_game_id("BLES80608") && !games_is_game_id("blES00680"));
    assert(games_region("BLES00680") == 'E' && games_region("BLUS30001") == 'U' && games_region("BLJM60001") == 'J');
    puts("PASS: Title ID shape (retail/PSN yes, ORBIT and homebrew 8xxxx no) and region from prefix");
    /* --- scan --- */
    char root[] = "/tmp/orbit-games-XXXXXX";
    assert(mkdtemp(root));
    char d[512], p[600];
    struct {
        const char *dir, *cat, *title, *id;
    } fx[] = {{"BCES00141", "HG", "LittleBigPlanet", "BCES00141"},
              {"BLES01717", "GD", "Black Ops II Patch", "BLES01717"},
              {"BLES01717DATA", "DG", "Call of Duty: Black Ops II", "BLES01717"},
              {"ORBT00001", "HG", "ORBIT", "ORB000001"},
              {"SAVEDATA", "SD", "Save", "BLES00680"},
              {"NPEB00001", "HG", "flOw", "NPEB00001"}};
    for (size_t i = 0; i < sizeof(fx) / sizeof(fx[0]); i++) {
        snprintf(d, sizeof(d), "%s/%s", root, fx[i].dir);
        mkdir(d, 0700);
        const char *vv[] = {fx[i].cat, fx[i].title, fx[i].id};
        n = put_sfo(sfo, k, vv, 3);
        snprintf(p, sizeof(p), "%s/PARAM.SFO", d);
        wfile(p, sfo, n);
    }
    snprintf(d, sizeof(d), "%s/EMPTY", root);
    mkdir(d, 0700);
    OrbitGame games[8];
    int count = games_scan(root, "/nonexistent-disc", games, 8);
    assert(count == 3);
    int lbp = 0, cod = 0;
    for (int i = 0; i < count; i++) {
        if (!strcmp(games[i].id, "BCES00141"))
            lbp = 1;
        if (!strcmp(games[i].id, "BLES01717"))
            cod = !strcmp(games[i].title, "Call of Duty: Black Ops II") && !strcmp(games[i].cat, "DG");
        assert(strstr(games[i].icon, "ICON0.PNG"));
    }
    assert(lbp && cod);
    assert(games_scan(root, NULL, games, 1) == 1);
    puts("PASS: game scan finds HDD games, merges patch+game by Title ID, skips ORBIT/homebrew/save data, respects capacity");
    /* --- PNG --- */
    uint8_t *png = malloc(1 << 20);
    OrbitImage img;
    for (int f = 0; f <= 4; f++) {
        size_t pn = make_png(png, 32, 18, 6, f, 0);
        assert(image_png_decode(png, pn, &img, 320, 176) == IMG_OK && img.w == 32 && img.h == 18);
        image_free(&img);
    }
    size_t pn = make_png(png, 20, 10, 2, 4, 0);
    assert(image_png_decode(png, pn, &img, 0, 0) == IMG_OK && (img.px[0] >> 24) == 255);
    image_free(&img);
    pn = make_png(png, 20, 10, 3, 1, 0);
    assert(image_png_decode(png, pn, &img, 0, 0) == IMG_OK);
    image_free(&img);
    puts("PASS: PNG decode RGBA/RGB/palette with all five row filters");
    pn = make_png(png, 640, 400, 6, 0, 0);
    assert(image_png_decode(png, pn, &img, 320, 176) == IMG_E_LIMIT && !img.px);
    /* decompression bomb: more inflated data than the header allows */
    pn = make_png(png, 16, 16, 6, 0, 500000);
    assert(image_png_decode(png, pn, &img, 0, 0) == IMG_E_DATA && !img.px);
    /* bad CRC, bad signature, truncation */
    pn = make_png(png, 16, 16, 6, 0, 0);
    png[40] ^= 1;
    assert(image_png_decode(png, pn, &img, 0, 0) < 0);
    png[40] ^= 1;
    png[0] = 'x';
    assert(image_png_decode(png, pn, &img, 0, 0) == IMG_E_FORMAT);
    png[0] = 137;
    for (size_t cut = 0; cut < pn; cut++)
        assert(image_png_decode(png, cut, &img, 0, 0) < 0 && !img.px);
    /* fake PNG: an executable / zip with a .png name */
    const uint8_t elf[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    assert(image_png_decode(elf, sizeof(elf), &img, 0, 0) == IMG_E_FORMAT);
    int ok = 0;
    uint8_t *mut = malloc(pn);
    for (int it = 0; it < 20000; it++) {
        memcpy(mut, png, pn);
        for (int m = 0; m < 3; m++)
            mut[8 + rand() % (pn - 8)] = (uint8_t)rand();
        if (it & 1)
            fix_crcs(mut, pn);
        if (image_png_decode(mut, pn, &img, 64, 64) == IMG_OK) {
            assert(img.w <= 64 && img.h <= 64 && img.px);
            ok++;
            image_free(&img);
        }
    }
    free(mut);
    printf("PASS: PNG limits, decompression bomb, CRC, signature, every truncation, ELF disguised as PNG, 20000 mutations (%d decodable)\n", ok);
    /* scaling */
    pn = make_png(png, 320, 176, 6, 0, 0);
    snprintf(p, sizeof(p), "%s/BCES00141/ICON0.PNG", root);
    wfile(p, png, pn);
    assert(image_load_png(p, &img, 320, 176) == IMG_OK);
    OrbitImage small_img, cover;
    assert(image_scale(&img, &small_img, 160, 88) == IMG_OK && small_img.w == 160);
    assert(image_cover(&img, &cover, 100, 100) == IMG_OK && cover.w == 100 && cover.h == 100);
    image_free(&img);
    image_free(&small_img);
    image_free(&cover);
    assert(image_load_png("/nonexistent.png", &img, 0, 0) == IMG_E_IO);
    puts("PASS: ICON0.PNG load from disk, area scaling and cover crop");
    free(png);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    assert(!system(cmd));
    return 0;
}
