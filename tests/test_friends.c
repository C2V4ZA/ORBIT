/* End-to-end Orbit 1.0: two complete apps (two consoles) on loopback.
 * Friend code -> request -> accept on both PS3s -> private chat -> server with template ->
 * friend added to the server -> offline queue delivered after the host comes back. */
#include "avatar.h"
#include "notify_core.h"
#include "ui.h"
#include "voice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static UiState view;
static const char *want_name, *want_text;
static int want_state;
static int wait_for(OrbitApp *a, int (*cond)(const UiState *), int ms) {
    for (int t = 0; t < ms; t += 100) {
        view.revision = 0;
        app_snapshot(a, &view);
        if (cond(&view))
            return 1;
        usleep(100000);
    }
    return 0;
}
static char want_pic[33];
static int friend_has_pic(const UiState *s) {
    for (int i = 0; i < s->friend_count; i++)
        if (!strcmp(s->friends[i].name, "Alex") && !strcmp(s->friends[i].pic, want_pic))
            return 1;
    return 0;
}
static int pic_arrived(const UiState *s) {
    return s->pic_gen > 0;
}
static int browse_settled(const UiState *s) {
    return s->browse_state == BROWSE_DONE || s->browse_state == BROWSE_NO_CONNECTION;
}
static const UiBrowse *browse_named(const UiState *s, const char *name) {
    for (int i = 0; i < s->browse_count; i++)
        if (!strcmp(s->browse[i].name, name))
            return &s->browse[i];
    return NULL;
}
static int browse_has_gaming_live(const UiState *s) {
    const UiBrowse *b = browse_named(s, "ORBIT GAMING");
    return b && b->live;
}
static int browse_has_gaming_offline(const UiState *s) {
    const UiBrowse *b = browse_named(s, "ORBIT GAMING");
    return b && !b->live && b->seen;
}
static int has_code(const UiState *s) {
    return strlen(s->friend_code) == 23 && strlen(s->oid) == 14;
}
static const UiFriend *friend_named(const UiState *s, const char *name) {
    for (int i = 0; i < s->friend_count; i++)
        if (!strcmp(s->friends[i].name, name))
            return &s->friends[i];
    return NULL;
}
static int friend_state(const UiState *s) {
    const UiFriend *f = friend_named(s, want_name);
    return f && f->state == want_state;
}
static int friend_known_user(const UiState *s) {
    const UiFriend *f = friend_named(s, want_name);
    return f && f->state == FR_FRIENDS && f->user >= 0 && f->online;
}
static const UiGroup *group_named(const UiState *s, const char *name, int kind) {
    for (int i = 0; i < s->group_count; i++)
        if (!strcmp(s->groups[i].name, name) && s->groups[i].kind == kind)
            return &s->groups[i];
    return NULL;
}
static int dm_selected(const UiState *s) {
    return s->selected >= 0 && s->groups[s->selected].kind == GK_DM && s->channel >= 0 &&
           s->groups[s->selected].state == LINK_ONLINE;
}
static int has_dm(const UiState *s) {
    return group_named(s, want_name, GK_DM) != NULL;
}
static int has_message(const UiState *s) {
    for (int i = 0; i < s->message_count; i++)
        if (!strcmp(s->messages[i].text, want_text) && !s->messages[i].pending)
            return 1;
    return 0;
}
static int has_pending(const UiState *s) {
    for (int i = 0; i < s->message_count; i++)
        if (!strcmp(s->messages[i].text, want_text) && s->messages[i].pending)
            return s->outbox_count == 1;
    return 0;
}
static int outbox_empty(const UiState *s) {
    return s->outbox_count == 0;
}
static int note_with(const UiState *s) {
    for (int i = 0; i < s->note_count; i++)
        if (strstr(s->notes[i].text, want_text))
            return 1;
    return 0;
}
static int server_ready(const UiState *s) {
    const UiGroup *g = group_named(s, "ORBIT GAMING", GK_SERVER);
    return g && g->nch == 10 && g->ncat == 3 && g->nrole == 4;
}
static int server_visible(const UiState *s) {
    const UiGroup *g = group_named(s, "ORBIT GAMING", GK_SERVER);
    return g && g->state == LINK_ONLINE && !g->own && g->nch == 10;
}
static int not_busy(const UiState *s) {
    return !s->busy;
}
static int group_offline(const UiState *s) {
    const UiGroup *g = group_named(s, "ORBIT GAMING", GK_SERVER);
    return g && g->state != LINK_ONLINE;
}
static void fresh(const char *dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    assert(!system(cmd));
}
int main(void) {
    app_shield_default = 0; /* these tests use direct loopback connections */
    i18n_set(LANG_EN);
    /* only the interface is translated */
    assert(!strcmp(T(T_TAB_FRIENDS), "FRIENDS"));
    i18n_set(LANG_FR);
    assert(!strcmp(T(T_ADD_FRIEND), "Ajouter un ami") && !strcmp(T(T_SET_PROFILE), "Profil"));
    i18n_set(LANG_ES);
    assert(!strcmp(T(T_CREATE_SERVER), "Crear servidor"));
    assert(i18n_from_ps3(4) == LANG_DE && i18n_from_ps3(0) == LANG_EN && i18n_from_ps3(19) == LANG_EN);
    i18n_set(LANG_DE);
    printf("PASS: interface languages DE/EN/FR/ES from the PS3 system language, English fallback\n");

    const char *da = "/tmp/orbit-fa", *db = "/tmp/orbit-fb";
    fresh(da);
    fresh(db);
    static OrbitApp A, B;
    assert(!app_start(&A, da, "127.0.0.1", "255.0.0.0", "", "Alex", 31511));
    assert(!app_start(&B, db, "127.0.0.1", "255.0.0.0", "", "Mika", 31512));
    app_set_name(&A, "Alex");
    app_set_name(&B, "Mika");
    assert(wait_for(&A, has_code, 15000));
    char code_a[ORBIT_INVITE_TEXT], oid_a[ORBIT_ID_TEXT], oid_b[ORBIT_ID_TEXT];
    snprintf(code_a, sizeof(code_a), "%s", view.friend_code);
    snprintf(oid_a, sizeof(oid_a), "%s", view.oid);
    assert(wait_for(&B, has_code, 15000));
    snprintf(oid_b, sizeof(oid_b), "%s", view.oid);
    assert(strcmp(oid_a, oid_b));
    printf("friend code A %s, Orbit IDs %s / %s\n", code_a, oid_a, oid_b);

    /* B sends a request with A's friend code */
    assert(!app_friend_add(&B, code_a));
    want_name = "Mika", want_state = FR_INCOMING;
    assert(wait_for(&A, friend_state, 20000));
    assert(!strcmp(friend_named(&view, "Mika")->fid, oid_b)); /* the ID A sees is B's real key */
    int idx = friend_named(&view, "Mika")->i;
    want_name = "Alex", want_state = FR_OUTGOING;
    assert(wait_for(&B, friend_state, 20000));
    printf("PASS: friend request with the 20-character friend code; the receiver sees the sender's verified Orbit ID\n");

    assert(wait_for(&A, not_busy, 5000));
    assert(!app_friend_op(&A, idx, "friend_respond", 1));
    want_name = "Alex", want_state = FR_FRIENDS;
    assert(wait_for(&B, friend_state, 25000));
    want_name = "Mika", want_state = FR_FRIENDS;
    assert(wait_for(&A, friend_state, 25000));
    want_name = "Alex";
    assert(wait_for(&B, friend_known_user, 25000)); /* A is now known on B's PS3 and online */
    want_name = "Mika";
    assert(wait_for(&A, friend_known_user, 25000));
    printf("PASS: accepting makes both PS3s friends, presence shows each other online\n");

    /* private chat started by B (hosted on B's PS3) */
    view.revision = 0;
    app_snapshot(&B, &view);
    int b_idx = friend_named(&view, "Alex")->i;
    assert(wait_for(&B, not_busy, 5000));
    assert(!app_friend_message(&B, b_idx));
    assert(wait_for(&B, dm_selected, 15000));
    want_text = "Yo bro wanna play some GTA?";
    assert(!app_send(&B, want_text));
    want_name = "Mika";
    assert(wait_for(&A, has_dm, 20000));
    const UiGroup *dm = group_named(&view, "Mika", GK_DM);
    app_open(&A, dm->link, dm->id, -1);
    assert(wait_for(&A, has_message, 15000)); /* exactly as written, never translated */
    printf("PASS: private chat between friends; message arrives unchanged in a German interface\n");

    /* notification on B for a reply from A while B looks elsewhere */
    assert(wait_for(&A, not_busy, 5000));
    app_open(&B, 0, -1, -1);
    want_text = "Klar, in 5 min!";
    assert(!app_send(&A, want_text));
    assert(wait_for(&B, note_with, 15000));
    printf("PASS: notification for a new private message (text quoted unchanged)\n");

    /* 1.2 profile picture: A sets one, B sees its name and fetches it from A's PS3 */
    {
        OrbitImage photo = {200, 150, malloc(200 * 150 * 4)};
        for (int i = 0; i < 200 * 150; i++)
            photo.px[i] = 0xFF000000u | (uint32_t)(i % 200) << 16 | (uint32_t)(i / 200) << 8 | 0x40;
        assert(wait_for(&A, not_busy, 5000));
        assert(!app_set_picture(&A, &photo));
        image_free(&photo);
        view.revision = 0;
        app_snapshot(&A, &view);
        assert(avatar_hash_valid(view.me.pic));
        snprintf(want_pic, sizeof(want_pic), "%s", view.me.pic);
        assert(wait_for(&B, friend_has_pic, 30000));
        app_pic_request(&B, want_pic);
        assert(wait_for(&B, pic_arrived, 15000));
        uint8_t blob[AVATAR_BLOB_MAX];
        size_t n = 0;
        OrbitImage got;
        assert(!avatar_load("/tmp/orbit-fb/pics", want_pic, blob, &n));
        assert(!avatar_decode(blob, n, want_pic, &got) && got.w == AVATAR_SIDE);
        image_free(&got);
        app_pic_request(&B, "0123456789abcdef0123456789abcdef"); /* unknown: not served */
        printf("PASS: profile picture: set from a photo (cropped to 96x96), name shared, fetched and "
               "verified from the friend's PS3\n");
    }

    /* server with the full template on A, B added directly as a friend */
    assert(wait_for(&A, not_busy, 5000));
    assert(!app_create_server(&A, "ORBIT GAMING", 1));
    assert(wait_for(&A, server_ready, 15000));
    const UiGroup *srv = group_named(&view, "ORBIT GAMING", GK_SERVER);
    int sid = srv->id;
    assert(!strcmp(srv->cat[0].name, "INFORMATION") || !strcmp(srv->cat[0].name, "INFORMATION"));
    view.revision = 0;
    app_snapshot(&A, &view);
    int a_idx = friend_named(&view, "Mika")->i;
    assert(wait_for(&A, not_busy, 5000));
    assert(!app_friend_invite(&A, a_idx, 0, sid));
    assert(wait_for(&B, server_visible, 20000));
    printf("PASS: community server from template (3 categories, 10 channels, 4 roles); friend added directly\n");

    /* 1.1: B's app fetched its background-notification key from A's PS3 and wrote the plugin
     * config; the plugin logic reads it back */
    {
        static char cfg[8192];
        NotifyConfig nc;
        int ok = 0;
        for (int t = 0; t < 200 && !ok; t++) {
            FILE *f = fopen("/tmp/orbit-fb/notify.cfg", "rb");
            size_t n = f ? fread(cfg, 1, sizeof(cfg) - 1, f) : 0;
            if (f)
                fclose(f);
            ok = n > 0 && !notify_parse(&nc, cfg, n) && nc.hosts == 1 && nc.on && nc.preview &&
                 nc.host[0].port == 31511 && strstr(cfg, "t_dm\tORBIT");
            if (!ok)
                usleep(100000);
        }
        assert(ok);
        /* the plugin must look where the PS3 app writes: APPDIR "/data" (main_ps3.c) */
        assert(strstr(NOTIFY_CONFIG, "/ORB000001/USRDIR/data/notify.cfg"));
        /* renewing: a new key reaches the config, the old one is different */
        uint8_t old_wid[8];
        memcpy(old_wid, nc.host[0].wid, 8);
        app_watch_renew(&B);
        int renewed = 0;
        for (int t = 0; t < 200 && !renewed; t++) {
            FILE *f = fopen("/tmp/orbit-fb/notify.cfg", "rb");
            size_t n = f ? fread(cfg, 1, sizeof(cfg) - 1, f) : 0;
            if (f)
                fclose(f);
            renewed = n > 0 && !notify_parse(&nc, cfg, n) && nc.hosts == 1 &&
                      memcmp(old_wid, nc.host[0].wid, 8);
            if (!renewed)
                usleep(100000);
        }
        assert(renewed);
    }
    printf("PASS: background notifications: key fetched from the friend's PS3, plugin config written and readable\n");

    /* offline queue: A closes Orbit, B writes into A's server, delivered after A is back */
    srv = group_named(&view, "ORBIT GAMING", GK_SERVER);
    view.revision = 0;
    app_snapshot(&B, &view);
    srv = group_named(&view, "ORBIT GAMING", GK_SERVER);
    int general = -1;
    for (int i = 0; i < srv->nch; i++)
        if (srv->ch[i].type == CH_TEXT && srv->ch[i].cat == 1 && general < 0)
            general = srv->ch[i].id;
    int b_link = srv->link, b_gid = srv->id;
    /* ORBIT NEXT: public directory. The owner sees the own public server (1.1 hid it) and the
     * community stays listed as "host offline" when A is switched off. */
    {
        cJSON *q = app_cmd("server_update", sid);
        cJSON_AddBoolToObject(q, "open", 1);
        view.revision = 0;
        app_snapshot(&A, &view);
        assert(!app_job(&A, group_named(&view, "ORBIT GAMING", GK_SERVER)->link, q));
        assert(wait_for(&A, not_busy, 5000));
        usleep(400000);
        app_browse(&A);
        assert(wait_for(&A, browse_has_gaming_live, 15000));
        const UiBrowse *own = browse_named(&view, "ORBIT GAMING");
        assert(own->member && own->members >= 2 && own->online >= 1);
        app_browse(&B);
        assert(wait_for(&B, browse_has_gaming_live, 15000));
        assert(browse_named(&view, "ORBIT GAMING")->member);
    }
    printf("PASS: Discover: own and joined public servers are listed with member and online counts\n");
    app_open(&B, b_link, b_gid, general);
    app_stop(&A);
    assert(wait_for(&B, group_offline, 20000));
    app_browse(&B); /* no reachable host: no endless "loading", cached entry stays */
    assert(wait_for(&B, browse_has_gaming_offline, 15000));
    assert(wait_for(&B, browse_settled, 20000)); /* the request ends; the entry is still there */
    assert(browse_has_gaming_offline(&view));
    assert(app_join_public(&B, 0) < 0); /* joining needs the host: refused, not queued */
    {
        char dirjson[8192];
        FILE *f = fopen("/tmp/orbit-fb/directory.json", "rb");
        size_t n = f ? fread(dirjson, 1, sizeof(dirjson) - 1, f) : 0;
        if (f)
            fclose(f);
        dirjson[n] = 0;
        assert(strstr(dirjson, "ORBIT GAMING") && strstr(dirjson, "\"seen\""));
    }
    printf("PASS: Discover: host offline keeps the community listed (cached, saved to directory.json), join refused\n");
    { /* 1.2: Discover search, filter and order */
        app_dir_filter(&B, "  gaming ", DIR_F_ALL, DIR_S_NAME);
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(browse_named(&view, "ORBIT GAMING") && !strcmp(view.dir_query, "gaming"));
        assert(view.dir_sort == DIR_S_NAME && view.dir_total >= view.browse_count);
        app_dir_filter(&B, "no such community", DIR_F_ALL, DIR_S_POPULAR);
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(view.browse_count == 0 && view.dir_total >= 1);
        assert(app_join_public(&B, 0) < 0); /* nothing listed: nothing to join */
        app_dir_filter(&B, "", DIR_F_LIVE, DIR_S_POPULAR); /* host is off: not reachable */
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(!browse_named(&view, "ORBIT GAMING"));
        app_dir_filter(&B, NULL, DIR_F_JOINED, DIR_S_SEEN);
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(browse_named(&view, "ORBIT GAMING"));
        app_dir_filter(&B, NULL, DIR_F_NEW, DIR_S_POPULAR);
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(!browse_named(&view, "ORBIT GAMING"));
        app_dir_filter(&B, "", DIR_F_ALL, DIR_S_POPULAR);
    }
    printf("PASS: Discover: search in name/description/host, filters (reachable, joined, not joined), order\n");
    { /* 1.2: report and hide; stays hidden, report kept for the public directory */
        app_snapshot(&B, &view);
        int idx = (int)(browse_named(&view, "ORBIT GAMING") - view.browse);
        assert(app_dir_hide(&B, idx, RR_SPAM) == 0);
        assert(app_dir_hide(&B, 99, RR_SPAM) < 0);
        view.revision = 0;
        app_snapshot(&B, &view);
        assert(!browse_named(&view, "ORBIT GAMING"));
        char dirjson[8192];
        FILE *f = fopen("/tmp/orbit-fb/directory.json", "rb");
        size_t n = f ? fread(dirjson, 1, sizeof(dirjson) - 1, f) : 0;
        if (f)
            fclose(f);
        dirjson[n] = 0;
        assert(strstr(dirjson, "\"hidden\":true") && strstr(dirjson, "\"reported\":1"));
    }
    printf("PASS: Discover: report and hide removes the community from the list, saved with the report\n");
    want_text = "Bin gleich da";
    assert(!app_send(&B, want_text));
    assert(wait_for(&B, has_pending, 5000)); /* shown as waiting, never as delivered */
    assert(!app_start(&A, da, "127.0.0.1", "255.0.0.0", "", "ignored", 31511));
    app_retry(&B);
    assert(wait_for(&B, outbox_empty, 40000));
    app_open(&A, 0, sid, general);
    assert(wait_for(&A, has_message, 15000));
    printf("PASS: offline queue: message waits on the sender's PS3 and is delivered when the host is back\n");

    app_stop(&B);
    app_stop(&A);
    printf("PASS: clean shutdown\n");
    return 0;
}
