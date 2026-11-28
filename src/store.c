/* Orbit meeting point ("Treffpunkt"): everything a PS3 hosts - servers with categories,
 * channels and roles, private conversations, the owner's friend list - plus the request handler.
 * Every permission is checked here, on the host; the interface is never a security boundary. */
#include <time.h>
#include "avatar.h"
#include "utf8copy.h"
#include "anim.h"
#include "i18n.h"
#include "monocypher.h"
#include "presence.h"
#include "watch.h"
#include "orbit.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int is_banned(const OrbitGroup *g, int uid);
#include <unistd.h>
const char *orbit_string(const cJSON *o, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static int number(const cJSON *o, const char *key, int fallback) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) && v->valuedouble == v->valueint ? v->valueint : fallback;
}
static int has(const cJSON *o, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(o, key) != NULL;
}
static cJSON *ok(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", 1);
    return r;
}
cJSON *orbit_error(const char *text) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", 0);
    cJSON_AddStringToObject(r, "error", text);
    return r;
}
/* Error with a stable key: a 1.0 client shows it in its own language, a 0.9 client gets the
 * text in the host's language. */
static int host_lang = LANG_DE;
cJSON *orbit_fail(int key) {
    cJSON *r = orbit_error(T_in(host_lang, key));
    cJSON_AddStringToObject(r, "ekey", i18n_name(key));
    return r;
}
#define fail orbit_fail
/* Eight base32 characters: 40 random bits, with no I/O/0/1 ambiguity. */
static int new_group_code(const OrbitData *d, char out[33]) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    _Static_assert(sizeof(alphabet) == 33, "32 symbol alphabet");
    for (int attempt = 0; attempt < 16; attempt++) {
        uint8_t random[8];
        if (orbit_random(random, sizeof(random)))
            return -1;
        for (int i = 0; i < 8; i++)
            out[i] = alphabet[random[i] & 31];
        out[8] = 0;
        int duplicate = 0;
        for (int i = 0; i < ORBIT_GROUPS; i++)
            if (d->group[i].used && !strcmp(d->group[i].invite, out))
                duplicate = 1;
        if (!duplicate)
            return 0;
    }
    return -1;
}
static int password_hash(uint8_t out[32], const char *pass, const uint8_t salt[16]) {
    void *work = malloc(8 * 1024 * 1024);
    if (!work)
        return -1;
    crypto_argon2_config c = {CRYPTO_ARGON2_ID, 8192, 3, 1};
    crypto_argon2_inputs in = {(const uint8_t *)pass, salt, (uint32_t)strlen(pass), 16};
    crypto_argon2(out, 32, work, c, in, crypto_argon2_no_extras);
    crypto_wipe(work, 8 * 1024 * 1024);
    free(work);
    return 0;
}
static void normalize_email(char out[97], const char *in) {
    size_t n = strlen(in);
    if (n > 96)
        n = 96;
    for (size_t i = 0; i < n; i++)
        out[i] = (char)tolower((unsigned char)in[i]);
    out[n] = 0;
}
static int valid_email(const char *s) {
    size_t n = strlen(s);
    if (n < 3 || n > 96)
        return 0;
    const char *a = strchr(s, '@');
    if (!a || a == s || !a[1] || strchr(a + 1, '@'))
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] <= 32 || (unsigned char)s[i] >= 127)
            return 0;
    return 1;
}
/* Optional text field: empty allowed. Returns -1 when present but invalid. */
static int opt_text(char *dst, size_t cap, const cJSON *o, const char *key, size_t max) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v)
        return 0;
    if (!cJSON_IsString(v))
        return -1;
    if (v->valuestring[0] && !orbit_text_valid(v->valuestring, 1, max))
        return -1;
    orbit_copy(dst, cap, v->valuestring);
    return 1;
}
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
/* ---------- defaults ---------- */
static void default_roles(OrbitGroup *g) {
    static const struct {
        const char *name;
        int prio;
        uint32_t color, perms;
    } r[4] = {{"Owner", 100, 0xf4b445, P_ALL},
              {"Admin", 80, 0xff6f6f, P_ALL},
              {"Moderator", 50, 0x39d98a,
               P_INVITE | P_SEND | P_DELETE | P_KICK | P_MUTE | P_MANAGE_VOICE},
              {"Member", 10, 0x9aa1ab, P_INVITE | P_SEND}};
    memset(g->role, 0, sizeof(g->role));
    for (int i = 0; i < 4; i++) {
        g->role[i].used = 1;
        g->role[i].priority = r[i].prio;
        g->role[i].color = r[i].color;
        g->role[i].perms = r[i].perms;
        orbit_copy(g->role[i].name, sizeof(g->role[i].name), r[i].name);
    }
    for (int u = 0; u < ORBIT_USERS; u++)
        g->roles[u] = 1 << 3; /* everyone starts as Member */
    g->roles[g->owner] = 1 << 0;
}
static int add_channel(OrbitGroup *g, const char *name, int type, int cat) {
    int pos = 0;
    for (int i = 0; i < ORBIT_CHANNELS; i++)
        if (g->ch[i].used && g->ch[i].pos >= pos)
            pos = g->ch[i].pos + 1;
    for (int i = 0; i < ORBIT_CHANNELS; i++)
        if (!g->ch[i].used) {
            memset(&g->ch[i], 0, sizeof(g->ch[i]));
            g->ch[i].used = 1;
            g->ch[i].type = type == CH_VOICE ? CH_VOICE : CH_TEXT;
            g->ch[i].category = cat;
            g->ch[i].pos = pos;
            orbit_copy(g->ch[i].name, sizeof(g->ch[i].name), name);
            return i;
        }
    return -1;
}
static int add_category(OrbitGroup *g, const char *name) {
    int pos = 0;
    for (int i = 0; i < ORBIT_CATEGORIES; i++)
        if (g->cat[i].used && g->cat[i].pos >= pos)
            pos = g->cat[i].pos + 1;
    for (int i = 0; i < ORBIT_CATEGORIES; i++)
        if (!g->cat[i].used) {
            memset(&g->cat[i], 0, sizeof(g->cat[i]));
            g->cat[i].used = 1;
            g->cat[i].pos = pos;
            orbit_copy(g->cat[i].name, sizeof(g->cat[i].name), name);
            return i;
        }
    return -1;
}
/* A 0.9.5 group (or a fresh server without a template) becomes a server with one text channel
 * and one voice channel. Old messages stay in the text channel (channel 0). */
static void default_server(OrbitGroup *g, int lang, int voice) {
    memset(g->cat, 0, sizeof(g->cat));
    memset(g->ch, 0, sizeof(g->ch));
    add_channel(g, T_in(lang, T_CH_GENERAL), CH_TEXT, -1);
    if (voice)
        add_channel(g, T_in(lang, T_CH_VOICE), CH_VOICE, -1);
    default_roles(g);
}
/* ---------- persistence: data.json (atomic, with backup) ---------- */
static void hex_or_empty(cJSON *o, const char *key, const uint8_t *b, size_t n, int present) {
    char h[129];
    if (present)
        orbit_hex(h, b, n);
    else
        h[0] = 0;
    cJSON_AddStringToObject(o, key, h);
}
static cJSON *encode(const OrbitData *d) {
    cJSON *root = cJSON_CreateObject(), *users = cJSON_AddArrayToObject(root, "users"),
          *groups = cJSON_AddArrayToObject(root, "groups"),
          *msgs = cJSON_AddArrayToObject(root, "messages");
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddNumberToObject(root, "next_id", d->next_id);
    cJSON_AddNumberToObject(root, "rev", d->rev);
    cJSON_AddNumberToObject(root, "owner_user", d->owner_user);
    for (int i = 0; i < d->users; i++) {
        const OrbitUser *u = &d->user[i];
        cJSON *v = cJSON_CreateObject();
        char salt[33], hash[65];
        orbit_hex(salt, u->salt, 16);
        orbit_hex(hash, u->hash, 32);
        cJSON_AddStringToObject(v, "email", u->email);
        cJSON_AddStringToObject(v, "name", u->name);
        cJSON_AddStringToObject(v, "salt", salt);
        cJSON_AddStringToObject(v, "hash", hash);
        cJSON *devices = cJSON_AddArrayToObject(v, "devices");
        for (int j = 0; j < 4; j++) {
            char h[65];
            orbit_hex(h, u->devices[j], 32);
            cJSON_AddItemToArray(devices, cJSON_CreateString(h));
        }
        hex_or_empty(v, "pub", u->pub, 32, u->has_pub);
        hex_or_empty(v, "watch", u->watch, 32, u->has_watch);
        cJSON_AddNumberToObject(v, "status", u->status);
        cJSON_AddNumberToObject(v, "color", u->color);
        cJSON_AddNumberToObject(v, "avatar", u->avatar);
        cJSON_AddNumberToObject(v, "created", u->created);
        if (u->plat)
            cJSON_AddNumberToObject(v, "plat", u->plat);
        cJSON_AddStringToObject(v, "bio", u->bio);
        cJSON_AddStringToObject(v, "game", u->game);
        cJSON_AddStringToObject(v, "custom", u->custom);
        cJSON_AddStringToObject(v, "pic", u->pic);
        cJSON_AddStringToObject(v, "anim", u->anim);
        cJSON_AddItemToArray(users, v);
    }
    for (int i = 0; i < ORBIT_GROUPS; i++)
        if (d->group[i].used) {
            const OrbitGroup *g = &d->group[i];
            cJSON *v = cJSON_CreateObject();
            cJSON_AddNumberToObject(v, "id", i);
            cJSON_AddNumberToObject(v, "owner", g->owner);
            cJSON_AddNumberToObject(v, "members", g->members);
            cJSON_AddNumberToObject(v, "pending", g->pending);
            cJSON_AddStringToObject(v, "name", g->name);
            cJSON_AddStringToObject(v, "invite", g->invite);
            cJSON_AddNumberToObject(v, "kind", g->kind);
            cJSON_AddNumberToObject(v, "icon", g->icon);
            cJSON_AddNumberToObject(v, "color", g->color);
            cJSON_AddNumberToObject(v, "banner", g->banner);
            cJSON_AddStringToObject(v, "pic", g->pic);
            cJSON_AddStringToObject(v, "anim", g->anim);
            cJSON_AddNumberToObject(v, "open", g->open);
            cJSON_AddNumberToObject(v, "banned", g->banned);
            cJSON *until = cJSON_AddArrayToObject(v, "ban_until");
            for (int u = 0; u < ORBIT_USERS; u++)
                cJSON_AddItemToArray(until, cJSON_CreateNumber((g->banned >> u & 1) ? g->ban_until[u] : 0));
            cJSON_AddNumberToObject(v, "muted", g->muted);
            cJSON_AddStringToObject(v, "desc", g->desc);
            cJSON_AddStringToObject(v, "welcome", g->welcome);
            cJSON *cats = cJSON_AddArrayToObject(v, "cats");
            for (int c = 0; c < ORBIT_CATEGORIES; c++)
                if (g->cat[c].used) {
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddNumberToObject(x, "i", c);
                    cJSON_AddNumberToObject(x, "pos", g->cat[c].pos);
                    cJSON_AddStringToObject(x, "name", g->cat[c].name);
                    cJSON_AddItemToArray(cats, x);
                }
            cJSON *chs = cJSON_AddArrayToObject(v, "chs");
            for (int c = 0; c < ORBIT_CHANNELS; c++)
                if (g->ch[c].used) {
                    const OrbitChannel *h = &g->ch[c];
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddNumberToObject(x, "i", c);
                    cJSON_AddNumberToObject(x, "type", h->type);
                    cJSON_AddNumberToObject(x, "cat", h->category);
                    cJSON_AddNumberToObject(x, "pos", h->pos);
                    cJSON_AddNumberToObject(x, "view", h->view);
                    cJSON_AddNumberToObject(x, "send", h->send);
                    cJSON_AddStringToObject(x, "name", h->name);
                    cJSON_AddStringToObject(x, "topic", h->topic);
                    cJSON_AddItemToArray(chs, x);
                }
            cJSON *roles = cJSON_AddArrayToObject(v, "roles");
            for (int r = 0; r < ORBIT_ROLES; r++)
                if (g->role[r].used) {
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddNumberToObject(x, "i", r);
                    cJSON_AddNumberToObject(x, "prio", g->role[r].priority);
                    cJSON_AddNumberToObject(x, "color", g->role[r].color);
                    cJSON_AddNumberToObject(x, "perms", g->role[r].perms);
                    cJSON_AddStringToObject(x, "name", g->role[r].name);
                    cJSON_AddItemToArray(roles, x);
                }
            cJSON *mr = cJSON_AddArrayToObject(v, "mroles");
            for (int u = 0; u < ORBIT_USERS; u++)
                cJSON_AddItemToArray(mr, cJSON_CreateNumber(g->roles[u]));
            cJSON_AddItemToArray(groups, v);
        }
    for (int i = 0; i < ORBIT_MESSAGES; i++)
        if (d->message[i].id) {
            const OrbitMessage *m = &d->message[i];
            cJSON *v = cJSON_CreateObject();
            cJSON_AddNumberToObject(v, "id", m->id);
            cJSON_AddNumberToObject(v, "group", m->group);
            cJSON_AddNumberToObject(v, "user", m->user);
            cJSON_AddNumberToObject(v, "time", m->time);
            cJSON_AddStringToObject(v, "text", m->text);
            cJSON_AddNumberToObject(v, "ch", m->channel);
            cJSON_AddNumberToObject(v, "reply", m->reply);
            cJSON_AddNumberToObject(v, "flags", m->flags);
            cJSON_AddNumberToObject(v, "rev", m->rev);
            cJSON *re = cJSON_AddArrayToObject(v, "react");
            for (int r = 0; r < ORBIT_REACTIONS; r++)
                cJSON_AddItemToArray(re, cJSON_CreateNumber(m->react[r]));
            cJSON_AddItemToArray(msgs, v);
        }
    cJSON *pending = cJSON_AddArrayToObject(root, "pending_emails");
    for (int i = 0; i < ORBIT_PENDING_EMAILS; i++)
        if (d->pending_email[i].used) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "group", d->pending_email[i].group);
            cJSON_AddStringToObject(item, "email", d->pending_email[i].email);
            cJSON_AddItemToArray(pending, item);
        }
    cJSON *audit = cJSON_AddArrayToObject(root, "audit");
    cJSON_AddNumberToObject(root, "next_audit", d->next_audit);
    for (int i = 0; i < ORBIT_AUDIT; i++)
        if (d->audit[i].used) {
            const OrbitAudit *e = &d->audit[i];
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "id", e->id);
            cJSON_AddNumberToObject(x, "group", e->group);
            cJSON_AddNumberToObject(x, "user", e->user);
            cJSON_AddNumberToObject(x, "what", e->what);
            cJSON_AddNumberToObject(x, "time", e->time);
            cJSON_AddItemToArray(audit, x);
        }
    cJSON *reports = cJSON_AddArrayToObject(root, "reports");
    cJSON_AddNumberToObject(root, "next_report", d->next_report);
    for (int i = 0; i < ORBIT_REPORTS; i++)
        if (d->report[i].used) {
            const OrbitReport *e = &d->report[i];
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "id", e->id);
            cJSON_AddNumberToObject(x, "group", e->group);
            cJSON_AddNumberToObject(x, "reporter", e->reporter);
            cJSON_AddNumberToObject(x, "target", e->target);
            cJSON_AddNumberToObject(x, "reason", e->reason);
            cJSON_AddNumberToObject(x, "state", e->state);
            cJSON_AddNumberToObject(x, "handler", e->handler);
            cJSON_AddNumberToObject(x, "msg", e->msg);
            cJSON_AddNumberToObject(x, "time", e->time);
            cJSON_AddStringToObject(x, "text", e->text);
            cJSON_AddStringToObject(x, "detail", e->detail);
            cJSON_AddItemToArray(reports, x);
        }
    cJSON *friends = cJSON_AddArrayToObject(root, "friends");
    for (int i = 0; i < ORBIT_FRIENDS; i++)
        if (d->friend_[i].used) {
            const OrbitFriend *f = &d->friend_[i];
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "state", f->state);
            cJSON_AddNumberToObject(x, "user", f->user);
            cJSON_AddNumberToObject(x, "since", f->since);
            hex_or_empty(x, "pub", f->pub, 32, 1);
            cJSON_AddStringToObject(x, "name", f->name);
            cJSON_AddStringToObject(x, "code", f->code);
            cJSON_AddItemToArray(friends, x);
        }
    return root;
}
static int save(const OrbitStore *s) {
    if (!s->path[0])
        return 0;
    char tmp[520];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s->path);
    cJSON *root = encode(&s->data);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json)
        return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(json);
        return -1;
    }
    size_t n = strlen(json);
    int good = fwrite(json, 1, n, f) == n;
    free(json);
    if (fflush(f))
        good = 0;
    if (fsync(fileno(f)))
        good = 0;
    if (fclose(f))
        good = 0;
    chmod(tmp, 0600);
    if (!good) {
        remove(tmp);
        return -1;
    }
    /* PS3 sys_fs_rename does not replace an existing destination. Keep the last
       committed snapshot as a recovery file instead of unlinking the only copy. */
    char backup[520];
    snprintf(backup, sizeof(backup), "%s.bak", s->path);
    if (remove(backup) && errno != ENOENT)
        return -1;
    int moved = rename(s->path, backup) == 0;
    if (!moved && errno != ENOENT)
        return -1;
    if (rename(tmp, s->path)) {
        if (moved)
            rename(backup, s->path);
        return -1;
    }
    return 0;
}
static void load_group_v2(OrbitGroup *g, const cJSON *v, int users, int lang) {
    g->kind = clampi(number(v, "kind", GK_SERVER), GK_SERVER, GK_HOME);
    g->icon = clampi(number(v, "icon", 0), 0, 15);
    g->color = clampi(number(v, "color", 0), 0, 11);
    g->banner = clampi(number(v, "banner", 0), 0, 7);
    if (avatar_hash_valid(orbit_string(v, "pic")))
        orbit_copy(g->pic, sizeof(g->pic), orbit_string(v, "pic"));
    if (g->pic[0] && avatar_hash_valid(orbit_string(v, "anim")))
        orbit_copy(g->anim, sizeof(g->anim), orbit_string(v, "anim"));
    g->open = number(v, "open", 0) == 1;
    cJSON *b = cJSON_GetObjectItem(v, "banned"), *m = cJSON_GetObjectItem(v, "muted");
    g->banned = cJSON_IsNumber(b) ? (uint32_t)b->valuedouble : 0;
    g->muted = cJSON_IsNumber(m) ? (uint32_t)m->valuedouble : 0;
    if (users < 32) {
        g->banned &= (UINT32_C(1) << users) - 1;
        g->muted &= (UINT32_C(1) << users) - 1;
    }
    int bu = 0;
    const cJSON *t;
    cJSON_ArrayForEach(t, cJSON_GetObjectItem(v, "ban_until")) {
        if (bu < ORBIT_USERS)
            g->ban_until[bu] = cJSON_IsNumber(t) && t->valuedouble > 0 ? (uint32_t)t->valuedouble : 0;
        bu++;
    }
    opt_text(g->desc, sizeof(g->desc), v, "desc", 128);
    opt_text(g->welcome, sizeof(g->welcome), v, "welcome", 192);
    const cJSON *x;
    cJSON *chs = cJSON_GetObjectItem(v, "chs");
    if (!cJSON_IsArray(chs)) { /* 0.9.5 group */
        default_server(g, lang, g->kind == GK_SERVER);
        return;
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(v, "cats")) {
        int i = number(x, "i", -1);
        const char *name = orbit_string(x, "name");
        if (i < 0 || i >= ORBIT_CATEGORIES || !orbit_text_valid(name, 1, 32))
            continue;
        g->cat[i].used = 1;
        g->cat[i].pos = number(x, "pos", i);
        orbit_copy(g->cat[i].name, sizeof(g->cat[i].name), name);
    }
    cJSON_ArrayForEach(x, chs) {
        int i = number(x, "i", -1);
        const char *name = orbit_string(x, "name");
        if (i < 0 || i >= ORBIT_CHANNELS || !orbit_text_valid(name, 1, 32))
            continue;
        OrbitChannel *h = &g->ch[i];
        h->used = 1;
        h->type = number(x, "type", 0) == CH_VOICE ? CH_VOICE : CH_TEXT;
        h->category = number(x, "cat", -1);
        if (h->category < -1 || h->category >= ORBIT_CATEGORIES || (h->category >= 0 && !g->cat[h->category].used))
            h->category = -1;
        h->pos = number(x, "pos", i);
        h->view = (uint8_t)number(x, "view", 0);
        h->send = (uint8_t)number(x, "send", 0);
        orbit_copy(h->name, sizeof(h->name), name);
        opt_text(h->topic, sizeof(h->topic), x, "topic", 96);
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(v, "roles")) {
        int i = number(x, "i", -1);
        const char *name = orbit_string(x, "name");
        if (i < 0 || i >= ORBIT_ROLES || !orbit_text_valid(name, 1, 24))
            continue;
        g->role[i].used = 1;
        g->role[i].priority = clampi(number(x, "prio", 1), 0, 100);
        cJSON *c = cJSON_GetObjectItem(x, "color"), *p = cJSON_GetObjectItem(x, "perms");
        g->role[i].color = cJSON_IsNumber(c) ? (uint32_t)c->valuedouble & 0xffffff : 0x9aa1ab;
        g->role[i].perms = cJSON_IsNumber(p) ? (uint32_t)p->valuedouble & P_ALL : 0;
        orbit_copy(g->role[i].name, sizeof(g->role[i].name), name);
    }
    int u = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(v, "mroles")) {
        if (u < ORBIT_USERS && cJSON_IsNumber(x))
            g->roles[u] = (uint8_t)x->valueint;
        u++;
    }
    int any = 0;
    for (int i = 0; i < ORBIT_CHANNELS; i++)
        any |= g->ch[i].used;
    if (!any)
        add_channel(g, T_in(lang, T_CH_GENERAL), CH_TEXT, -1);
    int roles = 0;
    for (int i = 0; i < ORBIT_ROLES; i++)
        roles |= g->role[i].used;
    if (!roles)
        default_roles(g);
}
int orbit_store_open(OrbitStore *s, const char *path) {
    return orbit_store_open_lang(s, path, LANG_DE);
}
int orbit_store_open_lang(OrbitStore *s, const char *path, int lang) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mutex, NULL);
    s->lang = lang;
    host_lang = lang;
    s->data.version = 2;
    s->data.next_id = 1;
    s->data.owner_user = -1;
    if (!path)
        return 0;
    if (strlen(path) >= sizeof(s->path))
        return -1;
    strcpy(s->path, path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno != ENOENT)
            return -1;
        char backup[520];
        snprintf(backup, sizeof(backup), "%s.bak", path);
        if (!rename(backup, path))
            f = fopen(path, "rb");
        else if (errno == ENOENT)
            return save(s);
        if (!f)
            return -1;
    }
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    rewind(f);
    if (n < 2 || n > 8 * 1024 * 1024) {
        fclose(f);
        return -1;
    }
    char *text = calloc(1, (size_t)n + 1);
    if (!text) {
        fclose(f);
        return -1;
    }
    int readok = fread(text, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    cJSON *root = readok ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
    free(text);
    if (!root)
        return -1;
    int version = number(root, "version", 0);
    int good = version == 1 || version == 2; /* 1 = Orbit 0.9.x, converted on load */
    int next = number(root, "next_id", 0);
    if (next < 1)
        good = 0;
    s->data.next_id = (uint32_t)next;
    cJSON *rv = cJSON_GetObjectItem(root, "rev");
    s->data.rev = cJSON_IsNumber(rv) && rv->valuedouble > 0 ? (uint32_t)rv->valuedouble : 1;
    cJSON *users = cJSON_GetObjectItemCaseSensitive(root, "users"),
          *groups = cJSON_GetObjectItemCaseSensitive(root, "groups"),
          *msgs = cJSON_GetObjectItemCaseSensitive(root, "messages"), *v;
    if (!cJSON_IsArray(users) || !cJSON_IsArray(groups) || !cJSON_IsArray(msgs) ||
        cJSON_GetArraySize(users) > ORBIT_USERS || cJSON_GetArraySize(groups) > ORBIT_GROUPS ||
        cJSON_GetArraySize(msgs) > ORBIT_MESSAGES)
        good = 0;
    if (good)
        cJSON_ArrayForEach(v, users) {
            OrbitUser *u = &s->data.user[s->data.users++];
            const char *email = orbit_string(v, "email"), *name = orbit_string(v, "name");
            if (!valid_email(email) || !orbit_text_valid(name, 1, 48) ||
                orbit_unhex(u->salt, orbit_string(v, "salt"), 16) ||
                orbit_unhex(u->hash, orbit_string(v, "hash"), 32)) {
                good = 0;
                break;
            }
            strcpy(u->email, email);
            strcpy(u->name, name);
            cJSON *devices = cJSON_GetObjectItem(v, "devices");
            if (devices) {
                if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) != 4) {
                    good = 0;
                    break;
                }
                for (int j = 0; j < 4; j++) {
                    cJSON *t = cJSON_GetArrayItem(devices, j);
                    if (!cJSON_IsString(t) || orbit_unhex(u->devices[j], t->valuestring, 32)) {
                        good = 0;
                        break;
                    }
                }
            }
            u->has_pub = !orbit_unhex(u->pub, orbit_string(v, "pub"), 32);
            u->has_watch = !orbit_unhex(u->watch, orbit_string(v, "watch"), 32);
            u->status = clampi(number(v, "status", 0), ST_ONLINE, ST_INVISIBLE);
            u->color = clampi(number(v, "color", 0), 0, 11);
            u->avatar = clampi(number(v, "avatar", 0), 0, 15);
            u->plat = clampi(number(v, "plat", 0), 0, 2);
            cJSON *c = cJSON_GetObjectItem(v, "created");
            u->created = cJSON_IsNumber(c) && c->valuedouble > 0 ? (uint32_t)c->valuedouble : 0;
            opt_text(u->bio, sizeof(u->bio), v, "bio", 96);
            opt_text(u->game, sizeof(u->game), v, "game", 48);
            opt_text(u->custom, sizeof(u->custom), v, "custom", 64);
            if (avatar_hash_valid(orbit_string(v, "pic")))
                orbit_copy(u->pic, sizeof(u->pic), orbit_string(v, "pic"));
            if (u->pic[0] && avatar_hash_valid(orbit_string(v, "anim")))
                orbit_copy(u->anim, sizeof(u->anim), orbit_string(v, "anim"));
        }
    int owner_user = number(root, "owner_user", -1);
    s->data.owner_user = owner_user >= 0 && owner_user < s->data.users ? owner_user : -1;
    if (good)
        cJSON_ArrayForEach(v, groups) {
            int id = number(v, "id", -1), owner = number(v, "owner", -1);
            const char *name = orbit_string(v, "name"), *invite = orbit_string(v, "invite");
            char normalized[33];
            cJSON *bits = cJSON_GetObjectItemCaseSensitive(v, "members");
            double b = cJSON_IsNumber(bits) ? bits->valuedouble : -1;
            int kind = number(v, "kind", GK_SERVER);
            if (id < 0 || id >= ORBIT_GROUPS || s->data.group[id].used || owner < 0 ||
                owner >= s->data.users || (kind == GK_SERVER && !orbit_text_valid(name, 1, 64)) ||
                strlen(name) > 64 || orbit_group_code(normalized, invite) || b < 1 ||
                b > UINT32_MAX || b != (uint32_t)b) {
                good = 0;
                break;
            }
            OrbitGroup *g = &s->data.group[id];
            g->used = 1;
            g->owner = owner;
            g->members = (uint32_t)b;
            cJSON *pending = cJSON_GetObjectItemCaseSensitive(v, "pending");
            if (pending) {
                double p = cJSON_IsNumber(pending) ? pending->valuedouble : -1;
                if (p < 0 || p > UINT32_MAX || p != (uint32_t)p ||
                    (s->data.users < 32 && ((uint32_t)p >> s->data.users))) {
                    good = 0;
                    break;
                }
                g->pending = (uint32_t)p & ~g->members;
            }
            if (!(g->members & (UINT32_C(1) << owner)) ||
                (s->data.users < 32 && (g->members >> s->data.users))) {
                good = 0;
                break;
            }
            strcpy(g->name, name);
            strcpy(g->invite, normalized);
            load_group_v2(g, v, s->data.users, lang);
        }
    cJSON *pending = cJSON_GetObjectItem(root, "pending_emails");
    if (pending) {
        if (!cJSON_IsArray(pending) || cJSON_GetArraySize(pending) > ORBIT_PENDING_EMAILS)
            good = 0;
        int i = 0;
        if (good)
            cJSON_ArrayForEach(v, pending) {
                int gid = number(v, "group", -1);
                const char *email = orbit_string(v, "email");
                if (gid < 0 || gid >= ORBIT_GROUPS || !s->data.group[gid].used ||
                    !valid_email(email)) {
                    good = 0;
                    break;
                }
                s->data.pending_email[i].used = 1;
                s->data.pending_email[i].group = gid;
                normalize_email(s->data.pending_email[i++].email, email);
            }
    }
    if (good)
        cJSON_ArrayForEach(v, msgs) {
            int id = number(v, "id", 0), g = number(v, "group", -1), u = number(v, "user", -1);
            int flags = number(v, "flags", 0);
            const char *body = orbit_string(v, "text");
            if (id < 1 || (uint32_t)id >= s->data.next_id || g < 0 || g >= ORBIT_GROUPS ||
                !s->data.group[g].used || u < 0 || u >= s->data.users ||
                (!(flags & MF_DELETED) && !orbit_text_valid(body, 1, 768)) || strlen(body) > 768 ||
                s->data.message[id % ORBIT_MESSAGES].id) {
                good = 0;
                break;
            }
            OrbitMessage *m = &s->data.message[id % ORBIT_MESSAGES];
            m->id = (uint32_t)id;
            m->group = g;
            m->user = u;
            m->flags = flags & 3;
            m->channel = clampi(number(v, "ch", 0), 0, ORBIT_CHANNELS - 1);
            cJSON *t = cJSON_GetObjectItemCaseSensitive(v, "time");
            if (cJSON_IsNumber(t) && t->valuedouble >= 0 && t->valuedouble < 4294967295.0)
                m->time = (uint32_t)t->valuedouble;
            cJSON *r = cJSON_GetObjectItem(v, "reply"), *mr = cJSON_GetObjectItem(v, "rev");
            m->reply = cJSON_IsNumber(r) && r->valuedouble > 0 ? (uint32_t)r->valuedouble : 0;
            m->rev = cJSON_IsNumber(mr) && mr->valuedouble > 0 ? (uint32_t)mr->valuedouble : 0;
            int k = 0;
            const cJSON *x;
            cJSON_ArrayForEach(x, cJSON_GetObjectItem(v, "react")) {
                if (k < ORBIT_REACTIONS && cJSON_IsNumber(x))
                    m->react[k] = (uint32_t)x->valuedouble;
                k++;
            }
            strcpy(m->text, body);
        }
    if (good) { /* 1.2 audit log */
        cJSON *na = cJSON_GetObjectItem(root, "next_audit");
        s->data.next_audit = cJSON_IsNumber(na) && na->valuedouble > 0 ? (uint32_t)na->valuedouble : 0;
        int ai = 0;
        cJSON_ArrayForEach(v, cJSON_GetObjectItem(root, "audit")) {
            if (ai >= ORBIT_AUDIT)
                break;
            int g = number(v, "group", -1), us = number(v, "user", -1);
            if (g < 0 || g >= ORBIT_GROUPS || us < 0 || us >= s->data.users)
                continue;
            OrbitAudit *e = &s->data.audit[ai++];
            e->used = 1;
            e->group = g;
            e->user = us;
            e->what = clampi(number(v, "what", 0), 0, AUD_COUNT - 1);
            e->id = (uint32_t)number(v, "id", 0);
            e->time = (uint32_t)number(v, "time", 0);
        }
    }
    int ri = 0;
    if (good) {
        cJSON *nr = cJSON_GetObjectItem(root, "next_report");
        s->data.next_report = cJSON_IsNumber(nr) && nr->valuedouble > 0 ? (uint32_t)nr->valuedouble : 0;
        cJSON_ArrayForEach(v, cJSON_GetObjectItem(root, "reports")) {
            if (ri >= ORBIT_REPORTS)
                break;
            OrbitReport *e = &s->data.report[ri];
            int g = number(v, "group", -1), rp = number(v, "reporter", -1), tg = number(v, "target", -1);
            if (g < 0 || g >= ORBIT_GROUPS || rp < 0 || rp >= s->data.users || tg < 0 || tg >= s->data.users)
                continue;
            e->used = 1;
            e->group = g;
            e->reporter = rp;
            e->target = tg;
            e->reason = clampi(number(v, "reason", RR_OTHER), 0, RR_COUNT - 1);
            e->state = clampi(number(v, "state", REPORT_OPEN), REPORT_OPEN, REPORT_DISMISSED);
            e->handler = number(v, "handler", -1);
            e->id = (uint32_t)number(v, "id", 0);
            e->msg = (uint32_t)number(v, "msg", 0);
            e->time = (uint32_t)number(v, "time", 0);
            opt_text(e->text, sizeof(e->text), v, "text", 128);
            opt_text(e->detail, sizeof(e->detail), v, "detail", 96);
            ri++;
        }
    }
    int fi = 0;
    if (good)
        cJSON_ArrayForEach(v, cJSON_GetObjectItem(root, "friends")) {
            if (fi >= ORBIT_FRIENDS)
                break;
            OrbitFriend *f = &s->data.friend_[fi];
            int state = number(v, "state", 0), user = number(v, "user", -1);
            if (state < FR_INCOMING || state > FR_BLOCKED || orbit_unhex(f->pub, orbit_string(v, "pub"), 32))
                continue;
            f->used = 1;
            f->state = state;
            f->user = user >= 0 && user < s->data.users ? user : -1;
            cJSON *since = cJSON_GetObjectItem(v, "since");
            f->since = cJSON_IsNumber(since) ? (uint32_t)since->valuedouble : 0;
            opt_text(f->name, sizeof(f->name), v, "name", 48);
            const char *code = orbit_string(v, "code");
            if (strlen(code) < sizeof(f->code))
                orbit_copy(f->code, sizeof(f->code), code);
            fi++;
        }
    cJSON_Delete(root);
    return good ? 0 : -1;
}
void orbit_store_close(OrbitStore *s) {
    for (int i = 0; i < ORBIT_STAGED; i++)
        free(s->staged[i].data);
    memset(s->staged, 0, sizeof(s->staged));
    pthread_mutex_destroy(&s->mutex);
    crypto_wipe(&s->data, sizeof(s->data));
}
/* ---------- permissions ---------- */
uint32_t orbit_perms(const OrbitData *d, int gid, int uid) {
    if (gid < 0 || gid >= ORBIT_GROUPS || uid < 0 || uid >= d->users)
        return 0;
    const OrbitGroup *g = &d->group[gid];
    if (!g->used || !(g->members & (UINT32_C(1) << uid)))
        return 0;
    if (g->owner == uid)
        return P_ALL;
    if (g->kind == GK_DM) /* private conversations: everyone may talk and invite nobody */
        return P_SEND;
    uint32_t p = 0;
    for (int r = 0; r < ORBIT_ROLES; r++)
        if (g->role[r].used && (g->roles[uid] & (1 << r)))
            p |= g->role[r].perms;
    return p;
}
static int top_priority(const OrbitGroup *g, int uid) {
    if (g->owner == uid)
        return 1000;
    int best = 0;
    for (int r = 0; r < ORBIT_ROLES; r++)
        if (g->role[r].used && (g->roles[uid] & (1 << r)) && g->role[r].priority > best)
            best = g->role[r].priority;
    return best;
}
static int roles_mask_allows(const OrbitGroup *g, int uid, uint8_t mask) {
    return !mask || g->owner == uid || (g->roles[uid] & mask);
}
int orbit_can_view(const OrbitData *d, int gid, int ch, int uid) {
    if (gid < 0 || gid >= ORBIT_GROUPS || uid < 0 || uid >= d->users)
        return 0;
    const OrbitGroup *g = &d->group[gid];
    if (!g->used || !(g->members & (UINT32_C(1) << uid)) || ch < 0 || ch >= ORBIT_CHANNELS ||
        !g->ch[ch].used)
        return 0;
    return roles_mask_allows(g, uid, g->ch[ch].view) ||
           (orbit_perms(d, gid, uid) & P_MANAGE_CHANNELS);
}
int orbit_voice_allowed(OrbitStore *s, int gid, int ch, int uid, int *muted) {
    pthread_mutex_lock(&s->mutex);
    OrbitData *d = &s->data;
    int ok = 0;
    if (gid >= 0 && gid < ORBIT_GROUPS && d->group[gid].used && uid >= 0 && uid < d->users) {
        const OrbitGroup *g = &d->group[gid];
        int legacy = ch < 0; /* 0.9 clients: first voice channel, or the group itself */
        if (legacy) {
            ch = 0;
            for (int i = 0; i < ORBIT_CHANNELS; i++)
                if (g->ch[i].used && g->ch[i].type == CH_VOICE) {
                    ch = i;
                    break;
                }
        }
        ok = (g->members & (UINT32_C(1) << uid)) && !is_banned(g, uid) &&
             (g->kind == GK_DM || orbit_can_view(d, gid, ch, uid)) &&
             (g->kind == GK_DM || legacy || g->ch[ch].type == CH_VOICE);
        if (ok)
            s->call_ch[gid][uid] = (uint8_t)ch;
        if (muted)
            *muted = (g->muted >> uid) & 1;
    }
    pthread_mutex_unlock(&s->mutex);
    return ok ? ch : -1;
}
void orbit_session_end(OrbitStore *s, OrbitSession *sess) {
    pthread_mutex_lock(&s->mutex);
    if (sess->counted && sess->user >= 0 && sess->user < ORBIT_USERS && s->online[sess->user] > 0)
        s->online[sess->user]--;
    sess->counted = 0;
    pthread_mutex_unlock(&s->mutex);
}
static void count_session(OrbitStore *s, OrbitSession *sess, int uid) {
    if (sess->counted && sess->user != uid && sess->user >= 0 && s->online[sess->user] > 0)
        s->online[sess->user]--, sess->counted = 0;
    if (!sess->counted) {
        s->online[uid]++;
        sess->counted = 1;
    }
}
/* ---------- helpers for replies ---------- */
static int shown_status(const OrbitStore *s, int uid) {
    const OrbitUser *u = &s->data.user[uid];
    if (!s->online[uid] || u->status == ST_INVISIBLE)
        return ST_OFFLINE;
    return u->status;
}
static void add_person(cJSON *x, const OrbitStore *s, int uid, int full) {
    const OrbitUser *u = &s->data.user[uid];
    cJSON_AddNumberToObject(x, "user", uid);
    cJSON_AddStringToObject(x, "name", u->name);
    char id[ORBIT_ID_TEXT] = "";
    if (u->has_pub)
        orbit_id_text(id, u->pub);
    cJSON_AddStringToObject(x, "oid", id);
    int st = shown_status(s, uid);
    cJSON_AddNumberToObject(x, "status", st);
    cJSON_AddNumberToObject(x, "color", u->color);
    cJSON_AddNumberToObject(x, "avatar", u->avatar);
    if (u->plat)
        cJSON_AddNumberToObject(x, "plat", u->plat);
    if (u->pic[0])
        cJSON_AddStringToObject(x, "pic", u->pic);
    if (u->anim[0])
        cJSON_AddStringToObject(x, "anim", u->anim);
    if (st != ST_OFFLINE || full)
        cJSON_AddStringToObject(x, "custom", st == ST_OFFLINE ? "" : u->custom);
    if (full) {
        cJSON_AddStringToObject(x, "bio", u->bio);
        cJSON_AddStringToObject(x, "game", u->game);
        cJSON_AddNumberToObject(x, "created", u->created);
    }
}
static int find_friend_pub(const OrbitData *d, const uint8_t pub[32]) {
    for (int i = 0; i < ORBIT_FRIENDS; i++)
        if (d->friend_[i].used && !crypto_verify32(d->friend_[i].pub, pub))
            return i;
    return -1;
}
static int blocked_by_owner(const OrbitData *d, int uid) {
    if (uid < 0 || uid >= d->users || !d->user[uid].has_pub)
        return 0;
    int f = find_friend_pub(d, d->user[uid].pub);
    return f >= 0 && d->friend_[f].state == FR_BLOCKED;
}
static void message_json(cJSON *a, const OrbitStore *s, const OrbitMessage *m, int proto) {
    const OrbitData *d = &s->data;
    cJSON *x = cJSON_CreateObject();
    cJSON_AddNumberToObject(x, "id", m->id);
    cJSON_AddNumberToObject(x, "user", m->user);
    cJSON_AddStringToObject(x, "name", d->user[m->user].name);
    cJSON_AddNumberToObject(x, "time", m->time);
    cJSON_AddStringToObject(x, "text", m->text);
    if (proto >= 10) {
        char id[ORBIT_ID_TEXT] = "";
        if (d->user[m->user].has_pub)
            orbit_id_text(id, d->user[m->user].pub);
        cJSON_AddStringToObject(x, "oid", id);
        cJSON_AddNumberToObject(x, "ch", m->channel);
        cJSON_AddNumberToObject(x, "flags", m->flags);
        cJSON_AddNumberToObject(x, "rev", m->rev);
        cJSON_AddNumberToObject(x, "color", d->user[m->user].color);
        cJSON_AddNumberToObject(x, "avatar", d->user[m->user].avatar);
        if (d->user[m->user].pic[0])
            cJSON_AddStringToObject(x, "pic", d->user[m->user].pic);
        if (d->user[m->user].anim[0])
            cJSON_AddStringToObject(x, "anim", d->user[m->user].anim);
        if (m->reply) {
            const OrbitMessage *r = &d->message[m->reply % ORBIT_MESSAGES];
            cJSON_AddNumberToObject(x, "reply", m->reply);
            if (r->id == m->reply && r->group == m->group) {
                cJSON_AddStringToObject(x, "rname", d->user[r->user].name);
                char quote[97];
                orbit_copy(quote, sizeof(quote), r->flags & MF_DELETED ? "" : r->text);
                /* never cut a UTF-8 sequence in half */
                size_t n = strlen(quote);
                while (n && ((unsigned char)quote[n - 1] & 0xc0) == 0x80)
                    n--;
                if (n && (unsigned char)quote[n - 1] >= 0xc0 && strlen(r->text) > 96)
                    n--;
                quote[n] = 0;
                cJSON_AddStringToObject(x, "rtext", quote);
            }
        }
        cJSON *re = cJSON_AddArrayToObject(x, "react");
        for (int r = 0; r < ORBIT_REACTIONS; r++)
            cJSON_AddItemToArray(re, cJSON_CreateNumber(m->react[r]));
    }
    cJSON_AddItemToArray(a, x);
}
static OrbitMessage *find_message(OrbitData *d, int gid, int id) {
    if (id <= 0)
        return NULL;
    OrbitMessage *m = &d->message[(uint32_t)id % ORBIT_MESSAGES];
    return m->id == (uint32_t)id && m->group == gid ? m : NULL;
}
static int find_channel_by_type(const OrbitGroup *g, int type) {
    int best = -1;
    for (int i = 0; i < ORBIT_CHANNELS; i++)
        if (g->ch[i].used && g->ch[i].type == type && (best < 0 || g->ch[i].pos < g->ch[best].pos))
            best = i;
    return best;
}
/* 1.2: a timed ban ends by itself */
static int is_banned(const OrbitGroup *g, int uid) {
    if (uid < 0 || uid >= ORBIT_USERS || !(g->banned >> uid & 1))
        return 0;
    return !g->ban_until[uid] || orbit_unix_time() < g->ban_until[uid];
}
static void remove_member(OrbitStore *s, OrbitGroup *g, int gid, int uid) {
    uint32_t bit = UINT32_C(1) << uid;
    g->members &= ~bit;
    g->pending &= ~bit;
    g->roles[uid] = 1 << 3;
    s->call[gid] &= ~bit;
}
static void delete_group(OrbitData *d, int gid) {
    for (int i = 0; i < ORBIT_PENDING_EMAILS; i++)
        if (d->pending_email[i].used && d->pending_email[i].group == gid)
            memset(&d->pending_email[i], 0, sizeof(d->pending_email[i]));
    memset(&d->group[gid], 0, sizeof(d->group[gid]));
    for (int i = 0; i < ORBIT_MESSAGES; i++)
        if (d->message[i].group == gid && d->message[i].id)
            memset(&d->message[i], 0, sizeof(d->message[i]));
}
static int new_group_slot(OrbitData *d) {
    for (int i = 0; i < ORBIT_GROUPS; i++)
        if (!d->group[i].used)
            return i;
    return -1;
}
/* moves an entry one step in its ordered list (channels share positions within a category) */
static void move_channel(OrbitGroup *g, int ch, int dir) {
    int best = -1;
    for (int i = 0; i < ORBIT_CHANNELS; i++) {
        if (i == ch || !g->ch[i].used || g->ch[i].category != g->ch[ch].category)
            continue;
        int before = g->ch[i].pos < g->ch[ch].pos;
        if ((dir < 0 && before && (best < 0 || g->ch[i].pos > g->ch[best].pos)) ||
            (dir > 0 && !before && g->ch[i].pos >= g->ch[ch].pos &&
             (best < 0 || g->ch[i].pos < g->ch[best].pos)))
            best = i;
    }
    if (best >= 0) {
        int p = g->ch[best].pos;
        g->ch[best].pos = g->ch[ch].pos;
        g->ch[ch].pos = p == g->ch[ch].pos ? p + dir : p;
    }
}
static void move_category(OrbitGroup *g, int c, int dir) {
    int best = -1;
    for (int i = 0; i < ORBIT_CATEGORIES; i++) {
        if (i == c || !g->cat[i].used)
            continue;
        int before = g->cat[i].pos < g->cat[c].pos;
        if ((dir < 0 && before && (best < 0 || g->cat[i].pos > g->cat[best].pos)) ||
            (dir > 0 && !before && (best < 0 || g->cat[i].pos < g->cat[best].pos)))
            best = i;
    }
    if (best >= 0) {
        int p = g->cat[best].pos;
        g->cat[best].pos = g->cat[c].pos;
        g->cat[c].pos = p == g->cat[c].pos ? p + dir : p;
    }
}
static void apply_profile(OrbitUser *u, const cJSON *r, int *changed) {
    char buf[97];
    if (opt_text(buf, sizeof(u->bio), r, "bio", 96) == 1 && strcmp(buf, u->bio))
        memcpy(u->bio, buf, strlen(buf) + 1), *changed = 1;
    if (opt_text(buf, sizeof(u->game), r, "game", 48) == 1 && strcmp(buf, u->game))
        memcpy(u->game, buf, strlen(buf) + 1), *changed = 1;
    if (opt_text(buf, sizeof(u->custom), r, "custom", 64) == 1 && strcmp(buf, u->custom))
        memcpy(u->custom, buf, strlen(buf) + 1), *changed = 1;
    if (has(r, "plat")) { /* 1.3 crossplay: which console this person uses */
        int p = number(r, "plat", 0);
        if (p >= 1 && p <= 2 && p != u->plat)
            u->plat = p, *changed = 1;
    }
    if (has(r, "status")) {
        int v = clampi(number(r, "status", 0), ST_ONLINE, ST_INVISIBLE);
        if (v != u->status)
            u->status = v, *changed = 1;
    }
    if (has(r, "color")) {
        int v = clampi(number(r, "color", 0), 0, 11);
        if (v != u->color)
            u->color = v, *changed = 1;
    }
    if (has(r, "avatar")) {
        int v = clampi(number(r, "avatar", 0), 0, 15);
        if (v != u->avatar)
            u->avatar = v, *changed = 1;
    }
    cJSON *c = cJSON_GetObjectItem(r, "created");
    if (!u->created && cJSON_IsNumber(c) && c->valuedouble > 0)
        u->created = (uint32_t)c->valuedouble, *changed = 1;
}
/* 1.2: is a picture/animation name still used by a person or a server here? */
static int pic_in_use(const OrbitData *d, const char *h) {
    for (int i = 0; i < d->users; i++)
        if (!strcmp(d->user[i].pic, h) || !strcmp(d->user[i].anim, h))
            return 1;
    for (int i = 0; i < ORBIT_GROUPS; i++)
        if (d->group[i].used && (!strcmp(d->group[i].pic, h) || !strcmp(d->group[i].anim, h)))
            return 1;
    return 0;
}
static void pic_drop(OrbitStore *s, const char *old) {
    if (!old[0] || !s->path[0] || pic_in_use(&s->data, old))
        return;
    char path[600];
    snprintf(path, sizeof(path), "%s.pics/%s.pic", s->path, old);
    remove(path);
    snprintf(path, sizeof(path), "%s.pics/%s.ani", s->path, old);
    remove(path);
}
/* a still picture sent along ("pic_data", base64) or already stored here */
static int pic_accept(OrbitStore *s, const char *pic, const cJSON *r) {
    char dir[530];
    snprintf(dir, sizeof(dir), "%s.pics", s->path);
    if (!s->path[0] || !avatar_hash_valid(pic))
        return -1;
    uint8_t *blob = malloc(AVATAR_BLOB_MAX);
    size_t n = 0;
    OrbitImage img;
    int bad = !blob;
    if (!bad && avatar_load(dir, pic, blob, &n)) /* not here yet: must come with the request */
        bad = avatar_b64_decode(blob, AVATAR_BLOB_MAX, &n, orbit_string(r, "pic_data")) || avatar_decode(blob, n, pic, &img) ||
              (image_free(&img), avatar_save(dir, pic, blob, n));
    free(blob);
    return bad ? -1 : 0;
}
/* an animation: already stored here, or completely uploaded by this person (blob_put) */
static int anim_accept(OrbitStore *s, int uid, const char *h) {
    char dir[530];
    snprintf(dir, sizeof(dir), "%s.pics", s->path);
    if (!s->path[0] || !avatar_hash_valid(h))
        return -1;
    uint8_t *have;
    size_t n;
    if (!anim_load(dir, h, &have, &n)) {
        free(have);
        return 0;
    }
    for (int i = 0; i < ORBIT_STAGED; i++) {
        OrbitStaged *t = &s->staged[i];
        if (t->data && t->user == uid && !strcmp(t->hash, h) && t->n == t->total) {
            OrbitAnim a;
            int bad = anim_decode(t->data, t->n, h, &a);
            if (!bad) {
                anim_free(&a);
                bad = anim_save(dir, h, t->data, t->n);
            }
            free(t->data);
            memset(t, 0, sizeof(*t));
            return bad ? -1 : 0;
        }
    }
    return -1;
}
/* "pic" = name ("" removes it, with the animation), "pic_data" = base64 blob, "anim" = name of an
 * animation (stored or uploaded with blob_put; needs a still "pic" as its first frame).
 * Returns 1 when something changed, 0 when nothing did, -1 when a picture did not check out. */
static int set_pictures(OrbitStore *s, int uid, char pic[33], char anim[33], const cJSON *r) {
    const char *np = orbit_string(r, "pic");
    if (!np)
        return 0;
    const char *na = orbit_string(r, "anim");
    if (!na || !np[0])
        na = "";
    if (np[0] && strcmp(np, pic) && pic_accept(s, np, r))
        return -1;
    if (na[0] && strcmp(na, anim) && anim_accept(s, uid, na))
        na = ""; /* not uploaded (yet): the still picture applies, the client uploads and asks again */
    if (!strcmp(np, pic) && !strcmp(na, anim))
        return 0;
    char old_p[33], old_a[33];
    orbit_copy(old_p, sizeof(old_p), pic);
    orbit_copy(old_a, sizeof(old_a), anim);
    orbit_copy(pic, 33, np);
    orbit_copy(anim, 33, na);
    pic_drop(s, old_p);
    pic_drop(s, old_a);
    return 1;
}
/* 1.2: profile picture. A picture that does not check out is ignored; the rest of the profile
 * still applies. */
static void apply_picture(OrbitStore *s, int uid, const cJSON *r, int *changed) {
    OrbitUser *u = &s->data.user[uid];
    if (set_pictures(s, uid, u->pic, u->anim, r) > 0)
        *changed = 1;
}
static void audit_add(OrbitData *d, int gid, int uid, int what) {
    int slot = 0;
    for (int i = 0; i < ORBIT_AUDIT; i++) {
        if (!d->audit[i].used) {
            slot = i;
            break;
        }
        if (d->audit[i].id < d->audit[slot].id)
            slot = i; /* full: replace the oldest */
    }
    OrbitAudit *e = &d->audit[slot];
    e->used = 1;
    e->group = gid;
    e->user = uid;
    e->what = what;
    e->id = ++d->next_audit;
    e->time = (uint32_t)time(NULL);
}
/* ---------- requests ---------- */
static uint32_t last_ids[ORBIT_GROUPS][ORBIT_CHANNELS];
static cJSON *groups_reply(OrbitStore *s, OrbitSession *sess, int uid) {
    OrbitData *d = &s->data;
    memset(last_ids, 0, sizeof(last_ids));
    for (int k = 0; k < ORBIT_MESSAGES; k++) {
        const OrbitMessage *m = &d->message[k];
        if (m->id && !(m->flags & MF_DELETED) && m->group >= 0 && m->group < ORBIT_GROUPS &&
            m->channel >= 0 && m->channel < ORBIT_CHANNELS && m->id > last_ids[m->group][m->channel])
            last_ids[m->group][m->channel] = m->id;
    }
    uint32_t bit = UINT32_C(1) << uid;
    int v10 = sess->proto >= 10;
    cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "groups");
    cJSON_AddNumberToObject(v, "mrev", d->rev);
    for (int i = 0; i < ORBIT_GROUPS; i++) {
        OrbitGroup *g = &d->group[i];
        if (!g->used || !(g->members & bit) || g->kind == GK_HOME || (!v10 && g->kind != GK_SERVER))
            continue;
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "id", i);
        cJSON_AddStringToObject(x, "name", g->name);
        cJSON_AddBoolToObject(x, "owner", g->owner == uid);
        int count = 0;
        for (int j = 0; j < d->users; j++)
            if (g->members & (UINT32_C(1) << j))
                count++;
        cJSON_AddNumberToObject(x, "members", count);
        cJSON_AddStringToObject(x, "host", d->user[g->owner].name);
        int text0 = find_channel_by_type(g, CH_TEXT);
        uint32_t last = 0;
        if (!v10)
            last = text0 >= 0 ? last_ids[i][text0] : 0;
        cJSON *call = cJSON_AddArrayToObject(x, "call");
        for (int j = 0; j < d->users; j++)
            if (s->call[i] & g->members & (UINT32_C(1) << j)) {
                cJSON *c = cJSON_CreateObject();
                cJSON_AddNumberToObject(c, "user", j);
                cJSON_AddStringToObject(c, "name", d->user[j].name);
                cJSON_AddNumberToObject(c, "ch", s->call_ch[i][j]);
                cJSON_AddBoolToObject(c, "muted", (g->muted >> j) & 1);
                cJSON_AddItemToArray(call, c);
            }
        cJSON *names = cJSON_AddArrayToObject(x, "people");
        for (int j = 0, shown = 0; j < d->users && shown < 8; j++)
            if (g->members & (UINT32_C(1) << j)) {
                cJSON_AddItemToArray(names, cJSON_CreateString(d->user[j].name));
                shown++;
            }
        if (v10) {
            cJSON_AddNumberToObject(x, "kind", g->kind);
            cJSON_AddNumberToObject(x, "icon", g->icon);
            cJSON_AddNumberToObject(x, "color", g->color);
            cJSON_AddNumberToObject(x, "banner", g->banner);
            if (g->pic[0])
                cJSON_AddStringToObject(x, "pic", g->pic);
            if (g->anim[0])
                cJSON_AddStringToObject(x, "anim", g->anim);
            cJSON_AddBoolToObject(x, "open", g->open);
            cJSON_AddNumberToObject(x, "perms", orbit_perms(d, i, uid));
            if (orbit_perms(d, i, uid) & (P_KICK | P_BAN | P_DELETE | P_MUTE)) {
                int open = 0;
                for (int k = 0; k < ORBIT_REPORTS; k++)
                    open += d->report[k].used && d->report[k].group == i && d->report[k].state == REPORT_OPEN;
                cJSON_AddNumberToObject(x, "reports", open);
            }
            cJSON_AddNumberToObject(x, "myroles", g->roles[uid]);
            cJSON_AddBoolToObject(x, "smuted", (g->muted >> uid) & 1);
            cJSON_AddStringToObject(x, "desc", g->desc);
            cJSON_AddStringToObject(x, "welcome", g->welcome);
            int online = 0;
            for (int j = 0; j < d->users; j++)
                if ((g->members & (UINT32_C(1) << j)) && shown_status(s, j) != ST_OFFLINE)
                    online++;
            cJSON_AddNumberToObject(x, "online", online);
            cJSON *cats = cJSON_AddArrayToObject(x, "cats");
            for (int c = 0; c < ORBIT_CATEGORIES; c++)
                if (g->cat[c].used) {
                    cJSON *y = cJSON_CreateObject();
                    cJSON_AddNumberToObject(y, "i", c);
                    cJSON_AddNumberToObject(y, "pos", g->cat[c].pos);
                    cJSON_AddStringToObject(y, "name", g->cat[c].name);
                    cJSON_AddItemToArray(cats, y);
                }
            cJSON *chs = cJSON_AddArrayToObject(x, "chs");
            for (int c = 0; c < ORBIT_CHANNELS; c++) {
                if (!g->ch[c].used || (g->kind != GK_DM && !orbit_can_view(d, i, c, uid)))
                    continue;
                const OrbitChannel *h = &g->ch[c];
                cJSON *y = cJSON_CreateObject();
                cJSON_AddNumberToObject(y, "i", c);
                cJSON_AddNumberToObject(y, "type", h->type);
                cJSON_AddNumberToObject(y, "cat", h->category);
                cJSON_AddNumberToObject(y, "pos", h->pos);
                cJSON_AddNumberToObject(y, "view", h->view);
                cJSON_AddNumberToObject(y, "send", h->send);
                cJSON_AddStringToObject(y, "name", h->name);
                cJSON_AddStringToObject(y, "topic", h->topic);
                uint32_t lid = h->type == CH_TEXT ? last_ids[i][c] : 0;
                cJSON_AddNumberToObject(y, "last", lid);
                if (lid) {
                    /* preview for notifications: who wrote what (text stays exactly as written) */
                    const OrbitMessage *lm = &d->message[lid % ORBIT_MESSAGES];
                    char pv[97];
                    size_t n = strlen(lm->text);
                    if (n > 96) {
                        n = 96;
                        while (n && ((unsigned char)lm->text[n] & 0xc0) == 0x80)
                            n--;
                    }
                    memcpy(pv, lm->text, n);
                    pv[n] = 0;
                    cJSON_AddStringToObject(y, "lname", d->user[lm->user].name);
                    cJSON_AddStringToObject(y, "ltext", pv);
                    cJSON_AddBoolToObject(y, "lmine", lm->user == uid);
                }
                int calls = 0;
                for (int j = 0; j < d->users; j++)
                    if ((s->call[i] & g->members & (UINT32_C(1) << j)) && s->call_ch[i][j] == c)
                        calls++;
                cJSON_AddNumberToObject(y, "call", calls);
                cJSON_AddItemToArray(chs, y);
            }
            cJSON *roles = cJSON_AddArrayToObject(x, "roles");
            for (int r = 0; r < ORBIT_ROLES; r++)
                if (g->role[r].used) {
                    cJSON *y = cJSON_CreateObject();
                    cJSON_AddNumberToObject(y, "i", r);
                    cJSON_AddNumberToObject(y, "prio", g->role[r].priority);
                    cJSON_AddNumberToObject(y, "color", g->role[r].color);
                    cJSON_AddNumberToObject(y, "perms", g->role[r].perms);
                    cJSON_AddStringToObject(y, "name", g->role[r].name);
                    cJSON_AddItemToArray(roles, y);
                }
            if (g->kind == GK_DM) {
                cJSON *ids = cJSON_AddArrayToObject(x, "dm");
                for (int j = 0; j < d->users; j++)
                    if ((g->members & (UINT32_C(1) << j)) && j != uid) {
                        cJSON *p = cJSON_CreateObject();
                        add_person(p, s, j, 0);
                        cJSON_AddItemToArray(ids, p);
                    }
            }
        }
        cJSON_AddNumberToObject(x, "last", last);
        cJSON_AddItemToArray(a, x);
    }
    return v;
}
static cJSON *friends_reply(OrbitStore *s) {
    OrbitData *d = &s->data;
    cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "friends");
    for (int i = 0; i < ORBIT_FRIENDS; i++) {
        const OrbitFriend *f = &d->friend_[i];
        if (!f->used)
            continue;
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "i", i);
        cJSON_AddNumberToObject(x, "state", f->state);
        char id[ORBIT_ID_TEXT];
        orbit_id_text(id, f->pub);
        char pub[65];
        orbit_hex(pub, f->pub, 32);
        cJSON_AddStringToObject(x, "fid", id);
        cJSON_AddStringToObject(x, "pub", pub);
        cJSON_AddStringToObject(x, "code", f->code);
        cJSON_AddNumberToObject(x, "since", f->since);
        if (f->user >= 0 && f->user < d->users) {
            add_person(x, s, f->user, 1);
            /* 1.2: automatically detected game, only for friends (never in public member lists) */
            if (f->state == FR_FRIENDS && s->playing[f->user].until > orbit_unix_time()) {
                cJSON *pl = cJSON_AddObjectToObject(x, "playing");
                cJSON_AddStringToObject(pl, "id", s->playing[f->user].id);
                cJSON_AddStringToObject(pl, "name", s->playing[f->user].name);
            }
            cJSON *m = cJSON_AddArrayToObject(x, "mutual");
            for (int g = 0; g < ORBIT_GROUPS; g++) {
                const OrbitGroup *G = &d->group[g];
                if (G->used && G->kind == GK_SERVER && (G->members >> f->user & 1) &&
                    d->owner_user >= 0 && (G->members >> d->owner_user & 1))
                    cJSON_AddItemToArray(m, cJSON_CreateString(G->name));
            }
        } else {
            cJSON_AddNumberToObject(x, "user", -1);
            cJSON_AddStringToObject(x, "name", f->name);
            cJSON_AddNumberToObject(x, "status", ST_OFFLINE);
        }
        cJSON_AddItemToArray(a, x);
    }
    return v;
}
static int friend_slot(OrbitData *d) {
    for (int i = 0; i < ORBIT_FRIENDS; i++)
        if (!d->friend_[i].used)
            return i;
    return -1;
}
static cJSON *dispatch(OrbitStore *s, OrbitSession *sess, const cJSON *r, int *changed) {
    OrbitData *d = &s->data;
    const char *op = orbit_string(r, "op");
    if (!strcmp(op, "hello")) {
        int proto = number(r, "protocol", 9);
        sess->proto = proto >= 10 ? 10 : 9;
        cJSON *v = ok();
        cJSON_AddStringToObject(v, "server", s->server_id);
        cJSON_AddNumberToObject(v, "protocol", ORBIT_PROTOCOL);
        if (d->owner_user >= 0 && d->user[d->owner_user].has_pub) {
            const OrbitUser *o = &d->user[d->owner_user];
            char pub[65], id[ORBIT_ID_TEXT];
            orbit_hex(pub, o->pub, 32);
            orbit_id_text(id, o->pub);
            cJSON_AddStringToObject(v, "owner_pub", pub);
            cJSON_AddStringToObject(v, "owner_id", id);
            cJSON_AddStringToObject(v, "owner_name", o->name);
        }
        return v;
    }
    if (!strcmp(op, "device")) {
        /* Console sign-in: a per-host device secret replaces e-mail and password. */
        uint64_t now = orbit_millis();
        if (now < s->auth_window || now - s->auth_window > 60000) {
            s->auth_window = now;
            s->auth_count = 0;
        }
        if (++s->auth_count > 30)
            return fail(E_TOO_MANY_LOGINS);
        uint8_t raw[32], hash[32];
        const char *name = orbit_string(r, "name");
        if (orbit_unhex(raw, orbit_string(r, "token"), 32))
            return fail(E_DEVICE_INVALID);
        if (!orbit_text_valid(name, 1, 48))
            return fail(E_NAME_LEN);
        int name_rule = orbit_name_check(name);
        /* optional 1.0 identity: Ed25519 key, proven by a signature for this meeting point */
        uint8_t pub[32], sig[64];
        int bound = !orbit_unhex(pub, orbit_string(r, "pub"), 32) &&
                    !orbit_unhex(sig, orbit_string(r, "sig"), 64);
        if (bound && orbit_bind_check(sig, pub, s->server_id))
            return fail(E_BIND);
        crypto_blake2b_keyed(hash, 32, (const uint8_t *)"orbit-device", 12, raw, 32);
        crypto_wipe(raw, 32);
        int uid = -1;
        for (int i = 0; i < d->users && uid < 0; i++)
            for (int j = 0; j < 4; j++)
                if (!crypto_verify32(hash, d->user[i].devices[j]))
                    uid = i;
        if (uid < 0) {
            if (d->users == ORBIT_USERS) {
                crypto_wipe(hash, 32);
                return fail(E_HOST_FULL);
            }
            if (name_rule) {
                crypto_wipe(hash, 32);
                return fail(name_rule);
            }
            OrbitUser *u = &d->user[d->users];
            memset(u, 0, sizeof(*u));
            char id[13];
            orbit_hex(id, hash, 6);
            snprintf(u->email, sizeof(u->email), "ps3-%s@orbit.local", id);
            if (orbit_random(u->salt, 16) || orbit_random(u->hash, 32)) {
                crypto_wipe(hash, 32);
                return fail(E_RANDOM);
            }
            memcpy(u->devices[0], hash, 32);
            strcpy(u->name, name);
            uid = d->users++;
            for (int g = 0; g < ORBIT_GROUPS; g++)
                d->group[g].roles[uid] = 1 << 3;
            *changed = 1;
        } else if (strcmp(d->user[uid].name, name) && !name_rule) {
            strcpy(d->user[uid].name, name);
            *changed = 1;
        }
        crypto_wipe(hash, 32);
        OrbitUser *u = &d->user[uid];
        if (bound && (!u->has_pub || crypto_verify32(u->pub, pub))) {
            memcpy(u->pub, pub, 32);
            u->has_pub = 1;
            *changed = 1;
        }
        apply_profile(u, r, changed);
        if (sess->local && d->owner_user != uid) {
            d->owner_user = uid;
            *changed = 1;
        }
        /* a known friend got a (new) account here: link the friend entry to it */
        if (u->has_pub) {
            int f = find_friend_pub(d, u->pub);
            if (f >= 0 && d->friend_[f].user != uid) {
                d->friend_[f].user = uid;
                *changed = 1;
            }
        }
        count_session(s, sess, uid);
        sess->user = uid;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "user", uid);
        cJSON_AddStringToObject(v, "name", d->user[uid].name);
        return v;
    }
    if (!strcmp(op, "resume")) {
        const char *email = orbit_string(r, "email");
        uint8_t raw[32], hash[32];
        if (!valid_email(email) || orbit_unhex(raw, orbit_string(r, "token"), 32))
            return fail(E_RESUME_INVALID);
        crypto_blake2b(hash, 32, raw, 32);
        crypto_wipe(raw, 32);
        char norm[97];
        normalize_email(norm, email);
        int uid = -1;
        for (int i = 0; i < d->users; i++)
            if (!strcmp(norm, d->user[i].email))
                for (int j = 0; j < 4; j++)
                    if (!crypto_verify32(hash, d->user[i].devices[j]))
                        uid = i;
        crypto_wipe(hash, 32);
        if (uid < 0)
            return fail(E_RESUME_REVOKED);
        count_session(s, sess, uid);
        sess->user = uid;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "user", uid);
        cJSON_AddStringToObject(v, "name", d->user[uid].name);
        return v;
    }
    if (!strcmp(op, "register") || !strcmp(op, "login")) {
        uint64_t now = orbit_millis();
        if (now < s->auth_window || now - s->auth_window > 60000) {
            s->auth_window = now;
            s->auth_count = 0;
        }
        if (++s->auth_count > 12)
            return fail(E_TOO_MANY_LOGINS);
        const char *email = orbit_string(r, "email"), *pass = orbit_string(r, "password"),
                   *name = orbit_string(r, "name");
        char norm[97];
        if (!valid_email(email) || !orbit_text_valid(pass, 10, 128))
            return fail(E_EMAIL_PASS);
        normalize_email(norm, email);
        int uid = -1;
        for (int i = 0; i < d->users; i++)
            if (!strcmp(norm, d->user[i].email))
                uid = i;
        if (!strcmp(op, "register")) {
            if (uid >= 0 || d->users == ORBIT_USERS)
                return fail(E_ACCOUNT);
            if (!orbit_text_valid(name, 1, 48))
                return fail(E_NAME_LEN);
            if (orbit_name_check(name))
                return fail(orbit_name_check(name));
            OrbitUser *u = &d->user[d->users];
            memset(u, 0, sizeof(*u));
            if (orbit_random(u->salt, 16) || password_hash(u->hash, pass, u->salt))
                return fail(E_SECURITY);
            strcpy(u->email, norm);
            strcpy(u->name, name);
            uid = d->users++;
            for (int g = 0; g < ORBIT_GROUPS; g++)
                d->group[g].roles[uid] = 1 << 3;
            for (int i = 0; i < ORBIT_PENDING_EMAILS; i++) {
                OrbitPendingEmail *e = &d->pending_email[i];
                if (e->used && !strcmp(e->email, norm)) {
                    if (d->group[e->group].used)
                        d->group[e->group].pending |= UINT32_C(1) << uid;
                    memset(e, 0, sizeof(*e));
                }
            }
            *changed = 1;
        } else {
            uint8_t hash[32], dummy[16] = {0};
            int h = password_hash(hash, pass, uid >= 0 ? d->user[uid].salt : dummy);
            int valid = h == 0 && uid >= 0 && crypto_verify32(hash, d->user[uid].hash) == 0;
            crypto_wipe(hash, sizeof(hash));
            if (!valid)
                return fail(E_LOGIN_FAILED);
        }
        count_session(s, sess, uid);
        sess->user = uid;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "user", uid);
        cJSON_AddStringToObject(v, "name", d->user[uid].name);
        return v;
    }
    if (sess->user < 0 || sess->user >= d->users)
        return fail(E_SIGN_IN_FIRST);
    int uid = sess->user;
    uint32_t bit = UINT32_C(1) << uid;
    if (!strcmp(op, "remember")) {
        uint8_t raw[32];
        if (orbit_random(raw, 32))
            return fail(E_SAVE_TOKEN);
        OrbitUser *u = &d->user[uid];
        memmove(u->devices + 1, u->devices, 3 * 32);
        crypto_blake2b(u->devices[0], 32, raw, 32);
        char token[65];
        orbit_hex(token, raw, 32);
        crypto_wipe(raw, 32);
        cJSON *v = ok();
        cJSON_AddStringToObject(v, "token", token);
        crypto_wipe(token, 65);
        *changed = 1;
        return v;
    }
    if (!strcmp(op, "bind_device")) {
        /* Migration from 0.6: attach this console to the previously signed-in account. */
        uint8_t raw[32], hash[32];
        if (orbit_unhex(raw, orbit_string(r, "token"), 32))
            return fail(E_DEVICE_INVALID);
        crypto_blake2b_keyed(hash, 32, (const uint8_t *)"orbit-device", 12, raw, 32);
        crypto_wipe(raw, 32);
        for (int i = 0; i < d->users; i++)
            for (int j = 0; j < 4; j++)
                if (!crypto_verify32(hash, d->user[i].devices[j]))
                    memset(d->user[i].devices[j], 0, 32);
        OrbitUser *u = &d->user[uid];
        memmove(u->devices + 1, u->devices, 3 * 32);
        memcpy(u->devices[0], hash, 32);
        crypto_wipe(hash, 32);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "forget_token")) {
        uint8_t raw[32], hash[32];
        if (orbit_unhex(raw, orbit_string(r, "token"), 32))
            return fail(E_RESUME_INVALID);
        crypto_blake2b(hash, 32, raw, 32);
        crypto_wipe(raw, 32);
        for (int j = 0; j < 4; j++)
            if (!crypto_verify32(hash, d->user[uid].devices[j])) {
                memset(d->user[uid].devices[j], 0, 32);
                *changed = 1;
            }
        crypto_wipe(hash, 32);
        sess->user = -1;
        return ok();
    }
    if (!strcmp(op, "invitations")) {
        cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "invitations");
        for (int i = 0; i < ORBIT_GROUPS; i++) {
            OrbitGroup *g = &d->group[i];
            if (!g->used || !(g->pending & bit))
                continue;
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "group", i);
            cJSON_AddStringToObject(x, "name", g->name);
            cJSON_AddStringToObject(x, "from", d->user[g->owner].name);
            cJSON_AddItemToArray(a, x);
        }
        return v;
    }
    if (!strcmp(op, "respond_invite")) {
        int id = number(r, "group", -1);
        cJSON *accept = cJSON_GetObjectItemCaseSensitive(r, "accept");
        if (id < 0 || id >= ORBIT_GROUPS || !d->group[id].used || !(d->group[id].pending & bit) ||
            !cJSON_IsBool(accept))
            return fail(E_INVITE_GONE);
        d->group[id].pending &= ~bit;
        if (cJSON_IsTrue(accept))
            d->group[id].members |= bit;
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "logout")) {
        sess->user = -1;
        return ok();
    }
    if (!strcmp(op, "profile")) {
        apply_profile(&d->user[uid], r, changed);
        apply_picture(s, uid, r, changed);
        return ok();
    }
    if (!strcmp(op, "blob_put")) {
        /* 1.2: one piece of an animation this person wants to use (profile or server icon). Kept
         * in RAM only, a few at a time, until a profile/server_update names it. */
        const char *h = orbit_string(r, "hash");
        int off = number(r, "offset", -1), total = number(r, "total", 0);
        if (!avatar_hash_valid(h) || total < 16 || total > (int)ANIM_BLOB_MAX || off < 0 || off >= total)
            return fail(E_PIC);
        if (off == 0 && s->path[0]) { /* already stored here: nothing to upload */
            char dir[530];
            uint8_t *have;
            size_t hn;
            snprintf(dir, sizeof(dir), "%s.pics", s->path);
            if (!anim_load(dir, h, &have, &hn)) {
                free(have);
                cJSON *v = ok();
                cJSON_AddNumberToObject(v, "have", (double)hn);
                return v;
            }
        }
        if (cJSON_IsTrue(cJSON_GetObjectItem(r, "probe"))) {
            cJSON *v = ok();
            cJSON_AddNumberToObject(v, "have", 0);
            return v;
        }
        uint64_t now = orbit_millis();
        OrbitStaged *t = NULL;
        for (int i = 0; i < ORBIT_STAGED && !t; i++)
            if (s->staged[i].data && s->staged[i].user == uid && !strcmp(s->staged[i].hash, h))
                t = &s->staged[i];
        if (!t && off == 0) {
            for (int i = 0; i < ORBIT_STAGED; i++) /* a free slot, one per person, or an old one */
                if (s->staged[i].data && s->staged[i].user == uid)
                    free(s->staged[i].data), memset(&s->staged[i], 0, sizeof(s->staged[i]));
            for (int i = 0; i < ORBIT_STAGED && !t; i++)
                if (!s->staged[i].data || now - s->staged[i].when > 120000)
                    t = &s->staged[i];
            if (!t)
                return fail(E_WAIT);
            free(t->data);
            memset(t, 0, sizeof(*t));
            if (!(t->data = malloc((size_t)total)))
                return fail(E_WAIT);
            t->user = uid;
            t->total = (size_t)total;
            orbit_copy(t->hash, sizeof(t->hash), h);
        }
        if (!t || (size_t)off != t->n || t->total != (size_t)total)
            return fail(E_PIC); /* pieces must arrive in order */
        size_t got = 0;
        if (avatar_b64_decode(t->data + t->n, t->total - t->n, &got, orbit_string(r, "data")) || !got ||
            got > ANIM_PIECE) {
            free(t->data);
            memset(t, 0, sizeof(*t));
            return fail(E_PIC);
        }
        t->n += got;
        t->when = now;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "have", (double)t->n);
        return v;
    }
    if (!strcmp(op, "pic")) {
        /* 1.2: a profile picture by name, only for names one of this host's people uses */
        const char *h = orbit_string(r, "hash");
        int known = avatar_hash_valid(h) && pic_in_use(d, h);
        if (known && !strcmp(orbit_string(r, "kind"), "ani")) {
            /* 1.2: an animation, in pieces of at most ANIM_PIECE bytes */
            char dir[530];
            snprintf(dir, sizeof(dir), "%s.pics", s->path);
            uint8_t *blob;
            size_t n;
            int off = number(r, "offset", 0);
            if (!s->path[0] || anim_load(dir, h, &blob, &n))
                return fail(E_PIC);
            cJSON *v = NULL;
            if (off >= 0 && (size_t)off < n) {
                size_t k = n - (size_t)off < ANIM_PIECE ? n - (size_t)off : ANIM_PIECE;
                char *text = malloc(ANIM_PIECE / 3 * 4 + 8);
                if (text && !avatar_b64_encode(text, ANIM_PIECE / 3 * 4 + 8, blob + off, k)) {
                    v = ok();
                    cJSON_AddStringToObject(v, "data", text);
                    cJSON_AddNumberToObject(v, "total", (double)n);
                }
                free(text);
            }
            free(blob);
            return v ? v : fail(E_PIC);
        }
        uint8_t *blob = known ? malloc(AVATAR_BLOB_MAX) : NULL;
        char *text = blob ? malloc(AVATAR_B64_MAX) : NULL;
        char dir[530];
        size_t n = 0;
        snprintf(dir, sizeof(dir), "%s.pics", s->path);
        cJSON *v = NULL;
        if (text && s->path[0] && !avatar_load(dir, h, blob, &n) &&
            !avatar_b64_encode(text, AVATAR_B64_MAX, blob, n)) {
            v = ok();
            cJSON_AddStringToObject(v, "data", text);
        }
        free(blob);
        free(text);
        return v ? v : fail(E_PIC);
    }
    if (!strcmp(op, "watch_key")) {
        /* 1.1: credential for this person's background-notification plugin. Created once and
         * kept; "renew" replaces it (old plugins are locked out). Only the person themself. */
        OrbitUser *u = &d->user[uid];
        if (!u->has_watch || number(r, "renew", 0) == 1) {
            if (orbit_random(u->watch, 32))
                return fail(E_MEMORY);
            u->has_watch = 1;
            *changed = 1;
        }
        uint8_t id[8];
        char wid[17], wkey[65];
        orbit_watch_id(id, u->watch);
        orbit_hex(wid, id, 8);
        orbit_hex(wkey, u->watch, 32);
        cJSON *v = ok();
        cJSON_AddStringToObject(v, "wid", wid);
        cJSON_AddStringToObject(v, "wkey", wkey);
        crypto_wipe(wkey, sizeof(wkey));
        return v;
    }
    if (!strcmp(op, "groups"))
        return groups_reply(s, sess, uid);
    if (!strcmp(op, "create")) {
        const char *name = orbit_string(r, "name");
        if (!orbit_text_valid(name, 1, 64))
            return fail(E_GROUP_NAME);
        if (orbit_text_offensive(name))
            return fail(E_NAME_BLOCKED);
        int id = new_group_slot(d);
        if (id < 0)
            return fail(E_GROUPS_FULL);
        char generated[33];
        if (new_group_code(d, generated))
            return fail(E_RANDOM);
        OrbitGroup *g = &d->group[id];
        memset(g, 0, sizeof(*g));
        g->used = 1;
        g->owner = uid;
        g->members = bit;
        g->kind = GK_SERVER;
        strcpy(g->name, name);
        strcpy(g->invite, generated);
        cJSON *cats = cJSON_GetObjectItem(r, "categories"), *chs = cJSON_GetObjectItem(r, "channels");
        if (cJSON_IsArray(chs) && cJSON_GetArraySize(chs) > 0) {
            /* template from the creator's app, in the creator's language */
            const cJSON *x;
            cJSON_ArrayForEach(x, cats) {
                if (cJSON_IsString(x) && orbit_text_valid(x->valuestring, 1, 32))
                    add_category(g, x->valuestring);
            }
            cJSON_ArrayForEach(x, chs) {
                const char *n = orbit_string(x, "name");
                int cat = number(x, "cat", -1);
                if (cat >= ORBIT_CATEGORIES || (cat >= 0 && !g->cat[cat].used))
                    cat = -1;
                if (orbit_text_valid(n, 1, 32))
                    add_channel(g, n, number(x, "type", CH_TEXT), cat);
            }
            default_roles(g);
            if (find_channel_by_type(g, CH_TEXT) < 0)
                add_channel(g, T_in(s->lang, T_CH_GENERAL), CH_TEXT, -1);
        } else
            default_server(g, s->lang, 1);
        g->roles[uid] = 1;
        g->icon = clampi(number(r, "icon", 0), 0, 15);
        g->color = clampi(number(r, "color", 0), 0, 11);
        opt_text(g->desc, sizeof(g->desc), r, "desc", 128);
        *changed = 1;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "group", id);
        return v;
    }
    if (!strcmp(op, "home")) {
        /* the hidden inbox whose invitation is this console's friend code */
        if (!sess->local)
            return fail(E_LOCAL_ONLY);
        int id = -1;
        for (int i = 0; i < ORBIT_GROUPS; i++)
            if (d->group[i].used && d->group[i].kind == GK_HOME)
                id = i;
        if (id < 0) {
            id = new_group_slot(d);
            char generated[33];
            if (id < 0)
                return fail(E_GROUPS_FULL);
            if (new_group_code(d, generated))
                return fail(E_RANDOM);
            OrbitGroup *g = &d->group[id];
            memset(g, 0, sizeof(*g));
            g->used = 1;
            g->kind = GK_HOME;
            g->owner = uid;
            g->members = bit;
            strcpy(g->invite, generated);
            add_channel(g, "inbox", CH_TEXT, -1);
            default_roles(g);
            *changed = 1;
        }
        cJSON *v = ok();
        cJSON_AddStringToObject(v, "invite", d->group[id].invite);
        return v;
    }
    if (!strcmp(op, "join")) {
        uint64_t now = orbit_millis();
        if (now - s->join_window[uid] >= 60000) {
            s->join_window[uid] = now;
            s->join_count[uid] = 0;
        }
        if (s->join_count[uid] >= 12)
            return fail(E_TOO_MANY_CODES);
        s->join_count[uid]++;
        char invite[33];
        if (orbit_group_code(invite, orbit_string(r, "invite")))
            return fail(E_CODE_MISMATCH);
        for (int i = 0; i < ORBIT_GROUPS; i++) {
            OrbitGroup *g = &d->group[i];
            if (g->used && !strcmp(invite, g->invite)) {
                if (g->kind == GK_HOME) {
                    /* a friend code: connecting is enough, nobody joins the inbox */
                    cJSON *v = ok();
                    cJSON_AddNumberToObject(v, "group", -1);
                    cJSON_AddBoolToObject(v, "home", 1);
                    return v;
                }
                if (g->kind != GK_SERVER)
                    return fail(E_CODE_MISMATCH);
                if (is_banned(g, uid))
                    return fail(E_BANNED);
                if (!(g->members & bit))
                    g->roles[uid] = 1 << 3;
                g->members |= bit;
                g->pending &= ~bit;
                *changed = 1;
                cJSON *v = ok();
                cJSON_AddNumberToObject(v, "group", i);
                return v;
            }
        }
        return fail(E_CODE_MISMATCH);
    }
    if (!strcmp(op, "browse")) {
        cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "servers");
        for (int i = 0; i < ORBIT_GROUPS; i++) {
            OrbitGroup *g = &d->group[i];
            /* 1.2: servers the asker is already in are listed too (flag "member"): before, a
             * host never showed its own public server and Discover looked empty. */
            if (!g->used || g->kind != GK_SERVER || !g->open || is_banned(g, uid))
                continue;
            cJSON *x = cJSON_CreateObject();
            int count = 0, online = 0;
            for (int j = 0; j < d->users; j++)
                if ((g->members >> j) & 1) {
                    count++;
                    online += shown_status(s, j) != ST_OFFLINE;
                }
            cJSON_AddNumberToObject(x, "online", online);
            cJSON_AddBoolToObject(x, "member", (g->members & bit) != 0);
            cJSON_AddNumberToObject(x, "id", i);
            cJSON_AddNumberToObject(x, "plat", ORBIT_PLAT); /* 1.3 */
            cJSON_AddStringToObject(x, "name", g->name);
            cJSON_AddStringToObject(x, "desc", g->desc);
            cJSON_AddNumberToObject(x, "icon", g->icon);
            if (g->pic[0])
                cJSON_AddStringToObject(x, "pic", g->pic);
            if (g->anim[0])
                cJSON_AddStringToObject(x, "anim", g->anim);
            cJSON_AddNumberToObject(x, "color", g->color);
            cJSON_AddNumberToObject(x, "members", count);
            cJSON_AddStringToObject(x, "host", d->user[g->owner].name);
            cJSON_AddItemToArray(a, x);
        }
        return v;
    }
    if (!strcmp(op, "join_public")) {
        int gid = number(r, "group", -1);
        if (gid < 0 || gid >= ORBIT_GROUPS || !d->group[gid].used || d->group[gid].kind != GK_SERVER ||
            !d->group[gid].open)
            return fail(E_NO_ACCESS);
        OrbitGroup *g = &d->group[gid];
        if (is_banned(g, uid))
            return fail(E_BANNED);
        if (!(g->members & bit))
            g->roles[uid] = 1 << 3;
        g->members |= bit;
        *changed = 1;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "group", gid);
        return v;
    }
    /* ---------- friends ---------- */
    if (!strcmp(op, "friend")) {
        /* a remote person (signed with their key) asks the owner of this PS3 */
        const OrbitUser *u = &d->user[uid];
        if (!u->has_pub)
            return fail(E_BIND);
        if (uid == d->owner_user)
            return fail(E_SELF);
        uint64_t now = orbit_millis();
        if (now - s->friend_window[uid] >= 60000) {
            s->friend_window[uid] = now;
            s->friend_count[uid] = 0;
        }
        if (++s->friend_count[uid] > 6)
            return fail(E_WAIT);
        int f = find_friend_pub(d, u->pub);
        cJSON *rm = cJSON_GetObjectItem(r, "remove");
        const char *code = orbit_string(r, "code");
        cJSON *v = ok();
        if (cJSON_IsTrue(rm)) {
            if (f >= 0 && d->friend_[f].state != FR_BLOCKED) {
                memset(&d->friend_[f], 0, sizeof(d->friend_[f]));
                *changed = 1;
            }
            cJSON_AddStringToObject(v, "state", "removed");
            return v;
        }
        if (f < 0) {
            f = friend_slot(d);
            if (f < 0) {
                cJSON_Delete(v);
                return fail(E_FRIENDS_FULL);
            }
            OrbitFriend *e = &d->friend_[f];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->state = FR_INCOMING;
            memcpy(e->pub, u->pub, 32);
            e->since = orbit_unix_time();
            *changed = 1;
        }
        OrbitFriend *e = &d->friend_[f];
        if (e->state == FR_OUTGOING) {
            e->state = FR_FRIENDS;
            e->since = orbit_unix_time();
            *changed = 1;
        }
        if (e->user != uid || strcmp(e->name, u->name) ||
            (strlen(code) < sizeof(e->code) && strcmp(e->code, code))) {
            e->user = uid;
            memcpy(e->name, u->name, sizeof(e->name));
            if (strlen(code) < sizeof(e->code))
                orbit_copy(e->code, sizeof(e->code), code);
            *changed = 1;
        }
        /* a blocked person learns nothing: it simply stays "pending" for them */
        cJSON_AddStringToObject(v, "state", e->state == FR_FRIENDS ? "friends" : "pending");
        return v;
    }
    if (!strcmp(op, "friends")) {
        if (!sess->local)
            return fail(E_LOCAL_ONLY);
        return friends_reply(s);
    }
    if (!strcmp(op, "friend_out") || !strcmp(op, "friend_block_pub")) {
        /* the owner asked someone else (or blocks someone by key) */
        if (!sess->local)
            return fail(E_LOCAL_ONLY);
        uint8_t pub[32];
        if (orbit_unhex(pub, orbit_string(r, "pub"), 32))
            return fail(E_BAD_REQUEST);
        if (d->user[uid].has_pub && !crypto_verify32(pub, d->user[uid].pub))
            return fail(E_SELF);
        int block = !strcmp(op, "friend_block_pub");
        int f = find_friend_pub(d, pub);
        if (f < 0) {
            f = friend_slot(d);
            if (f < 0)
                return fail(E_FRIENDS_FULL);
            memset(&d->friend_[f], 0, sizeof(d->friend_[f]));
            d->friend_[f].used = 1;
            d->friend_[f].user = -1;
            d->friend_[f].state = FR_OUTGOING;
            memcpy(d->friend_[f].pub, pub, 32);
            d->friend_[f].since = orbit_unix_time();
        }
        OrbitFriend *e = &d->friend_[f];
        if (block)
            e->state = FR_BLOCKED;
        else if (e->state == FR_INCOMING || !strcmp(orbit_string(r, "state"), "friends"))
            e->state = e->state == FR_BLOCKED ? FR_BLOCKED : FR_FRIENDS;
        char name[49];
        if (opt_text(name, sizeof(name), r, "name", 48) == 1 && name[0])
            orbit_copy(e->name, sizeof(e->name), name);
        const char *code = orbit_string(r, "code");
        if (code[0] && strlen(code) < sizeof(e->code))
            orbit_copy(e->code, sizeof(e->code), code);
        *changed = 1;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "i", f);
        cJSON_AddNumberToObject(v, "state", e->state);
        return v;
    }
    if (!strncmp(op, "friend_", 7)) {
        if (!sess->local)
            return fail(E_LOCAL_ONLY);
        int f = number(r, "i", -1);
        if (f < 0 || f >= ORBIT_FRIENDS || !d->friend_[f].used)
            return fail(E_NOT_FRIEND);
        OrbitFriend *e = &d->friend_[f];
        if (!strcmp(op, "friend_respond")) {
            if (e->state != FR_INCOMING)
                return fail(E_NOT_FRIEND);
            if (cJSON_IsTrue(cJSON_GetObjectItem(r, "accept"))) {
                e->state = FR_FRIENDS;
                e->since = orbit_unix_time();
            } else
                memset(e, 0, sizeof(*e));
        } else if (!strcmp(op, "friend_remove") || !strcmp(op, "friend_unblock"))
            memset(e, 0, sizeof(*e));
        else if (!strcmp(op, "friend_block")) {
            e->state = FR_BLOCKED;
            /* blocked people leave every private conversation on this PS3 */
            for (int g = 0; g < ORBIT_GROUPS; g++)
                if (e->user >= 0 && d->group[g].used && d->group[g].kind == GK_DM &&
                    (d->group[g].members >> e->user & 1))
                    remove_member(s, &d->group[g], g, e->user);
        } else
            return fail(E_UNKNOWN);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "dm_create")) {
        /* private (group) conversation with friends, hosted on this PS3 */
        if (!sess->local)
            return fail(E_LOCAL_ONLY);
        uint32_t want = bit;
        const cJSON *x;
        int n = 0;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "users")) {
            int u = cJSON_IsNumber(x) ? x->valueint : -1;
            if (u < 0 || u >= d->users || u == uid)
                return fail(E_NOT_FRIEND);
            int f = d->user[u].has_pub ? find_friend_pub(d, d->user[u].pub) : -1;
            if (f < 0 || d->friend_[f].state != FR_FRIENDS)
                return fail(E_NOT_FRIEND);
            want |= UINT32_C(1) << u;
            n++;
        }
        if (n < 1 || n > 9)
            return fail(E_BAD_REQUEST);
        if (n == 1)
            for (int i = 0; i < ORBIT_GROUPS; i++)
                if (d->group[i].used && d->group[i].kind == GK_DM && d->group[i].members == want) {
                    cJSON *v = ok();
                    cJSON_AddNumberToObject(v, "group", i);
                    return v;
                }
        int id = new_group_slot(d);
        if (id < 0)
            return fail(E_GROUPS_FULL);
        char generated[33];
        if (new_group_code(d, generated))
            return fail(E_RANDOM);
        OrbitGroup *g = &d->group[id];
        memset(g, 0, sizeof(*g));
        g->used = 1;
        g->kind = GK_DM;
        g->owner = uid;
        g->members = want;
        strcpy(g->invite, generated);
        char name[65] = "";
        if (opt_text(name, sizeof(name), r, "name", 64) == 1)
            orbit_copy(g->name, sizeof(g->name), name);
        add_channel(g, "dm", CH_TEXT, -1);
        default_roles(g);
        *changed = 1;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "group", id);
        return v;
    }
    /* ---------- everything below works on one group ---------- */
    int gid = number(r, "group", -1);
    if (gid < 0 || gid >= ORBIT_GROUPS || !d->group[gid].used || !(d->group[gid].members & bit) ||
        d->group[gid].kind == GK_HOME)
        return fail(E_NO_ACCESS);
    OrbitGroup *g = &d->group[gid];
    uint32_t perms = orbit_perms(d, gid, uid);
    int mytop = top_priority(g, uid);
    /* ---------- 1.2: reports to the moderators of this server ---------- */
    if (!strcmp(op, "report")) {
        int target = number(r, "user", -1), reason = number(r, "reason", -1);
        uint32_t msg = (uint32_t)number(r, "msg", 0);
        if (g->kind != GK_SERVER || target < 0 || target >= d->users || target == uid || reason < 0 ||
            reason >= RR_COUNT)
            return fail(E_BAD_REQUEST);
        int mine = 0, slot = -1;
        for (int i = 0; i < ORBIT_REPORTS; i++) {
            OrbitReport *e = &d->report[i];
            if (!e->used) {
                if (slot < 0)
                    slot = i;
                continue;
            }
            if (e->state == REPORT_OPEN && e->reporter == uid) {
                mine++;
                if (e->group == gid && e->target == target && e->msg == msg)
                    return ok(); /* already reported */
            }
        }
        if (mine >= 10)
            return fail(E_REPORT_LIMIT);
        if (slot < 0) /* full: the oldest handled report makes room, open ones are never dropped */
            for (int i = 0; i < ORBIT_REPORTS; i++)
                if (d->report[i].state != REPORT_OPEN && (slot < 0 || d->report[i].time < d->report[slot].time))
                    slot = i;
        if (slot < 0)
            return fail(E_REPORT_LIMIT);
        OrbitReport *e = &d->report[slot];
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->id = ++d->next_report;
        e->group = gid;
        e->reporter = uid;
        e->target = target;
        e->reason = reason;
        e->handler = -1;
        e->time = orbit_unix_time();
        if (msg) { /* quote the message as the server stored it (server channels are not E2EE) */
            const OrbitMessage *m = &d->message[msg % ORBIT_MESSAGES];
            if (m->id == msg && m->group == gid && m->user == target && !(m->flags & MF_DELETED)) {
                e->msg = msg;
                size_t n = strlen(m->text);
                if (n > 128)
                    for (n = 128; n && (m->text[n] & 0xC0) == 0x80; n--)
                        ;
                memcpy(e->text, m->text, n);
                e->text[n] = 0;
            }
        }
        opt_text(e->detail, sizeof(e->detail), r, "detail", 96);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "reports") || !strcmp(op, "report_resolve")) {
        if (!(perms & (P_KICK | P_BAN | P_DELETE | P_MUTE)))
            return fail(E_NO_PERMISSION);
        if (!strcmp(op, "report_resolve")) {
            uint32_t id = (uint32_t)number(r, "id", 0);
            int state = number(r, "state", REPORT_DONE);
            if (state != REPORT_DONE && state != REPORT_DISMISSED)
                return fail(E_BAD_REQUEST);
            for (int i = 0; i < ORBIT_REPORTS; i++)
                if (d->report[i].used && d->report[i].id == id && d->report[i].group == gid) {
                    d->report[i].state = state;
                    d->report[i].handler = uid;
                    *changed = 1;
                    return ok();
                }
            return fail(E_BAD_REQUEST);
        }
        cJSON *v = ok(), *list = cJSON_AddArrayToObject(v, "reports");
        for (int pass = 0; pass < 2; pass++) /* open ones first, newest first */
            for (uint32_t want = d->next_report; want > 0; want--)
                for (int i = 0; i < ORBIT_REPORTS; i++) {
                    const OrbitReport *e = &d->report[i];
                    if (!e->used || e->id != want || e->group != gid || (e->state == REPORT_OPEN) != (pass == 0))
                        continue;
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddNumberToObject(x, "id", e->id);
                    cJSON_AddNumberToObject(x, "user", e->target);
                    cJSON_AddStringToObject(x, "target", d->user[e->target].name);
                    cJSON_AddStringToObject(x, "reporter", d->user[e->reporter].name);
                    cJSON_AddNumberToObject(x, "reason", e->reason);
                    cJSON_AddNumberToObject(x, "state", e->state);
                    cJSON_AddNumberToObject(x, "msg", e->msg);
                    cJSON_AddNumberToObject(x, "time", e->time);
                    cJSON_AddStringToObject(x, "text", e->text);
                    cJSON_AddStringToObject(x, "detail", e->detail);
                    cJSON_AddBoolToObject(x, "banned", is_banned(g, e->target));
                    cJSON_AddItemToArray(list, x);
                }
        return v;
    }
    if (!strcmp(op, "invite_user")) {
        if (g->owner != uid)
            return fail(E_OWNER_ONLY);
        const char *email = orbit_string(r, "email");
        if (!valid_email(email))
            return fail(E_EMAIL);
        char norm[97];
        normalize_email(norm, email);
        int target = -1;
        for (int i = 0; i < d->users; i++)
            if (!strcmp(d->user[i].email, norm))
                target = i;
        if (target < 0) {
            int slot = -1;
            for (int i = 0; i < ORBIT_PENDING_EMAILS; i++) {
                OrbitPendingEmail *e = &d->pending_email[i];
                if (e->used && e->group == gid && !strcmp(e->email, norm)) {
                    slot = i;
                    break;
                }
                if (!e->used)
                    slot = i;
            }
            if (slot < 0)
                return fail(E_TOO_MANY_PENDING);
            OrbitPendingEmail *e = &d->pending_email[slot];
            e->used = 1;
            e->group = gid;
            strcpy(e->email, norm);
            *changed = 1;
            cJSON *v = ok();
            cJSON_AddBoolToObject(v, "waiting_registration", 1);
            return v;
        }
        uint32_t other = UINT32_C(1) << target;
        if (g->members & other)
            return fail(E_ALREADY_MEMBER);
        g->pending |= other;
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "invite")) {
        /* Every member with the invite right may share the server: one code is all it takes. */
        if (g->kind != GK_SERVER || !(perms & P_INVITE))
            return fail(E_NO_PERMISSION);
        cJSON *v = ok();
        cJSON_AddStringToObject(v, "invite", g->invite);
        return v;
    }
    if (!strcmp(op, "rotate")) {
        if (!(perms & P_MANAGE_SERVER))
            return fail(E_NO_PERMISSION);
        char generated[33];
        if (new_group_code(d, generated))
            return fail(E_RANDOM);
        strcpy(g->invite, generated);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "leave")) {
        if (g->owner == uid)
            return fail(E_OWNER_LEAVE);
        remove_member(s, g, gid, uid);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "delete")) {
        if (g->owner != uid)
            return fail(E_OWNER_ONLY);
        delete_group(d, gid);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "members")) {
        cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "members");
        for (int j = 0; j < d->users; j++) {
            if (!(g->members >> j & 1))
                continue;
            cJSON *x = cJSON_CreateObject();
            add_person(x, s, j, 1);
            cJSON_AddNumberToObject(x, "roles", g->owner == j ? 1 : g->roles[j]);
            cJSON_AddNumberToObject(x, "top", top_priority(g, j));
            cJSON_AddBoolToObject(x, "owner", g->owner == j);
            cJSON_AddBoolToObject(x, "smuted", (g->muted >> j) & 1);
            cJSON_AddNumberToObject(x, "call", (s->call[gid] >> j & 1) ? s->call_ch[gid][j] : -1);
            cJSON_AddItemToArray(a, x);
        }
        if (perms & P_BAN) {
            cJSON *b = cJSON_AddArrayToObject(v, "banned");
            for (int j = 0; j < d->users; j++)
                if (is_banned(g, j)) {
                    cJSON *x = cJSON_CreateObject();
                    add_person(x, s, j, 0);
                    cJSON_AddItemToArray(b, x);
                }
        }
        return v;
    }
    if (!strcmp(op, "send")) {
        const char *body = orbit_string(r, "text");
        int ch = number(r, "channel", -1);
        if (ch < 0)
            ch = find_channel_by_type(g, CH_TEXT); /* 0.9 clients */
        if (!orbit_text_valid(body, 1, 768))
            return fail(E_MESSAGE_LEN);
        if (ch < 0 || ch >= ORBIT_CHANNELS || !g->ch[ch].used || g->ch[ch].type != CH_TEXT ||
            (g->kind != GK_DM && !orbit_can_view(d, gid, ch, uid)))
            return fail(E_NO_CHANNEL);
        if (!(perms & P_SEND) || (g->muted & bit) || !roles_mask_allows(g, uid, g->ch[ch].send))
            return fail(E_NO_PERMISSION);
        if (g->kind == GK_DM && blocked_by_owner(d, uid))
            return fail(E_NO_PERMISSION);
        uint64_t now = orbit_millis();
        if (now >= sess->last_write && now - sess->last_write < 300)
            return fail(E_WAIT);
        if (d->next_id >= 2147483600)
            return fail(E_COUNTER);
        int reply = number(r, "reply", 0);
        OrbitMessage *m = &d->message[d->next_id % ORBIT_MESSAGES];
        memset(m, 0, sizeof(*m));
        m->id = d->next_id++;
        m->user = uid;
        m->group = gid;
        m->channel = ch;
        m->time = orbit_unix_time();
        m->rev = ++d->rev;
        m->reply = find_message(d, gid, reply) ? (uint32_t)reply : 0;
        strcpy(m->text, body);
        sess->last_write = now;
        *changed = 1;
        cJSON *v = ok();
        cJSON_AddNumberToObject(v, "id", m->id);
        return v;
    }
    if (!strcmp(op, "history")) {
        int after = number(r, "after", 0), ch = number(r, "channel", -1);
        if (ch < 0)
            ch = find_channel_by_type(g, CH_TEXT);
        if (after < 0)
            return fail(E_BAD_COUNTER);
        if (ch < 0 || (g->kind != GK_DM && !orbit_can_view(d, gid, ch, uid)))
            return fail(E_NO_CHANNEL);
        cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "messages");
        uint32_t start = d->next_id > ORBIT_MESSAGES ? d->next_id - ORBIT_MESSAGES : 1;
        int emitted = 0, v10 = sess->proto >= 10;
        for (uint32_t id = start; id < d->next_id; id++) {
            OrbitMessage *m = &d->message[id % ORBIT_MESSAGES];
            if (m->id == id && m->group == gid && m->channel == ch && id > (uint32_t)after &&
                (v10 || !(m->flags & MF_DELETED))) {
                if (emitted++ == 40)
                    break;
                message_json(a, s, m, sess->proto);
            }
        }
        /* edits, deletions and reactions of messages the client already has */
        cJSON *since = cJSON_GetObjectItem(r, "since");
        if (v10 && cJSON_IsNumber(since) && since->valuedouble >= 0) {
            uint32_t sr = (uint32_t)since->valuedouble;
            cJSON *c = cJSON_AddArrayToObject(v, "changed");
            for (int k = 0; k < ORBIT_MESSAGES; k++) {
                OrbitMessage *m = &d->message[k];
                if (m->id && m->group == gid && m->channel == ch && m->rev > sr &&
                    m->id <= (uint32_t)after)
                    message_json(c, s, m, sess->proto);
            }
        }
        cJSON_AddNumberToObject(v, "mrev", d->rev);
        return v;
    }
    if (!strcmp(op, "edit") || !strcmp(op, "remove_msg") || !strcmp(op, "react")) {
        OrbitMessage *m = find_message(d, gid, number(r, "id", 0));
        if (!m || (m->flags & MF_DELETED) || (g->kind != GK_DM && !orbit_can_view(d, gid, m->channel, uid)))
            return fail(E_NO_MESSAGE);
        if (!strcmp(op, "edit")) {
            const char *body = orbit_string(r, "text");
            if (m->user != uid)
                return fail(E_NO_PERMISSION);
            if (!orbit_text_valid(body, 1, 768))
                return fail(E_MESSAGE_LEN);
            strcpy(m->text, body);
            m->flags |= MF_EDITED;
        } else if (!strcmp(op, "remove_msg")) {
            if (m->user != uid && !(perms & P_DELETE))
                return fail(E_NO_PERMISSION);
            m->flags |= MF_DELETED;
            memset(m->text, 0, sizeof(m->text));
            memset(m->react, 0, sizeof(m->react));
        } else {
            int k = number(r, "r", -1);
            if (k < 0 || k >= ORBIT_REACTIONS || !(perms & P_SEND))
                return fail(E_BAD_REQUEST);
            m->react[k] ^= bit;
        }
        m->rev = ++d->rev;
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "search")) {
        const char *q = orbit_string(r, "q");
        int ch = number(r, "channel", -1);
        size_t qn = strlen(q);
        if (qn < 1 || qn > 64)
            return fail(E_BAD_REQUEST);
        cJSON *v = ok(), *a = cJSON_AddArrayToObject(v, "messages");
        int found = 0;
        for (uint32_t id = d->next_id; id-- > 1 && found < 20;) {
            if (d->next_id - id > ORBIT_MESSAGES)
                break;
            OrbitMessage *m = &d->message[id % ORBIT_MESSAGES];
            if (m->id != id || m->group != gid || (m->flags & MF_DELETED) || (ch >= 0 && m->channel != ch) ||
                (g->kind != GK_DM && !orbit_can_view(d, gid, m->channel, uid)))
                continue;
            /* case-insensitive for ASCII; other characters must match exactly */
            int hit = 0;
            for (const char *p = m->text; *p && !hit; p++) {
                size_t k = 0;
                while (k < qn && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)q[k]))
                    k++;
                hit = k == qn;
            }
            if (hit) {
                message_json(a, s, m, 10);
                found++;
            }
        }
        return v;
    }
    /* ---------- server management ---------- */
    if (g->kind != GK_SERVER && (!strncmp(op, "server_", 7) || !strncmp(op, "channel_", 8) ||
                                 !strncmp(op, "category_", 9) || !strncmp(op, "role_", 5)))
        return fail(E_NO_PERMISSION);
    if (!strcmp(op, "server_update")) {
        if (!(perms & P_MANAGE_SERVER))
            return fail(E_NO_PERMISSION);
        char name[65];
        int k = opt_text(name, sizeof(name), r, "name", 64);
        if (k < 0 || (k == 1 && !name[0]))
            return fail(E_GROUP_NAME);
        if (k == 1 && g->kind == GK_SERVER && orbit_text_offensive(name))
            return fail(E_NAME_BLOCKED);
        /* 1.2: everything is checked before anything changes; the audit log compares old/new */
        char old_desc[129], old_welcome[193], new_desc[129], new_welcome[193];
        orbit_copy(old_desc, sizeof(old_desc), g->desc);
        orbit_copy(old_welcome, sizeof(old_welcome), g->welcome);
        memcpy(new_desc, old_desc, sizeof(new_desc));
        memcpy(new_welcome, old_welcome, sizeof(new_welcome));
        if (opt_text(new_desc, sizeof(new_desc), r, "desc", 128) < 0 ||
            opt_text(new_welcome, sizeof(new_welcome), r, "welcome", 192) < 0)
            return fail(E_TEXT_LEN);
        int old_icon = g->icon, old_color = g->color, old_banner = g->banner, old_open = g->open;
        int pics = g->kind == GK_SERVER ? set_pictures(s, uid, g->pic, g->anim, r) : 0;
        if (pics < 0)
            return fail(E_PIC);
        if (k == 1 && strcmp(g->name, name)) {
            orbit_copy(g->name, sizeof(g->name), name);
            audit_add(d, gid, uid, AUD_NAME);
        }
        memcpy(g->desc, new_desc, sizeof(new_desc));
        memcpy(g->welcome, new_welcome, sizeof(new_welcome));
        if (has(r, "icon"))
            g->icon = clampi(number(r, "icon", 0), 0, 15);
        if (has(r, "color"))
            g->color = clampi(number(r, "color", 0), 0, 11);
        if (has(r, "banner"))
            g->banner = clampi(number(r, "banner", 0), 0, 7);
        if (has(r, "open"))
            g->open = cJSON_IsTrue(cJSON_GetObjectItem(r, "open"));
        if (strcmp(old_desc, g->desc))
            audit_add(d, gid, uid, AUD_DESC);
        if (strcmp(old_welcome, g->welcome))
            audit_add(d, gid, uid, AUD_WELCOME);
        if (pics > 0)
            audit_add(d, gid, uid, !g->pic[0] ? AUD_ICON_RESET : g->anim[0] ? AUD_ICON_ANIM : AUD_ICON_PIC);
        if (old_icon != g->icon)
            audit_add(d, gid, uid, AUD_ICON);
        if (old_color != g->color)
            audit_add(d, gid, uid, AUD_COLOR);
        if (old_banner != g->banner)
            audit_add(d, gid, uid, AUD_BANNER);
        if (old_open != g->open)
            audit_add(d, gid, uid, AUD_OPEN);
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "audit")) { /* 1.2: who changed the server's appearance, newest first */
        if (!(perms & P_MANAGE_SERVER))
            return fail(E_NO_PERMISSION);
        cJSON *v = ok(), *list = cJSON_AddArrayToObject(v, "audit");
        int shown = 0;
        for (uint32_t want = d->next_audit; want > 0 && shown < 40; want--)
            for (int i = 0; i < ORBIT_AUDIT; i++)
                if (d->audit[i].used && d->audit[i].id == want && d->audit[i].group == gid) {
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddNumberToObject(x, "id", want);
                    cJSON_AddNumberToObject(x, "what", d->audit[i].what);
                    cJSON_AddNumberToObject(x, "time", d->audit[i].time);
                    cJSON_AddStringToObject(x, "user", d->user[d->audit[i].user].name);
                    cJSON_AddItemToArray(list, x);
                    shown++;
                }
        return v;
    }
    if (!strncmp(op, "category_", 9)) {
        if (!(perms & P_MANAGE_CHANNELS))
            return fail(E_NO_PERMISSION);
        if (!strcmp(op, "category_add")) {
            const char *name = orbit_string(r, "name");
            if (!orbit_text_valid(name, 1, 32))
                return fail(E_CHANNEL_NAME);
            int c = add_category(g, name);
            if (c < 0)
                return fail(E_CATEGORIES_FULL);
            *changed = 1;
            cJSON *v = ok();
            cJSON_AddNumberToObject(v, "cat", c);
            return v;
        }
        int c = number(r, "cat", -1);
        if (c < 0 || c >= ORBIT_CATEGORIES || !g->cat[c].used)
            return fail(E_NO_CHANNEL);
        if (!strcmp(op, "category_remove")) {
            for (int i = 0; i < ORBIT_CHANNELS; i++)
                if (g->ch[i].category == c)
                    g->ch[i].category = -1;
            memset(&g->cat[c], 0, sizeof(g->cat[c]));
        } else if (!strcmp(op, "category_update")) {
            char name[33];
            int k = opt_text(name, sizeof(name), r, "name", 32);
            if (k < 0 || (k == 1 && !name[0]))
                return fail(E_CHANNEL_NAME);
            if (k == 1)
                orbit_copy(g->cat[c].name, sizeof(g->cat[c].name), name);
            int mv = number(r, "move", 0);
            if (mv)
                move_category(g, c, mv < 0 ? -1 : 1);
        } else
            return fail(E_UNKNOWN);
        *changed = 1;
        return ok();
    }
    if (!strncmp(op, "channel_", 8)) {
        int type = number(r, "type", CH_TEXT);
        int ch = number(r, "ch", -1);
        if (!strcmp(op, "channel_add")) {
            if (!(perms & P_MANAGE_CHANNELS) && !(type == CH_VOICE && (perms & P_MANAGE_VOICE)))
                return fail(E_NO_PERMISSION);
            const char *name = orbit_string(r, "name");
            int cat = number(r, "cat", -1);
            if (!orbit_text_valid(name, 1, 32))
                return fail(E_CHANNEL_NAME);
            if (cat >= ORBIT_CATEGORIES || (cat >= 0 && !g->cat[cat].used))
                cat = -1;
            int c = add_channel(g, name, type, cat);
            if (c < 0)
                return fail(E_CHANNELS_FULL);
            *changed = 1;
            cJSON *v = ok();
            cJSON_AddNumberToObject(v, "ch", c);
            return v;
        }
        if (ch < 0 || ch >= ORBIT_CHANNELS || !g->ch[ch].used)
            return fail(E_NO_CHANNEL);
        OrbitChannel *h = &g->ch[ch];
        if (!(perms & P_MANAGE_CHANNELS) && !(h->type == CH_VOICE && (perms & P_MANAGE_VOICE)))
            return fail(E_NO_PERMISSION);
        if (!strcmp(op, "channel_remove")) {
            int texts = 0;
            for (int i = 0; i < ORBIT_CHANNELS; i++)
                texts += g->ch[i].used && g->ch[i].type == CH_TEXT;
            if (h->type == CH_TEXT && texts <= 1)
                return fail(E_LAST_CHANNEL);
            for (int i = 0; i < ORBIT_MESSAGES; i++)
                if (d->message[i].id && d->message[i].group == gid && d->message[i].channel == ch)
                    memset(&d->message[i], 0, sizeof(d->message[i]));
            memset(h, 0, sizeof(*h));
        } else if (!strcmp(op, "channel_update")) {
            char name[33];
            int k = opt_text(name, sizeof(name), r, "name", 32);
            if (k < 0 || (k == 1 && !name[0]))
                return fail(E_CHANNEL_NAME);
            if (k == 1)
                orbit_copy(h->name, sizeof(h->name), name);
            if (opt_text(h->topic, sizeof(h->topic), r, "topic", 96) < 0)
                return fail(E_TEXT_LEN);
            if (has(r, "cat")) {
                int cat = number(r, "cat", -1);
                h->category = cat >= 0 && cat < ORBIT_CATEGORIES && g->cat[cat].used ? cat : -1;
            }
            if (has(r, "view"))
                h->view = (uint8_t)number(r, "view", 0);
            if (has(r, "send"))
                h->send = (uint8_t)number(r, "send", 0);
            int mv = number(r, "move", 0);
            if (mv)
                move_channel(g, ch, mv < 0 ? -1 : 1);
        } else
            return fail(E_UNKNOWN);
        *changed = 1;
        return ok();
    }
    if (!strncmp(op, "role_", 5)) {
        if (!(perms & P_MANAGE_ROLES))
            return fail(E_NO_PERMISSION);
        int ri = number(r, "role", -1);
        int prio = number(r, "prio", -1);
        cJSON *pv = cJSON_GetObjectItem(r, "perms"), *cv = cJSON_GetObjectItem(r, "color");
        uint32_t newperms = cJSON_IsNumber(pv) ? (uint32_t)pv->valuedouble & P_ALL : 0;
        /* nobody hands out rights they do not have, or ranks at or above their own */
        if ((cJSON_IsNumber(pv) && (newperms & ~perms)) || (prio >= 0 && prio >= mytop))
            return fail(E_ROLE_RANK);
        if (!strcmp(op, "role_add")) {
            const char *name = orbit_string(r, "name");
            if (!orbit_text_valid(name, 1, 24))
                return fail(E_CHANNEL_NAME);
            int slot = -1;
            for (int i = 0; i < ORBIT_ROLES && slot < 0; i++)
                if (!g->role[i].used)
                    slot = i;
            if (slot < 0)
                return fail(E_ROLES_FULL);
            OrbitRole *ro = &g->role[slot];
            memset(ro, 0, sizeof(*ro));
            ro->used = 1;
            ro->priority = prio >= 0 ? clampi(prio, 0, 99) : 20;
            if (ro->priority >= mytop)
                ro->priority = mytop - 1;
            ro->perms = cJSON_IsNumber(pv) ? newperms : (P_SEND | P_INVITE);
            ro->color = cJSON_IsNumber(cv) ? (uint32_t)cv->valuedouble & 0xffffff : 0x4f8cff;
            orbit_copy(ro->name, sizeof(ro->name), name);
            for (int u = 0; u < ORBIT_USERS; u++)
                g->roles[u] &= (uint8_t)~(1 << slot);
            *changed = 1;
            cJSON *v = ok();
            cJSON_AddNumberToObject(v, "role", slot);
            return v;
        }
        if (ri < 0 || ri >= ORBIT_ROLES || !g->role[ri].used)
            return fail(E_NO_CHANNEL);
        OrbitRole *ro = &g->role[ri];
        if (ro->priority >= mytop)
            return fail(E_ROLE_RANK);
        if (!strcmp(op, "role_remove")) {
            memset(ro, 0, sizeof(*ro));
            for (int u = 0; u < ORBIT_USERS; u++) {
                g->roles[u] &= (uint8_t)~(1 << ri);
                if (!g->roles[u] && u != g->owner)
                    g->roles[u] = 1 << 3;
            }
            for (int i = 0; i < ORBIT_CHANNELS; i++) {
                g->ch[i].view &= (uint8_t)~(1 << ri);
                g->ch[i].send &= (uint8_t)~(1 << ri);
            }
        } else if (!strcmp(op, "role_update")) {
            char name[25];
            int k = opt_text(name, sizeof(name), r, "name", 24);
            if (k < 0 || (k == 1 && !name[0]))
                return fail(E_CHANNEL_NAME);
            if (k == 1)
                orbit_copy(ro->name, sizeof(ro->name), name);
            if (cJSON_IsNumber(pv))
                ro->perms = newperms;
            if (cJSON_IsNumber(cv))
                ro->color = (uint32_t)cv->valuedouble & 0xffffff;
            if (prio >= 0)
                ro->priority = clampi(prio, 0, 99);
        } else
            return fail(E_UNKNOWN);
        *changed = 1;
        return ok();
    }
    /* ---------- members ---------- */
    int target = number(r, "user", -1);
    if (!strcmp(op, "add_member")) {
        if (g->kind != GK_SERVER || !(perms & P_INVITE))
            return fail(E_NO_PERMISSION);
        if (target < 0 || target >= d->users)
            return fail(E_NOT_FRIEND);
        if (is_banned(g, target))
            return fail(E_BANNED);
        if (g->members >> target & 1)
            return fail(E_ALREADY_MEMBER);
        g->members |= UINT32_C(1) << target;
        g->roles[target] = 1 << 3;
        *changed = 1;
        return ok();
    }
    if (target < 0 || target >= d->users || target == uid)
        return fail(E_BAD_REQUEST);
    uint32_t tbit = UINT32_C(1) << target;
    int ttop = (g->members & tbit) ? top_priority(g, target) : 0;
    if (target == g->owner || ttop >= mytop)
        return fail(E_ROLE_RANK);
    if (!strcmp(op, "member_roles")) {
        if (!(perms & P_MANAGE_ROLES) || !(g->members & tbit))
            return fail(E_NO_PERMISSION);
        int want = number(r, "roles", -1);
        if (want < 0 || want > 255)
            return fail(E_BAD_REQUEST);
        uint8_t now = g->roles[target], next = now;
        for (int i = 0; i < ORBIT_ROLES; i++) {
            if (((want ^ now) >> i & 1) == 0)
                continue;
            if (!g->role[i].used || g->role[i].priority >= mytop)
                return fail(E_ROLE_RANK);
            next ^= (uint8_t)(1 << i);
        }
        g->roles[target] = next;
        *changed = 1;
        return ok();
    }
    if (!strcmp(op, "kick")) {
        if (!(perms & P_KICK) || g->kind != GK_SERVER || !(g->members & tbit))
            return fail(E_NO_PERMISSION);
        remove_member(s, g, gid, target);
    } else if (!strcmp(op, "ban")) {
        if (!(perms & P_BAN) || g->kind != GK_SERVER)
            return fail(E_NO_PERMISSION);
        remove_member(s, g, gid, target);
        g->banned |= tbit;
        int days = clampi(number(r, "days", 0), 0, 365); /* 0 = until lifted */
        g->ban_until[target] = days ? orbit_unix_time() + (uint32_t)days * 86400u : 0;
    } else if (!strcmp(op, "unban")) {
        if (!(perms & P_BAN))
            return fail(E_NO_PERMISSION);
        g->banned &= ~tbit;
        g->ban_until[target] = 0;
    } else if (!strcmp(op, "server_mute")) {
        if (!(perms & P_MUTE) || !(g->members & tbit))
            return fail(E_NO_PERMISSION);
        if (cJSON_IsTrue(cJSON_GetObjectItem(r, "mute")))
            g->muted |= tbit;
        else
            g->muted &= ~tbit;
    } else
        return fail(E_UNKNOWN);
    *changed = 1;
    return ok();
}
int orbit_store_code_for_tag(OrbitStore *s, const char tag[17], char code[9]) {
    int found = -1;
    pthread_mutex_lock(&s->mutex);
    for (int i = 0; i < ORBIT_GROUPS && found < 0; i++) {
        const OrbitGroup *g = &s->data.group[i];
        char t[17];
        if (!g->used || strlen(g->invite) != 8)
            continue;
        orbit_group_tag(t, g->invite);
        if (!strcmp(t, tag)) {
            memcpy(code, g->invite, 9);
            found = 0;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return found;
}
cJSON *orbit_request(OrbitStore *s, OrbitSession *session, const cJSON *req) {
    if (!cJSON_IsObject(req))
        return fail(E_BAD_REQUEST);
    pthread_mutex_lock(&s->mutex);
    if (host_lang != s->lang) /* written only on a change: one store per console, no data race */
        host_lang = s->lang;
    static const char *const reads[] = {"hello", "groups", "history", "members", "friends",
                                        "browse", "search", "invite", "invitations", "reports", "pic", "audit", "blob_put"};
    const char *op = orbit_string(req, "op");
    for (size_t i = 0; i < sizeof(reads) / sizeof(reads[0]); i++)
        if (!strcmp(op, reads[i])) {
            int changed = 0;
            cJSON *r = dispatch(s, session, req, &changed);
            pthread_mutex_unlock(&s->mutex);
            return r; /* never changes the stored data */
        }
    OrbitData *before = malloc(sizeof(*before));
    if (!before) {
        pthread_mutex_unlock(&s->mutex);
        return fail(E_MEMORY);
    }
    memcpy(before, &s->data, sizeof(*before));
    OrbitSession prior = *session;
    int changed = 0;
    cJSON *r = dispatch(s, session, req, &changed);
    if (changed && save(s)) {
        memcpy(&s->data, before, sizeof(*before));
        *session = prior;
        cJSON_Delete(r);
        r = fail(E_SAVE_FAILED);
    }
    crypto_wipe(before, sizeof(before->user));
    free(before);
    pthread_mutex_unlock(&s->mutex);
    return r;
}

/* ---------- 1.1 background notifications (watch.h) ---------- */
int orbit_watch_user(OrbitStore *s, const uint8_t id[8], uint8_t secret[32]) {
    int found = -1;
    pthread_mutex_lock(&s->mutex);
    for (int u = 0; u < s->data.users && found < 0; u++) {
        uint8_t mine[8];
        if (!s->data.user[u].has_watch)
            continue;
        orbit_watch_id(mine, s->data.user[u].watch);
        if (!memcmp(mine, id, 8)) {
            memcpy(secret, s->data.user[u].watch, 32);
            found = u;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return found;
}
cJSON *orbit_store_public(OrbitStore *s) {
    cJSON *a = cJSON_CreateArray();
    pthread_mutex_lock(&s->mutex);
    OrbitData *d = &s->data;
    for (int i = 0; i < ORBIT_GROUPS; i++) {
        const OrbitGroup *g = &d->group[i];
        if (!g->used || g->kind != GK_SERVER || !g->open || !g->invite[0] || g->owner < 0 || g->owner >= d->users)
            continue;
        int count = 0, online = 0;
        for (int j = 0; j < d->users; j++)
            if ((g->members >> j) & 1) {
                count++;
                online += shown_status(s, j) != ST_OFFLINE;
            }
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "id", i);
        cJSON_AddNumberToObject(x, "plat", ORBIT_PLAT); /* 1.3 */
        cJSON_AddStringToObject(x, "name", g->name);
        cJSON_AddStringToObject(x, "desc", g->desc);
        cJSON_AddStringToObject(x, "host", d->user[g->owner].name);
        cJSON_AddNumberToObject(x, "members", count);
        cJSON_AddNumberToObject(x, "online", online);
        cJSON_AddNumberToObject(x, "icon", g->icon);
        cJSON_AddNumberToObject(x, "color", g->color % 12);
        cJSON_AddStringToObject(x, "code", g->invite);
        cJSON_AddItemToArray(a, x);
    }
    pthread_mutex_unlock(&s->mutex);
    return a;
}
void orbit_presence_set(OrbitStore *s, int uid, int playing, const char *id, const char *name) {
    if (uid < 0 || uid >= ORBIT_USERS)
        return;
    pthread_mutex_lock(&s->mutex);
    s->playing[uid].until = playing ? orbit_unix_time() + PRESENCE_TTL : 0;
    orbit_copy(s->playing[uid].id, sizeof(s->playing[uid].id), playing ? id : "");
    orbit_copy(s->playing[uid].name, sizeof(s->playing[uid].name), playing ? name : "");
    pthread_mutex_unlock(&s->mutex);
}
uint32_t orbit_watch_start(OrbitStore *s) {
    pthread_mutex_lock(&s->mutex);
    uint32_t next = s->data.next_id;
    pthread_mutex_unlock(&s->mutex);
    return next;
}
static size_t put_field(uint8_t *out, size_t at, size_t cap, const char *text, size_t max) {
    size_t len = watch_utf8_cut(text, strlen(text), max);
    if (at + len + 1 > cap)
        len = at + 1 < cap ? watch_utf8_cut(text, len, cap - at - 1) : 0;
    memcpy(out + at, text, len);
    at += len;
    if (at < cap)
        out[at++] = 0x1f;
    return at;
}
int orbit_watch_next(OrbitStore *s, int uid, const uint8_t id[8], uint32_t *since, uint8_t *out,
                     size_t cap) {
    int result = 0;
    pthread_mutex_lock(&s->mutex);
    OrbitData *d = &s->data;
    uint8_t mine[8];
    if (uid >= d->users || !d->user[uid].has_watch) {
        pthread_mutex_unlock(&s->mutex);
        return -1;
    }
    orbit_watch_id(mine, d->user[uid].watch);
    if (memcmp(mine, id, 8)) {
        pthread_mutex_unlock(&s->mutex);
        return -1; /* secret renewed: this plugin is locked out */
    }
    if (d->next_id - *since > ORBIT_MESSAGES)
        *since = d->next_id - ORBIT_MESSAGES;
    while (*since < d->next_id && !result) {
        const OrbitMessage *m = &d->message[*since % ORBIT_MESSAGES];
        uint32_t want = (*since)++;
        if (m->id != want || m->user == uid || (m->flags & MF_DELETED) || m->group < 0 ||
            m->group >= ORBIT_GROUPS || m->user < 0 || m->user >= d->users)
            continue;
        const OrbitGroup *g = &d->group[m->group];
        uint32_t bit = 1u << uid;
        if (!g->used || g->kind == GK_HOME || !(g->members & bit) || (g->banned & bit) ||
            m->channel < 0 || m->channel >= ORBIT_CHANNELS || !g->ch[m->channel].used ||
            (g->kind != GK_DM && !orbit_can_view(d, m->group, m->channel, uid)))
            continue;
        char num[24];
        size_t at = 0;
        out[at++] = 'M';
        at = put_field(out, at, cap, g->kind == GK_DM ? "d" : "s", 1);
        snprintf(num, sizeof(num), "%d", m->group);
        at = put_field(out, at, cap, num, 8);
        snprintf(num, sizeof(num), "%d", m->channel);
        at = put_field(out, at, cap, num, 8);
        at = put_field(out, at, cap, d->user[m->user].name, 48);
        at = put_field(out, at, cap, g->name, 64);
        at = put_field(out, at, cap, g->ch[m->channel].name, 32);
        at = put_field(out, at, cap, m->text, WATCH_TEXT);
        result = (int)(at - 1); /* without the trailing separator */
    }
    pthread_mutex_unlock(&s->mutex);
    return result;
}
