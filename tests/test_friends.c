/* End-to-end Orbit 1.0: two complete apps (two consoles) on loopback.
 * Friend code -> request -> accept on both PS3s -> private chat -> server with template ->
 * friend added to the server -> offline queue delivered after the host comes back. */
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
    app_open(&B, b_link, b_gid, general);
    app_stop(&A);
    assert(wait_for(&B, group_offline, 20000));
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
