#include "games.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
/* copy a UTF-8 value: stops at NUL or limit, drops control characters, never splits a sequence */
static void copy_value(char *dst, size_t cap, const uint8_t *src, size_t len) {
    size_t o = 0;
    for (size_t i = 0; i < len && src[i];) {
        uint8_t c = src[i];
        size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!n || i + n > len) {
            i++;
            continue;
        }
        int ok = 1;
        for (size_t k = 1; k < n; k++)
            if ((src[i + k] & 0xc0) != 0x80)
                ok = 0;
        if (!ok) {
            i++;
            continue;
        }
        if (n == 1 && c < 0x20) { /* PS3 titles may contain line breaks: make them spaces */
            if (o && o + 1 < cap && dst[o - 1] != ' ')
                dst[o++] = ' ';
            i++;
            continue;
        }
        if (o + n >= cap)
            break;
        memcpy(dst + o, src + i, n);
        o += n;
        i += n;
    }
    while (o && dst[o - 1] == ' ')
        o--;
    dst[o] = 0;
}
int sfo_parse(const uint8_t *d, size_t len, char *title, size_t title_cap, char *id, size_t id_cap,
              char *cat, size_t cat_cap) {
    if (title && title_cap)
        title[0] = 0;
    if (id && id_cap)
        id[0] = 0;
    if (cat && cat_cap)
        cat[0] = 0;
    if (!d || len < 20 || len > 65536 || memcmp(d, "\0PSF", 4))
        return -1;
    uint32_t keys = le32(d + 8), vals = le32(d + 12), n = le32(d + 16);
    if (n > 256 || 20 + (size_t)n * 16 > len || keys > len || vals > len)
        return -1;
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 20 + i * 16;
        uint32_t ko = keys + le16(e), dlen = le32(e + 4), doff = vals + le32(e + 12);
        uint16_t fmt = le16(e + 2);
        if (ko >= len || doff > len || dlen > len - doff)
            continue;
        const char *key = (const char *)d + ko;
        size_t kmax = len - ko;
        if (!memchr(key, 0, kmax))
            continue;
        if (fmt != 0x0204 && fmt != 0x0004) /* UTF-8 strings only */
            continue;
        if (!strcmp(key, "TITLE") && title)
            copy_value(title, title_cap, d + doff, dlen), found |= 1;
        else if (!strcmp(key, "TITLE_ID") && id)
            copy_value(id, id_cap, d + doff, dlen), found |= 2;
        else if (!strcmp(key, "CATEGORY") && cat)
            copy_value(cat, cat_cap, d + doff, dlen);
    }
    return (found & 3) == 3 && title[0] && id[0] ? 0 : -1;
}
int sfo_read(const char *path, char *title, size_t title_cap, char *id, size_t id_cap, char *cat,
             size_t cat_cap) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    uint8_t *buf = malloc(65536);
    int rc = -1;
    if (buf) {
        size_t n = fread(buf, 1, 65536, f);
        rc = sfo_parse(buf, n, title, title_cap, id, id_cap, cat, cat_cap);
        free(buf);
    }
    fclose(f);
    return rc;
}
int games_is_game_id(const char *id) {
    if (!id || strlen(id) != 9)
        return 0;
    for (int i = 0; i < 4; i++)
        if (id[i] < 'A' || id[i] > 'Z')
            return 0;
    for (int i = 4; i < 9; i++)
        if (id[i] < '0' || id[i] > '9')
            return 0;
    /* B = Blu-ray, N = PSN; second letter: L (licensed), C (first party), P (demo/other) */
    if ((id[0] != 'B' && id[0] != 'N') || (id[1] != 'L' && id[1] != 'C' && id[1] != 'P'))
        return 0;
    if (id[0] == 'N' && id[1] == 'P' && !strchr("EUJAHKI", id[2]))
        return 0;
    if (id[0] == 'B' && !strchr("EUJAHKT", id[2]))
        return 0;
    /* BL**8xxxx is the range homebrew managers use (e.g. BLES80608); BC**9xxxx are real first-party titles */
    if (id[0] == 'B' && id[1] == 'L' && id[4] == '8')
        return 0;
    return 1;
}
char games_region(const char *id) {
    if (!id || strlen(id) < 4)
        return 0;
    char r = id[0] == 'N' ? id[2] : id[2];
    switch (r) {
    case 'E': return 'E';
    case 'U': return 'U';
    case 'J': return 'J';
    case 'A': return 'A';
    case 'K': return 'K';
    case 'H': return 'H';
    default: return 0;
    }
}
static int keep_category(const char *cat) {
    return !strcmp(cat, "HG") || !strcmp(cat, "DG") || !strcmp(cat, "GD");
}
static int add_game(OrbitGame *out, int n, int cap, const OrbitGame *g) {
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i].id, g->id)) {
            /* prefer the disc / the real game folder over a patch (GD) and keep the newest time */
            int better = g->source == GAME_SRC_DISC || (!strcmp(out[i].cat, "GD") && strcmp(g->cat, "GD"));
            uint32_t t = out[i].mtime > g->mtime ? out[i].mtime : g->mtime;
            if (better)
                out[i] = *g;
            out[i].mtime = t;
            return n;
        }
    if (n >= cap)
        return n;
    out[n] = *g;
    return n + 1;
}
static int game_from(const char *dir, int source, OrbitGame *g) {
    char path[256];
    memset(g, 0, sizeof(*g));
    snprintf(path, sizeof(path), "%s/PARAM.SFO", dir);
    if (sfo_read(path, g->title, sizeof(g->title), g->id, sizeof(g->id), g->cat, sizeof(g->cat)))
        return -1;
    if (!games_is_game_id(g->id) || (source == GAME_SRC_HDD && !keep_category(g->cat)))
        return -1;
    struct stat st;
    if (!stat(path, &st))
        g->mtime = (uint32_t)st.st_mtime;
    snprintf(g->icon, sizeof(g->icon), "%s/ICON0.PNG", dir);
    g->source = source;
    return 0;
}
static int cmp_game(const void *a, const void *b) {
    const OrbitGame *x = a, *y = b;
    if (x->source != y->source)
        return y->source - x->source; /* disc first */
    uint32_t tx = x->played > x->mtime ? x->played : x->mtime, ty = y->played > y->mtime ? y->played : y->mtime;
    if (tx != ty)
        return tx < ty ? 1 : -1;
    return strcmp(x->title, y->title);
}
int games_scan(const char *root, const char *disc, OrbitGame *out, int cap) {
    int n = 0;
    OrbitGame g;
    if (!out || cap <= 0)
        return 0;
    if (disc && disc[0]) {
        char d[200];
        snprintf(d, sizeof(d), "%s/PS3_GAME", disc);
        if (!game_from(d, GAME_SRC_DISC, &g))
            n = add_game(out, n, cap, &g);
    }
    DIR *dir = root ? opendir(root) : NULL;
    if (dir) {
        struct dirent *e;
        int seen = 0;
        while ((e = readdir(dir)) && seen < 2048) {
            seen++;
            if (e->d_name[0] == '.' || strlen(e->d_name) > 64)
                continue;
            char d[256];
            snprintf(d, sizeof(d), "%s/%s", root, e->d_name);
            if (!game_from(d, GAME_SRC_HDD, &g))
                n = add_game(out, n, cap, &g);
        }
        closedir(dir);
    }
    qsort(out, (size_t)n, sizeof(*out), cmp_game);
    return n;
}
