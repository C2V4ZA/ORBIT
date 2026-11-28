/* ORBIT 1.3 (PS4): our line in GoldHEN's plugins.ini, see goldhen_plugins.h */
#include "goldhen_plugins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INI_MAX 65536

typedef struct {
    const char *start; /* the whole line without its '\n' */
    size_t len;        /* length without '\n' */
    size_t next;       /* offset of the following line */
    const char *trim;  /* without leading/trailing blanks and '\r' */
    size_t tlen;
} Line;

static int next_line(const char *in, size_t n, size_t at, Line *l) {
    if (at >= n)
        return 0;
    size_t end = at;
    while (end < n && in[end] != '\n')
        end++;
    l->start = in + at;
    l->len = end - at;
    l->next = end < n ? end + 1 : end;
    size_t a = 0, b = l->len;
    while (a < b && (l->start[a] == ' ' || l->start[a] == '\t'))
        a++;
    while (b > a && (l->start[b - 1] == ' ' || l->start[b - 1] == '\t' || l->start[b - 1] == '\r'))
        b--;
    l->trim = l->start + a;
    l->tlen = b - a;
    return 1;
}
static int is_header(const Line *l) { return l->tlen && l->trim[0] == '['; }
static int is_default(const Line *l) {
    static const char d[] = "[default]";
    if (l->tlen != sizeof(d) - 1)
        return 0;
    for (size_t i = 0; i < l->tlen; i++) {
        char c = l->trim[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != d[i])
            return 0;
    }
    return 1;
}
static int is(const Line *l, const char *text) {
    size_t k = strlen(text);
    return l->tlen == k && !memcmp(l->trim, text, k);
}
typedef struct {
    char *p;
    size_t cap, used;
    int bad;
} Out;
static void put(Out *o, const char *s, size_t k) {
    if (o->bad || o->used + k + 1 > o->cap) {
        o->bad = 1;
        return;
    }
    memcpy(o->p + o->used, s, k);
    o->used += k;
    o->p[o->used] = 0;
}
static void puts_(Out *o, const char *s) { put(o, s, strlen(s)); }
static int done(Out *o) { return o->bad ? -1 : (int)o->used; }

int goldhen_ini_listed(const char *in, size_t n, const char *line) {
    Line l;
    int in_default = 0;
    for (size_t at = 0; next_line(in, n, at, &l); at = l.next) {
        if (is_header(&l))
            in_default = is_default(&l);
        else if (in_default && is(&l, line))
            return 1;
    }
    return 0;
}

int goldhen_ini_add(const char *in, size_t n, const char *line, char *out, size_t cap) {
    Out o = {out, cap, 0, 0};
    if (cap)
        out[0] = 0;
    if (goldhen_ini_listed(in, n, line)) {
        put(&o, in, n);
        return done(&o);
    }
    Line l;
    for (size_t at = 0; next_line(in, n, at, &l); at = l.next)
        if (is_default(&l)) {
            /* everything up to and including the first [default] header, then our line */
            int crlf = l.len && l.start[l.len - 1] == '\r';
            put(&o, in, (size_t)(l.start - in) + l.len - (size_t)crlf); /* header without its line end */
            puts_(&o, crlf ? "\r\n" : "\n");
            puts_(&o, line);
            puts_(&o, crlf ? "\r\n" : "\n");
            if (l.next > (size_t)(l.start - in) + l.len) /* skip the header's own '\n' */
                put(&o, in + l.next, n - l.next);
            return done(&o);
        }
    put(&o, in, n);
    if (n && in[n - 1] != '\n')
        puts_(&o, "\n");
    puts_(&o, "[default]\n");
    puts_(&o, line);
    puts_(&o, "\n");
    return done(&o);
}

int goldhen_ini_remove(const char *in, size_t n, const char *line, char *out, size_t cap) {
    Out o = {out, cap, 0, 0};
    if (cap)
        out[0] = 0;
    Line l;
    for (size_t at = 0; next_line(in, n, at, &l); at = l.next)
        if (!is(&l, line)) /* our exact path, in whatever section; nothing else is touched */
            put(&o, l.start, l.next - at);
    return done(&o);
}

/* ---------- files ---------- */
static void path(char *out, size_t cap, const char *root, const char *p) { snprintf(out, cap, "%s%s", root, p); }
static int exists(const char *p) {
    struct stat st;
    return !stat(p, &st);
}
static int read_all(const char *p, char *buf, size_t cap) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, cap, f);
    int big = n == cap && fgetc(f) != EOF;
    fclose(f);
    return big ? -2 : (int)n;
}
static int write_all(const char *p, const char *buf, size_t n) {
    char tmp[620];
    snprintf(tmp, sizeof(tmp), "%s.orbit-tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int bad = fwrite(buf, 1, n, f) != n;
    if (fclose(f) || bad || rename(tmp, p)) {
        remove(tmp);
        return -1;
    }
    return 0;
}
static int copy_file(const char *from, const char *to) {
    char tmp[620];
    snprintf(tmp, sizeof(tmp), "%s.orbit-tmp", to);
    FILE *in = fopen(from, "rb");
    if (!in)
        return -1;
    FILE *o = fopen(tmp, "wb");
    int bad = !o;
    char buf[8192];
    size_t k;
    while (!bad && (k = fread(buf, 1, sizeof(buf), in)) > 0)
        bad = fwrite(buf, 1, k, o) != k;
    bad |= ferror(in);
    fclose(in);
    if (o && fclose(o))
        bad = 1;
    if (bad || rename(tmp, to)) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int goldhen_plugin_installed(const char *root) {
    char ini[600], prx[600];
    path(ini, sizeof(ini), root, GOLDHEN_INI);
    path(prx, sizeof(prx), root, GOLDHEN_PLUGIN);
    if (!exists(prx))
        return 0;
    char *text = malloc(INI_MAX);
    int n = text ? read_all(ini, text, INI_MAX) : -1;
    int yes = n >= 0 && goldhen_ini_listed(text, (size_t)n, GOLDHEN_PLUGIN);
    free(text);
    return yes;
}

int goldhen_plugin_install(const char *root, const char *prx_source) {
    char dir[600], ini[600], prx[600], backup[620];
    path(dir, sizeof(dir), root, GOLDHEN_DIR);
    path(ini, sizeof(ini), root, GOLDHEN_INI);
    path(prx, sizeof(prx), root, GOLDHEN_PLUGIN);
    if (!exists(dir)) /* no GoldHEN on this console: write nothing */
        return -1;
    path(dir, sizeof(dir), root, GOLDHEN_DIR "/plugins");
    mkdir(dir, 0777);
    if (copy_file(prx_source, prx))
        return -1;
    char *text = malloc(INI_MAX), *next = malloc(INI_MAX + 512);
    int rc = -1;
    if (!text || !next)
        goto out;
    int n = read_all(ini, text, INI_MAX);
    if (n == -2) /* larger than we can rewrite safely: leave it alone */
        goto out;
    if (n < 0)
        n = 0; /* no plugins.ini yet */
    else {
        snprintf(backup, sizeof(backup), "%s.orbit-backup", ini);
        if (!exists(backup) && write_all(backup, text, (size_t)n))
            goto out;
    }
    if (goldhen_ini_listed(text, (size_t)n, GOLDHEN_PLUGIN)) {
        rc = 0;
        goto out;
    }
    int k = goldhen_ini_add(text, (size_t)n, GOLDHEN_PLUGIN, next, INI_MAX + 512);
    if (k >= 0 && !write_all(ini, next, (size_t)k))
        rc = 0;
out:
    free(text);
    free(next);
    return rc;
}

int goldhen_plugin_remove(const char *root) {
    char ini[600], prx[600];
    path(ini, sizeof(ini), root, GOLDHEN_INI);
    path(prx, sizeof(prx), root, GOLDHEN_PLUGIN);
    int changed = 0, failed = 0;
    char *text = malloc(INI_MAX), *next = malloc(INI_MAX);
    int n = text && next ? read_all(ini, text, INI_MAX) : -1;
    if (n >= 0) {
        int k = goldhen_ini_remove(text, (size_t)n, GOLDHEN_PLUGIN, next, INI_MAX);
        if (k >= 0 && k != n) {
            if (write_all(ini, next, (size_t)k))
                failed = 1;
            else
                changed = 1;
        }
    }
    free(text);
    free(next);
    if (exists(prx)) {
        if (remove(prx))
            failed = 1;
        else
            changed = 1;
    }
    return failed ? -1 : changed ? 0 : 1;
}
