/* Orbit 1.0 application core.
 * Every console runs its own meeting point in the background. Servers and private chats you
 * create live on your PS3; the ones you were invited to live on the inviting PS3. The app keeps
 * one encrypted link per meeting point (also to every friend's PS3), signs in automatically with
 * a per-console secret plus a signed public identity, and saves everything. */
#include "monocypher.h"
#include "ui.h"
#include "voice.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define LOCK(a) pthread_mutex_lock(&(a)->lock)
#define UNLOCK(a) pthread_mutex_unlock(&(a)->lock)
static void notice_locked(OrbitApp *a, const char *text) {
    snprintf(a->status, sizeof(a->status), "%s", text);
    a->status_id++;
    a->changed++;
}
void app_notice(OrbitApp *a, const char *text) {
    LOCK(a);
    notice_locked(a, text);
    UNLOCK(a);
}
void app_notice_key(OrbitApp *a, int key) {
    app_notice(a, T(key));
}
static int success(const cJSON *r) {
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
}
/* host errors: shown in this console's language when the host sent a known key */
static const char *error_text(const cJSON *r) {
    int key = i18n_find(orbit_string(r, "ekey"));
    if (key >= 0)
        return T(key);
    const char *e = orbit_string(r, "error");
    return e[0] ? e : T(T_ERR_GENERIC);
}
static int num(const cJSON *o, const char *key, int fallback) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}
static uint32_t unum(const cJSON *o, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 0;
}
static void copy(char *dst, size_t cap, const cJSON *o, const char *key) {
    snprintf(dst, cap, "%s", orbit_string(o, key));
}
static cJSON *command(const char *op) {
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "op", op);
    return q;
}
cJSON *app_cmd(const char *op, int group) {
    cJSON *q = command(op);
    if (group >= 0)
        cJSON_AddNumberToObject(q, "group", group);
    return q;
}
static void wipe_token(cJSON *q) {
    cJSON *t = cJSON_GetObjectItemCaseSensitive(q, "token");
    if (cJSON_IsString(t))
        crypto_wipe(t->valuestring, strlen(t->valuestring));
}
static UiGroup *find_group(AppLink *L, int gid) {
    for (int i = 0; i < L->group_count; i++)
        if (L->groups[i].id == gid)
            return &L->groups[i];
    return NULL;
}
static const UiChannel *find_channel(const UiGroup *g, int ch) {
    for (int i = 0; g && i < g->nch; i++)
        if (g->ch[i].id == ch)
            return &g->ch[i];
    return NULL;
}
static int first_channel(const UiGroup *g, int type) {
    int best = -1, pos = 0;
    for (int i = 0; g && i < g->nch; i++)
        if (g->ch[i].type == type && (best < 0 || g->ch[i].pos < pos))
            best = g->ch[i].id, pos = g->ch[i].pos;
    return best;
}
static int contains_mention(const char *text, const char *name) {
    size_t n = strlen(name);
    for (const char *p = strchr(text, '@'); p && n; p = strchr(p + 1, '@')) {
        size_t k = 0;
        while (k < n && p[1 + k] && tolower((unsigned char)p[1 + k]) == tolower((unsigned char)name[k]))
            k++;
        if (k == n)
            return 1;
    }
    return 0;
}
/* ---------- notifications ---------- */
static void add_note(OrbitApp *a, int type, int link, int group, int channel, const char *text) {
    if (!(a->set.notify & (1 << type)))
        return;
    if (a->note_count == UI_NOTES) {
        memmove(a->notes, a->notes + 1, sizeof(UiNote) * (UI_NOTES - 1));
        a->note_count--;
    }
    UiNote *n = &a->notes[a->note_count++];
    n->type = type;
    n->link = link;
    n->group = group;
    n->channel = channel;
    n->time = orbit_unix_time();
    snprintf(n->text, sizeof(n->text), "%s", text);
    notice_locked(a, text);
}
/* ---------- persistence: data/profile.json (atomic, with backup) ---------- */
static int write_atomic(const char *path, const char *text) {
    char tmp[540], bak[540];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    snprintf(bak, sizeof(bak), "%s.bak", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    if (fflush(f) || fsync(fileno(f)))
        ok = 0;
    if (fclose(f))
        ok = 0;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    /* PS3 rename does not replace an existing file: keep the last copy as backup. */
    if (remove(bak) && errno != ENOENT)
        return -1;
    int moved = rename(path, bak) == 0;
    if (rename(tmp, path)) {
        if (moved)
            rename(bak, path);
        return -1;
    }
    return 0;
}
static char *read_file(const char *path, long limit) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *text = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        rewind(f);
        if (n > 0 && n < limit && (text = calloc(1, (size_t)n + 1)) &&
            fread(text, 1, (size_t)n, f) != (size_t)n) {
            free(text);
            text = NULL;
        }
    }
    fclose(f);
    return text;
}
static const char *link_server(const OrbitApp *a, const AppLink *L) {
    return L->own ? a->server_id : L->server;
}
static void save_profile_locked(OrbitApp *a) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 10);
    cJSON_AddStringToObject(root, "name", a->name);
    if (a->relay_user)
        cJSON_AddStringToObject(root, "relay", a->relay);
    cJSON *me = cJSON_AddObjectToObject(root, "me");
    cJSON_AddNumberToObject(me, "status", a->me.status);
    cJSON_AddNumberToObject(me, "color", a->me.color);
    cJSON_AddNumberToObject(me, "avatar", a->me.avatar);
    cJSON_AddNumberToObject(me, "created", a->me.created);
    cJSON_AddStringToObject(me, "bio", a->me.bio);
    cJSON_AddStringToObject(me, "game", a->me.game);
    cJSON_AddStringToObject(me, "custom", a->me.custom);
    cJSON *st = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(st, "lang_auto", a->set.lang_auto);
    cJSON_AddNumberToObject(st, "lang", a->set.lang);
    cJSON_AddNumberToObject(st, "theme", a->set.theme);
    cJSON_AddNumberToObject(st, "notify", a->set.notify);
    cJSON_AddNumberToObject(st, "ds_audio", a->set.ds_audio);
    cJSON_AddNumberToObject(st, "ds_mic", a->set.ds_mic);
    const AppLink *sel = a->selected_link >= 0 && a->selected_link < APP_LINKS
                             ? &a->links[a->selected_link]
                             : NULL;
    if (sel && sel->used && link_server(a, sel)[0]) {
        cJSON *s = cJSON_AddObjectToObject(root, "selected");
        cJSON_AddStringToObject(s, "server", link_server(a, sel));
        cJSON_AddNumberToObject(s, "group", a->selected_group);
        cJSON_AddNumberToObject(s, "channel", a->selected_channel);
    }
    cJSON *rooms = cJSON_AddArrayToObject(root, "rooms"), *seen = cJSON_AddArrayToObject(root, "seen"),
          *muted = cJSON_AddArrayToObject(root, "muted");
    for (int i = 0; i < APP_LINKS; i++) {
        const AppLink *L = &a->links[i];
        if (!L->used || L->remove)
            continue;
        const char *server = link_server(a, L);
        if (server[0])
            for (int g = 0; g < L->group_count; g++) {
                int gid = L->groups[g].id;
                if (a->chat_muted[i][gid]) {
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddStringToObject(x, "server", server);
                    cJSON_AddNumberToObject(x, "group", gid);
                    cJSON_AddItemToArray(muted, x);
                }
                for (int c = 0; c < ORBIT_CHANNELS; c++) {
                    uint32_t v = a->seen[i][gid][c];
                    if (!v)
                        continue;
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddStringToObject(x, "server", server);
                    cJSON_AddNumberToObject(x, "group", gid);
                    cJSON_AddNumberToObject(x, "ch", c);
                    cJSON_AddNumberToObject(x, "id", v);
                    cJSON_AddItemToArray(seen, x);
                }
            }
        if (L->own)
            continue;
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "host", L->host);
        cJSON_AddNumberToObject(r, "port", L->port);
        cJSON_AddStringToObject(r, "code", L->code);
        cJSON_AddNumberToObject(r, "code_group", L->code_group);
        cJSON_AddStringToObject(r, "server", L->server);
        cJSON_AddStringToObject(r, "join", L->join_code);
        cJSON_AddStringToObject(r, "relay", L->relay);
        cJSON_AddNumberToObject(r, "friend", L->friend_link);
        cJSON_AddStringToObject(r, "owner_pub", L->owner_pub);
        cJSON_AddStringToObject(r, "owner_name", L->owner_name);
        cJSON *gs = cJSON_AddArrayToObject(r, "groups");
        for (int g = 0; g < L->group_count; g++) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "id", L->groups[g].id);
            cJSON_AddNumberToObject(x, "kind", L->groups[g].kind);
            cJSON_AddNumberToObject(x, "icon", L->groups[g].icon);
            cJSON_AddNumberToObject(x, "color", L->groups[g].color);
            cJSON_AddStringToObject(x, "name", L->groups[g].name);
            cJSON_AddStringToObject(x, "host", L->groups[g].host);
            cJSON_AddStringToObject(x, "people", L->groups[g].people);
            cJSON_AddItemToArray(gs, x);
        }
        cJSON_AddItemToArray(rooms, r);
    }
    cJSON *out = cJSON_AddArrayToObject(root, "outbox");
    for (int i = 0; i < a->outbox_count; i++) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "server", a->outbox_server[i]);
        cJSON_AddNumberToObject(x, "group", a->outbox[i].group);
        cJSON_AddNumberToObject(x, "ch", a->outbox[i].channel);
        cJSON_AddNumberToObject(x, "time", a->outbox[i].time);
        cJSON_AddStringToObject(x, "text", a->outbox[i].text);
        cJSON_AddItemToArray(out, x);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    char path[512];
    snprintf(path, sizeof(path), "%s/profile.json", a->data_dir);
    if (!text || write_atomic(path, text))
        notice_locked(a, T(T_SAVE_FAILED));
    else
        a->dirty = 0;
    free(text);
}
static void load_profile(OrbitApp *a) {
    char path[512];
    snprintf(path, sizeof(path), "%s/profile.json", a->data_dir);
    char *text = read_file(path, 4 << 20);
    if (!text) {
        char bak[520];
        snprintf(bak, sizeof(bak), "%s.bak", path);
        text = read_file(bak, 4 << 20);
    }
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    if (!root)
        return;
    const char *name = orbit_string(root, "name");
    if (orbit_text_valid(name, 1, 48)) {
        snprintf(a->name, sizeof(a->name), "%s", name);
        a->profile = 1;
    }
    cJSON *relay = cJSON_GetObjectItem(root, "relay");
    if (cJSON_IsString(relay) && strlen(relay->valuestring) < sizeof(a->relay)) {
        snprintf(a->relay, sizeof(a->relay), "%s", relay->valuestring);
        a->relay_user = 1;
    }
    cJSON *me = cJSON_GetObjectItem(root, "me");
    if (me) {
        a->me.status = num(me, "status", 0);
        if (a->me.status < ST_ONLINE || a->me.status > ST_INVISIBLE)
            a->me.status = ST_ONLINE;
        a->me.color = num(me, "color", 0) % 12;
        a->me.avatar = num(me, "avatar", 0) % 16;
        a->me.created = unum(me, "created");
        if (orbit_text_valid(orbit_string(me, "bio"), 1, 96))
            copy(a->me.bio, sizeof(a->me.bio), me, "bio");
        if (orbit_text_valid(orbit_string(me, "game"), 1, 48))
            copy(a->me.game, sizeof(a->me.game), me, "game");
        if (orbit_text_valid(orbit_string(me, "custom"), 1, 64))
            copy(a->me.custom, sizeof(a->me.custom), me, "custom");
    }
    cJSON *st = cJSON_GetObjectItem(root, "settings");
    if (st) {
        a->set.lang_auto = num(st, "lang_auto", 1) != 0;
        a->set.lang = num(st, "lang", LANG_EN);
        if (a->set.lang < 0 || a->set.lang >= LANG_COUNT)
            a->set.lang = LANG_EN;
        a->set.theme = num(st, "theme", 0);
        if (a->set.theme < 0 || a->set.theme >= THEME_COUNT)
            a->set.theme = 0;
        a->set.notify = num(st, "notify", (1 << N_COUNT) - 1);
        /* 1.0.3: the own DualSense driver is off for good. Taking the controller over made the
         * PS3 lose it as a USB headset - the microphone then delivered only noise. */
        a->set.ds_audio = a->set.ds_mic = 0;
    }
    cJSON *r;
    int slot = 1;
    cJSON_ArrayForEach(r, cJSON_GetObjectItem(root, "rooms")) {
        if (slot >= APP_LINKS)
            break;
        AppLink *L = &a->links[slot];
        const char *host = orbit_string(r, "host"), *code = orbit_string(r, "code"),
                   *server = orbit_string(r, "server"), *join = orbit_string(r, "join");
        int port = num(r, "port", 0);
        char norm[33];
        struct in_addr probe;
        if (inet_pton(AF_INET, host, &probe) != 1 || port < 1 || port > 65535 ||
            orbit_group_code(norm, code) || strlen(norm) != 8 || strlen(server) > 16)
            continue;
        memset(L, 0, sizeof(*L));
        L->owner = a;
        L->used = 1;
        L->port = port;
        L->code_group = num(r, "code_group", -1);
        L->friend_link = num(r, "friend", 0) == 1;
        snprintf(L->host, sizeof(L->host), "%s", host);
        snprintf(L->code, sizeof(L->code), "%s", norm);
        snprintf(L->server, sizeof(L->server), "%s", server);
        if (strlen(orbit_string(r, "owner_pub")) == 64)
            copy(L->owner_pub, sizeof(L->owner_pub), r, "owner_pub");
        copy(L->owner_name, sizeof(L->owner_name), r, "owner_name");
        if (!orbit_group_code(norm, join) && strlen(norm) == 8)
            snprintf(L->join_code, sizeof(L->join_code), "%s", norm);
        if (strlen(orbit_string(r, "relay")) < sizeof(L->relay))
            snprintf(L->relay, sizeof(L->relay), "%s", orbit_string(r, "relay"));
        cJSON *g;
        cJSON_ArrayForEach(g, cJSON_GetObjectItem(r, "groups")) {
            if (L->group_count >= ORBIT_GROUPS)
                break;
            int id = num(g, "id", -1);
            if (id < 0 || id >= ORBIT_GROUPS)
                continue;
            UiGroup *x = &L->groups[L->group_count++];
            x->id = id;
            x->link = slot;
            x->kind = num(g, "kind", GK_SERVER);
            x->icon = num(g, "icon", 0);
            x->color = num(g, "color", 0);
            copy(x->name, sizeof(x->name), g, "name");
            copy(x->host, sizeof(x->host), g, "host");
            copy(x->people, sizeof(x->people), g, "people");
        }
        L->cached = 1;
        L->history_group = -1;
        slot++;
    }
    cJSON *s = cJSON_GetObjectItem(root, "selected");
    const char *selserver = orbit_string(s, "server");
    int selgroup = num(s, "group", -1), selch = num(s, "channel", -1);
    for (int i = 0; i < APP_LINKS; i++) {
        AppLink *L = &a->links[i];
        if (!L->used)
            continue;
        const char *id = link_server(a, L);
        if (selserver[0] && !strcmp(id, selserver)) {
            a->selected_link = i;
            a->selected_group = selgroup;
            a->selected_channel = selch;
        }
        cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "seen")) {
            int g = num(x, "group", -1), c = num(x, "ch", 0);
            uint32_t v = unum(x, "id");
            if (g >= 0 && g < ORBIT_GROUPS && c >= 0 && c < ORBIT_CHANNELS && v &&
                !strcmp(orbit_string(x, "server"), id))
                a->seen[i][g][c] = v;
        }
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "muted")) {
            int g = num(x, "group", -1);
            if (g >= 0 && g < ORBIT_GROUPS && !strcmp(orbit_string(x, "server"), id))
                a->chat_muted[i][g] = 1;
        }
    }
    cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "outbox")) {
        if (a->outbox_count >= UI_OUTBOX)
            break;
        const char *t = orbit_string(x, "text"), *server = orbit_string(x, "server");
        if (!orbit_text_valid(t, 1, 768) || strlen(server) != 16)
            continue;
        UiOutbox *o = &a->outbox[a->outbox_count];
        o->group = num(x, "group", -1);
        o->channel = num(x, "ch", -1);
        o->time = unum(x, "time");
        snprintf(o->text, sizeof(o->text), "%s", t);
        snprintf(a->outbox_server[a->outbox_count], 17, "%s", server);
        a->outbox_count++;
    }
    cJSON_Delete(root);
}
/* ---------- one link per meeting point ---------- */
static void device_token(const OrbitApp *a, const char *server, char out[65]) {
    uint8_t t[32];
    crypto_blake2b_keyed(t, 32, a->device, 32, (const uint8_t *)server, strlen(server));
    orbit_hex(out, t, 32);
    crypto_wipe(t, 32);
}
static void add_profile(cJSON *q, const UiProfile *me) {
    cJSON_AddNumberToObject(q, "status", me->status);
    cJSON_AddNumberToObject(q, "color", me->color);
    cJSON_AddNumberToObject(q, "avatar", me->avatar);
    cJSON_AddNumberToObject(q, "created", me->created);
    cJSON_AddStringToObject(q, "bio", me->bio);
    cJSON_AddStringToObject(q, "game", me->game);
    cJSON_AddStringToObject(q, "custom", me->custom);
}
static int sign_in(OrbitApp *a, AppLink *L, OrbitLink *c, const char *server) {
    char name[49], token[65];
    UiProfile me;
    LOCK(a);
    snprintf(name, sizeof(name), "%s", a->name);
    me = a->me;
    UNLOCK(a);
    device_token(a, server, token);
    if (L->own) {
        /* Migration from 0.6: keep the account (groups, messages) that signed in on this PS3. */
        OrbitRemember old;
        if (!orbit_remember_read(a->data_dir, &old) && old.hosting) {
            cJSON *q = command("resume");
            cJSON_AddStringToObject(q, "email", old.email);
            cJSON_AddStringToObject(q, "token", old.token);
            cJSON *r = orbit_exchange(c, q);
            wipe_token(q);
            cJSON_Delete(q);
            if (r && success(r)) {
                cJSON *b = command("bind_device");
                cJSON_AddStringToObject(b, "token", token);
                cJSON *br = orbit_exchange(c, b);
                wipe_token(b);
                cJSON_Delete(b);
                if (br && success(br))
                    orbit_remember_clear(a->data_dir, &old);
                cJSON_Delete(br);
            }
            cJSON_Delete(r);
        }
        crypto_wipe(&old, sizeof(old));
    }
    /* identity: the public key, signed for exactly this meeting point (no replay elsewhere) */
    uint8_t sig[64];
    char pub[65], sighex[129];
    orbit_bind_sign(sig, a->id_secret, a->id_pub, server);
    orbit_hex(pub, a->id_pub, 32);
    orbit_hex(sighex, sig, 64);
    cJSON *q = command("device");
    cJSON_AddStringToObject(q, "token", token);
    cJSON_AddStringToObject(q, "name", name);
    cJSON_AddStringToObject(q, "pub", pub);
    cJSON_AddStringToObject(q, "sig", sighex);
    add_profile(q, &me);
    cJSON *r = orbit_exchange(c, q);
    wipe_token(q);
    cJSON_Delete(q);
    crypto_wipe(token, sizeof(token));
    int ok = r && success(r);
    LOCK(a);
    if (ok)
        L->user = num(r, "user", -1);
    else if (r)
        snprintf(L->error, sizeof(L->error), "%s", error_text(r));
    UNLOCK(a);
    int rc = ok ? 0 : r ? -1 : -2;
    cJSON_Delete(r);
    return rc;
}
/* Returns 1 on a duplicate or own meeting point: the link is folded into another one. */
static int fold_duplicate(OrbitApp *a, AppLink *L, const char *server) {
    int index = (int)(L - a->links), folded = 0;
    LOCK(a);
    if (!L->own && !strcmp(server, a->server_id)) {
        /* An invitation to my own PS3: I am already in all its groups as the host's member. */
        AppLink *own = &a->links[0];
        if (L->join_code[0])
            snprintf(own->join_code, sizeof(own->join_code), "%s", L->join_code);
        if (L->friend_link)
            notice_locked(a, T(T_FRIEND_SELF));
        folded = 1;
    }
    for (int i = 0; i < APP_LINKS && !folded; i++) {
        AppLink *o = &a->links[i];
        if (i == index || !o->used || o->remove || o->own || strcmp(o->server, server))
            continue;
        if (L->join_code[0])
            snprintf(o->join_code, sizeof(o->join_code), "%s", L->join_code);
        snprintf(o->host, sizeof(o->host), "%s", L->host);
        o->port = L->port;
        o->retry_at = 0;
        if (L->friend_link) {
            o->friend_link = 1;
            o->synced = 0;
        }
        folded = 1;
    }
    if (folded) {
        L->remove = 1;
        a->dirty = 1;
        if (a->selected_link == index)
            a->selected_link = 0;
    }
    UNLOCK(a);
    return folded;
}
static OrbitLink *link_open(OrbitApp *a, AppLink *L, int voice) {
    char reason[200], host[16], code[9], broadcast[16];
    snprintf(reason, sizeof(reason), "%s", T(T_NO_CONNECTION));
    int port;
    LOCK(a);
    snprintf(host, sizeof(host), "%s", L->host);
    snprintf(code, sizeof(code), "%s", L->code);
    snprintf(broadcast, sizeof(broadcast), "%s", a->broadcast);
    port = L->port;
    UNLOCK(a);
    OrbitLink *c = NULL;
    if (L->own)
        c = orbit_connect_detailed("127.0.0.1", a->port, a->server_key, reason, sizeof(reason));
    else {
        /* Same home network first: no router involved, works even without UPnP. */
        char tag[17], lan[16];
        orbit_group_tag(tag, code);
        if (!orbit_discover(tag, a->port, broadcast, lan, 700))
            c = orbit_connect_group(lan, a->port, code, reason, sizeof(reason));
        if (!c && strcmp(host, "0.0.0.0"))
            c = orbit_connect_group(host, port, code, reason, sizeof(reason));
        /* No router settings anywhere: both consoles meet at the relay. */
        char relay[104];
        LOCK(a);
        snprintf(relay, sizeof(relay), "%s", L->relay[0] ? L->relay : a->relay);
        UNLOCK(a);
        if (!c && relay[0]) {
            char why[200];
            int fd = orbit_relay_dial(relay, tag, why, sizeof(why));
            if (fd >= 0)
                c = orbit_wrap_group(fd, code, reason, sizeof(reason));
            else
                snprintf(reason, sizeof(reason), "%s", why);
        }
    }
    if (!c) {
        LOCK(a);
        snprintf(L->error, sizeof(L->error), "%s", reason);
        UNLOCK(a);
        return NULL;
    }
    cJSON *q = command("hello");
    cJSON_AddNumberToObject(q, "protocol", ORBIT_PROTOCOL);
    cJSON *r = orbit_exchange(c, q);
    cJSON_Delete(q);
    char server[17] = {0};
    int proto = 0;
    if (r && success(r) && strlen(orbit_string(r, "server")) == 16) {
        snprintf(server, sizeof(server), "%s", orbit_string(r, "server"));
        proto = num(r, "protocol", 9);
    }
    if (server[0] && !voice) {
        LOCK(a);
        if (strlen(orbit_string(r, "owner_pub")) == 64) {
            copy(L->owner_pub, sizeof(L->owner_pub), r, "owner_pub");
            copy(L->owner_id, sizeof(L->owner_id), r, "owner_id");
            copy(L->owner_name, sizeof(L->owner_name), r, "owner_name");
        }
        UNLOCK(a);
    }
    cJSON_Delete(r);
    if (!server[0] || proto < 9) {
        LOCK(a);
        TF(L->error, sizeof(L->error), T_OLD_VERSION, ORBIT_VERSION);
        UNLOCK(a);
        orbit_disconnect(c);
        return NULL;
    }
    if (!voice && fold_duplicate(a, L, server)) {
        orbit_disconnect(c);
        return NULL;
    }
    if (sign_in(a, L, c, server)) {
        orbit_disconnect(c);
        return NULL;
    }
    LOCK(a);
    if (strcmp(L->server, server)) {
        snprintf(L->server, sizeof(L->server), "%s", server);
        a->dirty = 1;
    }
    if (proto < 10)
        snprintf(L->error, sizeof(L->error), "%s", T(T_HOST_OLD));
    UNLOCK(a);
    return c;
}
static void parse_person(UiPerson *p, const cJSON *x) {
    p->user = num(x, "user", -1);
    p->status = num(x, "status", ST_OFFLINE);
    p->color = num(x, "color", 0);
    p->avatar = num(x, "avatar", 0);
    copy(p->name, sizeof(p->name), x, "name");
    copy(p->oid, sizeof(p->oid), x, "oid");
}
static int is_viewing(const OrbitApp *a, int link, int gid, int ch) {
    return a->selected_link == link && a->selected_group == gid && (ch < 0 || a->selected_channel == ch);
}
static void apply_groups(OrbitApp *a, AppLink *L, const cJSON *r) {
    static UiGroup backup[ORBIT_GROUPS];
    static pthread_mutex_t backup_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&backup_lock);
    LOCK(a);
    int index = (int)(L - a->links), before = L->group_count, first = !a->groups_loaded[index];
    /* only the filled part: these arrays are large and this runs every 0.7 s per link */
    memcpy(backup, L->groups, sizeof(UiGroup) * (size_t)before);
    memset(L->groups, 0, sizeof(UiGroup) * (size_t)before);
    L->group_count = 0;
    const cJSON *g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItem(r, "groups")) {
        if (L->group_count >= ORBIT_GROUPS)
            break;
        int id = num(g, "id", -1);
        if (id < 0 || id >= ORBIT_GROUPS)
            continue;
        UiGroup *x = &L->groups[L->group_count++];
        x->id = id;
        x->link = index;
        x->own = L->own;
        x->members = num(g, "members", 1);
        x->kind = num(g, "kind", GK_SERVER);
        x->icon = num(g, "icon", 0);
        x->color = num(g, "color", 0);
        x->banner = num(g, "banner", 0);
        x->open = cJSON_IsTrue(cJSON_GetObjectItem(g, "open"));
        x->perms = num(g, "perms", cJSON_IsTrue(cJSON_GetObjectItem(g, "owner")) ? P_ALL : P_SEND | P_INVITE);
        x->myroles = num(g, "myroles", 0);
        x->smuted = cJSON_IsTrue(cJSON_GetObjectItem(g, "smuted"));
        x->online = num(g, "online", 0);
        copy(x->name, sizeof(x->name), g, "name");
        copy(x->host, sizeof(x->host), g, "host");
        copy(x->desc, sizeof(x->desc), g, "desc");
        copy(x->welcome, sizeof(x->welcome), g, "welcome");
        const cJSON *c;
        cJSON_ArrayForEach(c, cJSON_GetObjectItem(g, "cats")) {
            if (x->ncat >= ORBIT_CATEGORIES)
                break;
            UiCategory *k = &x->cat[x->ncat++];
            k->i = num(c, "i", 0);
            k->pos = num(c, "pos", 0);
            copy(k->name, sizeof(k->name), c, "name");
        }
        cJSON *chs = cJSON_GetObjectItem(g, "chs");
        if (cJSON_IsArray(chs))
            cJSON_ArrayForEach(c, chs) {
                if (x->nch >= ORBIT_CHANNELS)
                    break;
                UiChannel *h = &x->ch[x->nch++];
                h->id = num(c, "i", 0);
                h->type = num(c, "type", CH_TEXT);
                h->cat = num(c, "cat", -1);
                h->pos = num(c, "pos", 0);
                h->view = num(c, "view", 0);
                h->send = num(c, "send", 0);
                h->call = num(c, "call", 0);
                h->last = unum(c, "last");
                copy(h->name, sizeof(h->name), c, "name");
                copy(h->topic, sizeof(h->topic), c, "topic");
                copy(h->lname, sizeof(h->lname), c, "lname");
                copy(h->ltext, sizeof(h->ltext), c, "ltext");
                /* my own message counts as read */
                if (cJSON_IsTrue(cJSON_GetObjectItem(c, "lmine")) && h->last > a->seen[index][id][h->id]) {
                    a->seen[index][id][h->id] = h->last;
                    a->dirty = 1;
                }
            }
        else {
            /* 0.9 host: one channel with the group's messages */
            UiChannel *h = &x->ch[x->nch++];
            h->id = 0;
            h->type = CH_TEXT;
            h->cat = -1;
            h->last = unum(g, "last");
            snprintf(h->name, sizeof(h->name), "%s", T(T_CH_GENERAL));
        }
        cJSON_ArrayForEach(c, cJSON_GetObjectItem(g, "roles")) {
            if (x->nrole >= ORBIT_ROLES)
                break;
            UiRole *ro = &x->role[x->nrole++];
            ro->i = num(c, "i", 0);
            ro->prio = num(c, "prio", 0);
            ro->color = unum(c, "color");
            ro->perms = unum(c, "perms");
            copy(ro->name, sizeof(ro->name), c, "name");
        }
        cJSON_ArrayForEach(c, cJSON_GetObjectItem(g, "dm")) {
            if (x->ndm >= UI_DM_PEOPLE)
                break;
            parse_person(&x->dm[x->ndm++], c);
        }
        cJSON_ArrayForEach(c, cJSON_GetObjectItem(g, "call")) {
            x->call++;
            if (x->ncall < CALL_PEOPLE) {
                UiCaller *p = &x->caller[x->ncall++];
                p->user = num(c, "user", -1);
                p->ch = num(c, "ch", 0);
                p->muted = cJSON_IsTrue(cJSON_GetObjectItem(c, "muted"));
                copy(p->name, sizeof(p->name), c, "name");
            }
        }
        const cJSON *p;
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(g, "people")) {
            if (!cJSON_IsString(p))
                continue;
            size_t n = strlen(x->people);
            snprintf(x->people + n, sizeof(x->people) - n, "%s%s", n ? ", " : "", p->valuestring);
        }
        if (x->kind == GK_DM && !x->name[0]) {
            /* a private chat is named after the other people in it */
            for (int k = 0; k < x->ndm; k++) {
                size_t n = strlen(x->name);
                snprintf(x->name + n, sizeof(x->name) - n, "%s%s", k ? ", " : "", x->dm[k].name);
            }
            if (!x->name[0])
                snprintf(x->name, sizeof(x->name), "%s", T(T_ONLY_YOU));
        }
        /* voice chat card: who is in my voice channel right now */
        int in_call = a->call_running && a->call_link == index && a->call_group == id;
        if (in_call) {
            int keep[CALL_PEOPLE][3], kept = a->call_count;
            for (int k = 0; k < kept; k++)
                keep[k][0] = a->call_people[k].user, keep[k][1] = a->call_people[k].local_muted,
                keep[k][2] = a->call_people[k].volume;
            a->call_count = 0;
            for (int k = 0; k < x->ncall && a->call_count < CALL_PEOPLE; k++) {
                if (x->kind != GK_DM && x->caller[k].ch != a->call_channel)
                    continue;
                typeof(a->call_people[0]) *cp = &a->call_people[a->call_count++];
                cp->user = x->caller[k].user;
                cp->server_muted = x->caller[k].muted;
                cp->local_muted = 0;
                cp->volume = 100;
                for (int j = 0; j < kept; j++)
                    if (keep[j][0] == cp->user)
                        cp->local_muted = keep[j][1], cp->volume = keep[j][2];
                snprintf(cp->name, sizeof(cp->name), "%s", x->caller[k].name);
            }
            const UiChannel *vc = find_channel(x, a->call_channel);
            if (x->kind == GK_DM || !vc)
                snprintf(a->call_name, sizeof(a->call_name), "%s", x->name);
            else
                snprintf(a->call_name, sizeof(a->call_name), "%s / %s", x->name, vc->name);
        }
        /* notifications: compare with what this link showed before */
        UiGroup *old = NULL;
        for (int k = 0; k < before; k++)
            if (backup[k].id == id)
                old = &backup[k];
        if (first || L->cached)
            continue;
        char line[200];
        if (!old) {
            if (x->kind == GK_SERVER && !is_viewing(a, index, id, -1)) {
                TF(line, sizeof(line), T_N_ADDED, x->name);
                add_note(a, N_INVITE, index, id, -1, line);
            }
            continue;
        }
        if (!old->smuted && x->smuted) {
            TF(line, sizeof(line), T_N_MUTED, x->name);
            add_note(a, N_MODERATION, index, id, -1, line);
        }
        int incall_here = a->call_running && a->call_link == index && a->call_group == id;
        if (x->kind == GK_DM && old->call == 0 && x->call > 0 && !incall_here && x->ncall) {
            TF(line, sizeof(line), T_N_CALL, x->caller[0].name);
            add_note(a, N_CALL, index, id, 0, line);
        }
        for (int k = 0; k < x->nch; k++) {
            const UiChannel *h = &x->ch[k], *oh = find_channel(old, h->id);
            if (!oh || h->last <= oh->last || h->last <= a->seen[index][id][h->id] ||
                is_viewing(a, index, id, h->id) || !h->lname[0])
                continue;
            int mention = contains_mention(h->ltext, a->name);
            if (a->chat_muted[index][id] && !mention)
                continue;
            if (x->kind == GK_DM) {
                TF(line, sizeof(line), T_N_DM, h->lname, h->ltext);
                add_note(a, N_DM, index, id, h->id, line);
            } else {
                TF(line, sizeof(line), mention ? T_N_MENTION : T_N_GROUP, h->lname, x->name, h->ltext);
                add_note(a, mention ? N_MENTION : N_GROUP, index, id, h->id, line);
            }
        }
    }
    if (!first && !L->cached)
        for (int k = 0; k < before; k++) {
            int still = 0;
            for (int j = 0; j < L->group_count; j++)
                still |= L->groups[j].id == backup[k].id;
            if (!still && backup[k].kind == GK_SERVER && !(a->selected_link == index && a->busy)) {
                char line[200];
                TF(line, sizeof(line), T_N_REMOVED, backup[k].name);
                add_note(a, N_MODERATION, index, backup[k].id, -1, line);
            }
        }
    a->groups_loaded[index] = 1;
    /* Redraw only when something visible changed: no needless full-screen renders. */
    int changed = before != L->group_count || L->cached ||
                  memcmp(backup, L->groups, sizeof(UiGroup) * (size_t)before);
    int renamed = before != L->group_count || L->cached;
    for (int i = 0; i < L->group_count && !renamed; i++)
        renamed = strcmp(backup[i].name, L->groups[i].name) != 0;
    L->cached = 0;
    if (renamed)
        a->dirty = 1;
    if (changed)
        a->changed++;
    UNLOCK(a);
    pthread_mutex_unlock(&backup_lock);
}
static void parse_message(UiMessage *u, const cJSON *m, int me) {
    memset(u, 0, sizeof(*u));
    u->id = num(m, "id", 0);
    u->user = num(m, "user", -2);
    u->mine = u->user == me;
    u->time = unum(m, "time");
    u->rev = unum(m, "rev");
    u->flags = num(m, "flags", 0);
    u->reply = num(m, "reply", 0);
    u->color = num(m, "color", 0);
    u->avatar = num(m, "avatar", 0);
    copy(u->name, sizeof(u->name), m, "name");
    copy(u->oid, sizeof(u->oid), m, "oid");
    copy(u->text, sizeof(u->text), m, "text");
    copy(u->rname, sizeof(u->rname), m, "rname");
    copy(u->rtext, sizeof(u->rtext), m, "rtext");
    int k = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(m, "react")) {
        if (k < ORBIT_REACTIONS && cJSON_IsNumber(x))
            u->react[k] = (uint32_t)x->valuedouble;
        k++;
    }
}
static void apply_history(OrbitApp *a, AppLink *L, int gid, int ch, const cJSON *r) {
    LOCK(a);
    int index = (int)(L - a->links);
    if (a->selected_link != index || a->selected_group != gid || a->selected_channel != ch) {
        UNLOCK(a);
        return;
    }
    if (L->history_group != gid || L->history_channel != ch) {
        L->history_group = gid;
        L->history_channel = ch;
        L->message_count = 0;
        L->history_rev = 0;
    }
    const cJSON *m;
    cJSON_ArrayForEach(m, cJSON_GetObjectItem(r, "messages")) {
        int id = num(m, "id", 0);
        if (id <= 0 || (L->message_count && id <= L->messages[L->message_count - 1].id))
            continue;
        if (L->message_count == UI_MESSAGES) {
            memmove(L->messages, L->messages + 1, (UI_MESSAGES - 1) * sizeof(UiMessage));
            L->message_count--;
        }
        UiMessage *u = &L->messages[L->message_count++];
        parse_message(u, m, L->user);
        if (gid >= 0 && ch >= 0 && ch < ORBIT_CHANNELS && (uint32_t)id > a->seen[index][gid][ch]) {
            a->seen[index][gid][ch] = (uint32_t)id;
            a->dirty = 1;
        }
    }
    /* edits, deletions and reactions */
    cJSON_ArrayForEach(m, cJSON_GetObjectItem(r, "changed")) {
        int id = num(m, "id", 0);
        for (int i = 0; i < L->message_count; i++)
            if (L->messages[i].id == id)
                parse_message(&L->messages[i], m, L->user);
    }
    uint32_t mrev = unum(r, "mrev");
    if (mrev > L->history_rev)
        L->history_rev = mrev;
    a->changed++;
    UNLOCK(a);
}
/* ---------- this console's address, for invitations and the friend code ---------- */
static int own_endpoint(OrbitApp *a, uint32_t *ip, int *port, char *scope, size_t cap) {
    struct in_addr in;
    *ip = 0;
    *port = 0;
    if (!a->nat.ok && a->bore_online && a->bore_ip && a->bore_port) {
        *ip = a->bore_ip; /* public tunnel address of this console */
        *port = a->bore_port;
        snprintf(scope, cap, "%s", T(T_SCOPE_TUNNEL));
    } else if (a->relay_ip && !a->nat.ok) {
        *ip = a->relay_ip; /* relay address, flagged in the port field */
        *port = ORBIT_INVITE_RELAY | a->relay_port;
        snprintf(scope, cap, "%s", T(T_SCOPE_RELAY));
    } else if (a->nat.ok && inet_pton(AF_INET, a->nat.external, &in) == 1) {
        *ip = ntohl(in.s_addr);
        *port = a->nat.port;
        snprintf(scope, cap, "%s", T(T_SCOPE_INTERNET));
    } else if (inet_pton(AF_INET, a->ip, &in) == 1) {
        *ip = ntohl(in.s_addr);
        *port = a->port;
        snprintf(scope, cap, "%s %.150s", T(T_SCOPE_HOME), a->nat.reason[0] ? a->nat.reason : "");
    }
    return *port ? 0 : -1;
}
static void update_friend_code_locked(OrbitApp *a) {
    uint32_t ip;
    int port;
    char scope[300];
    if (a->home_code[0] && !own_endpoint(a, &ip, &port, scope, sizeof(scope))) {
        char code[ORBIT_INVITE_TEXT];
        if (!orbit_invite_encode(code, ip, port, a->home_code) && strcmp(code, a->friend_code)) {
            snprintf(a->friend_code, sizeof(a->friend_code), "%s", code);
            a->changed++;
        }
    }
}
/* ---------- background requests (never block the interface) ---------- */
#define BG_JOBS 8
static void bg_queue_locked(OrbitApp *a, int link, cJSON *job) {
    for (int i = 0; i < BG_JOBS; i++)
        if (!a->links[link].bg[i]) {
            a->links[link].bg[i] = job;
            return;
        }
    cJSON_Delete(job);
}
static void bg_queue(OrbitApp *a, int link, cJSON *job) {
    LOCK(a);
    bg_queue_locked(a, link, job);
    UNLOCK(a);
}
static int free_slot_locked(OrbitApp *a) {
    for (int i = 1; i < APP_LINKS; i++)
        if (!a->links[i].used && !a->links[i].running)
            return i;
    return -1;
}
static void start_link_locked(OrbitApp *a, AppLink *L);
/* Opens (or reuses) a link from a 20-character code. friend: keep it even without groups. */
static int link_for_code(OrbitApp *a, const char *text, int friend_link, int join) {
    uint32_t ip;
    int port;
    char code[9];
    if (orbit_invite_decode(text, &ip, &port, code))
        return -2;
    char host[16], relay[24] = "";
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    if (port & ORBIT_INVITE_RELAY) {
        snprintf(relay, sizeof(relay), "%s:%d", host, port & 0xffff);
        snprintf(host, sizeof(host), "0.0.0.0");
        port = 1;
        ip = 0;
        LOCK(a);
        int adopt = !a->relay[0];
        UNLOCK(a);
        if (adopt)
            app_set_relay(a, relay, 1); /* now this console can invite through it, too */
    }
    char tag[17], local[9];
    orbit_group_tag(tag, code);
    if (a->store && !orbit_store_code_for_tag(a->store, tag, local)) {
        if (friend_link)
            return -3; /* my own friend code */
        LOCK(a);
        snprintf(a->links[0].join_code, sizeof(a->links[0].join_code), "%s", code);
        UNLOCK(a);
        return 0;
    }
    LOCK(a);
    for (int i = 1; i < APP_LINKS; i++) {
        AppLink *L = &a->links[i];
        if (L->used && !L->remove && ip && !strcmp(L->host, host) && L->port == port) {
            if (join)
                snprintf(L->join_code, sizeof(L->join_code), "%s", code);
            if (friend_link)
                L->friend_link = 1, L->synced = 0;
            L->retry_at = 0;
            UNLOCK(a);
            return i;
        }
    }
    int slot = free_slot_locked(a);
    if (slot < 0) {
        UNLOCK(a);
        return -1;
    }
    AppLink *L = &a->links[slot];
    memset(L, 0, sizeof(*L));
    L->used = 1;
    L->port = port;
    L->code_group = -1;
    L->friend_link = friend_link;
    snprintf(L->host, sizeof(L->host), "%s", host);
    snprintf(L->relay, sizeof(L->relay), "%s", relay);
    snprintf(L->code, sizeof(L->code), "%s", code);
    if (join)
        snprintf(L->join_code, sizeof(L->join_code), "%s", code);
    start_link_locked(a, L);
    a->dirty = 1;
    UNLOCK(a);
    return slot;
}
static void apply_friends(OrbitApp *a, const cJSON *r) {
    static UiFriend before[ORBIT_FRIENDS];
    static pthread_mutex_t before_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&before_lock);
    LOCK(a);
    int count = a->friend_count, first = !a->friends_loaded;
    memcpy(before, a->friends, sizeof(before));
    a->friend_count = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "friends")) {
        if (a->friend_count >= ORBIT_FRIENDS)
            break;
        UiFriend *f = &a->friends[a->friend_count];
        memset(f, 0, sizeof(*f));
        f->i = num(x, "i", -1);
        f->state = num(x, "state", 0);
        f->user = num(x, "user", -1);
        f->status = num(x, "status", ST_OFFLINE);
        f->color = num(x, "color", 0);
        f->avatar = num(x, "avatar", 0);
        f->since = unum(x, "since");
        f->created = unum(x, "created");
        copy(f->name, sizeof(f->name), x, "name");
        copy(f->fid, sizeof(f->fid), x, "fid");
        copy(f->code, sizeof(f->code), x, "code");
        copy(f->custom, sizeof(f->custom), x, "custom");
        copy(f->bio, sizeof(f->bio), x, "bio");
        copy(f->game, sizeof(f->game), x, "game");
        copy(f->pub, sizeof(f->pub), x, "pub");
        const cJSON *m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItem(x, "mutual")) {
            if (!cJSON_IsString(m))
                continue;
            size_t n = strlen(f->mutual);
            snprintf(f->mutual + n, sizeof(f->mutual) - n, "%s%s", n ? ", " : "", m->valuestring);
            f->nmutual++;
        }
        a->friend_count++;
    }
    /* presence: online on my PS3, or their own PS3 answers our link */
    for (int i = 0; i < a->friend_count; i++) {
        UiFriend *f = &a->friends[i];
        f->online = f->status != ST_OFFLINE;
        for (int k = 1; k < APP_LINKS; k++) {
            AppLink *L = &a->links[k];
            if (L->used && !L->remove && L->owner_pub[0] && !strcmp(L->owner_pub, a->friends[i].pub)) {
                if (L->state == LINK_ONLINE && f->status == ST_OFFLINE)
                    f->online = 1, f->status = ST_ONLINE;
                /* mutual servers on their PS3 */
                for (int g = 0; g < L->group_count; g++)
                    if (L->groups[g].kind == GK_SERVER) {
                        size_t n = strlen(f->mutual);
                        snprintf(f->mutual + n, sizeof(f->mutual) - n, "%s%s", n ? ", " : "",
                                 L->groups[g].name);
                        f->nmutual++;
                    }
            }
        }
        if (first)
            continue;
        const UiFriend *old = NULL;
        for (int k = 0; k < count; k++)
            if (!strcmp(before[k].fid, f->fid))
                old = &before[k];
        char line[200];
        if (f->state == FR_INCOMING && (!old || old->state != FR_INCOMING)) {
            TF(line, sizeof(line), T_N_FRIEND_REQUEST, f->name);
            add_note(a, N_FRIEND_REQUEST, 0, -1, -1, line);
        }
        if (f->state == FR_FRIENDS && old && old->state == FR_OUTGOING) {
            TF(line, sizeof(line), T_N_FRIEND_ACCEPTED, f->name);
            add_note(a, N_FRIEND_ACCEPTED, 0, -1, -1, line);
        }
    }
    /* every friend's PS3 gets a link, so presence, DMs and their servers just work */
    for (int i = 0; i < a->friend_count; i++) {
        UiFriend *f = &a->friends[i];
        if ((f->state != FR_FRIENDS && f->state != FR_OUTGOING) || strlen(f->code) < 20)
            continue;
        int have = 0;
        for (int k = 1; k < APP_LINKS; k++)
            if (a->links[k].used && !a->links[k].remove && !strcmp(a->links[k].owner_pub, a->friends[i].pub))
                have = 1;
        if (!have) {
            char code[ORBIT_INVITE_TEXT];
            snprintf(code, sizeof(code), "%s", f->code);
            UNLOCK(a);
            int slot = link_for_code(a, code, 1, 0);
            LOCK(a);
            if (slot > 0)
                snprintf(a->links[slot].owner_pub, sizeof(a->links[slot].owner_pub), "%s", a->friends[i].pub);
        }
    }
    /* removed friends: their PS3 link stays only while we share servers there */
    for (int k = 1; k < APP_LINKS; k++) {
        AppLink *L = &a->links[k];
        if (!L->used || !L->friend_link || !L->owner_pub[0])
            continue;
        int still = 0;
        for (int i = 0; i < a->friend_count; i++)
            if (!strcmp(a->friends[i].pub, L->owner_pub) &&
                (a->friends[i].state == FR_FRIENDS || a->friends[i].state == FR_OUTGOING))
                still = 1;
        if (!still && a->friends_loaded && !L->synced)
            continue; /* a just added friend is not in the list yet */
        if (!still && a->friends_loaded)
            L->friend_link = 0;
    }
    a->friends_loaded = 1;
    if (count != a->friend_count || memcmp(before, a->friends, sizeof(before)))
        a->changed++;
    UNLOCK(a);
    pthread_mutex_unlock(&before_lock);
}
/* After sign-in on a friend's PS3: tell them (again) who we are and where to reach us. */
static int friend_sync(OrbitApp *a, AppLink *L, OrbitLink *c) {
    LOCK(a);
    int need = L->friend_link && !L->synced && L->owner_pub[0] && a->friend_code[0];
    char code[ORBIT_INVITE_TEXT], pub[65], name[49];
    snprintf(code, sizeof(code), "%s", a->friend_code);
    snprintf(pub, sizeof(pub), "%s", L->owner_pub);
    snprintf(name, sizeof(name), "%s", L->owner_name);
    UNLOCK(a);
    if (!need)
        return 0;
    cJSON *q = command("friend");
    cJSON_AddStringToObject(q, "code", code);
    cJSON *r = orbit_exchange(c, q);
    cJSON_Delete(q);
    if (!r)
        return -1;
    if (success(r)) {
        const char *state = orbit_string(r, "state");
        /* remember it on my own PS3 as well: outgoing request or friendship */
        cJSON *o = command("friend_out");
        cJSON_AddStringToObject(o, "pub", pub);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddStringToObject(o, "state", state);
        char back[ORBIT_INVITE_TEXT];
        uint32_t ip;
        int port;
        LOCK(a);
        int idx = (int)(L - a->links);
        /* the code that reaches them: rebuild it from this link's address */
        if (strcmp(L->host, "0.0.0.0") && inet_pton(AF_INET, L->host, &(struct in_addr){0}) == 1) {
            struct in_addr in;
            inet_pton(AF_INET, L->host, &in);
            ip = ntohl(in.s_addr);
            port = L->port;
        } else {
            ip = a->relay_ip;
            port = ORBIT_INVITE_RELAY | a->relay_port;
        }
        if (!orbit_invite_encode(back, ip, port, L->code))
            cJSON_AddStringToObject(o, "code", back);
        L->synced = 1;
        (void)idx;
        UNLOCK(a);
        bg_queue(a, 0, o);
    } else
        app_notice(a, error_text(r));
    cJSON_Delete(r);
    return 0;
}
/* Sends queued offline messages once their meeting point is reachable again. */
static int flush_outbox(OrbitApp *a, AppLink *L, OrbitLink *c) {
    for (;;) {
        LOCK(a);
        const char *server = link_server(a, L);
        int k = -1;
        for (int i = 0; i < a->outbox_count && k < 0; i++)
            if (!strcmp(a->outbox_server[i], server))
                k = i;
        UiOutbox o;
        if (k >= 0)
            o = a->outbox[k];
        UNLOCK(a);
        if (k < 0)
            return 0;
        cJSON *q = app_cmd("send", o.group);
        cJSON_AddNumberToObject(q, "channel", o.channel);
        cJSON_AddStringToObject(q, "text", o.text);
        cJSON *r = orbit_exchange(c, q);
        cJSON_Delete(q);
        if (!r)
            return -1;
        int wait = !success(r) && !strcmp(orbit_string(r, "ekey"), "E_WAIT");
        LOCK(a);
        if (!wait) {
            if (!success(r)) {
                char line[260];
                TF(line, sizeof(line), T_QUEUE_FAILED, error_text(r));
                notice_locked(a, line);
            }
            memmove(a->outbox + k, a->outbox + k + 1, sizeof(UiOutbox) * (size_t)(a->outbox_count - k - 1));
            memmove(a->outbox_server + k, a->outbox_server + k + 1, 17 * (size_t)(a->outbox_count - k - 1));
            a->outbox_count--;
            a->dirty = 1;
            a->changed++;
        }
        UNLOCK(a);
        cJSON_Delete(r);
        if (wait)
            usleep(350000);
    }
}
/* Runs one queued action. Returns -1 when the connection broke. */
static int run_job(OrbitApp *a, AppLink *L, OrbitLink *c, cJSON *job, int background) {
    const char *op = orbit_string(job, "op");
    cJSON *r = orbit_exchange(c, job);
    if (!r) {
        /* Never leave the app "busy": that froze every later action in 0.7. */
        LOCK(a);
        if (!background) {
            a->busy = 0;
            if (!strcmp(op, "invite"))
                a->invite_state = 3;
            notice_locked(a, T(T_INTERRUPTED));
        }
        UNLOCK(a);
        return -1;
    }
    int index = (int)(L - a->links);
    LOCK(a);
    if (!success(r)) {
        notice_locked(a, error_text(r));
        if (!strcmp(op, "invite"))
            a->invite_state = 3;
        if (!strcmp(op, "search"))
            a->search_state = 3;
    } else if (!strcmp(op, "send")) {
        a->sent_count++;
    } else if (!strcmp(op, "create") || !strcmp(op, "dm_create") || !strcmp(op, "join_public")) {
        a->selected_link = index;
        a->selected_group = num(r, "group", -1);
        a->selected_channel = -1;
        a->dirty = 1;
        if (!strcmp(op, "create"))
            notice_locked(a, T(T_SERVER_CREATED));
        else if (!strcmp(op, "join_public"))
            notice_locked(a, T(T_JOINED));
    } else if (!strcmp(op, "leave") || !strcmp(op, "delete")) {
        a->selected_group = -1;
        notice_locked(a, T(!strcmp(op, "delete") ? T_DELETED : T_LEFT));
    } else if (!strcmp(op, "home")) {
        char norm[33];
        if (!orbit_group_code(norm, orbit_string(r, "invite")) && strlen(norm) == 8) {
            snprintf(a->home_code, sizeof(a->home_code), "%s", norm);
            update_friend_code_locked(a);
        }
    } else if (!strcmp(op, "friends")) {
        UNLOCK(a);
        apply_friends(a, r);
        LOCK(a);
    } else if (!strcmp(op, "members")) {
        L->member_count = 0;
        const cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "members")) {
            if (L->member_count >= ORBIT_USERS)
                break;
            UiMember *m = &L->members[L->member_count++];
            memset(m, 0, sizeof(*m));
            m->user = num(x, "user", -1);
            m->status = num(x, "status", ST_OFFLINE);
            m->color = num(x, "color", 0);
            m->avatar = num(x, "avatar", 0);
            m->roles = num(x, "roles", 0);
            m->top = num(x, "top", 0);
            m->owner = cJSON_IsTrue(cJSON_GetObjectItem(x, "owner"));
            m->smuted = cJSON_IsTrue(cJSON_GetObjectItem(x, "smuted"));
            m->call = num(x, "call", -1);
            m->created = unum(x, "created");
            copy(m->name, sizeof(m->name), x, "name");
            copy(m->oid, sizeof(m->oid), x, "oid");
            copy(m->custom, sizeof(m->custom), x, "custom");
            copy(m->bio, sizeof(m->bio), x, "bio");
            copy(m->game, sizeof(m->game), x, "game");
        }
        L->banned_count = 0;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "banned")) {
            if (L->banned_count < ORBIT_USERS)
                parse_person(&L->banned[L->banned_count++], x);
        }
    } else if (!strcmp(op, "browse")) {
        const cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "servers")) {
            if (a->browse_count >= UI_RESULTS)
                break;
            UiBrowse *b = &a->browse[a->browse_count++];
            b->link = index;
            b->id = num(x, "id", -1);
            b->members = num(x, "members", 0);
            b->icon = num(x, "icon", 0);
            b->color = num(x, "color", 0);
            copy(b->name, sizeof(b->name), x, "name");
            copy(b->desc, sizeof(b->desc), x, "desc");
            copy(b->host, sizeof(b->host), x, "host");
        }
        a->browse_state = 2;
    } else if (!strcmp(op, "search")) {
        a->search_count = 0;
        const cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "messages")) {
            if (a->search_count < UI_RESULTS)
                parse_message(&a->search[a->search_count++], x, L->user);
        }
        a->search_state = 2;
    } else if (!strcmp(op, "friend_respond") || !strcmp(op, "friend_remove") ||
               !strcmp(op, "friend_block") || !strcmp(op, "friend_unblock")) {
        static const struct {
            const char *op;
            int key;
        } done[] = {{"friend_respond", T_FRIEND_UPDATED}, {"friend_remove", T_FRIEND_REMOVED},
                    {"friend_block", T_FRIEND_BLOCKED}, {"friend_unblock", T_FRIEND_UNBLOCKED}};
        for (int i = 0; i < 4; i++)
            if (!strcmp(op, done[i].op))
                notice_locked(a, T(done[i].key));
    } else if (!strcmp(op, "invite")) {
        const char *code = orbit_string(r, "invite");
        uint32_t ip = 0;
        int port = 0;
        char scope[300] = "";
        struct in_addr in;
        if (L->own)
            own_endpoint(a, &ip, &port, scope, sizeof(scope));
        else if (!strcmp(L->host, "0.0.0.0")) {
            char rh[ORBIT_HOST];
            int rp = ORBIT_RELAY_PORT;
            const char *rl = L->relay[0] ? L->relay : a->relay;
            if (!orbit_parse_endpoint(rl, rh, &rp) && inet_pton(AF_INET, rh, &in) == 1) {
                ip = ntohl(in.s_addr);
                port = ORBIT_INVITE_RELAY | (strchr(rl, ':') ? rp : ORBIT_RELAY_PORT);
            } else if (a->relay_ip) {
                ip = a->relay_ip;
                port = ORBIT_INVITE_RELAY | a->relay_port;
            }
            snprintf(scope, sizeof(scope), "%s", T(T_SCOPE_RELAY));
        } else if (inet_pton(AF_INET, L->host, &in) == 1) {
            ip = ntohl(in.s_addr);
            port = L->port;
            snprintf(scope, sizeof(scope), "%s",
                     T(orbit_ipv4_private(ip) ? T_SCOPE_HOST_HOME : T_SCOPE_HOST_INTERNET));
        }
        if (port && !orbit_invite_encode(a->invite_code, ip, port, code)) {
            a->invite_state = 2;
            snprintf(a->invite_scope, sizeof(a->invite_scope), "%s", scope);
            int gid = num(job, "group", -1);
            a->invite_group[0] = 0;
            UiGroup *g = find_group(L, gid);
            if (g)
                snprintf(a->invite_group, sizeof(a->invite_group), "%s", g->name);
        } else {
            a->invite_state = 3;
            notice_locked(a, T(T_NO_ADDRESS));
        }
    } else if (!background && strcmp(op, "react") && strcmp(op, "profile") && strcmp(op, "friend_out"))
        notice_locked(a, T(T_DONE));
    if (!background)
        a->busy = 0;
    a->changed++;
    UNLOCK(a);
    cJSON_Delete(r);
    return 0;
}
static void *link_main(void *arg) {
    AppLink *L = arg;
    OrbitApp *a = L->owner;
    int index = (int)(L - a->links);
    OrbitLink *c = NULL;
    uint64_t poll_at = 0, friends_at = 0, members_at = 0;
    int watched = -2, watched_ch = -2;
    uint32_t watched_last = 0, watched_rev = 0;
    for (;;) {
        LOCK(a);
        int stop = a->stop || L->remove;
        cJSON *job = c ? L->job : NULL;
        if (c)
            L->job = NULL;
        cJSON *bjob = NULL;
        if (c && L->bg[0]) {
            bjob = L->bg[0];
            memmove(&L->bg[0], &L->bg[1], sizeof(cJSON *) * (BG_JOBS - 1));
            L->bg[BG_JOBS - 1] = NULL;
        }
        int reauth = L->reauth;
        L->reauth = 0;
        int watch = a->selected_link == index ? a->selected_group : -1;
        int watch_ch = a->selected_link == index ? a->selected_channel : -1;
        int want_members = L->want_members, want_browse = L->want_browse;
        L->want_browse = 0;
        char join[9];
        snprintf(join, sizeof(join), "%s", L->join_code);
        uint64_t retry = L->retry_at;
        UNLOCK(a);
        if (stop) {
            cJSON_Delete(job);
            cJSON_Delete(bjob);
            break;
        }
        uint64_t now = orbit_millis();
        if (!c) {
            if (now < retry) {
                LOCK(a);
                if (L->job) {
                    /* No connection: fail fast instead of freezing the UI. */
                    cJSON_Delete(L->job);
                    L->job = NULL;
                    a->busy = 0;
                    if (a->invite_state == 1)
                        a->invite_state = 3;
                    notice_locked(a, T(T_HOST_OFFLINE));
                }
                UNLOCK(a);
                usleep(100000);
                continue;
            }
            LOCK(a);
            L->state = LINK_CONNECTING;
            a->changed++;
            UNLOCK(a);
            c = link_open(a, L, 0);
            LOCK(a);
            if (c) {
                L->state = LINK_ONLINE;
                L->backoff = 2000;
                L->error[0] = 0;
                L->synced = 0;
            } else if (!L->remove) {
                L->state = LINK_FAILED;
                if (L->backoff < 2000)
                    L->backoff = 2000;
                L->retry_at = orbit_millis() + L->backoff;
                /* short waits: a friend should be back within seconds, not half a minute */
                L->backoff = L->backoff * 2 > 6000 ? 6000 : L->backoff * 2;
            }
            a->changed++;
            UNLOCK(a);
            poll_at = friends_at = members_at = 0;
            watched = -2;
            if (c && L->own) {
                bg_queue(a, 0, command("home"));
            }
            continue;
        }
        int broken = 0;
        if (reauth) {
            LOCK(a);
            char id[17];
            snprintf(id, sizeof(id), "%s", link_server(a, L));
            UNLOCK(a);
            broken = sign_in(a, L, c, id) == -2;
            poll_at = 0;
        }
        if (!broken)
            broken = friend_sync(a, L, c) != 0;
        if (!broken)
            broken = flush_outbox(a, L, c) != 0;
        if (!broken && join[0]) {
            cJSON *q = command("join");
            cJSON_AddStringToObject(q, "invite", join);
            cJSON *r = orbit_exchange(c, q);
            cJSON_Delete(q);
            if (!r)
                broken = 1;
            else {
                LOCK(a);
                if (!strcmp(L->join_code, join))
                    L->join_code[0] = 0;
                if (success(r) && num(r, "group", -1) >= 0) {
                    a->selected_link = index;
                    a->selected_group = num(r, "group", -1);
                    a->selected_channel = -1;
                    if (!L->own && (L->code_group < 0 || !strcmp(L->code, join)))
                        L->code_group = a->selected_group;
                    notice_locked(a, T(T_INVITE_ACCEPTED));
                } else if (!success(r))
                    notice_locked(a, error_text(r));
                a->dirty = 1;
                UNLOCK(a);
                cJSON_Delete(r);
                poll_at = 0;
            }
        }
        if (!broken && job) {
            broken = run_job(a, L, c, job, 0) != 0;
            poll_at = 0;
        }
        cJSON_Delete(job);
        if (!broken && bjob) {
            broken = run_job(a, L, c, bjob, 1) != 0;
            poll_at = 0;
        }
        cJSON_Delete(bjob);
        if (!broken && want_browse) {
            cJSON *q = command("browse");
            broken = run_job(a, L, c, q, 1) != 0;
            cJSON_Delete(q);
        }
        if (!broken && L->own && now >= friends_at) {
            friends_at = now + 2000;
            cJSON *q = command("friends");
            broken = run_job(a, L, c, q, 1) != 0;
            cJSON_Delete(q);
            LOCK(a);
            update_friend_code_locked(a); /* the address may have changed (tunnel, router) */
            UNLOCK(a);
        }
        if (!broken && want_members >= 0 && (now >= members_at || want_members != L->want_members)) {
            members_at = now + 3000;
            cJSON *q = app_cmd("members", want_members);
            broken = run_job(a, L, c, q, 1) != 0;
            cJSON_Delete(q);
        }
        if (!broken && (now >= poll_at || watch != watched || watch_ch != watched_ch)) {
            poll_at = now + 700;
            cJSON *q = command("groups"), *r = orbit_exchange(c, q);
            cJSON_Delete(q);
            if (!r)
                broken = 1;
            else if (success(r)) {
                apply_groups(a, L, r);
                uint32_t mrev = unum(r, "mrev");
                LOCK(a);
                int present = 0, empty = !L->own && !L->friend_link && L->group_count == 0 &&
                                         !L->join_code[0];
                uint32_t last = 0;
                UiGroup *wg = NULL;
                for (int i = 0; i < L->group_count; i++) {
                    if (L->groups[i].id == L->code_group)
                        present = 1;
                    if (L->groups[i].id == watch)
                        wg = &L->groups[i];
                }
                if (wg && watch_ch < 0) {
                    /* nothing chosen yet: first text channel */
                    a->selected_channel = watch_ch = first_channel(wg, CH_TEXT);
                }
                const UiChannel *wc = find_channel(wg, watch_ch);
                if (wc)
                    last = wc->last;
                int refresh = !L->own && !L->friend_link && !present && L->group_count &&
                                      L->groups[0].kind == GK_SERVER
                                  ? L->groups[0].id
                                  : -1;
                if (empty) {
                    L->remove = 1;
                    a->dirty = 1;
                }
                UNLOCK(a);
                if (refresh >= 0) {
                    /* Keep a valid invitation for reconnecting when a group was removed. */
                    cJSON *iq = app_cmd("invite", refresh);
                    cJSON *ir = orbit_exchange(c, iq);
                    cJSON_Delete(iq);
                    char norm[33];
                    LOCK(a);
                    if (ir && success(ir) && !orbit_group_code(norm, orbit_string(ir, "invite")) &&
                        strlen(norm) == 8) {
                        snprintf(L->code, sizeof(L->code), "%s", norm);
                        L->code_group = refresh;
                        a->dirty = 1;
                    }
                    UNLOCK(a);
                    cJSON_Delete(ir);
                }
                if (watch >= 0 && wc && wc->type == CH_TEXT &&
                    (watch != watched || watch_ch != watched_ch || last != watched_last ||
                     mrev != watched_rev)) {
                    LOCK(a);
                    int same = L->history_group == watch && L->history_channel == watch_ch;
                    int after = same && L->message_count ? L->messages[L->message_count - 1].id : 0;
                    uint32_t since = same ? L->history_rev : 0;
                    UNLOCK(a);
                    cJSON *h = app_cmd("history", watch);
                    cJSON_AddNumberToObject(h, "channel", watch_ch);
                    cJSON_AddNumberToObject(h, "after", after);
                    if (same && after)
                        cJSON_AddNumberToObject(h, "since", since);
                    cJSON *hr = orbit_exchange(c, h);
                    cJSON_Delete(h);
                    if (!hr)
                        broken = 1;
                    else if (success(hr)) {
                        apply_history(a, L, watch, watch_ch, hr);
                        /* More than one page waiting: fetch the rest right away. */
                        if (cJSON_GetArraySize(cJSON_GetObjectItem(hr, "messages")) >= 40)
                            poll_at = 0, last = 0;
                    }
                    cJSON_Delete(hr);
                    watched = watch;
                    watched_ch = watch_ch;
                    watched_last = last;
                    watched_rev = mrev;
                } else if (watch < 0 || !wc)
                    watched = watch, watched_ch = watch_ch;
            }
            cJSON_Delete(r);
        }
        if (broken) {
            orbit_disconnect(c);
            c = NULL;
            LOCK(a);
            L->state = LINK_FAILED;
            snprintf(L->error, sizeof(L->error), "%s", T(T_RECONNECTING));
            L->retry_at = orbit_millis() + 1500;
            a->changed++;
            UNLOCK(a);
            continue;
        }
        usleep(25000);
    }
    orbit_disconnect(c);
    LOCK(a);
    if (L->job) {
        cJSON_Delete(L->job);
        L->job = NULL;
        a->busy = 0;
    }
    for (int i = 0; i < BG_JOBS; i++) {
        cJSON_Delete(L->bg[i]);
        L->bg[i] = NULL;
    }
    L->running = 0;
    L->state = LINK_OFF;
    UNLOCK(a);
    return NULL;
}
static int queue(OrbitApp *a, int link, cJSON *job);
static void broadcast_of(char out[16], const char *ip, const char *mask);
static void start_link_locked(OrbitApp *a, AppLink *L) {
    L->owner = a;
    L->history_group = -1;
    L->history_channel = -1;
    L->want_members = -1;
    L->backoff = 2000;
    if (!pthread_create(&L->thread, NULL, link_main, L))
        L->running = L->thread_joinable = 1;
}
/* ---------- manager: hosting, router opening, saving ---------- */
static void *manager_main(void *arg) {
    OrbitApp *a = arg;
    uint64_t nat_at = 0, net_check = 0;
    for (;;) {
        LOCK(a);
        int stop = a->stop;
        UNLOCK(a);
        if (stop)
            break;
        uint64_t now = orbit_millis();
        if (a->netinfo && now >= net_check) {
            /* The PS3 may still be connecting at start, or get a new address later. */
            net_check = now + 5000;
            char ip[16] = "", mask[16] = "", gw[16] = "";
            if (!a->netinfo(ip, mask, gw) && ip[0]) {
                LOCK(a);
                if (strcmp(ip, a->ip) || strcmp(gw, a->gateway)) {
                    snprintf(a->ip, sizeof(a->ip), "%s", ip);
                    snprintf(a->netmask, sizeof(a->netmask), "%s", mask);
                    snprintf(a->gateway, sizeof(a->gateway), "%s", gw);
                    broadcast_of(a->broadcast, a->ip, a->netmask);
                    a->nat_now = 1;
                    for (int i = 0; i < APP_LINKS; i++)
                        a->links[i].retry_at = 0;
                }
                UNLOCK(a);
            }
        }
        /* Internet reachability without router settings: built-in tunnel first, then relay. */
        LOCK(a);
        {
            char why[200], bwhy[200];
            int online = orbit_relay_host_state(a->relay_host, why, sizeof(why));
            uint32_t bip = 0;
            int bport = 0;
            int bon = orbit_bore_state(a->bore, &bip, &bport, NULL, 0, bwhy, sizeof(bwhy));
            int dirty_state = online != a->relay_online || strcmp(why, a->relay_reason) ||
                              bon != a->bore_online || bport != a->bore_port ||
                              strcmp(bwhy, a->bore_reason);
            a->relay_online = online;
            snprintf(a->relay_reason, sizeof(a->relay_reason), "%s", why);
            a->bore_online = bon;
            a->bore_ip = bip;
            a->bore_port = bport;
            snprintf(a->bore_reason, sizeof(a->bore_reason), "%s", bwhy);
            if (dirty_state) {
                if (!a->nat.ok && a->net != NET_CHECKING && strcmp(a->ip, "127.0.0.1")) {
                    a->net = bon || online ? NET_INTERNET : NET_HOME;
                    snprintf(a->net_detail, sizeof(a->net_detail), "%s",
                             bon      ? T(T_NET_TUNNEL)
                             : online ? T(T_NET_RELAY)
                             : a->bore ? a->bore_reason
                                       : a->relay_reason);
                }
                update_friend_code_locked(a);
                a->changed++;
            }
            /* A waiting invitation goes out as soon as the best address is known. */
            if (a->invite_wait && a->net != NET_CHECKING &&
                (a->nat.ok || bon || !a->bore || now - a->invite_wait_since > 15000)) {
                a->invite_wait = 0;
                a->invite_dispatch = 1;
            }
        }
        int dispatch = a->invite_dispatch, wl = a->invite_wait_link, wg = a->invite_wait_group;
        a->invite_dispatch = 0;
        UNLOCK(a);
        if (dispatch) {
            if (queue(a, wl, app_cmd("invite", wg))) {
                LOCK(a);
                a->invite_state = 3;
                UNLOCK(a);
            }
        }
        LOCK(a);
        if (a->nat_now) {
            a->nat_now = 0;
            nat_at = 0;
        }
        /* Safety net: no action may block the interface for more than 12 seconds. */
        if (a->busy && now - a->busy_since > 12000) {
            a->busy = 0;
            if (a->invite_state == 1 && !a->invite_wait)
                a->invite_state = 3;
            a->changed++;
        }
        UNLOCK(a);
        if (now >= nat_at) {
            /* Refresh twice per hour: lease renewal and changed public addresses. */
            nat_at = now + 30 * 60 * 1000;
            LOCK(a);
            if (a->net != NET_INTERNET) {
                a->net = NET_CHECKING;
                snprintf(a->net_detail, sizeof(a->net_detail), "%s", T(T_NET_CHECKING_LONG));
                a->changed++;
            }
            UNLOCK(a);
            OrbitNat n;
            char ip[16], gw[16];
            LOCK(a);
            snprintf(ip, sizeof(ip), "%s", a->ip);
            snprintf(gw, sizeof(gw), "%s", a->gateway);
            UNLOCK(a);
            int ok = !orbit_nat_open(ip, gw, a->port, &n);
            LOCK(a);
            a->nat = n;
            a->net = ok ? NET_INTERNET : !strcmp(ip, "127.0.0.1") ? NET_OFFLINE : NET_HOME;
            snprintf(a->net_detail, sizeof(a->net_detail), "%s", ok ? T(T_NET_REACHABLE) : n.reason);
            a->relay_online = -1; /* recompute the combined state below */
            if (!ok && !n.cgnat && a->net == NET_HOME)
                nat_at = now + 5 * 60 * 1000; /* the router may allow it later */
            update_friend_code_locked(a);
            a->changed++;
            UNLOCK(a);
        }
        LOCK(a);
        for (int i = 1; i < APP_LINKS; i++) {
            AppLink *L = &a->links[i];
            if (L->used && L->remove && !L->running) {
                if (L->thread_joinable) {
                    pthread_t t = L->thread;
                    L->thread_joinable = 0;
                    UNLOCK(a);
                    pthread_join(t, NULL);
                    LOCK(a);
                }
                if (a->selected_link == i)
                    a->selected_link = 0, a->selected_group = -1;
                memset(a->seen[i], 0, sizeof(a->seen[i]));
                memset(a->chat_muted[i], 0, sizeof(a->chat_muted[i]));
                a->groups_loaded[i] = 0;
                memset(L, 0, sizeof(*L));
                a->dirty = 1;
                a->changed++;
            }
        }
        if (a->dirty)
            save_profile_locked(a);
        UNLOCK(a);
        usleep(250000);
    }
    return NULL;
}
static void broadcast_of(char out[16], const char *ip, const char *mask) {
    struct in_addr i, m;
    out[0] = 0;
    if (inet_pton(AF_INET, ip, &i) == 1 && inet_pton(AF_INET, mask, &m) == 1) {
        uint32_t b = ntohl(i.s_addr) | ~ntohl(m.s_addr);
        snprintf(out, 16, "%u.%u.%u.%u", b >> 24, (b >> 16) & 255, (b >> 8) & 255, b & 255);
    }
}
static void relay_restart(OrbitApp *a);
int app_start(OrbitApp *a, const char *dir, const char *ip, const char *netmask,
              const char *gateway, const char *suggested, int port) {
    memset(a, 0, sizeof(*a));
    a->port = port > 0 ? port : ORBIT_PORT;
    pthread_mutex_init(&a->lock, NULL);
    snprintf(a->data_dir, sizeof(a->data_dir), "%s", dir);
    snprintf(a->ip, sizeof(a->ip), "%s", ip && ip[0] ? ip : "127.0.0.1");
    snprintf(a->netmask, sizeof(a->netmask), "%s", netmask ? netmask : "");
    snprintf(a->gateway, sizeof(a->gateway), "%s", gateway ? gateway : "");
    broadcast_of(a->broadcast, a->ip, a->netmask);
    a->selected_group = -1;
    a->selected_channel = -1;
    a->changed = 1;
    a->net = NET_CHECKING;
    a->set.lang_auto = 1;
    a->set.lang = i18n_lang();
    a->set.notify = (1 << N_COUNT) - 1;
    snprintf(a->net_detail, sizeof(a->net_detail), "%s", T(T_NET_CHECKING_LONG));
    char path[512];
    snprintf(path, sizeof(path), "%s/device.key", dir);
    int device_ok = !orbit_keyfile(path, a->device);
    snprintf(path, sizeof(path), "%s/server.key", dir);
    int key_ok = !orbit_keyfile(path, a->server_key);
    uint8_t fp[8];
    crypto_blake2b(fp, 8, a->server_key, 32);
    orbit_hex(a->server_id, fp, 8);
    orbit_identity(a->device, a->id_secret, a->id_pub);
    orbit_id_text(a->oid, a->id_pub);
    AppLink *own = &a->links[0];
    own->used = 1;
    own->own = 1;
    own->code_group = -1;
    load_profile(a);
    if (!a->me.created) {
        a->me.created = orbit_unix_time();
        a->dirty = 1;
    }
    if (!a->set.lang_auto)
        i18n_set(a->set.lang);
    if (!a->profile && suggested && orbit_text_valid(suggested, 1, 48))
        snprintf(a->name, sizeof(a->name), "%s", suggested);
    if (!a->name[0])
        snprintf(a->name, sizeof(a->name), "%s", T(T_DEFAULT_NAME));
    a->store = calloc(1, sizeof(*a->store));
    snprintf(path, sizeof(path), "%s/data.json", dir);
    if (!device_ok || !key_ok || !a->store || orbit_store_open_lang(a->store, path, i18n_lang())) {
        free(a->store);
        a->store = NULL;
        notice_locked(a, T(T_STORE_FAILED));
    } else {
        memcpy(a->store->server_id, a->server_id, 17);
        a->server = orbit_server_start(a->store, a->server_key, a->port);
        if (!a->server)
            notice_locked(a, T(T_SERVER_FAILED));
    }
    if (pthread_create(&a->manager, NULL, manager_main, a)) {
        pthread_mutex_destroy(&a->lock);
        return -1;
    }
    LOCK(a);
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && (i || a->server))
            start_link_locked(a, &a->links[i]);
    UNLOCK(a);
    if (a->relay[0])
        relay_restart(a);
    return 0;
}
void app_set_netinfo(OrbitApp *a, AppNetInfo fn) {
    LOCK(a);
    a->netinfo = fn;
    UNLOCK(a);
}
void app_stop(OrbitApp *a) {
    app_mic_test(a, 0);
    LOCK(a);
    a->stop = 1;
    if (a->dirty)
        save_profile_locked(a);
    int call = a->call_joinable;
    a->call_joinable = 0;
    UNLOCK(a);
    if (call)
        pthread_join(a->call_thread, NULL);
    pthread_join(a->manager, NULL);
    for (int i = 0; i < APP_LINKS; i++) {
        LOCK(a);
        int join = a->links[i].thread_joinable;
        a->links[i].thread_joinable = 0;
        UNLOCK(a);
        if (join)
            pthread_join(a->links[i].thread, NULL);
        cJSON_Delete(a->links[i].job);
        a->links[i].job = NULL;
    }
    LOCK(a);
    if (a->dirty)
        save_profile_locked(a);
    UNLOCK(a);
    orbit_bore_stop(a->bore);
    a->bore = NULL;
    orbit_relay_host_stop(a->relay_host);
    a->relay_host = NULL;
    orbit_nat_close(&a->nat, a->gateway);
    orbit_server_stop(a->server);
    if (a->store) {
        orbit_store_close(a->store);
        free(a->store);
    }
    crypto_wipe(a->device, 32);
    crypto_wipe(a->server_key, 32);
    crypto_wipe(a->id_secret, 64);
    pthread_mutex_destroy(&a->lock);
}
/* ---------- UI side ---------- */
static int blocked_oid(const OrbitApp *a, const char *oid) {
    for (int i = 0; oid[0] && i < a->friend_count; i++)
        if (a->friends[i].state == FR_BLOCKED && !strcmp(a->friends[i].fid, oid))
            return 1;
    return 0;
}
void app_snapshot(OrbitApp *a, UiState *s) {
    LOCK(a);
    /* Who is talking changes many times per second: compare it cheaply on every frame. */
    uint32_t speak = 0;
    int mic = 0;
    if (a->mic_test)
        mic = 100 + voice_mic_level() / 4; /* live level bar in the settings */
    if (a->call_running) {
        const AppLink *CL = &a->links[a->call_link];
        for (int i = 0; i < a->call_count; i++) {
            int self = a->call_people[i].user == CL->user;
            if (self ? voice_self_speaking() : voice_speaking(a->call_people[i].user))
                speak |= 1u << i;
        }
        speak |= (uint32_t)voice_muted() << 31;
        if (!a->mic_test)
            mic = voice_mic_level() / 25;
    }
    if (s->revision && a->changed == s->revision && speak == s->speak_mask && mic == s->call_mic) {
        UNLOCK(a);
        return;
    }
    s->speak_mask = speak;
    s->call_mic = mic;
    s->call_state = a->call_running ? a->call_state : CALL_OFF;
    s->call_muted = voice_muted();
    s->mic_test = a->mic_test;
    s->mic_level = voice_mic_level();
    s->mic_floor = voice_noise_floor();
    snprintf(s->mic_info, sizeof(s->mic_info), "%s", voice_mic_info());
    snprintf(s->ds_note, sizeof(s->ds_note), "%s", voice_ds_note());
    s->call_count = a->call_running ? a->call_count : 0;
    s->call_link = a->call_link;
    s->call_group = a->call_group;
    s->call_channel = a->call_channel;
    snprintf(s->call_name, sizeof(s->call_name), "%s", a->call_name);
    snprintf(s->call_note, sizeof(s->call_note), "%s", a->call_note);
    for (int i = 0; i < s->call_count; i++) {
        UiCallPerson *p = &s->call_people[i];
        snprintf(p->name, 49, "%s", a->call_people[i].name);
        p->user = a->call_people[i].user;
        p->self = a->call_people[i].user == a->links[a->call_link].user;
        p->speaking = (speak >> i) & 1;
        p->local_muted = a->call_people[i].local_muted;
        p->volume = a->call_people[i].volume;
        p->server_muted = a->call_people[i].server_muted;
    }
    s->revision = a->changed ? a->changed : 1;
    s->profile = a->profile;
    s->busy = a->busy;
    s->sent_count = a->sent_count;
    s->net = a->net;
    s->relay_online = a->relay_online > 0;
    s->tunnel_online = a->bore_online;
    if (a->bore && a->bore_port)
        snprintf(s->tunnel, sizeof(s->tunnel), "%s:%d", a->bore_host, a->bore_port);
    else
        snprintf(s->tunnel, sizeof(s->tunnel), "%s", a->bore ? T(T_TUNNEL_BUILDING) : T(T_OFF));
    snprintf(s->relay, sizeof(s->relay), "%s", a->relay);
    s->invite_state = a->invite_state;
    s->status_id = a->status_id;
    snprintf(s->status, sizeof(s->status), "%s", a->status);
    snprintf(s->username, sizeof(s->username), "%s", a->name);
    snprintf(s->invite_code, sizeof(s->invite_code), "%s", a->invite_code);
    snprintf(s->invite_group, sizeof(s->invite_group), "%s", a->invite_group);
    snprintf(s->invite_scope, sizeof(s->invite_scope), "%s", a->invite_scope);
    snprintf(s->net_detail, sizeof(s->net_detail), "%s", a->net_detail);
    snprintf(s->local_ip, sizeof(s->local_ip), "%s", a->ip);
    snprintf(s->public_ip, sizeof(s->public_ip), "%s", a->nat.ok ? a->nat.external : "");
    snprintf(s->server_id, sizeof(s->server_id), "%s", a->server_id);
    snprintf(s->oid, sizeof(s->oid), "%s", a->oid);
    snprintf(s->friend_code, sizeof(s->friend_code), "%s", a->friend_code);
    s->me = a->me;
    s->set = a->set;
    s->group_count = 0;
    s->selected = -1;
    s->channel = a->selected_channel;
    s->link_error[0] = 0;
    s->unread_dm = s->unread_server = 0;
    for (int i = 0; i < APP_LINKS; i++) {
        const AppLink *L = &a->links[i];
        if (!L->used || L->remove)
            continue;
        for (int g = 0; g < L->group_count && s->group_count < UI_GROUPS; g++) {
            UiGroup *u = &s->groups[s->group_count];
            *u = L->groups[g];
            u->link = i;
            u->own = L->own;
            u->state = L->state;
            u->muted_chat = a->chat_muted[i][u->id];
            u->unread = 0;
            for (int c = 0; c < u->nch; c++)
                if (u->ch[c].type == CH_TEXT && u->ch[c].last > a->seen[i][u->id][u->ch[c].id] &&
                    !(a->selected_link == i && a->selected_group == u->id &&
                      a->selected_channel == u->ch[c].id))
                    u->unread++;
            if (u->unread && !u->muted_chat)
                *(u->kind == GK_DM ? &s->unread_dm : &s->unread_server) += 1;
            if (a->selected_link == i && a->selected_group == u->id)
                s->selected = s->group_count;
            s->group_count++;
        }
    }
    s->call_here = s->call_state != CALL_OFF && a->call_link == a->selected_link &&
                   a->call_group == a->selected_group;
    s->message_count = 0;
    s->loading = 0;
    if (s->selected >= 0) {
        const AppLink *L = &a->links[s->groups[s->selected].link];
        if (L->history_group == s->groups[s->selected].id && L->history_channel == a->selected_channel) {
            s->message_count = L->message_count;
            memcpy(s->messages, L->messages, sizeof(UiMessage) * (size_t)L->message_count);
        } else
            s->loading = L->state == LINK_ONLINE || L->state == LINK_CONNECTING;
        /* messages waiting for delivery are shown, clearly marked as not delivered yet */
        const char *server = link_server(a, L);
        for (int k = 0; k < a->outbox_count; k++) {
            const UiOutbox *o = &a->outbox[k];
            if (strcmp(a->outbox_server[k], server) || o->group != a->selected_group ||
                o->channel != a->selected_channel)
                continue;
            if (s->message_count == UI_MESSAGES) {
                memmove(s->messages, s->messages + 1, sizeof(UiMessage) * (UI_MESSAGES - 1));
                s->message_count--;
            }
            UiMessage *m = &s->messages[s->message_count++];
            memset(m, 0, sizeof(*m));
            m->mine = 1;
            m->pending = 1;
            m->time = o->time;
            m->color = a->me.color;
            m->avatar = a->me.avatar;
            snprintf(m->name, sizeof(m->name), "%s", a->name);
            snprintf(m->text, sizeof(m->text), "%s", o->text);
        }
        for (int k = 0; k < s->message_count; k++)
            s->messages[k].blocked = blocked_oid(a, s->messages[k].oid);
        if (L->state != LINK_ONLINE)
            snprintf(s->link_error, sizeof(s->link_error), "%s", L->error);
        else if (L->error[0])
            snprintf(s->link_error, sizeof(s->link_error), "%s", L->error);
        s->members_link = s->groups[s->selected].link;
        s->members_group = L->want_members;
        s->member_count = L->want_members == a->selected_group ? L->member_count : 0;
        memcpy(s->members, L->members, sizeof(UiMember) * (size_t)s->member_count);
        s->banned_count = L->want_members == a->selected_group ? L->banned_count : 0;
        memcpy(s->banned, L->banned, sizeof(UiPerson) * (size_t)s->banned_count);
    } else
        s->member_count = s->banned_count = 0;
    s->friend_count = a->friend_count;
    memcpy(s->friends, a->friends, sizeof(UiFriend) * (size_t)a->friend_count);
    s->requests = 0;
    for (int i = 0; i < a->friend_count; i++)
        s->requests += a->friends[i].state == FR_INCOMING;
    s->note_count = a->note_count;
    memcpy(s->notes, a->notes, sizeof(UiNote) * (size_t)a->note_count);
    s->browse_count = a->browse_count;
    s->browse_state = a->browse_state;
    memcpy(s->browse, a->browse, sizeof(UiBrowse) * (size_t)a->browse_count);
    s->search_count = a->search_count;
    s->search_state = a->search_state;
    memcpy(s->search, a->search, sizeof(UiMessage) * (size_t)a->search_count);
    s->outbox_count = a->outbox_count;
    UNLOCK(a);
}
void app_open(OrbitApp *a, int link, int group, int channel) {
    LOCK(a);
    if (link >= 0 && link < APP_LINKS && a->links[link].used) {
        if (a->selected_link != link || a->selected_group != group || a->selected_channel != channel) {
            a->selected_link = link;
            a->selected_group = group;
            UiGroup *g = find_group(&a->links[link], group);
            a->selected_channel = channel >= 0 ? channel : first_channel(g, CH_TEXT);
            a->dirty = 1;
            a->changed++;
        }
    }
    UNLOCK(a);
}
void app_select(OrbitApp *a, int dir) {
    UiState *tmp = calloc(1, sizeof(*tmp));
    if (!tmp)
        return;
    app_snapshot(a, tmp);
    if (tmp->group_count > 1) {
        int next = (tmp->selected + dir + tmp->group_count) % tmp->group_count;
        app_open(a, tmp->groups[next].link, tmp->groups[next].id, -1);
    }
    free(tmp);
}
static int queue(OrbitApp *a, int link, cJSON *job) {
    LOCK(a);
    AppLink *L = link >= 0 && link < APP_LINKS ? &a->links[link] : NULL;
    int ok = L && L->used && !L->remove && !L->job && !a->busy && L->state == LINK_ONLINE;
    if (ok) {
        L->job = job;
        a->busy = 1;
        a->busy_since = orbit_millis();
        a->changed++;
    } else if (L && L->used && L->state == LINK_CONNECTING)
        notice_locked(a, T(T_CONNECTING_WAIT));
    else if (L && L->used && L->state != LINK_ONLINE)
        notice_locked(a, T(T_HOST_OFFLINE));
    else if (a->busy)
        notice_locked(a, T(T_ONE_MOMENT));
    UNLOCK(a);
    if (!ok)
        cJSON_Delete(job);
    return ok ? 0 : -1;
}
int app_job(OrbitApp *a, int link, cJSON *job) {
    return queue(a, link, job);
}
static int selected(OrbitApp *a, int *link, int *gid) {
    LOCK(a);
    *link = a->selected_link;
    *gid = a->selected_group;
    int ok = *gid >= 0 && *link >= 0 && *link < APP_LINKS && a->links[*link].used;
    UNLOCK(a);
    return ok ? 0 : -1;
}
int app_set_name(OrbitApp *a, const char *name) {
    if (!orbit_text_valid(name, 1, 48))
        return -1;
    LOCK(a);
    snprintf(a->name, sizeof(a->name), "%s", name);
    a->profile = 1;
    a->dirty = 1;
    a->changed++;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used)
            a->links[i].reauth = 1;
    UNLOCK(a);
    return 0;
}
int app_create_group(OrbitApp *a, const char *name) {
    return app_create_server(a, name, 0);
}
int app_create_server(OrbitApp *a, const char *name, int with_template) {
    if (!orbit_text_valid(name, 1, 64)) {
        app_notice(a, T(T_E_SERVER_NAME));
        return -1;
    }
    cJSON *q = command("create");
    cJSON_AddStringToObject(q, "name", name);
    /* default structure, named in this console's language (afterwards it is user content) */
    cJSON *cats = cJSON_AddArrayToObject(q, "categories"), *chs = cJSON_AddArrayToObject(q, "channels");
    struct {
        int key, type, cat;
    } tpl_small[] = {{T_CH_GENERAL, CH_TEXT, -1}, {T_CH_VOICE, CH_VOICE, -1}},
      tpl_full[] = {{T_CH_WELCOME, CH_TEXT, 0},     {T_CH_RULES, CH_TEXT, 0},
                    {T_CH_ANNOUNCE, CH_TEXT, 0},    {T_CH_GENERAL, CH_TEXT, 1},
                    {T_CH_GAMES, CH_TEXT, 1},       {T_CH_SCREENSHOTS, CH_TEXT, 1},
                    {T_CH_OFFTOPIC, CH_TEXT, 1},    {T_CH_VOICE, CH_VOICE, 2},
                    {T_CH_GAMING_ROOM, CH_VOICE, 2}, {T_CH_LOUNGE, CH_VOICE, 2}};
    if (with_template) {
        cJSON_AddItemToArray(cats, cJSON_CreateString(T(T_CAT_INFO)));
        cJSON_AddItemToArray(cats, cJSON_CreateString(T(T_CAT_COMMUNITY)));
        cJSON_AddItemToArray(cats, cJSON_CreateString(T(T_CAT_VOICE)));
    }
    int n = with_template ? 10 : 2;
    for (int i = 0; i < n; i++) {
        cJSON *x = cJSON_CreateObject();
        int key = with_template ? tpl_full[i].key : tpl_small[i].key;
        cJSON_AddStringToObject(x, "name", T(key));
        cJSON_AddNumberToObject(x, "type", with_template ? tpl_full[i].type : tpl_small[i].type);
        cJSON_AddNumberToObject(x, "cat", with_template ? tpl_full[i].cat : -1);
        cJSON_AddItemToArray(chs, x);
    }
    return queue(a, 0, q);
}
int app_send_reply(OrbitApp *a, const char *text, int reply) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    LOCK(a);
    int ch = a->selected_channel, online = a->links[link].state == LINK_ONLINE;
    if (!online) {
        /* offline: keep it on this PS3 and deliver it when the host is back */
        int ok = a->outbox_count < UI_OUTBOX && link_server(a, &a->links[link])[0];
        if (ok) {
            UiOutbox *o = &a->outbox[a->outbox_count];
            o->group = gid;
            o->channel = ch;
            o->time = orbit_unix_time();
            snprintf(o->text, sizeof(o->text), "%s", text);
            snprintf(a->outbox_server[a->outbox_count], 17, "%s", link_server(a, &a->links[link]));
            a->outbox_count++;
            a->sent_count++;
            a->dirty = 1;
            notice_locked(a, T(T_QUEUED));
        } else
            notice_locked(a, T(T_QUEUE_FULL));
        UNLOCK(a);
        return ok ? 0 : -1;
    }
    UNLOCK(a);
    cJSON *q = app_cmd("send", gid);
    cJSON_AddNumberToObject(q, "channel", ch);
    cJSON_AddStringToObject(q, "text", text);
    if (reply > 0)
        cJSON_AddNumberToObject(q, "reply", reply);
    return queue(a, link, q);
}
int app_send(OrbitApp *a, const char *text) {
    return app_send_reply(a, text, 0);
}
static int message_op(OrbitApp *a, const char *op, int id, const char *text, int reaction) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    cJSON *q = app_cmd(op, gid);
    cJSON_AddNumberToObject(q, "id", id);
    if (text)
        cJSON_AddStringToObject(q, "text", text);
    if (reaction >= 0)
        cJSON_AddNumberToObject(q, "r", reaction);
    return queue(a, link, q);
}
int app_edit(OrbitApp *a, int id, const char *text) {
    return message_op(a, "edit", id, text, -1);
}
int app_delete_message(OrbitApp *a, int id) {
    return message_op(a, "remove_msg", id, NULL, -1);
}
int app_react(OrbitApp *a, int id, int r) {
    return message_op(a, "react", id, NULL, r);
}
int app_search(OrbitApp *a, const char *query) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    LOCK(a);
    a->search_count = 0;
    a->search_state = 1;
    UNLOCK(a);
    cJSON *q = app_cmd("search", gid);
    cJSON_AddStringToObject(q, "q", query);
    return queue(a, link, q);
}
int app_invite(OrbitApp *a) {
    int link, gid;
    if (selected(a, &link, &gid)) {
        app_notice(a, T(T_CREATE_FIRST));
        return -1;
    }
    LOCK(a);
    a->invite_state = 1;
    a->invite_code[0] = 0;
    if (a->links[link].own && !a->nat.ok &&
        (a->net == NET_CHECKING || (a->bore && !a->bore_online))) {
        /* Wait for the router check / tunnel, otherwise the code would only work at home. */
        a->invite_wait = 1;
        a->invite_wait_since = orbit_millis();
        a->invite_wait_link = link;
        a->invite_wait_group = gid;
        a->changed++;
        UNLOCK(a);
        return 0;
    }
    UNLOCK(a);
    int rc = queue(a, link, app_cmd("invite", gid));
    if (rc) {
        LOCK(a);
        a->invite_state = 3;
        UNLOCK(a);
    }
    return rc;
}
int app_leave(OrbitApp *a) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    LOCK(a);
    UiGroup *g = find_group(&a->links[link], gid);
    int own = g && (g->perms == P_ALL) && a->links[link].own;
    UNLOCK(a);
    return queue(a, link, app_cmd(own ? "delete" : "leave", gid));
}
void app_retry(OrbitApp *a) {
    LOCK(a);
    a->nat_now = 1;
    for (int i = 0; i < APP_LINKS; i++)
        a->links[i].retry_at = 0;
    UNLOCK(a);
}
int app_join(OrbitApp *a, const char *text) {
    int slot = link_for_code(a, text, 0, 1);
    if (slot == -2) {
        app_notice(a, T(T_CODE_UNKNOWN));
        return -1;
    }
    if (slot == -1) {
        app_notice(a, T(T_TOO_MANY_LINKS));
        return -1;
    }
    app_notice(a, T(T_CONNECTING_FRIEND));
    return 0;
}
/* ---------- friends ---------- */
void app_friend_code(OrbitApp *a) {
    LOCK(a);
    update_friend_code_locked(a);
    UNLOCK(a);
    if (!a->home_code[0])
        bg_queue(a, 0, command("home"));
}
int app_friend_add(OrbitApp *a, const char *code) {
    int slot = link_for_code(a, code, 1, 0);
    if (slot == -2 || slot == 0) {
        app_notice(a, T(T_CODE_UNKNOWN));
        return -1;
    }
    if (slot == -3) {
        app_notice(a, T(T_FRIEND_SELF));
        return -1;
    }
    if (slot < 0) {
        app_notice(a, T(T_TOO_MANY_LINKS));
        return -1;
    }
    LOCK(a);
    a->links[slot].synced = 0;
    a->links[slot].retry_at = 0;
    notice_locked(a, T(T_FRIEND_SENDING));
    UNLOCK(a);
    return 0;
}
static const UiFriend *friend_at(OrbitApp *a, int index) {
    for (int i = 0; i < a->friend_count; i++)
        if (a->friends[i].i == index)
            return &a->friends[i];
    return NULL;
}
int app_friend_op(OrbitApp *a, int index, const char *op, int accept) {
    LOCK(a);
    const UiFriend *f = friend_at(a, index);
    char code[ORBIT_INVITE_TEXT] = "", pub[65] = "";
    if (f) {
        snprintf(code, sizeof(code), "%s", f->code);
        snprintf(pub, sizeof(pub), "%s", f->pub);
    }
    UNLOCK(a);
    if (!f)
        return -1;
    cJSON *q = command(op);
    cJSON_AddNumberToObject(q, "i", index);
    if (!strcmp(op, "friend_respond"))
        cJSON_AddBoolToObject(q, "accept", accept);
    int rc = queue(a, 0, q);
    if (!rc && !strcmp(op, "friend_respond") && accept && strlen(code) >= 20)
        link_for_code(a, code, 1, 0); /* tell their PS3: accepted */
    if (!rc && (!strcmp(op, "friend_remove") || !strcmp(op, "friend_block"))) {
        /* let their PS3 forget the friendship as well (blocking says nothing to them) */
        LOCK(a);
        for (int k = 1; k < APP_LINKS; k++) {
            AppLink *L = &a->links[k];
            if (L->used && pub[0] && !strcmp(L->owner_pub, pub)) {
                if (!strcmp(op, "friend_remove") && L->state == LINK_ONLINE) {
                    cJSON *x = command("friend");
                    cJSON_AddBoolToObject(x, "remove", 1);
                    bg_queue_locked(a, k, x);
                }
                L->friend_link = 0;
            }
        }
        UNLOCK(a);
    }
    return rc;
}
int app_friend_message(OrbitApp *a, int index) {
    LOCK(a);
    const UiFriend *f = friend_at(a, index);
    int user = f && f->state == FR_FRIENDS ? f->user : -2;
    char fid[ORBIT_ID_TEXT] = "";
    if (f)
        snprintf(fid, sizeof(fid), "%s", f->fid);
    /* an existing private chat with exactly this person, on either PS3 */
    for (int i = 0; i < APP_LINKS && fid[0]; i++) {
        AppLink *L = &a->links[i];
        if (!L->used || L->remove)
            continue;
        for (int g = 0; g < L->group_count; g++)
            if (L->groups[g].kind == GK_DM && L->groups[g].ndm == 1 &&
                !strcmp(L->groups[g].dm[0].oid, fid)) {
                int gid = L->groups[g].id;
                UNLOCK(a);
                app_open(a, i, gid, -1);
                return 0;
            }
    }
    UNLOCK(a);
    if (user == -2) {
        app_notice(a, T(T_NOT_FRIENDS_YET));
        return -1;
    }
    if (user < 0) {
        app_notice(a, T(T_FRIEND_NEVER_ONLINE));
        return -1;
    }
    cJSON *q = command("dm_create");
    cJSON *u = cJSON_AddArrayToObject(q, "users");
    cJSON_AddItemToArray(u, cJSON_CreateNumber(user));
    return queue(a, 0, q);
}
int app_group_dm(OrbitApp *a, const int *friends, int count) {
    cJSON *q = command("dm_create");
    cJSON *u = cJSON_AddArrayToObject(q, "users");
    LOCK(a);
    int n = 0;
    for (int k = 0; k < count; k++) {
        const UiFriend *f = friend_at(a, friends[k]);
        if (f && f->state == FR_FRIENDS && f->user >= 0) {
            cJSON_AddItemToArray(u, cJSON_CreateNumber(f->user));
            n++;
        }
    }
    UNLOCK(a);
    if (!n) {
        cJSON_Delete(q);
        app_notice(a, T(T_FRIEND_NEVER_ONLINE));
        return -1;
    }
    return queue(a, 0, q);
}
int app_friend_call(OrbitApp *a, int index) {
    if (app_friend_message(a, index))
        return -1;
    LOCK(a);
    int link = a->selected_link, gid = a->selected_group;
    UiGroup *g = gid >= 0 ? find_group(&a->links[link], gid) : NULL;
    int ready = g && g->kind == GK_DM;
    UNLOCK(a);
    if (!ready) {
        app_notice(a, T(T_CALL_AFTER_CHAT));
        return 0;
    }
    return app_call_join(a, link, gid, 0);
}
int app_friend_invite(OrbitApp *a, int index, int link, int gid) {
    LOCK(a);
    const UiFriend *f = friend_at(a, index);
    int user = f ? f->user : -1;
    int own = link == 0;
    UNLOCK(a);
    if (!own) {
        app_notice(a, T(T_INVITE_FOREIGN));
        return -1;
    }
    if (user < 0) {
        app_notice(a, T(T_FRIEND_NEVER_ONLINE));
        return -1;
    }
    cJSON *q = app_cmd("add_member", gid);
    cJSON_AddNumberToObject(q, "user", user);
    return queue(a, 0, q);
}
/* ---------- servers ---------- */
void app_want_members(OrbitApp *a, int link, int gid) {
    LOCK(a);
    for (int i = 0; i < APP_LINKS; i++)
        a->links[i].want_members = i == link ? gid : -1;
    UNLOCK(a);
}
void app_browse(OrbitApp *a) {
    LOCK(a);
    a->browse_count = 0;
    a->browse_state = 1;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && !a->links[i].remove && a->links[i].state == LINK_ONLINE)
            a->links[i].want_browse = 1;
    a->changed++;
    UNLOCK(a);
}
int app_join_public(OrbitApp *a, int index) {
    LOCK(a);
    int ok = index >= 0 && index < a->browse_count;
    UiBrowse b;
    if (ok)
        b = a->browse[index];
    UNLOCK(a);
    if (!ok)
        return -1;
    return queue(a, b.link, app_cmd("join_public", b.id));
}
void app_mute_chat(OrbitApp *a, int link, int gid, int muted) {
    LOCK(a);
    if (link >= 0 && link < APP_LINKS && gid >= 0 && gid < ORBIT_GROUPS) {
        a->chat_muted[link][gid] = (uint8_t)(muted != 0);
        a->dirty = 1;
        a->changed++;
        notice_locked(a, T(muted ? T_CHAT_MUTED : T_CHAT_UNMUTED));
    }
    UNLOCK(a);
}
/* ---------- profile, settings ---------- */
void app_set_profile(OrbitApp *a, const UiProfile *p) {
    LOCK(a);
    uint32_t created = a->me.created;
    a->me = *p;
    a->me.created = created;
    a->dirty = 1;
    a->changed++;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && a->links[i].state == LINK_ONLINE) {
            cJSON *q = command("profile");
            add_profile(q, &a->me);
            bg_queue_locked(a, i, q);
        }
    UNLOCK(a);
}
void app_set_settings(OrbitApp *a, const UiSettings *s) {
    LOCK(a);
    a->set = *s;
    a->dirty = 1;
    a->changed++;
    UNLOCK(a);
}
/* ---------- voice chat ---------- */
static void call_state_set(OrbitApp *a, int state) {
    LOCK(a);
    if (a->call_state != state) {
        a->call_state = state;
        a->changed++;
    }
    UNLOCK(a);
}
static void *call_main(void *arg) {
    OrbitApp *a = arg;
    char message[200];
    if (voice_device_start(message, sizeof(message))) {
        LOCK(a);
        a->call_running = 0;
        a->call_state = CALL_OFF;
        notice_locked(a, message);
        UNLOCK(a);
        return NULL;
    }
    LOCK(a);
    snprintf(a->call_note, sizeof(a->call_note), "%s", message);
    if (message[0])
        notice_locked(a, message);
    UNLOCK(a);
    OrbitLink *c = NULL;
    uint64_t retry = 0, ping = 0;
    uint8_t packet[VOICE_PACKET + 8];
    for (;;) {
        LOCK(a);
        int stop = a->stop || a->call_leave, link = a->call_link, gid = a->call_group,
            ch = a->call_channel;
        UNLOCK(a);
        if (stop)
            break;
        uint64_t now = orbit_millis();
        if (!c) {
            if (now < retry) {
                usleep(50000);
                continue;
            }
            c = link_open(a, &a->links[link], 1);
            if (c) {
                cJSON *q = app_cmd("voice", gid);
                if (ch >= 0)
                    cJSON_AddNumberToObject(q, "channel", ch);
                cJSON *r = orbit_exchange(c, q);
                cJSON_Delete(q);
                if (!r || !success(r)) {
                    if (r)
                        app_notice(a, error_text(r));
                    orbit_disconnect(c);
                    c = NULL;
                    if (r) { /* no permission: give up instead of retrying forever */
                        cJSON_Delete(r);
                        LOCK(a);
                        a->call_leave = 1;
                        UNLOCK(a);
                        continue;
                    }
                }
                cJSON_Delete(r);
            }
            if (!c) {
                call_state_set(a, CALL_RECONNECTING);
                retry = orbit_millis() + 2000;
                continue;
            }
            call_state_set(a, CALL_ONLINE);
            LOCK(a);
            a->links[link].retry_at = 0;
            /* local volumes survive a reconnect */
            for (int i = 0; i < a->call_count; i++)
                voice_set_gain(a->call_people[i].user,
                               a->call_people[i].local_muted ? 0 : a->call_people[i].volume);
            UNLOCK(a);
        }
        int bad = 0;
        while (!bad && voice_take_packet(packet))
            bad = orbit_frame_send(c, packet, VOICE_PACKET);
        if (!bad && now >= ping) {
            uint8_t p = VF_PING;
            bad = orbit_frame_send(c, &p, 1);
            ping = now + 2000;
        }
        for (int i = 0; i < 8 && !bad; i++) {
            int n = orbit_frame_recv(c, packet, sizeof(packet), i ? 0 : 8);
            if (n < 0)
                bad = 1;
            else if (!n)
                break;
            else
                voice_receive(packet, n);
        }
        if (bad) {
            orbit_disconnect(c);
            c = NULL;
            call_state_set(a, CALL_RECONNECTING);
            retry = orbit_millis() + 1000;
        }
    }
    orbit_disconnect(c);
    voice_device_stop();
    LOCK(a);
    a->call_running = 0;
    a->call_state = CALL_OFF;
    a->call_count = 0;
    a->call_note[0] = 0;
    a->changed++;
    UNLOCK(a);
    return NULL;
}
int app_mic_test(OrbitApp *a, int on) {
    LOCK(a);
    int call = a->call_running, active = a->mic_test;
    UNLOCK(a);
    if (on && !active) {
        if (!call) {
            char message[200];
            if (voice_device_start(message, sizeof(message))) {
                app_notice(a, message);
                return -1;
            }
            if (message[0])
                app_notice(a, message);
        }
        voice_set_loopback(1);
        LOCK(a);
        a->mic_test = 1;
        a->changed++;
        UNLOCK(a);
        app_notice_key(a, T_MIC_TEST_ON);
    } else if (!on && active) {
        voice_set_loopback(0);
        if (!call)
            voice_device_stop();
        LOCK(a);
        a->mic_test = 0;
        a->changed++;
        UNLOCK(a);
    }
    return 0;
}
int app_call_join(OrbitApp *a, int link, int gid, int ch) {
    app_mic_test(a, 0); /* the test owns the audio device: stop it before a call */
    LOCK(a);
    if (a->call_running) {
        int same = a->call_link == link && a->call_group == gid && a->call_channel == ch;
        a->call_leave = 1;
        notice_locked(a, T(T_CALL_LEFT));
        UNLOCK(a);
        if (same)
            return 0;
        /* switching channels: wait for the old call to end, then join the new one */
        for (int i = 0; i < 100; i++) {
            LOCK(a);
            int running = a->call_running;
            UNLOCK(a);
            if (!running)
                break;
            usleep(20000);
        }
        LOCK(a);
    }
    int joinable = a->call_joinable;
    a->call_joinable = 0;
    UNLOCK(a);
    if (joinable)
        pthread_join(a->call_thread, NULL); /* previous call has already ended */
    LOCK(a);
    if (link < 0 || link >= APP_LINKS || a->links[link].state != LINK_ONLINE) {
        notice_locked(a, T(T_HOST_OFFLINE));
        UNLOCK(a);
        return -1;
    }
    UiGroup *g = find_group(&a->links[link], gid);
    if (!g) {
        UNLOCK(a);
        return -1;
    }
    if (g->kind == GK_SERVER) {
        const UiChannel *h = find_channel(g, ch);
        if (!h || h->type != CH_VOICE)
            ch = first_channel(g, CH_VOICE);
        if (ch < 0) {
            notice_locked(a, T(T_NO_VOICE_CHANNEL));
            UNLOCK(a);
            return -1;
        }
    }
    a->call_link = link;
    a->call_group = gid;
    a->call_channel = ch;
    a->call_leave = 0;
    a->call_count = 0;
    a->call_state = CALL_CONNECTING;
    snprintf(a->call_name, sizeof(a->call_name), "%s", g->name);
    a->call_running = 1;
    voice_set_muted(0);
    int ok = !pthread_create(&a->call_thread, NULL, call_main, a);
    if (ok)
        a->call_joinable = 1;
    else {
        a->call_running = 0;
        a->call_state = CALL_OFF;
    }
    a->changed++;
    UNLOCK(a);
    return ok ? 0 : -1;
}
int app_call_toggle(OrbitApp *a) {
    LOCK(a);
    if (a->call_running) {
        a->call_leave = 1;
        notice_locked(a, T(T_CALL_LEFT));
        UNLOCK(a);
        return 0;
    }
    UNLOCK(a);
    int link, gid;
    if (selected(a, &link, &gid)) {
        app_notice(a, T(T_CREATE_FIRST));
        return -1;
    }
    LOCK(a);
    int ch = a->selected_channel;
    UNLOCK(a);
    return app_call_join(a, link, gid, ch);
}
void app_call_mute(OrbitApp *a) {
    LOCK(a);
    if (a->call_running) {
        voice_set_muted(!voice_muted());
        notice_locked(a, T(voice_muted() ? T_MIC_MUTED : T_MIC_ON));
    }
    UNLOCK(a);
}
void app_call_person(OrbitApp *a, int index, int local_muted, int volume) {
    LOCK(a);
    if (index >= 0 && index < a->call_count) {
        a->call_people[index].local_muted = local_muted;
        a->call_people[index].volume = volume < 0 ? 0 : volume > 200 ? 200 : volume;
        voice_set_gain(a->call_people[index].user, local_muted ? 0 : a->call_people[index].volume);
        a->changed++;
    }
    UNLOCK(a);
}
/* ---------- relay ("Vermittler") ---------- */
static void relay_restart(OrbitApp *a) {
    LOCK(a);
    OrbitRelayHost *old = a->relay_host;
    a->relay_host = NULL;
    char relay[104];
    snprintf(relay, sizeof(relay), "%s", a->relay);
    UNLOCK(a);
    orbit_relay_host_stop(old);
    OrbitRelayHost *h = NULL;
    if (relay[0] && a->store && a->server)
        h = orbit_relay_host_start(relay, a->server_id, a->server_key, a->store, a->port);
    /* The invitation carries the relay as IPv4: resolve a name once, here. */
    uint32_t rip = 0;
    int rport = ORBIT_RELAY_PORT;
    char rhost[ORBIT_HOST];
    if (relay[0] && !orbit_parse_endpoint(relay, rhost, &rport)) {
        if (!strchr(relay, ':'))
            rport = ORBIT_RELAY_PORT;
        rip = orbit_resolve(rhost);
    }
    LOCK(a);
    a->relay_ip = rport > 0 && rport <= 65535 ? rip : 0;
    a->relay_port = rport;
    a->relay_host = h;
    a->relay_online = -1;
    for (int i = 0; i < APP_LINKS; i++)
        a->links[i].retry_at = 0;
    a->changed++;
    UNLOCK(a);
}
void app_set_relay(OrbitApp *a, const char *relay, int user) {
    char host[ORBIT_HOST];
    int port;
    if (relay && relay[0] && orbit_parse_endpoint(relay, host, &port)) {
        app_notice(a, T(T_RELAY_INVALID));
        return;
    }
    LOCK(a);
    if (!user && a->relay_user) {
        UNLOCK(a);
        if (!a->relay_host)
            relay_restart(a);
        return; /* the user's own choice wins over the package default */
    }
    snprintf(a->relay, sizeof(a->relay), "%s", relay ? relay : "");
    if (user) {
        a->relay_user = 1;
        a->dirty = 1;
    }
    UNLOCK(a);
    relay_restart(a);
}
/* ---------- built-in internet tunnel ---------- */
void app_set_tunnel(OrbitApp *a, const char *server) {
    LOCK(a);
    OrbitBore *old = a->bore;
    a->bore = NULL;
    a->bore_online = a->bore_port = 0;
    UNLOCK(a);
    orbit_bore_stop(old);
    if (!server || !server[0] || !a->server)
        return;
    /* A fixed public port per console keeps invitation codes valid across restarts. */
    unsigned long h = strtoul((char[9]){a->server_id[0], a->server_id[1], a->server_id[2],
                                        a->server_id[3], a->server_id[4], a->server_id[5],
                                        a->server_id[6], a->server_id[7], 0},
                              NULL, 16);
    int want = 20000 + (int)(h % 40000);
    OrbitBore *b = orbit_bore_start(server, want, a->port);
    char host[ORBIT_HOST];
    int port;
    LOCK(a);
    a->bore = b;
    if (!orbit_parse_endpoint(server, host, &port))
        snprintf(a->bore_host, sizeof(a->bore_host), "%s", host);
    a->changed++;
    UNLOCK(a);
}
