/* Game covers by Title ID: config, URL template, untrusted-image check, disk + RAM cache.
 * See include/covers.h and docs/next/COVERS.md. */
#include "covers.h"
#include "games.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int bad_url_char(unsigned char ch) {
    return ch <= 0x20 || ch >= 0x7f || ch == '"' || ch == '\'' || ch == '\\' || ch == '<' || ch == '>' ||
           ch == '`';
}
int covers_template_valid(const char *t) {
    if (!t || strncmp(t, "https://", 8) || strlen(t) >= COVER_URL_MAX)
        return 0;
    const char *host = t + 8;
    if (!*host || *host == '/' || *host == '{')
        return 0;
    int ids = 0, regions = 0;
    for (const char *p = t; *p; p++) {
        if (bad_url_char((unsigned char)*p))
            return 0;
        if (*p == '}')
            return 0; /* a closing brace without its opening one */
        if (*p != '{')
            continue;
        if (!strncmp(p, "{ID}", 4))
            ids++, p += 3;
        else if (!strncmp(p, "{REGION}", 8))
            regions++, p += 7;
        else
            return 0;
    }
    /* the placeholders must be in the path, never in the host name */
    const char *slash = strchr(host, '/');
    const char *brace = strchr(host, '{');
    if (!slash || (brace && brace < slash))
        return 0;
    return ids == 1 && regions <= 1;
}
static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    if (i)
        memmove(s, s + i, strlen(s + i) + 1);
}
int covers_config_parse(const char *text, size_t n, CoverConfig *cfg) {
    if (!cfg)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1; /* default on (brief); the user can switch it off */
    snprintf(cfg->url, sizeof(cfg->url), "%s", COVER_DEFAULT_URL);
    snprintf(cfg->credit, sizeof(cfg->credit), "%s", COVER_DEFAULT_CREDIT);
    if (!text)
        return 0;
    size_t i = 0;
    int own_credit = 0;
    while (i < n) {
        char line[512];
        size_t l = 0;
        while (i < n && text[i] != '\n') {
            if (l + 1 < sizeof(line))
                line[l++] = text[i];
            i++;
        }
        i++;
        line[l] = 0;
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!strcmp(key, "url")) {
            /* "url=none" (or an invalid URL) means: no cover source at all */
            if (covers_template_valid(val))
                snprintf(cfg->url, sizeof(cfg->url), "%s", val);
            else
                cfg->url[0] = 0, cfg->credit[0] = 0;
        } else if (!strcmp(key, "credit")) {
            own_credit = 1;
            /* printable text only; cut between UTF-8 characters */
            size_t o = 0;
            for (size_t k = 0; val[k] && o + 1 < sizeof(cfg->credit); k++) {
                unsigned char ch = (unsigned char)val[k];
                if (ch < 0x20 || ch == 0x7f)
                    continue;
                cfg->credit[o++] = (char)ch;
            }
            /* drop a last character that did not fit completely */
            size_t s = o;
            while (s && ((unsigned char)cfg->credit[s - 1] & 0xC0) == 0x80)
                s--;
            if (s) {
                unsigned char lead = (unsigned char)cfg->credit[s - 1];
                size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                if (o - (s - 1) < need)
                    o = s - 1;
            }
            cfg->credit[o] = 0;
        } else if (!strcmp(key, "enabled")) {
            cfg->enabled = !strcmp(val, "1") || !strcmp(val, "on") || !strcmp(val, "true");
        }
    }
    /* another source never shows the GameTDB credit; it brings its own credit= line */
    if (!own_credit && strcmp(cfg->url, COVER_DEFAULT_URL))
        cfg->credit[0] = 0;
    return 0;
}
const char *covers_region_code(const char *id) {
    switch (games_region(id)) {
    case 'E': return "EN";
    case 'U': return "US";
    case 'J': return "JA";
    case 'K': return "KO";
    case 'A':
    case 'H': return "ZH";
    default: return NULL;
    }
}
/* 1.2.1: folders to try, best first. PAL games: the console's language first (GameTDB has
 * DE, FR, RU... PAL covers), then EN and the other PAL languages; another region only last. */
int covers_regions(const char *id, const char *pref, const char *out[], int cap) {
    static const char *const pal[] = {"EN", "FR", "DE", "ES", "IT", "RU", "AU", "US"};
    static const char *const us[] = {"US", "EN"}, *const ja[] = {"JA", "US"}, *const ko[] = {"KO", "US", "EN"},
                             *const zh[] = {"ZH", "US", "EN"};
    static const char *const pal_lang[] = {"DE", "FR", "ES", "IT", "NL", "PT", "RU", "PL", "SV", "DA", "FI", "NO"};
    const char *const *list = NULL;
    int n = 0, k = 0;
    switch (games_region(id)) {
    case 'E': list = pal, n = 8; break;
    case 'U': list = us, n = 2; break;
    case 'J': list = ja, n = 2; break;
    case 'K': list = ko, n = 3; break;
    case 'A':
    case 'H': list = zh, n = 3; break;
    default: return 0;
    }
    if (list == pal && pref && strlen(pref) == 2)
        for (size_t i = 0; i < sizeof(pal_lang) / sizeof(pal_lang[0]) && k < cap; i++)
            if (!strcmp(pref, pal_lang[i]))
                out[k++] = pal_lang[i];
    for (int i = 0; i < n && k < cap; i++) {
        int dup = 0;
        for (int j = 0; j < k; j++)
            dup |= !strcmp(out[j], list[i]);
        if (!dup)
            out[k++] = list[i];
    }
    return k;
}
int covers_url(const CoverConfig *cfg, const char *id, char *out, size_t cap) {
    return covers_url_region(cfg, id, covers_region_code(id), out, cap);
}
int covers_url_region(const CoverConfig *cfg, const char *id, const char *region, char *out, size_t cap) {
    if (!cfg || !out || !cap)
        return COVER_E_URL;
    out[0] = 0;
    if (!cfg->enabled || !cfg->url[0])
        return COVER_E_OFF;
    if (!id || !games_is_game_id(id))
        return COVER_E_ID;
    if (!covers_template_valid(cfg->url))
        return COVER_E_URL;
    if (region && (strlen(region) != 2 || region[0] < 'A' || region[0] > 'Z' || region[1] < 'A' || region[1] > 'Z'))
        return COVER_E_ID;
    size_t o = 0;
    for (const char *p = cfg->url; *p;) {
        const char *put = NULL;
        size_t skip = 1;
        if (!strncmp(p, "{ID}", 4))
            put = id, skip = 4;
        else if (!strncmp(p, "{REGION}", 8)) {
            if (!region)
                return COVER_E_ID;
            put = region, skip = 8;
        }
        if (put) {
            size_t l = strlen(put);
            if (o + l >= cap)
                return COVER_E_URL;
            memcpy(out + o, put, l);
            o += l;
        } else {
            if (o + 1 >= cap)
                return COVER_E_URL;
            out[o++] = *p;
        }
        p += skip;
    }
    out[o] = 0;
    return COVER_OK;
}
int covers_decode(const uint8_t *data, size_t n, OrbitImage *out) {
    if (!data || !out)
        return COVER_E_IMAGE;
    memset(out, 0, sizeof(*out));
    if (n < 16 || n > COVER_MAX_FILE)
        return COVER_E_IMAGE;
    OrbitImage src = {0};
    int rc;
    if (n >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        rc = image_png_decode(data, n, &src, IMG_MAX_SIDE, IMG_MAX_SIDE);
    else if (data[0] == 0xFF && data[1] == 0xD8)
        rc = image_jpeg_decode(data, n, &src, 0);
    else
        return COVER_E_IMAGE; /* covers are PNG or JPEG; GIF and anything else is refused */
    if (rc != IMG_OK)
        return COVER_E_IMAGE;
    int ok = src.w >= 32 && src.h >= 32 && src.w <= 2 * src.h && src.h <= 3 * src.w;
    if (ok)
        ok = image_cover(&src, out, COVER_W, COVER_H) == IMG_OK;
    image_free(&src);
    if (!ok) {
        image_free(out);
        memset(out, 0, sizeof(*out));
        return COVER_E_IMAGE;
    }
    return COVER_OK;
}
int covers_path(const char *dir, const char *id, char *out, size_t cap) {
    if (!dir || !out || !id || !games_is_game_id(id))
        return COVER_E_ID;
    if (snprintf(out, cap, "%s/%s.cov", dir, id) >= (int)cap)
        return COVER_E_FILE;
    return COVER_OK;
}
int covers_store(const char *dir, const char *id, const uint8_t *data, size_t n) {
    char path[512], tmp[520];
    int rc = covers_path(dir, id, path, sizeof(path));
    if (rc)
        return rc;
    if (!data || !n || n > COVER_MAX_FILE)
        return COVER_E_IMAGE;
    mkdir(dir, 0700);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return COVER_E_FILE;
    int ok = fwrite(data, 1, n, f) == n;
    if (fclose(f) || !ok) {
        remove(tmp);
        return COVER_E_FILE;
    }
    remove(path); /* PS3: rename never overwrites */
    if (rename(tmp, path)) {
        remove(tmp);
        return COVER_E_FILE;
    }
    return COVER_OK;
}
static int is_cover_name(const char *name) {
    size_t l = strlen(name);
    if (l == 13 && !strcmp(name + 9, ".cov"))
        return 1;
    if (l == 13 && !strcmp(name + 9, ".nil"))
        return 1;
    return l == 17 && !strcmp(name + 9, ".cov.tmp");
}
/* "<ID>.nil": the source had no usable cover; ask again after COVER_RETRY seconds */
void covers_nil_path(const char *dir, const char *id, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s.nil", dir, id);
}
void covers_mark_missing(const char *dir, const char *id) {
    char p[512];
    if (!games_is_game_id(id))
        return;
    mkdir(dir, 0700);
    covers_nil_path(dir, id, p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (f)
        fclose(f);
}
static int read_file(const char *path, uint8_t **out, size_t *n);
static char local_dirs[COVER_LOCAL_MAX][160];
static int local_n;
void covers_set_local(const char *const *dirs, int n) {
    local_n = 0;
    for (int i = 0; dirs && i < n && local_n < COVER_LOCAL_MAX; i++) {
        if (!dirs[i] || !dirs[i][0] || strlen(dirs[i]) >= sizeof(local_dirs[0]))
            continue;
        DIR *d = opendir(dirs[i]);
        if (!d)
            continue;
        closedir(d);
        snprintf(local_dirs[local_n++], sizeof(local_dirs[0]), "%s", dirs[i]);
    }
}
int covers_local_count(void) {
    return local_n;
}
int covers_local_find(const char *id, char *path, size_t cap) {
    static const char *const ext[] = {".JPG", ".PNG", ".jpg", ".png"};
    char check[512];
    if (!path || covers_path("x", id, check, sizeof(check)))
        return 0; /* only real game Title IDs: nothing else can reach a file name */
    struct stat st;
    for (int d = 0; d < local_n; d++)
        for (int e = 0; e < 4; e++) {
            snprintf(path, cap, "%s/%s%s", local_dirs[d], id, ext[e]);
            if (!stat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= (long)COVER_MAX_FILE)
                return 1;
        }
    return 0;
}
int covers_need(const char *dir, const char *id, long now) {
    char p[512];
    struct stat st;
    if (covers_path(dir, id, p, sizeof(p)))
        return 0;
    {
        char lp[512];
        uint8_t *b = NULL;
        size_t n = 0;
        if (covers_local_find(id, lp, sizeof(lp)) && !read_file(lp, &b, &n)) {
            OrbitImage im;
            int ok = !covers_decode(b, n, &im);
            free(b);
            if (ok) {
                image_free(&im);
                return 0; /* the user already has a usable one on the console */
            }
        }
    }
    if (!stat(p, &st))
        return 0; /* already cached */
    covers_nil_path(dir, id, p, sizeof(p));
    if (!stat(p, &st) && (long)st.st_mtime <= now && now - (long)st.st_mtime < COVER_RETRY)
        return 0; /* the source had nothing a moment ago */
    return 1;
}
int covers_clear_disk(const char *dir) {
    return covers_trim_disk(dir, 0);
}
int covers_trim_disk(const char *dir, int keep) {
    if (!dir || keep < 0)
        return 0;
    enum { MAXF = 1024 };
    struct Item { char name[20]; long t; } *items = calloc(MAXF, sizeof(*items));
    if (!items)
        return 0;
    int n = 0, removed = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < MAXF) {
            if (!is_cover_name(e->d_name))
                continue;
            char p[600];
            struct stat st;
            snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
            if (strstr(e->d_name, ".tmp")) { /* left over from an interrupted download */
                removed += !remove(p);
                continue;
            }
            if (stat(p, &st))
                continue;
            snprintf(items[n].name, sizeof(items[n].name), "%s", e->d_name);
            items[n].t = (long)st.st_mtime;
            n++;
        }
        closedir(d);
    }
    while (n > keep) {
        int old = 0;
        for (int i = 1; i < n; i++)
            if (items[i].t < items[old].t ||
                (items[i].t == items[old].t && strcmp(items[i].name, items[old].name) < 0))
                old = i;
        char p[600];
        snprintf(p, sizeof(p), "%s/%s", dir, items[old].name);
        removed += !remove(p);
        items[old] = items[--n];
    }
    free(items);
    return removed;
}

/* RAM cache */
void (*covers_evict_hook)(const uint32_t *px);
static CoverSlot *find(CoverCache *c, const char *id) {
    for (int i = 0; i < COVER_RAM_SLOTS; i++)
        if (c->slot[i].state != COVER_SLOT_EMPTY && !strcmp(c->slot[i].id, id))
            return &c->slot[i];
    return NULL;
}
static CoverSlot *victim(CoverCache *c) {
    CoverSlot *v = &c->slot[0];
    for (int i = 0; i < COVER_RAM_SLOTS; i++) {
        if (c->slot[i].state == COVER_SLOT_EMPTY)
            return &c->slot[i];
        if (c->slot[i].used < v->used)
            v = &c->slot[i];
    }
    if (covers_evict_hook && v->img.px)
        covers_evict_hook(v->img.px);
    image_free(&v->img);
    memset(v, 0, sizeof(*v));
    return v;
}
static int read_file(const char *path, uint8_t **out, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    uint8_t *b = malloc(COVER_MAX_FILE + 1);
    size_t k = b ? fread(b, 1, COVER_MAX_FILE + 1, f) : 0;
    fclose(f);
    if (!b || !k || k > COVER_MAX_FILE) {
        free(b);
        return -1;
    }
    *out = b, *n = k;
    return 0;
}
int covers_load(const char *dir, const char *id, OrbitImage *out) {
    char path[512];
    if (!out)
        return COVER_E_IMAGE;
    memset(out, 0, sizeof(*out));
    int rc = covers_path(dir, id, path, sizeof(path));
    if (rc)
        return rc;
    uint8_t *b = NULL;
    size_t n = 0;
    { /* 1.3: the user's own cover first; a broken one is skipped, never deleted */
        char lp[512];
        if (covers_local_find(id, lp, sizeof(lp)) && !read_file(lp, &b, &n)) {
            rc = covers_decode(b, n, out);
            free(b);
            b = NULL;
            if (!rc)
                return 0;
            memset(out, 0, sizeof(*out));
        }
    }
    if (read_file(path, &b, &n))
        return COVER_E_NONE;
    rc = covers_decode(b, n, out);
    free(b);
    if (rc)
        remove(path); /* a broken file is not kept */
    return rc;
}
const OrbitImage *covers_get(CoverCache *c, const char *dir, const char *id) {
    char path[512];
    if (!c || covers_path(dir, id, path, sizeof(path)))
        return NULL;
    CoverSlot *s = find(c, id);
    if (s) {
        s->used = ++c->tick;
        return s->state == COVER_SLOT_READY ? &s->img : NULL;
    }
    s = victim(c);
    snprintf(s->id, sizeof(s->id), "%s", id);
    s->used = ++c->tick;
    s->state = covers_load(dir, id, &s->img) ? COVER_SLOT_MISSING : COVER_SLOT_READY;
    return s->state == COVER_SLOT_READY ? &s->img : NULL;
}
void covers_forget(CoverCache *c, const char *id) {
    if (!c || !id)
        return;
    CoverSlot *s = find(c, id);
    if (s) {
        if (covers_evict_hook && s->img.px)
            covers_evict_hook(s->img.px);
        image_free(&s->img);
        memset(s, 0, sizeof(*s));
    }
}
void covers_cache_free(CoverCache *c) {
    if (!c)
        return;
    for (int i = 0; i < COVER_RAM_SLOTS; i++) {
        if (covers_evict_hook && c->slot[i].img.px)
            covers_evict_hook(c->slot[i].img.px);
        image_free(&c->slot[i].img);
    }
    memset(c, 0, sizeof(*c));
}
int covers_cache_count(const CoverCache *c) {
    int k = 0;
    for (int i = 0; c && i < COVER_RAM_SLOTS; i++)
        k += c->slot[i].state == COVER_SLOT_READY;
    return k;
}
int covers_peek(const CoverCache *c, const char *id) {
    for (int i = 0; c && id && i < COVER_RAM_SLOTS; i++)
        if (c->slot[i].state != COVER_SLOT_EMPTY && !strcmp(c->slot[i].id, id))
            return c->slot[i].state;
    return -1;
}
void covers_forget_missing(CoverCache *c) {
    for (int i = 0; c && i < COVER_RAM_SLOTS; i++)
        if (c->slot[i].state == COVER_SLOT_MISSING)
            memset(&c->slot[i], 0, sizeof(c->slot[i]));
}
