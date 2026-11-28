/* ORBIT NEXT: game covers - config, URL template, untrusted images, disk + bounded RAM cache.
 * No network: covers_fetch is only checked for "off / no source / bad id". */
#include "covers.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <zlib.h>
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
/* RGB PNG, w x h, simple gradient */
static size_t make_png(uint8_t *o, int w, int h) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    size_t stride = (size_t)w * 3, rawn = (stride + 1) * h;
    uint8_t *raw = calloc(1, rawn), *z = malloc(compressBound(rawn));
    for (int y = 0; y < h; y++)
        for (size_t x = 0; x < stride; x++)
            raw[y * (stride + 1) + 1 + x] = (uint8_t)(x + y);
    uLongf zn = compressBound(rawn);
    compress(z, &zn, raw, rawn);
    size_t n = 8;
    memcpy(o, sig, 8);
    uint8_t ih[13] = {0};
    be(ih, (uint32_t)w);
    be(ih + 4, (uint32_t)h);
    ih[8] = 8, ih[9] = 2;
    n += chunk(o + n, "IHDR", ih, 13);
    n += chunk(o + n, "IDAT", z, (uint32_t)zn);
    n += chunk(o + n, "IEND", NULL, 0);
    free(raw);
    free(z);
    return n;
}
static size_t read_fixture(const char *name, uint8_t *buf, size_t cap) {
    char p[256];
    snprintf(p, sizeof(p), "tests/fixtures/%s", name);
    FILE *f = fopen(p, "rb");
    assert(f);
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static int count_files(const char *dir) {
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)))
        n += e->d_name[0] != '.';
    if (d)
        closedir(d);
    return n;
}
static void set_mtime(const char *dir, const char *id, long t) {
    char p[256];
    snprintf(p, sizeof(p), "%s/%s.cov", dir, id);
    struct utimbuf u = {t, t};
    assert(!utime(p, &u));
}
int main(void) {
    /* template rules */
    assert(covers_template_valid("https://covers.example.org/ps3/{REGION}/{ID}.jpg"));
    assert(covers_template_valid("https://covers.example.org/{ID}.png"));
    assert(!covers_template_valid("http://covers.example.org/{ID}.png"));          /* https only */
    assert(!covers_template_valid("https://covers.example.org/cover.png"));        /* no {ID} */
    assert(!covers_template_valid("https://covers.example.org/{ID}/{ID}.png"));    /* two {ID} */
    assert(!covers_template_valid("https://covers.example.org/{TITLE}/{ID}.png")); /* unknown */
    assert(!covers_template_valid("https://{ID}.example.org/x.png"));              /* host */
    assert(!covers_template_valid("https://covers.example.org/{ID} .png"));        /* space */
    assert(!covers_template_valid("https://covers.example.org/{ID}\".png"));
    assert(!covers_template_valid("https://covers.example.org/{ID}}.png"));
    assert(!covers_template_valid("https://covers.example.org"));
    assert(!covers_template_valid("https:///{ID}.png"));
    assert(!covers_template_valid(NULL));

    /* region codes from the Title ID */
    assert(!strcmp(covers_region_code("BLES00680"), "EN"));
    assert(!strcmp(covers_region_code("BLUS30443"), "US"));
    assert(!strcmp(covers_region_code("BLJM60005"), "JA"));
    assert(!strcmp(covers_region_code("NPEB00001"), "EN"));
    assert(!strcmp(covers_region_code("BCAS20001"), "ZH"));
    assert(!strcmp(covers_region_code("BLKS20001"), "KO"));
    assert(covers_region_code("XXXX00000") == NULL);
    /* 1.2.1: folders to try, best first */
    const char *rg[COVER_TRIES];
    int rn = covers_regions("BLES00680", "DE", rg, COVER_TRIES);
    assert(rn == 8 && !strcmp(rg[0], "DE") && !strcmp(rg[1], "EN") && !strcmp(rg[2], "FR") &&
           !strcmp(rg[rn - 1], "US")); /* German console: German PAL cover first, DE not twice */
    rn = covers_regions("BLES00680", NULL, rg, COVER_TRIES);
    assert(rn == 8 && !strcmp(rg[0], "EN"));
    rn = covers_regions("BLES00680", "JA", rg, COVER_TRIES); /* not a PAL language: ignored */
    assert(rn == 8 && !strcmp(rg[0], "EN"));
    rn = covers_regions("BLES00680", "../", rg, COVER_TRIES);
    assert(rn == 8 && !strcmp(rg[0], "EN"));
    rn = covers_regions("BLUS30443", "DE", rg, COVER_TRIES);
    assert(rn == 2 && !strcmp(rg[0], "US") && !strcmp(rg[1], "EN"));
    rn = covers_regions("BLJM60005", NULL, rg, COVER_TRIES);
    assert(rn == 2 && !strcmp(rg[0], "JA"));
    assert(covers_regions("XXXX00000", "DE", rg, COVER_TRIES) == 0);
    assert(covers_regions("BLES00680", "DE", rg, 3) == 3);

    /* config */
    CoverConfig cfg;
    /* no covers.cfg: built-in GameTDB source, on, with its credit */
    assert(!covers_config_parse(NULL, 0, &cfg) && cfg.enabled);
    assert(!strcmp(cfg.url, COVER_DEFAULT_URL) && covers_template_valid(cfg.url));
    assert(strstr(cfg.credit, "GameTDB"));
    char url[400];
    assert(!covers_url(&cfg, "BLES00680", url, sizeof(url)));
    assert(!strcmp(url, "https://art.gametdb.com/ps3/coverM/EN/BLES00680.jpg"));
    assert(!covers_url(&cfg, "BLJM60066", url, sizeof(url)));
    assert(!strcmp(url, "https://art.gametdb.com/ps3/coverM/JA/BLJM60066.jpg"));
    /* "url=none": no source at all, nothing is sent */
    const char *none = "url=none\n";
    assert(!covers_config_parse(none, strlen(none), &cfg) && !cfg.url[0] && !cfg.credit[0]);
    assert(covers_url(&cfg, "BLES00680", url, sizeof(url)) == COVER_E_OFF && !url[0]);
    /* own source without credit= does not inherit the GameTDB credit */
    const char *own = "url=https://covers.example.org/{ID}.png\n";
    assert(!covers_config_parse(own, strlen(own), &cfg) && !cfg.credit[0]);
    const char *text = "# cover source\r\n url = https://covers.example.org/ps3/{REGION}/{ID}.jpg \n"
                       "credit=Covers: example.org \xC3\xA9\x01\nunknown=1\nenabled=1\n";
    assert(!covers_config_parse(text, strlen(text), &cfg));
    assert(!strcmp(cfg.url, "https://covers.example.org/ps3/{REGION}/{ID}.jpg"));
    assert(!strcmp(cfg.credit, "Covers: example.org \xC3\xA9"));
    char longc[400] = "credit=x";
    for (int i = 0; i < 100; i++)
        strcat(longc, "\xC3\xA9"); /* 1 + 2*n bytes: the cut must fall between characters */
    CoverConfig lc;
    assert(!covers_config_parse(longc, strlen(longc), &lc));
    assert(strlen(lc.credit) == 95 && (strlen(lc.credit) - 1) % 2 == 0);
    assert(!covers_url(&cfg, "BLES00680", url, sizeof(url)));
    assert(!strcmp(url, "https://covers.example.org/ps3/EN/BLES00680.jpg"));
    assert(!covers_url_region(&cfg, "BLES00680", "DE", url, sizeof(url)));
    assert(!strcmp(url, "https://covers.example.org/ps3/DE/BLES00680.jpg"));
    assert(covers_url_region(&cfg, "BLES00680", "d/", url, sizeof(url)) == COVER_E_ID);
    assert(covers_url_region(&cfg, "BLES00680", "DEU", url, sizeof(url)) == COVER_E_ID);
    assert(!covers_url(&cfg, "BLUS30443", url, sizeof(url)));
    assert(!strcmp(url, "https://covers.example.org/ps3/US/BLUS30443.jpg"));
    assert(covers_url(&cfg, "BLES80608", url, sizeof(url)) == COVER_E_ID); /* homebrew range */
    assert(covers_url(&cfg, "../../etc", url, sizeof(url)) == COVER_E_ID);
    assert(covers_url(&cfg, "BLES0068", url, sizeof(url)) == COVER_E_ID);
    assert(covers_url(&cfg, "BLES00680", url, 20) == COVER_E_URL); /* too small, no overflow */
    const char *off = "url=https://covers.example.org/{ID}.png\nenabled=0\n";
    assert(!covers_config_parse(off, strlen(off), &cfg) && !cfg.enabled);
    assert(covers_url(&cfg, "BLES00680", url, sizeof(url)) == COVER_E_OFF);
    assert(covers_fetch(&cfg, "/nonexistent", "BLES00680", "DE") == COVER_E_OFF); /* never touches the net */
    const char *bad = "url=http://covers.example.org/{ID}.png\n";
    assert(!covers_config_parse(bad, strlen(bad), &cfg) && !cfg.url[0]);

    /* untrusted images */
    uint8_t *buf = malloc(COVER_MAX_FILE + 64);
    OrbitImage img;
    size_t n = make_png(buf, 300, 420);
    assert(!covers_decode(buf, n, &img) && img.w == COVER_W && img.h == COVER_H && img.px);
    image_free(&img);
    n = read_fixture("base420.jpg", buf, COVER_MAX_FILE);
    assert(!covers_decode(buf, n, &img) && img.w == COVER_W && img.h == COVER_H);
    image_free(&img);
    n = make_png(buf, 16, 16); /* too small */
    assert(covers_decode(buf, n, &img) == COVER_E_IMAGE && !img.px);
    n = make_png(buf, 600, 100); /* banner, not a cover */
    assert(covers_decode(buf, n, &img) == COVER_E_IMAGE && !img.px);
    n = read_fixture("anim.gif", buf, COVER_MAX_FILE); /* GIF refused */
    assert(covers_decode(buf, n, &img) == COVER_E_IMAGE);
    static const uint8_t elf[32] = {0x7f, 'E', 'L', 'F', 2, 2, 1};
    assert(covers_decode(elf, sizeof(elf), &img) == COVER_E_IMAGE);
    memset(buf, 0, COVER_MAX_FILE + 1);
    memcpy(buf, "\x89PNG\r\n\x1a\n", 8);
    assert(covers_decode(buf, COVER_MAX_FILE + 1, &img) == COVER_E_IMAGE); /* oversize */
    n = make_png(buf, 300, 420);
    for (size_t cut = 0; cut < n; cut += 37)
        assert(covers_decode(buf, cut, &img) == COVER_E_IMAGE && !img.px);
    n = read_fixture("base420.jpg", buf, COVER_MAX_FILE);
    for (size_t cut = 0; cut < n; cut += 53) {
        int rc = covers_decode(buf, cut, &img);
        assert(rc == COVER_OK || rc == COVER_E_IMAGE);
        image_free(&img);
    }

    /* disk cache: paths, PS3-safe store, broken files dropped, trim */
    const char *dir = "build/test-covers-dir";
    mkdir("build", 0755);
    covers_clear_disk(dir);
    char path[300];
    assert(covers_path(dir, "../x", path, sizeof(path)) == COVER_E_ID);
    assert(covers_path(dir, "BLES00680", path, sizeof(path)) == 0 && strstr(path, "/BLES00680.cov"));
    assert(covers_store(dir, "BLES80608", buf, n) == COVER_E_ID);
    n = make_png(buf, 300, 420);
    assert(!covers_store(dir, "BLES00680", buf, n));
    assert(!covers_store(dir, "BLES00680", buf, n)); /* replace an existing file */
    n = read_fixture("base420.jpg", buf, COVER_MAX_FILE);
    assert(!covers_store(dir, "BLUS30443", buf, n));
    assert(!covers_store(dir, "BLJM60005", (const uint8_t *)"not a picture at all", 20));

    CoverCache cache;
    memset(&cache, 0, sizeof(cache));
    const OrbitImage *c1 = covers_get(&cache, dir, "BLES00680");
    assert(c1 && c1->w == COVER_W && c1->h == COVER_H);
    assert(covers_get(&cache, dir, "BLES00680") == c1);            /* cached */
    assert(covers_get(&cache, dir, "BLUS30443"));
    assert(covers_get(&cache, dir, "BLJM60005") == NULL);          /* broken file */
    assert(access("build/test-covers-dir/BLJM60005.cov", F_OK));   /* ...and removed */
    assert(covers_get(&cache, dir, "BCES00001") == NULL);          /* no file: remembered */
    assert(covers_get(&cache, dir, "../etc") == NULL);
    assert(covers_cache_count(&cache) == 2);
    /* the cache never holds more than COVER_RAM_SLOTS pictures; least recently used goes */
    char id[16];
    for (int i = 0; i < 30; i++) {
        snprintf(id, sizeof(id), "BLES%05d", 10000 + i);
        n = make_png(buf, 64, 80);
        assert(!covers_store(dir, id, buf, n));
        assert(covers_get(&cache, dir, id));
        covers_get(&cache, dir, "BLES00680"); /* keep this one hot */
        assert(covers_cache_count(&cache) <= COVER_RAM_SLOTS);
    }
    assert(covers_cache_count(&cache) == COVER_RAM_SLOTS);
    assert(covers_get(&cache, dir, "BLES00680")); /* still loaded */
    covers_forget(&cache, "BLES00680");
    assert(covers_cache_count(&cache) == COVER_RAM_SLOTS - 1);
    covers_cache_free(&cache);
    assert(covers_cache_count(&cache) == 0);

    /* covers_need: cached -> no; recent "nothing there" marker -> no; old marker -> yes */
    long now = 2000000000L;
    assert(!covers_need(dir, "BLES10000", now));   /* cached above */
    assert(covers_need(dir, "BLUS31111", now));    /* never asked */
    assert(!covers_need(dir, "BLES80608", now));   /* homebrew: never sent */
    FILE *nil = fopen("build/test-covers-dir/BLUS31111.nil", "wb");
    assert(nil);
    fclose(nil);
    struct utimbuf un = {now - 60, now - 60};
    assert(!utime("build/test-covers-dir/BLUS31111.nil", &un));
    assert(!covers_need(dir, "BLUS31111", now));
    un.actime = un.modtime = now - COVER_RETRY - 1;
    assert(!utime("build/test-covers-dir/BLUS31111.nil", &un));
    assert(covers_need(dir, "BLUS31111", now));
    un.actime = un.modtime = now + 3600; /* clock went back: ask again */
    assert(!utime("build/test-covers-dir/BLUS31111.nil", &un));
    assert(covers_need(dir, "BLUS31111", now));

    /* trim keeps the newest files; leftovers from interrupted downloads go */
    covers_clear_disk(dir);
    assert(count_files(dir) == 0);
    for (int i = 0; i < 5; i++) {
        snprintf(id, sizeof(id), "BLUS%05d", 30000 + i);
        assert(!covers_store(dir, id, buf, n));
        set_mtime(dir, id, 1000000 + i);
    }
    FILE *t = fopen("build/test-covers-dir/BLUS39999.cov.tmp", "wb");
    assert(t);
    fclose(t);
    FILE *other = fopen("build/test-covers-dir/keep.txt", "wb"); /* not ours: untouched */
    assert(other);
    fclose(other);
    assert(covers_trim_disk(dir, 3) == 3); /* 2 oldest + the .tmp */
    assert(access("build/test-covers-dir/BLUS30000.cov", F_OK) && access("build/test-covers-dir/BLUS30001.cov", F_OK));
    assert(!access("build/test-covers-dir/BLUS30004.cov", F_OK));
    assert(!access("build/test-covers-dir/keep.txt", F_OK));
    assert(covers_clear_disk(dir) == 3);
    remove("build/test-covers-dir/keep.txt");
    rmdir(dir);
    free(buf);
    printf("PASS covers\n");

    { /* 1.3 stage 1: covers the user already has (multiMAN/webMAN/USB folder), read only */
        FILE *src = fopen("tests/fixtures/base420.jpg", "rb");
        assert(src);
        static uint8_t jpg[1 << 20];
        size_t jn = fread(jpg, 1, sizeof(jpg), src);
        fclose(src);
        assert(!system("rm -rf build/test-covers-local build/test-covers-cache && mkdir -p build/test-covers-local build/test-covers-cache"));
        FILE *w = fopen("build/test-covers-local/BLES00680.JPG", "wb");
        fwrite(jpg, 1, jn, w);
        fclose(w);
        w = fopen("build/test-covers-local/BLUS12345.PNG", "wb"); /* broken: must stay untouched */
        fwrite("not a png", 1, 9, w);
        fclose(w);
        const char *dirs[] = {"/nonexistent/covers", "build/test-covers-local"};
        covers_set_local(dirs, 2);
        assert(covers_local_count() == 1); /* missing folders are dropped */
        char lp[512];
        assert(covers_local_find("BLES00680", lp, sizeof(lp)) && strstr(lp, "BLES00680.JPG"));
        assert(!covers_local_find("../../etc", lp, sizeof(lp)) && !covers_local_find("BLES80608", lp, sizeof(lp)));
        OrbitImage im;
        assert(covers_load("build/test-covers-cache", "BLES00680", &im) == COVER_OK && im.w == COVER_W && im.h == COVER_H);
        image_free(&im);
        assert(!covers_need("build/test-covers-cache", "BLES00680", 1000)); /* no download needed */
        assert(covers_load("build/test-covers-cache", "BLUS12345", &im) != COVER_OK);
        assert(!access("build/test-covers-local/BLUS12345.PNG", F_OK)); /* the user's file is kept */
        assert(covers_need("build/test-covers-cache", "BLUS12345", 1000));
        covers_set_local(NULL, 0);
        assert(!system("rm -rf build/test-covers-local build/test-covers-cache"));
        printf("PASS: local covers (multiMAN/webMAN/USB folders): used first, strict decode, never deleted, "
               "no download for them, only Title IDs as names\n");
    }
    return 0;
}
