/* Orbit 1.0 application core.
 * Every console runs its own meeting point in the background. Servers and private chats you
 * create live on your PS3; the ones you were invited to live on the inviting PS3. The app keeps
 * one encrypted link per meeting point (also to every friend's PS3), signs in automatically with
 * a per-console secret plus a signed public identity, and saves everything. */
#include "avatar.h"
#include "anim.h"
#include "gif.h"
#include "monocypher.h"
#include "notify_core.h"
#include "presence.h"
#include "update.h"
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
static void copy_pic(char out[33], const cJSON *x);
static void copy_anim(char out[33], const cJSON *x);
static void upl_start_locked(OrbitApp *a, int link, const char *hash, cJSON *then);
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
/* ---------- ORBIT NEXT: persistent public community directory ----------
 * Every answer of a host to "browse" is merged here and saved to data/directory.json.
 * A host going offline does not remove its communities: they stay listed with "host offline"
 * and the last time they were confirmed. Entries unseen for 30 days are dropped. */
static void dir_save_locked(OrbitApp *a);
static int dir_live_link(const OrbitApp *a, const char *server) {
    if (!server || strlen(server) != 16)
        return -1;
    for (int i = 0; i < APP_LINKS; i++) {
        const AppLink *L = &a->links[i];
        if (L->used && !L->remove && L->state == LINK_ONLINE && !strcmp(link_server(a, L), server))
            return i;
    }
    return -1;
}
static void dir_merge_locked(OrbitApp *a, const char *server, const cJSON *servers) {
    if (!server || strlen(server) != 16 || !cJSON_IsArray(servers))
        return;
    uint32_t now = (uint32_t)time(NULL);
    /* the host's answer is complete: its entries that are missing now are no longer public */
    int keep = 0;
    for (int i = 0; i < a->dir_count; i++) {
        AppDirEntry *e = &a->dir[i];
        int listed = 0;
        const cJSON *x;
        if (!strcmp(e->server, server))
            cJSON_ArrayForEach(x, servers) listed |= num(x, "id", -1) == e->id;
        if (strcmp(e->server, server) || listed)
            a->dir[keep++] = *e;
    }
    a->dir_count = keep;
    const cJSON *x;
    int taken = 0;
    cJSON_ArrayForEach(x, servers) {
        int id = num(x, "id", -1);
        if (id < 0 || id >= ORBIT_GROUPS || ++taken > ORBIT_GROUPS)
            continue;
        AppDirEntry *e = NULL;
        for (int i = 0; i < a->dir_count && !e; i++)
            if (a->dir[i].id == id && !strcmp(a->dir[i].server, server))
                e = &a->dir[i];
        if (!e) {
            if (a->dir_count < APP_DIR)
                e = &a->dir[a->dir_count++];
            else { /* full: replace the entry that was confirmed longest ago */
                e = &a->dir[0];
                for (int i = 1; i < a->dir_count; i++)
                    if (a->dir[i].seen < e->seen)
                        e = &a->dir[i];
            }
            memset(e, 0, sizeof(*e));
        }
        e->id = id;
        snprintf(e->server, sizeof(e->server), "%s", server);
        e->members = num(x, "members", 0);
        e->online = num(x, "online", -1);
        e->icon = num(x, "icon", 0);
        e->color = num(x, "color", 0);
        copy_pic(e->pic, x);
        copy_anim(e->anim, x);
        e->member = cJSON_IsTrue(cJSON_GetObjectItem(x, "member"));
        e->seen = now;
        copy(e->name, sizeof(e->name), x, "name");
        copy(e->desc, sizeof(e->desc), x, "desc");
        copy(e->host, sizeof(e->host), x, "host");
        if (!e->name[0])
            snprintf(e->name, sizeof(e->name), "?");
    }
    dir_save_locked(a);
}
static int dir_cmp(const UiBrowse *x, const UiBrowse *y, int sort) {
    if (sort == DIR_S_NAME) {
        int c = strcasecmp(x->name, y->name);
        if (c)
            return c;
    } else if (sort == DIR_S_SEEN && x->seen != y->seen)
        return x->seen > y->seen ? -1 : 1;
    if (x->live != y->live)
        return y->live - x->live;
    if (x->online != y->online)
        return y->online - x->online;
    if (x->members != y->members)
        return y->members - x->members;
    return strcmp(x->name, y->name);
}
/* ASCII case-insensitive substring search (UTF-8 bytes beyond ASCII compare exactly) */
static int dir_has(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!n)
        return 1;
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n))
            return 1;
    return 0;
}
/* reachable now: through a connected host, or announced to the public directory recently */
static int dir_entry_live(const OrbitApp *a, const AppDirEntry *e, uint32_t now) {
    return dir_live_link(a, e->server) >= 0 ||
           (e->central && e->invite[0] && now >= e->seen && now - e->seen <= DIR_LIVE);
}
static int dir_build_locked(OrbitApp *a, UiBrowse *out, int cap) {
    int n = 0;
    uint32_t now = (uint32_t)time(NULL);
    a->dir_total = 0;
    for (int i = 0; i < a->dir_count && n < cap; i++) {
        const AppDirEntry *e = &a->dir[i];
        if (e->hidden || (now > e->seen && now - e->seen > 30u * 86400u))
            continue;
        a->dir_total++;
        int live = dir_entry_live(a, e, now);
        if ((a->dir_filter == DIR_F_LIVE && !live) || (a->dir_filter == DIR_F_JOINED && !e->member) ||
            (a->dir_filter == DIR_F_NEW && e->member))
            continue;
        if (a->dir_query[0] && !dir_has(e->name, a->dir_query) && !dir_has(e->desc, a->dir_query) &&
            !dir_has(e->host, a->dir_query))
            continue;
        UiBrowse *b = &out[n];
        memset(b, 0, sizeof(*b));
        b->link = dir_live_link(a, e->server);
        b->live = live;
        b->central = e->central;
        snprintf(b->invite, sizeof(b->invite), "%s", e->invite);
        b->id = e->id;
        b->members = e->members;
        b->online = b->live ? e->online : 0;
        b->icon = e->icon;
        b->color = e->color;
        snprintf(b->pic, sizeof(b->pic), "%s", e->pic);
        snprintf(b->anim, sizeof(b->anim), "%s", e->anim);
        b->member = e->member;
        b->seen = e->seen;
        snprintf(b->name, sizeof(b->name), "%s", e->name);
        snprintf(b->desc, sizeof(b->desc), "%s", e->desc);
        snprintf(b->host, sizeof(b->host), "%s", e->host);
        snprintf(b->server, sizeof(b->server), "%s", e->server);
        /* insertion sort: lists are small (<= 64) */
        int k = n++;
        while (k > 0 && dir_cmp(b, &out[k - 1], a->dir_sort) < 0) {
            UiBrowse t = out[k - 1];
            out[k - 1] = *b;
            out[k] = t;
            b = &out[--k];
        }
    }
    return n;
}
static void dir_save_locked(OrbitApp *a) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON *list = cJSON_AddArrayToObject(root, "communities");
    for (int i = 0; i < a->dir_count; i++) {
        const AppDirEntry *e = &a->dir[i];
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "server", e->server);
        cJSON_AddNumberToObject(x, "id", e->id);
        cJSON_AddStringToObject(x, "name", e->name);
        cJSON_AddStringToObject(x, "desc", e->desc);
        cJSON_AddStringToObject(x, "host", e->host);
        cJSON_AddNumberToObject(x, "members", e->members);
        cJSON_AddNumberToObject(x, "online", e->online);
        cJSON_AddNumberToObject(x, "icon", e->icon);
        cJSON_AddNumberToObject(x, "color", e->color);
        cJSON_AddStringToObject(x, "pic", e->pic);
        cJSON_AddStringToObject(x, "anim", e->anim);
        cJSON_AddBoolToObject(x, "member", e->member);
        cJSON_AddNumberToObject(x, "seen", (double)e->seen);
        if (e->hidden)
            cJSON_AddBoolToObject(x, "hidden", 1);
        if (e->reported)
            cJSON_AddNumberToObject(x, "reported", e->reported);
        if (e->report_sent)
            cJSON_AddBoolToObject(x, "report_sent", 1);
        if (e->central)
            cJSON_AddBoolToObject(x, "central", 1);
        if (e->invite[0])
            cJSON_AddStringToObject(x, "invite", e->invite);
        cJSON_AddItemToArray(list, x);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    char path[512];
    snprintf(path, sizeof(path), "%s/directory.json", a->data_dir);
    if (text)
        write_atomic(path, text);
    free(text);
}
static void dir_load(OrbitApp *a) {
    char path[512];
    snprintf(path, sizeof(path), "%s/directory.json", a->data_dir);
    char *text = read_file(path, 1 << 20);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    a->dir_count = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "communities")) {
        if (a->dir_count >= APP_DIR)
            break;
        AppDirEntry *e = &a->dir[a->dir_count];
        memset(e, 0, sizeof(*e));
        copy(e->server, sizeof(e->server), x, "server");
        e->id = num(x, "id", -1);
        if (strlen(e->server) != 16 || e->id < 0 || e->id >= ORBIT_GROUPS)
            continue;
        copy(e->name, sizeof(e->name), x, "name");
        copy(e->desc, sizeof(e->desc), x, "desc");
        copy(e->host, sizeof(e->host), x, "host");
        e->members = num(x, "members", 0);
        e->online = num(x, "online", -1);
        e->icon = num(x, "icon", 0);
        e->color = num(x, "color", 0);
        copy_pic(e->pic, x);
        copy_anim(e->anim, x);
        e->member = cJSON_IsTrue(cJSON_GetObjectItem(x, "member"));
        const cJSON *seen = cJSON_GetObjectItem(x, "seen");
        e->seen = cJSON_IsNumber(seen) && seen->valuedouble > 0 ? (uint32_t)seen->valuedouble : 0;
        e->hidden = cJSON_IsTrue(cJSON_GetObjectItem(x, "hidden"));
        e->reported = num(x, "reported", 0);
        if (e->reported < 0 || e->reported > RR_COUNT)
            e->reported = 0;
        e->report_sent = cJSON_IsTrue(cJSON_GetObjectItem(x, "report_sent"));
        e->central = cJSON_IsTrue(cJSON_GetObjectItem(x, "central"));
        if (orbit_text_valid(orbit_string(x, "invite"), 1, ORBIT_INVITE_TEXT - 1))
            copy(e->invite, sizeof(e->invite), x, "invite");
        a->dir_count++;
    }
    cJSON_Delete(root);
}
/* PS3 rename does not replace an existing file (see save_text): move the old one aside first,
 * put it back if the new one cannot take its place. */
static int replace_file(const char *tmp, const char *path) {
    char bak[600];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    remove(bak);
    int moved = rename(path, bak) == 0;
    if (rename(tmp, path)) {
        if (moved)
            rename(bak, path);
        remove(tmp);
        return -1;
    }
    remove(bak);
    return 0;
}
/* 1.1: what the XMB plugin needs (notify_core.h). Written atomically next to the profile. */
static void notify_template(FILE *f, const char *key, int text) {
    char t[NOTIFY_TEMPLATE];
    snprintf(t, sizeof(t), "%s", T(text));
    for (char *c = t; *c; c++)
        if (*c == '\t' || *c == '\n' || *c == '\r')
            *c = ' ';
    fprintf(f, "%s\t%s\n", key, t);
}
static void write_notify_locked(OrbitApp *a) {
    char path[512], tmp[520];
    snprintf(path, sizeof(path), "%s/notify.cfg", a->data_dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "ORBITNOTIFY\t1\non\t%d\ndm\t%d\nserver\t%d\npreview\t%d\nbusy\t%d\n", a->set.bg != 0,
            (a->set.notify >> N_DM) & 1, (a->set.notify >> N_GROUP) & 1, a->set.bg_preview != 0,
            a->me.status == ST_BUSY || a->set.dnd); /* "busy" mutes the XMB pop-ups */
    notify_template(f, "t_dm", T_NTF_DM);
    notify_template(f, "t_server", T_NTF_SERVER);
    notify_template(f, "t_from", T_NTF_FROM);
    notify_template(f, "t_many_from", T_NTF_MANY_FROM);
    notify_template(f, "t_many", T_NTF_MANY);
    int hosts = 0;
    for (int i = 0; i < APP_LINKS && hosts < NOTIFY_HOSTS; i++) {
        const AppLink *L = &a->links[i];
        if (!L->used || L->remove || L->own || !L->wid[0] || strlen(L->server) != 16)
            continue;
        char tag[17], relay[ORBIT_HOST] = "0";
        orbit_group_tag(tag, L->code);
        int rport = 0;
        /* relay fallback when the tunnel port changed: the link's own relay if it is an IPv4
         * address (the plugin has no DNS), else the resolved global one */
        char rhost[ORBIT_HOST];
        int rp = ORBIT_RELAY_PORT;
        struct in_addr probe;
        if (L->relay[0] && !orbit_parse_endpoint(L->relay, rhost, &rp) &&
            inet_pton(AF_INET, rhost, &probe) == 1) {
            snprintf(relay, sizeof(relay), "%s", rhost);
            rport = strchr(L->relay, ':') ? rp : ORBIT_RELAY_PORT;
        } else if (a->relay_ip && (!L->relay[0] || !strcmp(L->relay, a->relay))) {
            uint32_t r = a->relay_ip;
            snprintf(relay, sizeof(relay), "%u.%u.%u.%u", r >> 24, r >> 16 & 255, r >> 8 & 255, r & 255);
            rport = a->relay_port;
        }
        fprintf(f, "host\t%s\t%d\t%s\t%d\t%s\t%s\t%s\t%s\n", L->host, L->port, relay, rport, tag,
                L->wid, L->wkey, L->server);
        hosts++;
        for (int g = 0; g < L->group_count; g++)
            if (L->groups[g].id >= 0 && L->groups[g].id < ORBIT_GROUPS && a->chat_muted[i][L->groups[g].id])
                fprintf(f, "mute\t%s\t%d\n", L->server, L->groups[g].id);
    }
    if (fclose(f) == 0)
        replace_file(tmp, path);
    /* 1.2: the presence plugin's switch (presence.h) */
    snprintf(path, sizeof(path), "%s/presence.cfg", a->data_dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (f) {
        fprintf(f, "ORBITPRESENCE\t1\nshare\t%d\n", a->set.presence != 0);
        if (fclose(f) == 0)
            replace_file(tmp, path);
    }
    else
        remove(tmp);
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
    cJSON_AddStringToObject(me, "pic", a->me.pic);
    cJSON_AddStringToObject(me, "anim", a->me.anim);
    cJSON *st = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(st, "anim_off", a->set.anim_off);
    cJSON_AddNumberToObject(st, "reduce_motion", a->set.reduce_motion);
    cJSON_AddNumberToObject(st, "lang_auto", a->set.lang_auto);
    cJSON_AddNumberToObject(st, "lang", a->set.lang);
    cJSON_AddNumberToObject(st, "theme", a->set.theme);
    cJSON_AddNumberToObject(st, "notify", a->set.notify);
    cJSON_AddNumberToObject(st, "bg", a->set.bg);
    cJSON_AddNumberToObject(st, "bg_preview", a->set.bg_preview);
    cJSON_AddNumberToObject(st, "presence", a->set.presence);
    cJSON_AddNumberToObject(st, "dir_publish", a->set.dir_publish);
    cJSON_AddNumberToObject(st, "ds_audio", a->set.ds_audio);
    cJSON_AddNumberToObject(st, "ds_mic", a->set.ds_mic);
    cJSON_AddNumberToObject(st, "sharp", a->set.sharp);
    cJSON_AddNumberToObject(st, "sounds", a->set.sounds);
    cJSON_AddNumberToObject(st, "suggest", a->set.suggest);
    cJSON_AddNumberToObject(st, "dnd", a->set.dnd);
    cJSON_AddNumberToObject(st, "update_auto", a->set.update_auto);
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
        cJSON_AddStringToObject(r, "wid", L->wid);
        cJSON_AddStringToObject(r, "wkey", L->wkey);
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
    write_notify_locked(a);
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
        if (avatar_hash_valid(orbit_string(me, "pic")))
            snprintf(a->me.pic, sizeof(a->me.pic), "%s", orbit_string(me, "pic"));
        if (a->me.pic[0] && avatar_hash_valid(orbit_string(me, "anim")))
            snprintf(a->me.anim, sizeof(a->me.anim), "%s", orbit_string(me, "anim"));
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
        a->set.bg = num(st, "bg", 1) != 0;
        a->set.bg_preview = num(st, "bg_preview", 1) != 0;
        a->set.presence = num(st, "presence", 0) != 0;
        a->set.dir_publish = num(st, "dir_publish", 0) != 0;
        a->set.sharp = num(st, "sharp", 0) != 0;
        a->set.sounds = num(st, "sounds", 60);
        a->set.suggest = num(st, "suggest", 1) != 0;
        a->set.dnd = num(st, "dnd", 0) != 0;
        a->set.update_auto = num(st, "update_auto", 0) != 0;
        if (a->set.sounds < 0 || a->set.sounds > 100)
            a->set.sounds = 60;
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
        if (strlen(orbit_string(r, "wid")) == 16 && strlen(orbit_string(r, "wkey")) == 64) {
            copy(L->wid, sizeof(L->wid), r, "wid");
            copy(L->wkey, sizeof(L->wkey), r, "wkey");
        }
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
static void add_pic(cJSON *q, const UiProfile *me, const char *data_dir) {
    char dir[420];
    snprintf(dir, sizeof(dir), "%s/pics", data_dir);
    uint8_t *blob = malloc(AVATAR_BLOB_MAX);
    char *text = malloc(AVATAR_B64_MAX);
    size_t n = 0;
    if (!me->pic[0])
        cJSON_AddStringToObject(q, "pic", "");
    else if (blob && text && !avatar_load(dir, me->pic, blob, &n) &&
             !avatar_b64_encode(text, AVATAR_B64_MAX, blob, n)) {
        cJSON_AddStringToObject(q, "pic", me->pic);
        cJSON_AddStringToObject(q, "pic_data", text);
        cJSON_AddStringToObject(q, "anim", me->anim);
    }
    free(blob);
    free(text);
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
    add_pic(q, &me, a->data_dir);
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
    if (a->me.anim[0]) { /* 1.2: upload the animation if this host does not have it yet */
        cJSON *q = command("profile");
        add_profile(q, &a->me);
        add_pic(q, &a->me, a->data_dir);
        upl_start_locked(a, (int)(L - a->links), a->me.anim, q);
    }
    UNLOCK(a);
    if (proto >= 11 && !voice && !L->own) {
        LOCK(a);
        int have = L->wid[0] != 0;
        UNLOCK(a);
        if (!have) {
            cJSON *wq = command("watch_key");
            cJSON *wr = orbit_exchange(c, wq);
            cJSON_Delete(wq);
            if (wr && success(wr) && strlen(orbit_string(wr, "wid")) == 16 &&
                strlen(orbit_string(wr, "wkey")) == 64) {
                LOCK(a);
                copy(L->wid, sizeof(L->wid), wr, "wid");
                copy(L->wkey, sizeof(L->wkey), wr, "wkey");
                a->dirty = 1;
                UNLOCK(a);
            }
            cJSON *wk = cJSON_GetObjectItem(wr, "wkey");
            if (cJSON_IsString(wk))
                crypto_wipe(wk->valuestring, strlen(wk->valuestring));
            cJSON_Delete(wr);
        }
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
static void copy_pic(char out[33], const cJSON *x) {
    const char *h = orbit_string(x, "pic");
    snprintf(out, 33, "%s", avatar_hash_valid(h) ? h : "");
}
static void copy_anim(char out[33], const cJSON *x) {
    const char *h = orbit_string(x, "anim");
    snprintf(out, 33, "%s", avatar_hash_valid(h) && avatar_hash_valid(orbit_string(x, "pic")) ? h : "");
}
static void parse_person(UiPerson *p, const cJSON *x) {
    p->user = num(x, "user", -1);
    p->status = num(x, "status", ST_OFFLINE);
    p->color = num(x, "color", 0);
    p->avatar = num(x, "avatar", 0);
    copy_pic(p->pic, x);
    copy_anim(p->anim, x);
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
        copy_pic(x->pic, g);
        copy_anim(x->anim, g);
        x->open = cJSON_IsTrue(cJSON_GetObjectItem(g, "open"));
        x->perms = num(g, "perms", cJSON_IsTrue(cJSON_GetObjectItem(g, "owner")) ? P_ALL : P_SEND | P_INVITE);
        x->myroles = num(g, "myroles", 0);
        x->smuted = cJSON_IsTrue(cJSON_GetObjectItem(g, "smuted"));
        x->reports = num(g, "reports", 0); /* 1.2: open reports, only sent to moderators */
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
    copy_pic(u->pic, m);
    copy_anim(u->anim, m);
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
        copy_pic(f->pic, x);
        copy_anim(f->anim, x);
        f->since = unum(x, "since");
        f->created = unum(x, "created");
        copy(f->name, sizeof(f->name), x, "name");
        copy(f->fid, sizeof(f->fid), x, "fid");
        copy(f->code, sizeof(f->code), x, "code");
        copy(f->custom, sizeof(f->custom), x, "custom");
        copy(f->bio, sizeof(f->bio), x, "bio");
        copy(f->game, sizeof(f->game), x, "game");
        copy(f->pub, sizeof(f->pub), x, "pub");
        const cJSON *pl = cJSON_GetObjectItem(x, "playing");
        f->playing = cJSON_IsObject(pl);
        f->playing_id[0] = f->playing_name[0] = 0;
        if (f->playing) {
            if (presence_id_valid(orbit_string(pl, "id")))
                copy(f->playing_id, sizeof(f->playing_id), pl, "id");
            if (orbit_text_valid(orbit_string(pl, "name"), 1, PRESENCE_NAME))
                copy(f->playing_name, sizeof(f->playing_name), pl, "name");
        }
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
static int index_of(OrbitApp *a, AppLink *L) {
    return (int)(L - a->links);
}
/* ---------- 1.2 animations in pieces (anim.h) ---------- */
static cJSON *piece_cmd(const char *data_dir, const char *hash, size_t off, int probe) {
    char dir[420];
    snprintf(dir, sizeof(dir), "%s/pics", data_dir);
    uint8_t *blob;
    size_t n;
    if (anim_load(dir, hash, &blob, &n) || off >= n) {
        if (!anim_load(dir, hash, &blob, &n)) /* off past the end */
            free(blob);
        return NULL;
    }
    cJSON *q = command("blob_put");
    cJSON_AddStringToObject(q, "hash", hash);
    cJSON_AddNumberToObject(q, "offset", (double)off);
    cJSON_AddNumberToObject(q, "total", (double)n);
    if (probe)
        cJSON_AddBoolToObject(q, "probe", 1);
    else {
        size_t k = n - off < ANIM_PIECE ? n - off : ANIM_PIECE;
        char *text = malloc(ANIM_PIECE / 3 * 4 + 8);
        if (!text || avatar_b64_encode(text, ANIM_PIECE / 3 * 4 + 8, blob + off, k)) {
            free(text);
            free(blob);
            cJSON_Delete(q);
            return NULL;
        }
        cJSON_AddStringToObject(q, "data", text);
        free(text);
    }
    free(blob);
    return q;
}
/* make sure the host behind link has the animation, then send `then` (taken over) */
static void upl_start_locked(OrbitApp *a, int link, const char *hash, cJSON *then) {
    int slot = -1;
    for (int i = 0; i < 4; i++)
        if (a->upl[i].then && a->upl[i].link == link && !strcmp(a->upl[i].hash, hash))
            slot = i;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (!a->upl[i].then)
            slot = i;
    if (slot < 0)
        slot = 0;
    cJSON_Delete(a->upl[slot].then);
    a->upl[slot].link = link;
    snprintf(a->upl[slot].hash, sizeof(a->upl[slot].hash), "%s", hash);
    a->upl[slot].then = then;
    cJSON *q = piece_cmd(a->data_dir, hash, 0, 1);
    if (q)
        bg_queue_locked(a, link, q);
    else { /* the animation file is gone: send the rest without it */
        a->upl[slot].then = NULL;
        bg_queue_locked(a, link, then);
    }
}
static void piece_reply(OrbitApp *a, int link, const cJSON *job, const cJSON *r) {
    const char *h = orbit_string(job, "hash");
    size_t total = (size_t)num(job, "total", 0), have = r && success(r) ? (size_t)num(r, "have", 0) : 0;
    LOCK(a);
    int slot = -1;
    for (int i = 0; i < 4; i++)
        if (a->upl[i].then && a->upl[i].link == link && !strcmp(a->upl[i].hash, h))
            slot = i;
    if (slot >= 0) {
        if (!r || !success(r)) { /* refused: the still picture still goes out */
            bg_queue_locked(a, link, a->upl[slot].then);
            a->upl[slot].then = NULL;
            if (a->import_state == IMPORT_SENDING) {
                a->import_state = IMPORT_FAILED;
                a->import_err = IMG_E_LIMIT;
            }
        } else if (have >= total) {
            bg_queue_locked(a, link, a->upl[slot].then);
            a->upl[slot].then = NULL;
            if (a->import_state == IMPORT_SENDING)
                a->import_state = IMPORT_DONE;
            a->changed++;
        } else {
            cJSON *q = piece_cmd(a->data_dir, h, have, 0);
            if (q)
                bg_queue_locked(a, link, q);
        }
    }
    UNLOCK(a);
}
static void dl_reset_locked(OrbitApp *a) {
    free(a->dl_buf);
    a->dl_buf = NULL;
    a->dl_hash[0] = 0;
    a->dl_n = a->dl_total = 0;
}
static void anim_piece_reply(OrbitApp *a, int link, const cJSON *job, const cJSON *r) {
    const char *h = orbit_string(job, "hash");
    size_t off = (size_t)num(job, "offset", 0);
    uint8_t *done = NULL;
    size_t done_n = 0;
    char hash[33] = "";
    LOCK(a);
    if (!strcmp(a->dl_hash, h) && a->dl_buf && off == a->dl_n && r && success(r)) {
        size_t total = (size_t)num(r, "total", 0), got = 0;
        if (total < 16 || total > ANIM_BLOB_MAX || (a->dl_total && total != a->dl_total) ||
            avatar_b64_decode(a->dl_buf + a->dl_n, total - a->dl_n, &got, orbit_string(r, "data")) || !got)
            dl_reset_locked(a);
        else {
            a->dl_total = total;
            a->dl_n += got;
            if (a->dl_n < total) {
                cJSON *q = command("pic");
                cJSON_AddStringToObject(q, "hash", h);
                cJSON_AddStringToObject(q, "kind", "ani");
                cJSON_AddNumberToObject(q, "offset", (double)a->dl_n);
                bg_queue_locked(a, link, q);
            } else { /* complete: check outside the lock */
                done = a->dl_buf;
                done_n = a->dl_n;
                snprintf(hash, sizeof(hash), "%s", a->dl_hash);
                a->dl_buf = NULL;
                dl_reset_locked(a);
            }
        }
    } else if (!strcmp(a->dl_hash, h))
        dl_reset_locked(a);
    UNLOCK(a);
    if (!done)
        return;
    OrbitAnim an;
    char dir[420];
    snprintf(dir, sizeof(dir), "%s/pics", a->data_dir);
    int good = !anim_decode(done, done_n, hash, &an);
    if (good) {
        anim_free(&an);
        good = !anim_save(dir, hash, done, done_n);
    }
    free(done);
    if (good) {
        LOCK(a);
        a->pic_gen++;
        a->changed++;
        UNLOCK(a);
    }
}
static void pic_reply(OrbitApp *a, int link, const cJSON *job, const cJSON *r) {
    const char *h = orbit_string(job, "hash");
    uint8_t *blob = malloc(AVATAR_BLOB_MAX);
    size_t n = 0;
    OrbitImage img;
    int good = blob && success(r) && avatar_hash_valid(h) &&
               !avatar_b64_decode(blob, AVATAR_BLOB_MAX, &n, orbit_string(r, "data")) &&
               !avatar_decode(blob, n, h, &img);
    if (good) {
        image_free(&img);
        char dir[420];
        snprintf(dir, sizeof(dir), "%s/pics", a->data_dir);
        good = !avatar_save(dir, h, blob, n);
    }
    free(blob);
    LOCK(a);
    if (good) {
        a->pic_gen++;
        a->changed++;
    } else /* ask the next meeting point that is online */
        for (int i = link + 1; i < APP_LINKS; i++)
            if (a->links[i].used && a->links[i].state == LINK_ONLINE) {
                cJSON *q = command("pic");
                cJSON_AddStringToObject(q, "hash", h);
                bg_queue_locked(a, i, q);
                break;
            }
    UNLOCK(a);
}
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
    if (!strcmp(op, "pic") && !strcmp(orbit_string(job, "kind"), "ani")) { /* 1.2 animation piece */
        anim_piece_reply(a, index_of(a, L), job, r);
        cJSON_Delete(r);
        return 0;
    }
    if (!strcmp(op, "pic")) { /* 1.2: never a notice; a failure tries the next meeting point */
        pic_reply(a, index_of(a, L), job, r);
        cJSON_Delete(r);
        return 0;
    }
    if (!strcmp(op, "blob_put")) { /* 1.2: next piece, or the request that uses the animation */
        piece_reply(a, index_of(a, L), job, r);
        cJSON_Delete(r);
        return 0;
    }
    LOCK(a);
    if (!success(r)) {
        notice_locked(a, error_text(r));
        if (!strcmp(op, "invite"))
            a->invite_state = 3;
        if (!strcmp(op, "search"))
            a->search_state = 3;
        if (!strcmp(op, "reports"))
            a->report_state = 3;
        if (!strcmp(op, "audit"))
            a->audit_state = 3;
    } else if (!strcmp(op, "reports")) { /* 1.2 moderation list */
        a->report_count = 0;
        a->report_group = num(job, "group", -1);
        const cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "reports")) {
            if (a->report_count >= UI_REPORTS)
                break;
            UiReport *e = &a->reports[a->report_count++];
            memset(e, 0, sizeof(*e));
            e->id = num(x, "id", 0);
            e->user = num(x, "user", -1);
            e->reason = num(x, "reason", RR_OTHER);
            if (e->reason < 0 || e->reason >= RR_COUNT)
                e->reason = RR_OTHER;
            e->state = num(x, "state", REPORT_OPEN);
            e->banned = cJSON_IsTrue(cJSON_GetObjectItem(x, "banned"));
            e->msg = unum(x, "msg");
            e->time = unum(x, "time");
            copy(e->target, sizeof(e->target), x, "target");
            copy(e->reporter, sizeof(e->reporter), x, "reporter");
            copy(e->text, sizeof(e->text), x, "text");
            copy(e->detail, sizeof(e->detail), x, "detail");
        }
        a->report_state = 2;
        a->changed++;
    } else if (!strcmp(op, "audit")) { /* 1.2 server appearance change log */
        a->audit_count = 0;
        const cJSON *x;
        cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "audit")) {
            if (a->audit_count >= UI_AUDIT)
                break;
            UiAudit *e = &a->audit[a->audit_count++];
            e->what = num(x, "what", 0);
            if (e->what < 0 || e->what >= AUD_COUNT)
                e->what = 0;
            e->time = unum(x, "time");
            copy(e->user, sizeof(e->user), x, "user");
        }
        a->audit_state = 2;
        a->changed++;
    } else if (!strcmp(op, "report")) {
        notice_locked(a, T(T_REPORT_SENT));
    } else if (!strcmp(op, "report_resolve")) {
        int id = num(job, "id", 0), st = num(job, "state", REPORT_DONE);
        for (int i = 0; i < a->report_count; i++)
            if (a->reports[i].id == id)
                a->reports[i].state = st;
        notice_locked(a, T(T_MOD_DONE));
        a->changed++;
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
            copy_pic(m->pic, x);
            copy_anim(m->anim, x);
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
        dir_merge_locked(a, link_server(a, L), cJSON_GetObjectItem(r, "servers"));
        a->browse_state = BROWSE_DONE;
        a->changed++;
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
        int renew = c && L->watch_renew;
        if (renew)
            L->watch_renew = 0;
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
        if (renew) {
            /* 1.1: new background-notification key; the host locks the old one out */
            cJSON *q = command("watch_key");
            cJSON_AddNumberToObject(q, "renew", 1);
            cJSON *r = orbit_exchange(c, q);
            cJSON_Delete(q);
            if (!r)
                broken = 1;
            else if (success(r) && strlen(orbit_string(r, "wid")) == 16 &&
                     strlen(orbit_string(r, "wkey")) == 64) {
                LOCK(a);
                copy(L->wid, sizeof(L->wid), r, "wid");
                copy(L->wkey, sizeof(L->wkey), r, "wkey");
                a->dirty = 1;
                UNLOCK(a);
            }
            cJSON *wk = cJSON_GetObjectItem(r, "wkey");
            if (cJSON_IsString(wk))
                crypto_wipe(wk->valuestring, strlen(wk->valuestring));
            cJSON_Delete(r);
        }
        if (!broken && reauth) {
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
/* ---------- 1.2 public community directory (directory.h) ---------- */
static void dir_merge_central_locked(OrbitApp *a, const DirCommunity *c) {
    AppDirEntry *e = NULL;
    for (int i = 0; i < a->dir_count && !e; i++)
        if (a->dir[i].id == c->id && !strcmp(a->dir[i].server, c->server))
            e = &a->dir[i];
    if (!e) {
        if (a->dir_count < APP_DIR)
            e = &a->dir[a->dir_count++];
        else {
            e = &a->dir[0];
            for (int i = 1; i < a->dir_count; i++)
                if (a->dir[i].seen < e->seen)
                    e = &a->dir[i];
        }
        memset(e, 0, sizeof(*e));
        e->id = c->id;
        snprintf(e->server, sizeof(e->server), "%s", c->server);
    }
    e->central = 1;
    snprintf(e->invite, sizeof(e->invite), "%s", c->invite);
    if (dir_live_link(a, e->server) < 0) { /* a connected host knows better */
        e->members = c->members;
        e->online = c->online;
        e->icon = c->icon;
        e->color = c->color;
        snprintf(e->name, sizeof(e->name), "%s", c->name);
        snprintf(e->desc, sizeof(e->desc), "%s", c->desc);
        snprintf(e->host, sizeof(e->host), "%s", c->host);
    }
    if (c->seen > e->seen)
        e->seen = c->seen;
}
static int dir_fetch(OrbitApp *a, const DirConfig *cfg) {
    size_t cap = 256 * 1024, n = 0;
    char *buf = malloc(cap), url[260];
    if (!buf)
        return -1;
    snprintf(url, sizeof(url), "%s/v1/list", cfg->url);
    int rc = http_get(url, buf, cap, &n);
    cJSON *p = rc ? NULL : dir_open(buf, n, cfg->pub, NULL);
    free(buf);
    if (!p)
        return -1;
    const cJSON *x;
    LOCK(a);
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(p, "communities")) {
        DirCommunity c;
        /* the list is signed by the directory, which already checked every entry; the app checks
         * again (it must never trust remote text blindly) */
        if (!dir_community_parse(x, &c, 1) && c.server[0] && strcmp(c.server, a->server_id))
            dir_merge_central_locked(a, &c);
    }
    dir_save_locked(a);
    a->changed++;
    UNLOCK(a);
    cJSON_Delete(p);
    return 0;
}
static void dir_send_reports(OrbitApp *a, const DirConfig *cfg) {
    for (;;) {
        char body[160], url[260], reply[256];
        int found = -1;
        LOCK(a);
        for (int i = 0; i < a->dir_count && found < 0; i++)
            if (a->dir[i].reported && !a->dir[i].report_sent && a->dir[i].central)
                found = i;
        if (found >= 0)
            snprintf(body, sizeof(body), "{\"server\":\"%s\",\"id\":%d,\"reason\":%d}", a->dir[found].server,
                     a->dir[found].id, a->dir[found].reported - 1);
        UNLOCK(a);
        if (found < 0)
            return;
        snprintf(url, sizeof(url), "%s/v1/report", cfg->url);
        size_t n;
        if (http_post(url, body, strlen(body), reply, sizeof(reply), &n))
            return; /* try again with the next round */
        LOCK(a);
        if (found < a->dir_count)
            a->dir[found].report_sent = 1;
        dir_save_locked(a);
        UNLOCK(a);
    }
}
/* returns the number of communities announced, -1 on failure */
static int dir_announce(OrbitApp *a, const DirConfig *cfg, int publish) {
    cJSON *list = publish && a->store ? orbit_store_public(a->store) : cJSON_CreateArray();
    cJSON *payload = cJSON_CreateObject(), *out = cJSON_AddArrayToObject(payload, "communities");
    uint8_t secret[64], pub[32];
    uint32_t ip = 0;
    int port = 0, count = 0;
    char scope[300];
    LOCK(a);
    int have = !own_endpoint(a, &ip, &port, scope, sizeof(scope));
    cJSON_AddNumberToObject(payload, "v", 1);
    cJSON_AddStringToObject(payload, "server", a->server_id);
    cJSON_AddNumberToObject(payload, "time", orbit_unix_time());
    dir_host_keys(a->server_key, secret, pub);
    UNLOCK(a);
    const cJSON *x;
    cJSON_ArrayForEach(x, list) {
        if (!have || count >= DIR_PER_SERVER)
            break;
        char invite[ORBIT_INVITE_TEXT];
        if (orbit_invite_encode(invite, ip, port, orbit_string(x, "code")))
            continue;
        cJSON *c = cJSON_Duplicate(x, 1);
        cJSON_DeleteItemFromObject(c, "code");
        cJSON_AddStringToObject(c, "invite", invite);
        DirCommunity check;
        if (dir_community_parse(c, &check, 1)) { /* e.g. a name the directory would refuse */
            cJSON_Delete(c);
            continue;
        }
        cJSON_AddItemToArray(out, c);
        count++;
    }
    cJSON_Delete(list);
    char *body = dir_seal(payload, secret, pub, 1);
    crypto_wipe(secret, sizeof(secret));
    cJSON_Delete(payload);
    if (!body)
        return -1;
    char url[260], reply[256];
    size_t n;
    snprintf(url, sizeof(url), "%s/v1/announce", cfg->url);
    int rc = http_post(url, body, strlen(body), reply, sizeof(reply), &n);
    free(body);
    return rc ? -1 : count;
}
static void *dir_main(void *arg) {
    OrbitApp *a = arg;
    uint64_t next_fetch = orbit_millis() + 3000, next_announce = orbit_millis() + 15000;
    int listed = 0;
    for (;;) {
        usleep(250000);
        LOCK(a);
        int stop = a->stop, fetch = a->dir_fetch_now, announce = a->dir_announce_now, publish = a->set.dir_publish;
        DirConfig cfg = a->dircfg;
        a->dir_fetch_now = a->dir_announce_now = 0;
        UNLOCK(a);
        if (stop)
            break;
        uint64_t now = orbit_millis();
        if (fetch || now >= next_fetch) {
            next_fetch = now + DIR_ANNOUNCE_MS;
            int ok = !dir_fetch(a, &cfg);
            if (ok)
                dir_send_reports(a, &cfg);
            LOCK(a);
            a->dir_central = ok ? 2 : 3;
            if (!ok && a->browse_state == BROWSE_ASKING) {
                int any = 0;
                for (int i = 0; i < APP_LINKS; i++)
                    any |= a->links[i].used && !a->links[i].remove && a->links[i].state == LINK_ONLINE;
                if (!any)
                    a->browse_state = BROWSE_NO_CONNECTION;
            } else if (ok && a->browse_state == BROWSE_ASKING) {
                int waiting = 0;
                for (int i = 0; i < APP_LINKS; i++)
                    waiting |= a->links[i].used && a->links[i].want_browse;
                if (!waiting)
                    a->browse_state = BROWSE_DONE;
            }
            a->changed++;
            UNLOCK(a);
        }
        if (announce || now >= next_announce) {
            next_announce = now + DIR_ANNOUNCE_MS;
            if (publish || listed) { /* switched off: one empty announcement removes the listing */
                int n = dir_announce(a, &cfg, publish);
                if (n >= 0)
                    listed = n;
                LOCK(a);
                a->dir_listed = listed;
                a->changed++;
                UNLOCK(a);
            }
        }
    }
    return NULL;
}
static void *manager_main(void *arg) {
    OrbitApp *a = arg;
    uint64_t nat_at = 0, net_check = 0, active_at = 0;
    for (;;) {
        LOCK(a);
        int stop = a->stop;
        UNLOCK(a);
        if (stop)
            break;
        uint64_t now = orbit_millis();
        if (now >= active_at) {
            /* 1.1: the XMB plugin stays quiet while the app is open (notify_core.h) */
            active_at = now + 5000;
            FILE *f = fopen(NOTIFY_ACTIVE, "w");
            if (f) {
                fprintf(f, "%u", (unsigned)orbit_unix_time());
                fclose(f);
            }
        }
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
    a->set.sounds = 60;
    a->set.suggest = 1;
    a->set.bg = a->set.bg_preview = 1;
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
    dir_load(a); /* ORBIT NEXT: cached public communities (survive host and network outages) */
    if (!a->me.created) {
        a->me.created = orbit_unix_time();
        a->dirty = 1;
    }
    if (!a->set.lang_auto && !i18n_unavailable(a->set.lang)) /* a missing language file: English */
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
    {   /* 1.2: public directory, only when data/directory.cfg names one (nothing is built in) */
        char cfgpath[512];
        snprintf(cfgpath, sizeof(cfgpath), "%s/directory.cfg", dir);
        if (!dir_config_load(cfgpath, &a->dircfg)) {
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 256 * 1024); /* TLS handshake + buffers */
            LOCK(a);
            a->dir_ok = 1;
            a->dir_central = 1;
            UNLOCK(a);
            if (!pthread_create(&a->dir_thread, &attr, dir_main, a))
                a->dir_joinable = 1;
            pthread_attr_destroy(&attr);
        }
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
    remove(NOTIFY_ACTIVE);
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
    if (a->dir_joinable) {
        a->dir_joinable = 0;
        pthread_join(a->dir_thread, NULL);
    }
    if (a->import_thread_used) {
        a->import_thread_used = 0;
        pthread_join(a->import_thread, NULL);
    }
    LOCK(a);
    free(a->import_src);
    a->import_src = NULL;
    dl_reset_locked(a);
    for (int i = 0; i < 4; i++) {
        cJSON_Delete(a->upl[i].then);
        a->upl[i].then = NULL;
    }
    UNLOCK(a);
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
    s->pic_gen = a->pic_gen;
    s->import_state = a->import_state;
    s->import_err = a->import_err;
    s->import_target = a->import_target;
    s->import_anchor = a->import_anchor;
    snprintf(s->import_pic, sizeof(s->import_pic), "%s", a->prep_pic);
    snprintf(s->import_anim, sizeof(s->import_anim), "%s", a->prep_anim);
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
    if (a->browse_state == BROWSE_ASKING && orbit_millis() - a->browse_since > 12000)
        a->browse_state = BROWSE_DONE; /* hosts that did not answer stay listed as offline */
    s->browse_count = dir_build_locked(a, s->browse, UI_BROWSE);
    s->browse_state = a->browse_state;
    s->dir_total = a->dir_total;
    s->dir_central = a->dir_ok ? a->dir_central : 0;
    s->dir_listed = a->dir_listed;
    {
        const char *h = strstr(a->dircfg.url, "://");
        snprintf(s->dir_host, sizeof(s->dir_host), "%s", a->dir_ok && h ? h + 3 : "");
    }
    s->dir_filter = a->dir_filter;
    s->dir_sort = a->dir_sort;
    memcpy(s->dir_query, a->dir_query, sizeof(s->dir_query));
    s->search_count = a->search_count;
    s->search_state = a->search_state;
    memcpy(s->search, a->search, sizeof(UiMessage) * (size_t)a->search_count);
    s->report_count = a->report_count;
    s->report_state = a->report_state;
    s->report_group = a->report_group;
    memcpy(s->reports, a->reports, sizeof(UiReport) * (size_t)a->report_count);
    s->audit_count = a->audit_count;
    s->audit_state = a->audit_state;
    memcpy(s->audit, a->audit, sizeof(UiAudit) * (size_t)a->audit_count);
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
    int rule = orbit_name_check(name);
    if (rule)
        return rule; /* E_* key */
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
/* ---------- 1.2 moderation ---------- */
int app_report(OrbitApp *a, int user, uint32_t msg, int reason) {
    int link, gid;
    if (selected(a, &link, &gid) || user < 0 || reason < 0 || reason >= RR_COUNT)
        return -1;
    cJSON *q = app_cmd("report", gid);
    cJSON_AddNumberToObject(q, "user", user);
    cJSON_AddNumberToObject(q, "reason", reason);
    if (msg)
        cJSON_AddNumberToObject(q, "msg", msg);
    return queue(a, link, q);
}
int app_audit(OrbitApp *a) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    LOCK(a);
    a->audit_count = 0;
    a->audit_state = 1;
    UNLOCK(a);
    int rc = queue(a, link, app_cmd("audit", gid));
    if (rc) {
        LOCK(a);
        a->audit_state = 3;
        UNLOCK(a);
    }
    return rc;
}
int app_reports(OrbitApp *a) {
    int link, gid;
    if (selected(a, &link, &gid))
        return -1;
    LOCK(a);
    a->report_count = 0;
    a->report_state = 1;
    a->report_group = gid;
    UNLOCK(a);
    int rc = queue(a, link, app_cmd("reports", gid));
    if (rc) {
        LOCK(a);
        a->report_state = 3;
        UNLOCK(a);
    }
    return rc;
}
void app_dir_filter(OrbitApp *a, const char *query, int filter, int sort) {
    LOCK(a);
    if (query) {
        while (*query == ' ')
            query++;
        snprintf(a->dir_query, sizeof(a->dir_query), "%s", query);
        size_t n = strlen(a->dir_query);
        while (n && a->dir_query[n - 1] == ' ')
            a->dir_query[--n] = 0;
    }
    a->dir_filter = filter >= 0 && filter < DIR_F_COUNT ? filter : DIR_F_ALL;
    a->dir_sort = sort >= 0 && sort < DIR_S_COUNT ? sort : DIR_S_POPULAR;
    a->changed++;
    UNLOCK(a);
}
int app_dir_hide(OrbitApp *a, int index, int reason) {
    if (reason < 0 || reason >= RR_COUNT)
        return -1;
    UiBrowse *list = malloc(sizeof(UiBrowse) * UI_BROWSE);
    if (!list)
        return -1;
    int rc = -1;
    LOCK(a);
    int n = dir_build_locked(a, list, UI_BROWSE);
    for (int i = 0; index >= 0 && index < n && i < a->dir_count; i++) {
        AppDirEntry *e = &a->dir[i];
        if (e->id == list[index].id && !strcmp(e->server, list[index].server)) {
            e->hidden = 1;
            e->reported = reason + 1;
            rc = 0;
        }
    }
    if (!rc) {
        dir_save_locked(a);
        notice_locked(a, T(T_DIR_HIDDEN));
        a->changed++;
    }
    UNLOCK(a);
    free(list);
    return rc;
}
int app_report_resolve(OrbitApp *a, int id, int state) {
    int link, gid;
    if (selected(a, &link, &gid) || (state != REPORT_DONE && state != REPORT_DISMISSED))
        return -1;
    cJSON *q = app_cmd("report_resolve", gid);
    cJSON_AddNumberToObject(q, "id", id);
    cJSON_AddNumberToObject(q, "state", state);
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
    int asked = 0;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && !a->links[i].remove && a->links[i].state == LINK_ONLINE) {
            a->links[i].want_browse = 1;
            asked++;
        }
    if (a->dir_ok) { /* 1.2: the public directory answers too */
        a->dir_fetch_now = 1;
        asked++;
    }
    /* never "loading" forever: without any reachable PS3 the cached directory is shown as is */
    a->browse_state = asked ? BROWSE_ASKING : BROWSE_NO_CONNECTION;
    a->browse_since = orbit_millis();
    a->changed++;
    UNLOCK(a);
}
int app_join_public(OrbitApp *a, int index) {
    UiBrowse *list = malloc(sizeof(UiBrowse) * UI_BROWSE);
    if (!list)
        return -1;
    LOCK(a);
    int n = dir_build_locked(a, list, UI_BROWSE), link = -1, gid = -1;
    char invite[ORBIT_INVITE_TEXT] = "";
    if (index >= 0 && index < n) {
        link = list[index].live ? list[index].link : -1;
        gid = list[index].id;
        /* 1.2: known only from the public directory: join with its published code */
        if (link < 0 && list[index].live && list[index].invite[0])
            snprintf(invite, sizeof(invite), "%s", list[index].invite);
        else if (link < 0)
            notice_locked(a, T(T_HOST_OFFLINE_SHORT));
    }
    UNLOCK(a);
    free(list);
    if (link < 0 && invite[0])
        return app_join(a, invite);
    if (link < 0)
        return -1;
    return queue(a, link, app_cmd("join_public", gid));
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
/* ---------- profile pictures ---------- */
int app_set_picture(OrbitApp *a, const void *image) {
    UiProfile me;
    LOCK(a);
    me = a->me;
    UNLOCK(a);
    if (!image)
        me.pic[0] = 0;
    else {
        uint8_t *blob = malloc(AVATAR_BLOB_MAX);
        size_t n = 0;
        char dir[420];
        snprintf(dir, sizeof(dir), "%s/pics", a->data_dir);
        int rc = !blob || avatar_make(image, blob, &n, me.pic) || avatar_save(dir, me.pic, blob, n);
        free(blob);
        if (rc)
            return -1;
    }
    app_set_profile(a, &me);
    LOCK(a);
    a->pic_gen++;
    UNLOCK(a);
    return 0;
}
void app_pic_request(OrbitApp *a, const char *hash) {
    if (!avatar_hash_valid(hash))
        return;
    uint64_t now = orbit_millis();
    LOCK(a);
    int slot = 0;
    for (int i = 0; i < 8; i++) {
        if (!strcmp(a->pic_want[i], hash)) {
            if (now - a->pic_when[i] < 60000) { /* asked recently */
                UNLOCK(a);
                return;
            }
            slot = i;
            break;
        }
        if (a->pic_when[i] < a->pic_when[slot])
            slot = i;
    }
    snprintf(a->pic_want[slot], sizeof(a->pic_want[slot]), "%s", hash);
    a->pic_when[slot] = now;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && a->links[i].state == LINK_ONLINE) {
            cJSON *q = command("pic");
            cJSON_AddStringToObject(q, "hash", hash);
            bg_queue_locked(a, i, q);
            break;
        }
    UNLOCK(a);
}
void app_anim_request(OrbitApp *a, const char *hash) {
    if (!avatar_hash_valid(hash))
        return;
    uint64_t now = orbit_millis();
    LOCK(a);
    if (a->dl_hash[0] && now - a->dl_since < 30000) { /* one download at a time */
        UNLOCK(a);
        return;
    }
    int slot = 0;
    for (int i = 0; i < 8; i++) {
        if (!strcmp(a->anim_want[i], hash)) {
            if (now - a->anim_when[i] < 120000) { /* asked recently */
                UNLOCK(a);
                return;
            }
            slot = i;
            break;
        }
        if (a->anim_when[i] < a->anim_when[slot])
            slot = i;
    }
    snprintf(a->anim_want[slot], sizeof(a->anim_want[slot]), "%s", hash);
    a->anim_when[slot] = now;
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && a->links[i].state == LINK_ONLINE) {
            dl_reset_locked(a);
            a->dl_buf = malloc(ANIM_BLOB_MAX);
            if (!a->dl_buf)
                break;
            snprintf(a->dl_hash, sizeof(a->dl_hash), "%s", hash);
            a->dl_link = i;
            a->dl_since = now;
            cJSON *q = command("pic");
            cJSON_AddStringToObject(q, "hash", hash);
            cJSON_AddStringToObject(q, "kind", "ani");
            cJSON_AddNumberToObject(q, "offset", 0);
            bg_queue_locked(a, i, q);
            break;
        }
    UNLOCK(a);
}
/* ---------- 1.2 picture import (never on the interface thread) ----------
 * prepare (decode, crop, scale, re-encode; nothing is sent) -> preview -> confirm or cancel.
 * The bytes the user brought stay on this console only until confirm/cancel; what goes out is
 * the re-encoded 96x96 picture and, for a GIF, the ORBIT animation (anim.h). */
static void *import_main(void *arg) {
    OrbitApp *a = arg;
    LOCK(a);
    uint8_t *data = a->import_src;
    size_t n = a->import_n;
    int anchor = a->import_anchor;
    char path[300];
    snprintf(path, sizeof(path), "%s", a->import_path);
    UNLOCK(a);
    int rc = IMG_OK;
    if (!data) { /* a file (USB): read it here, not on the interface thread */
        FILE *f = fopen(path, "rb");
        long size = -1;
        if (!f)
            rc = IMG_E_IO;
        else {
            if (!fseek(f, 0, SEEK_END))
                size = ftell(f);
            if (size <= 0 || (unsigned long)size > IMG_MAX_UPLOAD || fseek(f, 0, SEEK_SET))
                rc = size <= 0 ? IMG_E_IO : IMG_E_LIMIT;
            else if (!(data = malloc((size_t)size)))
                rc = IMG_E_MEMORY;
            else if (fread(data, 1, (size_t)size, f) != (size_t)size)
                rc = IMG_E_IO;
            fclose(f);
        }
        n = size > 0 ? (size_t)size : 0;
        LOCK(a);
        a->import_src = data;
        a->import_n = n;
        UNLOCK(a);
    }
    OrbitImage still;
    memset(&still, 0, sizeof(still));
    uint8_t *ablob = NULL, *blob = NULL;
    size_t an = 0, bn = 0;
    char ahash[33] = "", phash[33] = "", dir[420];
    if (rc == IMG_OK) {
        if (gif_sniff(data, n)) {
            rc = anim_make_gif_at(data, n, anchor, &ablob, &an, ahash, &still);
            if (rc == IMG_E_UNSUPPORTED) /* a still GIF */
                rc = IMG_OK;
        } else {
            OrbitImage full;
            rc = image_decode_any(data, n, &full);
            if (rc == IMG_OK) {
                rc = image_cover_at(&full, &still, AVATAR_SIDE, AVATAR_SIDE, anchor);
                image_free(&full);
            }
        }
    }
    snprintf(dir, sizeof(dir), "%s/pics", a->data_dir);
    if (rc == IMG_OK && (!(blob = malloc(AVATAR_BLOB_MAX)) || avatar_make(&still, blob, &bn, phash) ||
                         avatar_save(dir, phash, blob, bn)))
        rc = IMG_E_MEMORY;
    if (rc == IMG_OK && ablob && anim_save(dir, ahash, ablob, an))
        rc = IMG_E_IO;
    image_free(&still);
    free(ablob);
    free(blob);
    LOCK(a);
    if (rc != IMG_OK) {
        a->import_state = IMPORT_FAILED;
        a->import_err = rc;
        free(a->import_src);
        a->import_src = NULL;
    } else {
        a->import_state = IMPORT_READY;
        snprintf(a->prep_pic, sizeof(a->prep_pic), "%s", phash);
        snprintf(a->prep_anim, sizeof(a->prep_anim), "%s", ahash);
        a->pic_gen++; /* the preview can show it */
    }
    a->import_running = 0;
    a->changed++;
    UNLOCK(a);
    return NULL;
}
static int import_start_locked(OrbitApp *a) {
    if (a->import_thread_used) { /* the previous worker has finished: collect it */
        a->import_thread_used = 0;
        UNLOCK(a);
        pthread_join(a->import_thread, NULL);
        LOCK(a);
    }
    a->import_state = IMPORT_WORKING;
    a->import_err = 0;
    a->import_running = 1;
    a->prep_pic[0] = a->prep_anim[0] = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    int ok = !pthread_create(&a->import_thread, &attr, import_main, a);
    pthread_attr_destroy(&attr);
    if (ok)
        a->import_thread_used = 1;
    else {
        a->import_running = 0;
        a->import_state = IMPORT_FAILED;
        a->import_err = IMG_E_MEMORY;
    }
    a->changed++;
    return ok ? 0 : -1;
}
static int import_begin(OrbitApp *a, uint8_t *data, size_t n, const char *path, int link, int gid) {
    LOCK(a);
    if (a->import_running) {
        UNLOCK(a);
        free(data);
        return -1;
    }
    free(a->import_src);
    a->import_src = data;
    a->import_n = n;
    snprintf(a->import_path, sizeof(a->import_path), "%s", path ? path : "");
    a->import_target = gid;
    a->import_link = link;
    a->import_anchor = 0;
    int rc = import_start_locked(a);
    UNLOCK(a);
    return rc;
}
int app_import_picture(OrbitApp *a, uint8_t *data, size_t n, int link, int gid) {
    if (!data || !n) {
        free(data);
        return -1;
    }
    return import_begin(a, data, n, NULL, link, gid);
}
int app_import_file(OrbitApp *a, const char *path, int link, int gid) {
    return path && path[0] ? import_begin(a, NULL, 0, path, link, gid) : -1;
}
int app_import_crop(OrbitApp *a, int anchor) {
    LOCK(a);
    int rc = -1;
    if (!a->import_running && a->import_src && a->import_state == IMPORT_READY) {
        a->import_anchor = anchor;
        rc = import_start_locked(a);
    }
    UNLOCK(a);
    return rc;
}
void app_import_cancel(OrbitApp *a) {
    LOCK(a);
    if (!a->import_running) {
        free(a->import_src);
        a->import_src = NULL;
        a->import_state = IMPORT_IDLE;
        a->prep_pic[0] = a->prep_anim[0] = 0;
        a->changed++;
    }
    UNLOCK(a);
}
int app_import_confirm(OrbitApp *a) {
    LOCK(a);
    if (a->import_running || a->import_state != IMPORT_READY) {
        UNLOCK(a);
        return -1;
    }
    char phash[33], ahash[33], dir[420];
    snprintf(phash, sizeof(phash), "%s", a->prep_pic);
    snprintf(ahash, sizeof(ahash), "%s", a->prep_anim);
    int target = a->import_target, link = a->import_link;
    free(a->import_src);
    a->import_src = NULL;
    UNLOCK(a);
    if (target < 0) { /* own profile picture */
        UiProfile me;
        LOCK(a);
        me = a->me;
        UNLOCK(a);
        snprintf(me.pic, sizeof(me.pic), "%s", phash);
        snprintf(me.anim, sizeof(me.anim), "%s", ahash);
        app_set_profile(a, &me);
        LOCK(a);
        a->import_state = ahash[0] ? IMPORT_SENDING : IMPORT_DONE;
    } else { /* server icon: the host checks the permission */
        snprintf(dir, sizeof(dir), "%s/pics", a->data_dir);
        uint8_t *blob = malloc(AVATAR_BLOB_MAX);
        char *text = malloc(AVATAR_B64_MAX);
        size_t bn = 0;
        cJSON *q = app_cmd("server_update", target);
        int good = blob && text && !avatar_load(dir, phash, blob, &bn) && !avatar_b64_encode(text, AVATAR_B64_MAX, blob, bn);
        if (good) {
            cJSON_AddStringToObject(q, "pic", phash);
            cJSON_AddStringToObject(q, "pic_data", text);
            cJSON_AddStringToObject(q, "anim", ahash);
        }
        free(blob);
        free(text);
        LOCK(a);
        if (good && link >= 0 && link < APP_LINKS && a->links[link].used) {
            if (ahash[0]) {
                upl_start_locked(a, link, ahash, q);
                a->import_state = IMPORT_SENDING;
            } else {
                bg_queue_locked(a, link, q);
                a->import_state = IMPORT_DONE;
            }
        } else {
            cJSON_Delete(q);
            a->import_state = IMPORT_FAILED;
            a->import_err = IMG_E_IO;
        }
    }
    a->pic_gen++;
    a->changed++;
    UNLOCK(a);
    return 0;
}
void app_import_reset(OrbitApp *a) {
    LOCK(a);
    if (!a->import_running)
        a->import_state = IMPORT_IDLE;
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
            add_pic(q, &a->me, a->data_dir);
            if (a->me.anim[0])
                upl_start_locked(a, i, a->me.anim, q);
            else
                bg_queue_locked(a, i, q);
        }
    UNLOCK(a);
}
void app_set_settings(OrbitApp *a, const UiSettings *s) {
    LOCK(a);
    if (s->dir_publish != a->set.dir_publish)
        a->dir_announce_now = 1;
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

/* ---------- 1.1 background-notification plugin (Cobra / webMAN boot plugin list) ---------- */
#define PLUGIN_FILE "/dev_hdd0/plugins/orbit_notify.sprx"
static const char *const plugin_lists[] = {"/dev_hdd0/boot_plugins.txt", "/dev_hdd0/boot_plugins_hen.txt"};
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}
#define PRESENCE_FILE "/dev_hdd0/plugins/orbit_presence.sprx"
static int list_has(const char *list, const char *file) {
    FILE *f = fopen(list, "r");
    char line[512];
    int found = 0;
    if (!f)
        return 0;
    while (!found && fgets(line, sizeof(line), f))
        found = strstr(line, file) != NULL;
    fclose(f);
    return found;
}
static int plugin_installed_file(const char *file) {
    if (!file_exists(file))
        return 0;
    for (size_t i = 0; i < sizeof(plugin_lists) / sizeof(plugin_lists[0]); i++)
        if (list_has(plugin_lists[i], file))
            return 1;
    return 0;
}
int app_plugin_installed(void) {
    return plugin_installed_file(PLUGIN_FILE);
}
int app_presence_installed(void) {
    return plugin_installed_file(PRESENCE_FILE);
}
static int plugin_install_file(const char *appdir, const char *name, const char *PLUGIN);
int app_plugin_install(const char *appdir) {
    return plugin_install_file(appdir, "orbit_notify.sprx", PLUGIN_FILE);
}
int app_presence_install(const char *appdir) {
    return plugin_install_file(appdir, "orbit_presence.sprx", PRESENCE_FILE);
}
/* PLUGIN: target path, also the line in the boot lists */
static int plugin_install_file(const char *appdir, const char *name, const char *PLUGIN) {
    char src[512], part[300];
    snprintf(src, sizeof(src), "%s/%s", appdir, name);
    snprintf(part, sizeof(part), "%s.tmp", PLUGIN);
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;
    mkdir("/dev_hdd0/plugins", 0777);
    FILE *out = fopen(part, "wb");
    int bad = !out;
    char buf[4096];
    size_t n;
    while (!bad && (n = fread(buf, 1, sizeof(buf), in)) > 0)
        bad = fwrite(buf, 1, n, out) != n;
    fclose(in);
    if (out && fclose(out))
        bad = 1;
    if (bad || replace_file(part, PLUGIN)) {
        remove(part);
        return -1;
    }
    /* add one line to every boot list that exists (Cobra/webMAN, PS3HEN); create the Cobra one
     * when there is none */
    int listed = 0, any = 0;
    for (size_t i = 0; i < sizeof(plugin_lists) / sizeof(plugin_lists[0]); i++) {
        if (!file_exists(plugin_lists[i]))
            continue;
        any = 1;
        if (list_has(plugin_lists[i], PLUGIN)) {
            listed = 1;
            continue;
        }
        FILE *f = fopen(plugin_lists[i], "a");
        if (f) {
            fprintf(f, "\n%s\n", PLUGIN);
            listed = fclose(f) == 0 || listed;
        }
    }
    if (!any) {
        FILE *f = fopen(plugin_lists[0], "w");
        if (f) {
            fprintf(f, "%s\n", PLUGIN);
            listed = fclose(f) == 0;
        }
    }
    return listed ? 0 : -1;
}
static int plugin_remove_file(const char *PLUGIN) {
    int changed = 0;
    for (size_t i = 0; i < sizeof(plugin_lists) / sizeof(plugin_lists[0]); i++) {
        if (!list_has(plugin_lists[i], PLUGIN))
            continue;
        /* rewrite the list without our line; every other plugin stays exactly as it was */
        FILE *f = fopen(plugin_lists[i], "r");
        if (!f)
            continue;
        static char keep[16384];
        size_t used = 0;
        char line[512];
        while (fgets(line, sizeof(line), f))
            if (!strstr(line, PLUGIN)) {
                size_t len = strlen(line);
                if (used + len < sizeof(keep)) {
                    memcpy(keep + used, line, len);
                    used += len;
                }
            }
        fclose(f);
        char tmp[300];
        snprintf(tmp, sizeof(tmp), "%s.tmp", plugin_lists[i]);
        FILE *o = fopen(tmp, "w");
        if (!o)
            continue;
        int bad = fwrite(keep, 1, used, o) != used;
        if (fclose(o) || bad || replace_file(tmp, plugin_lists[i]))
            remove(tmp);
        else
            changed = 1;
    }
    if (file_exists(PLUGIN) && !remove(PLUGIN))
        changed = 1;
    return changed ? 0 : 1;
}
int app_plugin_remove(void) {
    return plugin_remove_file(PLUGIN_FILE);
}
int app_presence_remove(void) {
    return plugin_remove_file(PRESENCE_FILE);
}

void app_watch_renew(OrbitApp *a) {
    LOCK(a);
    for (int i = 0; i < APP_LINKS; i++)
        if (a->links[i].used && !a->links[i].own)
            a->links[i].watch_renew = 1; /* done by each link thread once it is connected */
    UNLOCK(a);
}
