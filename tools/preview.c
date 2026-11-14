/* Renders Orbit 1.0 screens with fixture data to build/ui-*.ppm in several languages
 * (desktop rendering of the real PS3 renderer, not a PS3 measurement). */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint32_t buf[1920 * 1080];
static void ppm(const char *file, int w, int h) {
    FILE *f = fopen(file, "wb");
    if (!f)
        exit(2);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char rgb[3] = {(unsigned char)(buf[i] >> 16), (unsigned char)(buf[i] >> 8), (unsigned char)buf[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static UiGroup *server(UiState *s, const char *name, const char *host, int own, int color, int icon) {
    UiGroup *g = &s->groups[s->group_count++];
    memset(g, 0, sizeof(*g));
    g->link = own ? 0 : 2;
    g->id = s->group_count;
    g->own = own;
    g->state = LINK_ONLINE;
    g->kind = GK_SERVER;
    g->color = color;
    g->icon = icon;
    g->banner = 1;
    g->members = 14;
    g->online = 6;
    g->perms = own ? P_ALL : P_SEND | P_INVITE;
    g->myroles = own ? 1 : 8;
    snprintf(g->name, sizeof(g->name), "%s", name);
    snprintf(g->host, sizeof(g->host), "%s", host);
    snprintf(g->people, sizeof(g->people), "Alex, Lena, Mika, Jonas");
    const char *roles[4] = {"Owner", "Admin", "Moderator", "Member"};
    uint32_t rc[4] = {0xf4b445, 0xff6f6f, 0x39d98a, 0x9aa1ab};
    for (int i = 0; i < 4; i++) {
        g->role[i].i = i;
        g->role[i].prio = (int[]){100, 80, 50, 10}[i];
        g->role[i].color = rc[i];
        snprintf(g->role[i].name, 25, "%s", roles[i]);
    }
    g->nrole = 4;
    return g;
}
static void cat(UiGroup *g, const char *name) {
    UiCategory *c = &g->cat[g->ncat];
    c->i = g->ncat;
    c->pos = g->ncat;
    snprintf(c->name, sizeof(c->name), "%s", name);
    g->ncat++;
}
static UiChannel *chan(UiGroup *g, const char *name, int type, int category, int last) {
    UiChannel *h = &g->ch[g->nch];
    h->id = g->nch;
    h->pos = g->nch;
    h->type = type;
    h->cat = category;
    h->last = (uint32_t)last;
    snprintf(h->name, sizeof(h->name), "%s", name);
    g->nch++;
    return h;
}
static UiGroup *dm(UiState *s, const char *name, int status, int color, int avatar, int unread) {
    UiGroup *g = &s->groups[s->group_count++];
    memset(g, 0, sizeof(*g));
    g->link = 3 + s->group_count;
    g->id = s->group_count;
    g->state = LINK_ONLINE;
    g->kind = GK_DM;
    g->perms = P_SEND;
    g->unread = unread;
    g->ndm = 1;
    g->dm[0].status = status;
    g->dm[0].color = color;
    g->dm[0].avatar = avatar;
    snprintf(g->dm[0].name, 49, "%s", name);
    snprintf(g->dm[0].oid, 15, "Q84Q-355T-966L");
    snprintf(g->name, sizeof(g->name), "%s", name);
    chan(g, "dm", CH_TEXT, -1, 9);
    return g;
}
static void msg(UiState *s, const char *name, int mine, int ago, const char *text, int color) {
    UiMessage *m = &s->messages[s->message_count++];
    memset(m, 0, sizeof(*m));
    m->id = s->message_count;
    m->mine = mine;
    m->color = color;
    m->time = (uint32_t)(1794657600 - ago);
    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->text, sizeof(m->text), "%s", text);
}
static void friend(UiState *s, const char *name, int state, int status, int color, int avatar, const char *custom) {
    UiFriend *f = &s->friends[s->friend_count];
    memset(f, 0, sizeof(*f));
    f->i = s->friend_count;
    f->state = state;
    f->status = status;
    f->online = status != ST_OFFLINE;
    f->color = color;
    f->avatar = avatar;
    f->user = 1;
    f->since = (uint32_t)1794657600 - 86400 * 20;
    f->created = (uint32_t)1794657600 - 86400 * 90;
    snprintf(f->name, sizeof(f->name), "%s", name);
    snprintf(f->fid, sizeof(f->fid), "Q84Q-355T-%04d", 1000 + s->friend_count);
    snprintf(f->custom, sizeof(f->custom), "%s", custom);
    snprintf(f->bio, sizeof(f->bio), "PS3 collector since 2007. Always up for a race.");
    snprintf(f->game, sizeof(f->game), "Gran Turismo 5");
    snprintf(f->mutual, sizeof(f->mutual), "ORBIT GAMING, Racing Crew");
    s->friend_count++;
}
static void note(UiState *s, int type, const char *text, int ago) {
    UiNote *n = &s->notes[s->note_count++];
    n->type = type;
    n->time = (uint32_t)(1794657600 - ago);
    n->group = -1;
    snprintf(n->text, sizeof(n->text), "%s", text);
}
static void shot(const char *lang_tag, const char *name, UiState *s, UiLocal *l) {
    char file[128];
    s->revision++;
    ui_render(buf, 1280, 720, s, l);
    snprintf(file, sizeof(file), "build/ui-%s-%s.ppm", lang_tag, name);
    ppm(file, 1280, 720);
}
static void set_row(UiState *s, UiLocal *l, int tab, int kind, int a, int b, int c) {
    UiRow rows[96];
    l->tab = tab;
    int n = ui_rows(s, tab, rows, 96);
    for (int i = 0; i < n; i++)
        if (rows[i].kind == kind && rows[i].a == a && rows[i].b == b && (kind != ROW_CHANNEL || rows[i].c == c)) {
            l->side = i;
            return;
        }
    l->side = 0;
}
int main(void) {
    if (ui_init("assets/OrbitSans.ttf"))
        return 1;
    ui_set_timezone(60);
    const char *tags[LANG_COUNT] = {"de", "en", "fr", "es"};
    for (int lang = 0; lang < LANG_COUNT; lang++) {
        i18n_set(lang);
        UiState *s = calloc(1, sizeof(*s));
        UiLocal *l = calloc(1, sizeof(*l));
        strcpy(s->username, "Alex");
        strcpy(s->oid, "PQ96-D2FM-5DBS");
        strcpy(s->friend_code, "EZB3C-R2AGP-9GGSD-DZDAQ");
        s->profile = 1;
        s->net = NET_INTERNET;
        s->me.color = 0;
        s->me.status = ST_ONLINE;
        strcpy(s->me.custom, "Ready for a race");
        strcpy(s->me.bio, "Better nights start with friends.");
        strcpy(s->me.game, "Uncharted 2");
        s->me.created = (uint32_t)1794657600 - 86400 * 120;
        s->set.notify = 255;
        s->set.lang_auto = 1;
        strcpy(s->net_detail, T(T_NET_TUNNEL));
        strcpy(s->local_ip, "192.168.0.23");
        strcpy(s->tunnel, "bore.pub:28399");
        s->tunnel_online = 1;
        strcpy(s->server_id, "a12b34cd56ef7890");
        /* servers */
        UiGroup *g = server(s, "ORBIT GAMING", "Alex", 1, 0, 0);
        cat(g, "INFORMATION");
        cat(g, "COMMUNITY");
        cat(g, "VOICE CHANNELS");
        chan(g, "welcome", CH_TEXT, 0, 2);
        chan(g, "rules", CH_TEXT, 0, 1);
        chan(g, "announcements", CH_TEXT, 0, 3);
        UiChannel *gen = chan(g, "general", CH_TEXT, 1, 40);
        snprintf(gen->topic, sizeof(gen->topic), "Your PS3 crew. Your next great night.");
        chan(g, "ps3-games", CH_TEXT, 1, 12);
        chan(g, "screenshots", CH_TEXT, 1, 4);
        chan(g, "off-topic", CH_TEXT, 1, 7);
        UiChannel *v = chan(g, "General Voice", CH_VOICE, 2, 0);
        v->call = 2;
        chan(g, "Gaming Room", CH_VOICE, 2, 0);
        UiChannel *lounge = chan(g, "Private Lounge", CH_VOICE, 2, 0);
        lounge->view = 2;
        g->call = 2;
        g->ncall = 2;
        g->caller[0].ch = 7;
        g->caller[0].user = 1;
        snprintf(g->caller[0].name, 49, "Lena");
        g->caller[1].ch = 7;
        g->caller[1].user = 0;
        snprintf(g->caller[1].name, 49, "Alex");
        snprintf(g->desc, sizeof(g->desc), "Your place for game nights, trophy hunts and PS3 classics.");
        snprintf(g->welcome, sizeof(g->welcome), "Welcome! Read #rules, then say hello in #general.");
        UiGroup *g2 = server(s, "Racing Crew", "Jonas", 0, 3, 1);
        chan(g2, "general", CH_TEXT, -1, 5);
        chan(g2, "Voice Chat", CH_VOICE, -1, 0);
        g2->unread = 1;
        /* private chats */
        dm(s, "Lena", ST_ONLINE, 3, 0, 1);
        dm(s, "Mika", ST_AWAY, 4, 4, 0);
        dm(s, "Jonas", ST_OFFLINE, 1, 6, 0);
        s->unread_dm = 1;
        s->unread_server = 1;
        /* friends */
        friend(s, "Lena", FR_FRIENDS, ST_ONLINE, 3, 0, "Up for Gran Turismo");
        friend(s, "Mika", FR_FRIENDS, ST_AWAY, 4, 4, "");
        friend(s, "Jonas", FR_FRIENDS, ST_OFFLINE, 1, 6, "");
        friend(s, "Sam", FR_INCOMING, ST_OFFLINE, 6, 2, "");
        s->requests = 1;
        note(s, N_FRIEND_REQUEST, "Sam sent you a friend request.", 300);
        note(s, N_DM, "Lena: Ready for a race tonight?", 120);
        note(s, N_MENTION, "Mika mentioned you: @Alex joining us tonight?", 60);
        /* chat in #general */
        s->selected = 0;
        s->channel = gen->id;
        msg(s, "Lena", 0, 3600, "Who's up for Gran Turismo 5 tonight?", 3);
        msg(s, "Mika", 0, 3000, "Count me in. I'll bring the Racing Crew.", 4);
        UiMessage *m = &s->messages[1];
        m->react[0] = 3;
        m->react[1] = 1;
        msg(s, "Alex", 1, 1200, "I'm on at 8 - opening the voice channel.", 0);
        m = &s->messages[2];
        m->reply = 1;
        snprintf(m->rname, 49, "Lena");
        snprintf(m->rtext, 97, "Who's up for Gran Turismo 5 tonight?");
        m->flags = MF_EDITED;
        msg(s, "Alex", 1, 30, "See you in General Voice!", 0);
        s->messages[3].pending = 1;
        s->outbox_count = 1;
        /* voice call */
        s->call_state = CALL_ONLINE;
        s->call_count = 2;
        snprintf(s->call_name, sizeof(s->call_name), "ORBIT GAMING / General Voice");
        snprintf(s->call_people[0].name, 49, "Lena");
        s->call_people[0].speaking = 1;
        s->call_people[0].volume = 100;
        snprintf(s->call_people[1].name, 49, "Alex");
        s->call_people[1].self = 1;
        s->call_people[1].volume = 100;
        /* members of ORBIT GAMING */
        const char *mn[5] = {"Alex", "Lena", "Mika", "Jonas", "Sam"};
        int ms[5] = {ST_ONLINE, ST_ONLINE, ST_AWAY, ST_OFFLINE, ST_OFFLINE};
        int mr[5] = {1, 4, 8, 8, 8};
        for (int i = 0; i < 5; i++) {
            UiMember *x = &s->members[i];
            snprintf(x->name, 49, "%s", mn[i]);
            x->status = ms[i];
            x->roles = mr[i];
            x->color = i;
            x->owner = i == 0;
        }
        s->member_count = 5;
        s->members_group = g->id;
        s->members_link = g->link;

        set_row(s, l, TAB_HOME, ROW_ACTION, 0, 0, 0);
        s->call_state = CALL_OFF;
        shot(tags[lang], "1-home", s, l);
        s->call_state = CALL_ONLINE;
        set_row(s, l, TAB_SERVERS, ROW_CHANNEL, g->link, g->id, gen->id);
        shot(tags[lang], "2-server-chat", s, l);
        set_row(s, l, TAB_SERVERS, ROW_SERVER, g->link, g->id, 0);
        shot(tags[lang], "3-server-overview", s, l);
        set_row(s, l, TAB_SERVERS, ROW_CHANNEL, g->link, g->id, 7);
        s->call_link = g->link, s->call_group = g->id, s->call_channel = 7;
        shot(tags[lang], "4-voice-channel", s, l);
        set_row(s, l, TAB_FRIENDS, ROW_FRIEND, 0, 0, 0);
        shot(tags[lang], "5-friend-profile", s, l);
        set_row(s, l, TAB_FRIENDS, ROW_ACTION, 2, 0, 0);
        shot(tags[lang], "6-friend-code", s, l);
        set_row(s, l, TAB_MESSAGES, ROW_DM, s->groups[2].link, s->groups[2].id, 0);
        s->selected = 2;
        s->channel = 0;
        shot(tags[lang], "7-messages", s, l);
        s->selected = 0;
        s->channel = gen->id;
        set_row(s, l, TAB_SERVERS, ROW_CHANNEL, g->link, g->id, gen->id);
        l->msg_mode = 1;
        l->msg_sel = 1;
        shot(tags[lang], "8-message-mode", s, l);
        l->msg_mode = 0;
        /* list dialog: server settings */
        l->page = PAGE_LIST;
        snprintf(l->list_title, sizeof(l->list_title), "%s", T(T_M_SERVER_SETTINGS));
        const int keys[8] = {T_S_NAME, T_S_DESC, T_S_WELCOME, T_S_ICON, T_S_COLOR, T_S_PUBLIC, T_S_CHANNELS, T_S_ROLES};
        l->list_count = 8;
        for (int i = 0; i < 8; i++) {
            l->list[i].id = i;
            l->list[i].flags = i == 5 ? LI_CHECKED : 0;
            l->list[i].color = i == 4 ? 0x4f8cff : 0;
            snprintf(l->list[i].text, sizeof(l->list[i].text), "%s", T(keys[i]));
            snprintf(l->list[i].sub, sizeof(l->list[i].sub), "%s", i == 0 ? "ORBIT GAMING" : "");
        }
        l->list_focus = 6;
        shot(tags[lang], "9-server-settings", s, l);
        l->page = PAGE_CHAT;
        set_row(s, l, TAB_SETTINGS, ROW_SETTING, 1, 0, 0);
        shot(tags[lang], "10-settings-language", s, l);
        l->kb_active = 1;
        l->kb_layout = lang;
        l->kb_mode = 2;
        l->kb_max = 768;
        l->kb_row = 2;
        l->kb_col = 9;
        snprintf(l->kb_title, sizeof(l->kb_title), "#general");
        snprintf(l->kb_text, sizeof(l->kb_text), "%s", lang == LANG_FR ? "Ça marche, à ce soir" : lang == LANG_ES ? "¡Mañana sí!" : "See you tonight!");
        shot(tags[lang], "11-keyboard", s, l);
        free(s);
        free(l);
        ui_set_theme(0); /* the other themes in the next language */
    }
    ui_close();
    return 0;
}
