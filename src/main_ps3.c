#include "monocypher.h"
#include "rsxutil.h"
#include "ui.h"
#include "voice.h"
#include <io/pad.h>
#include <lv2/systime.h>
#include <malloc.h>
#include <net/net.h>
#include <net/netctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sysutil/sysutil.h>
#include <unistd.h>
#define APPDIR "/dev_hdd0/game/ORB000001/USRDIR"
enum {
    EDIT_NONE,
    EDIT_NAME,
    EDIT_MESSAGE,
    EDIT_GROUP,
    EDIT_INVITE,
    EDIT_RELAY,
    EDIT_FRIEND,
    EDIT_EDIT_MSG,
    EDIT_SEARCH,
    EDIT_CUSTOM,
    EDIT_BIO,
    EDIT_GAME,
    EDIT_SRV_NAME,
    EDIT_SRV_DESC,
    EDIT_SRV_WELCOME,
    EDIT_CAT_NEW,
    EDIT_CAT_RENAME,
    EDIT_CH_NEW_TEXT,
    EDIT_CH_NEW_VOICE,
    EDIT_CH_RENAME,
    EDIT_CH_TOPIC,
    EDIT_ROLE_NEW,
    EDIT_ROLE_RENAME
};
/* list dialogs */
enum {
    LK_MENU,
    LK_CREATE,
    LK_FRIEND_MENU,
    LK_PICK_SERVER,
    LK_GROUPDM,
    LK_BROWSE,
    LK_SEARCH,
    LK_MSG,
    LK_REACT,
    LK_PROFILE,
    LK_STATUS,
    LK_COLOR,
    LK_AVATAR,
    LK_LANG,
    LK_THEME,
    LK_NOTIFY,
    LK_AUDIO,
    LK_PRIVACY,
    LK_BLOCKED,
    LK_SERVER,
    LK_SRV_ICON,
    LK_SRV_COLOR,
    LK_SRV_BANNER,
    LK_CHANNELS,
    LK_CHANNEL,
    LK_CATEGORY,
    LK_PICK_CAT,
    LK_CH_VIEW,
    LK_CH_SEND,
    LK_ROLES,
    LK_ROLE,
    LK_ROLE_COLOR,
    LK_ROLE_PRIO,
    LK_ROLE_PERMS,
    LK_MEMBERS,
    LK_MEMBER,
    LK_MEMBER_ROLES,
    LK_BANNED,
    LK_CALLERS,
    LK_CALLER,
    LK_CONFIRM
};
static int running = 1, kb_edit, system_menu, system_lang = LANG_EN;
static OrbitApp application;
static UiState state;
static UiLocal local;
/* sidebar cursor per tab, kept by identity (rows move when lists change) */
static UiRow cursor[TAB_COUNT];
static int follow_selection;
static int groupdm_pick[ORBIT_FRIENDS];
static int list_stack[8][3], list_depth;
static int confirm_action, confirm_arg;
static void notice(const char *msg) {
    app_notice(&application, msg);
}
static void callback(u64 status, u64 param, void *user) {
    (void)param;
    (void)user;
    if (status == SYSUTIL_EXIT_GAME)
        running = 0;
    /* PS button: the XMB opens on top of Orbit. Chat and voice keep running underneath. */
    else if (status == SYSUTIL_MENU_OPEN)
        system_menu = 1;
    else if (status == SYSUTIL_MENU_CLOSE)
        system_menu = 0;
}
/* Network may come up after Orbit started (or change): the app asks again periodically. */
static int netinfo(char ip[16], char mask[16], char gw[16]) {
    union net_ctl_info info;
    memset(&info, 0, sizeof(info));
    if (netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info))
        return -1;
    snprintf(ip, 16, "%s", info.ip_address);
    memset(&info, 0, sizeof(info));
    if (!netCtlGetInfo(NET_CTL_INFO_NETMASK, &info))
        snprintf(mask, 16, "%s", info.netmask);
    memset(&info, 0, sizeof(info));
    if (!netCtlGetInfo(NET_CTL_INFO_DEFAULT_ROUTE, &info))
        snprintf(gw, 16, "%s", info.default_route);
    return 0;
}
/* ---------- helpers on the current selection ---------- */
static const UiGroup *sel_group(void) {
    return state.selected >= 0 && state.selected < state.group_count ? &state.groups[state.selected] : NULL;
}
static const UiChannel *sel_channel(void) {
    const UiGroup *g = sel_group();
    for (int i = 0; g && i < g->nch; i++)
        if (g->ch[i].id == state.channel)
            return &g->ch[i];
    return NULL;
}
static int current_rows(UiRow *rows) {
    return ui_rows(&state, local.tab, rows, 96);
}
static const UiRow *current_row(UiRow *rows) {
    int n = current_rows(rows);
    if (!n)
        return NULL;
    if (local.side >= n)
        local.side = n - 1;
    return &rows[local.side];
}
static const char *status_text_for(int st) {
    static const int keys[5] = {T_ST_ONLINE, T_ST_AWAY, T_ST_BUSY, T_ST_INVISIBLE, T_ST_OFFLINE};
    return T(keys[st < 0 || st > 4 ? 4 : st]);
}
static const UiFriend *friend_by_index(int i) {
    for (int k = 0; k < state.friend_count; k++)
        if (state.friends[k].i == i)
            return &state.friends[k];
    return NULL;
}
static int selectable(int kind) {
    return kind != ROW_HEADER && kind != ROW_CATEGORY;
}
/* opens what a row stands for (servers, channels and chats load their messages) */
static void activate_row(const UiRow *r) {
    local.history_offset = 0;
    local.msg_mode = 0;
    local.reply_id = local.edit_id = 0;
    if (!r) {
        app_want_members(&application, -1, -1);
        return;
    }
    if (r->kind == ROW_SERVER) {
        app_open(&application, r->a, r->b, -1);
        app_want_members(&application, r->a, r->b);
    } else if (r->kind == ROW_CHANNEL) {
        app_open(&application, r->a, r->b, r->c);
        app_want_members(&application, -1, -1);
    } else if (r->kind == ROW_DM) {
        app_open(&application, r->a, r->b, -1);
        app_want_members(&application, -1, -1);
    } else
        app_want_members(&application, -1, -1);
}
static void move_cursor(int dir) {
    UiRow rows[96];
    int n = current_rows(rows);
    if (!n)
        return;
    int i = local.side;
    for (int step = 0; step < n; step++) {
        i = (i + dir + n) % n;
        if (selectable(rows[i].kind))
            break;
    }
    local.side = i;
    cursor[local.tab] = rows[i];
    activate_row(&rows[i]);
}
/* after a snapshot: find the remembered row again */
static void sync_cursor(void) {
    UiRow rows[96];
    int n = current_rows(rows);
    if (!n) {
        local.side = 0;
        return;
    }
    if (follow_selection) {
        const UiGroup *g = sel_group();
        if (g) {
            int kind = g->kind == GK_DM ? ROW_DM : ROW_CHANNEL;
            for (int i = 0; i < n; i++)
                if (rows[i].kind == kind && rows[i].a == g->link && rows[i].b == g->id &&
                    (kind == ROW_DM || rows[i].c == state.channel)) {
                    local.tab = g->kind == GK_DM ? TAB_MESSAGES : TAB_SERVERS;
                    local.side = i;
                    cursor[local.tab] = rows[i];
                    follow_selection = 0;
                    return;
                }
        }
    }
    const UiRow *c = &cursor[local.tab];
    for (int i = 0; i < n; i++)
        if (rows[i].kind == c->kind && rows[i].a == c->a && rows[i].b == c->b &&
            (rows[i].kind != ROW_CHANNEL || rows[i].c == c->c)) {
            local.side = i;
            return;
        }
    if (local.side >= n)
        local.side = n - 1;
    while (local.side > 0 && !selectable(rows[local.side].kind))
        local.side--;
    if (!selectable(rows[local.side].kind) && n > 1)
        local.side = 1;
    cursor[local.tab] = rows[local.side];
}
static void switch_tab(int dir) {
    local.tab = (local.tab + dir + TAB_COUNT) % TAB_COUNT;
    local.msg_mode = 0;
    sync_cursor();
    UiRow rows[96];
    activate_row(current_row(rows));
}
/* ---------- in-app keyboard ---------- */
static void keyboard(int edit, const char *title, const char *initial, int max, int mode) {
    local.kb_active = 1;
    local.kb_row = 1;
    local.kb_col = 0;
    local.kb_mode = mode;
    local.kb_layout = i18n_lang();
    local.kb_shift = mode == 1 || (edit == EDIT_NAME && !initial[0]);
    local.kb_max = max < KB_TEXT - 1 ? max : KB_TEXT - 1;
    kb_edit = edit;
    snprintf(local.kb_title, sizeof(local.kb_title), "%s", title);
    snprintf(local.kb_text, sizeof(local.kb_text), "%s", initial);
}
static void kb_append(const char *t) {
    size_t n = strlen(local.kb_text), k = strlen(t);
    if (n + k > (size_t)local.kb_max)
        return;
    memcpy(local.kb_text + n, t, k + 1);
    /* Invitation codes: insert the dashes automatically after every five characters. */
    if (local.kb_mode == 1) {
        int symbols = 0;
        for (const char *p = local.kb_text; *p; p++)
            if (*p != '-')
                symbols++;
        n = strlen(local.kb_text);
        if (symbols % 5 == 0 && symbols < 20 && n < (size_t)local.kb_max)
            strcat(local.kb_text, "-");
    } else if (local.kb_shift == 1 && kb_edit != EDIT_INVITE && kb_edit != EDIT_FRIEND)
        local.kb_shift = 0; /* one capital letter, then lower case again */
}
static void kb_backspace(void) {
    size_t n = strlen(local.kb_text);
    while (n) {
        unsigned char c = (unsigned char)local.kb_text[--n];
        local.kb_text[n] = 0;
        if ((c & 0xc0) != 0x80)
            break;
    }
    if (local.kb_mode == 1 && n && local.kb_text[n - 1] == '-')
        local.kb_text[--n] = 0;
}
static void list_open(int kind, const char *title, int arg, int arg2);
static void list_back(void);
static int server_job(const char *op, cJSON *q) {
    const UiGroup *g = sel_group();
    if (!g) {
        cJSON_Delete(q);
        return -1;
    }
    cJSON_AddStringToObject(q, "op", op);
    cJSON_AddNumberToObject(q, "group", g->id);
    return app_job(&application, g->link, q);
}
static void kb_finish(int accept) {
    char value[KB_TEXT];
    snprintf(value, sizeof(value), "%s", local.kb_text);
    local.kb_active = 0;
    int edit = kb_edit;
    kb_edit = EDIT_NONE;
    char *v = value;
    while (*v == ' ')
        v++;
    size_t n = strlen(v);
    while (n && v[n - 1] == ' ')
        v[--n] = 0;
    if (edit == EDIT_MESSAGE) {
        snprintf(local.draft, sizeof(local.draft), "%s", value); /* never lose a draft */
        if (accept && n && app_send_reply(&application, v, local.reply_id))
            notice(T(T_NOT_SENT));
        else if (accept && n)
            local.reply_id = 0;
        return;
    }
    if (edit == EDIT_RELAY && accept && !n) {
        app_set_relay(&application, "", 1); /* empty: no relay */
        return;
    }
    UiProfile me = state.me;
    int text_ok = accept && n;
    /* fields that may be emptied */
    if (accept && (edit == EDIT_CUSTOM || edit == EDIT_BIO || edit == EDIT_GAME)) {
        if (edit == EDIT_CUSTOM)
            snprintf(me.custom, sizeof(me.custom), "%s", v);
        if (edit == EDIT_BIO)
            snprintf(me.bio, sizeof(me.bio), "%s", v);
        if (edit == EDIT_GAME)
            snprintf(me.game, sizeof(me.game), "%s", v);
        app_set_profile(&application, &me);
        return;
    }
    if (accept && (edit == EDIT_SRV_DESC || edit == EDIT_SRV_WELCOME || edit == EDIT_CH_TOPIC)) {
        cJSON *q = cJSON_CreateObject();
        if (edit == EDIT_CH_TOPIC) {
            cJSON_AddNumberToObject(q, "ch", local.list_arg);
            cJSON_AddStringToObject(q, "topic", v);
            server_job("channel_update", q);
        } else {
            cJSON_AddStringToObject(q, edit == EDIT_SRV_DESC ? "desc" : "welcome", v);
            server_job("server_update", q);
        }
        return;
    }
    if (!text_ok)
        return;
    switch (edit) {
    case EDIT_NAME:
        if (app_set_name(&application, v))
            notice(T(T_E_NAME));
        break;
    case EDIT_GROUP:
        app_create_server(&application, v, local.list_arg2);
        follow_selection = 1;
        break;
    case EDIT_INVITE:
        if (!app_join(&application, v)) {
            local.page = PAGE_CHAT;
            follow_selection = 1;
        }
        break;
    case EDIT_RELAY:
        app_set_relay(&application, v, 1);
        break;
    case EDIT_FRIEND:
        app_friend_add(&application, v);
        break;
    case EDIT_EDIT_MSG:
        app_edit(&application, local.edit_id, v);
        local.edit_id = 0;
        break;
    case EDIT_SEARCH:
        if (!app_search(&application, v))
            list_open(LK_SEARCH, T(T_SEARCH_RESULTS), 0, 0);
        break;
    case EDIT_SRV_NAME: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddStringToObject(q, "name", v);
        server_job("server_update", q);
        break;
    }
    case EDIT_CAT_NEW:
    case EDIT_CH_NEW_TEXT:
    case EDIT_CH_NEW_VOICE:
    case EDIT_ROLE_NEW: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddStringToObject(q, "name", v);
        if (edit == EDIT_CH_NEW_VOICE)
            cJSON_AddNumberToObject(q, "type", CH_VOICE);
        server_job(edit == EDIT_CAT_NEW ? "category_add" : edit == EDIT_ROLE_NEW ? "role_add" : "channel_add", q);
        break;
    }
    case EDIT_CAT_RENAME:
    case EDIT_CH_RENAME:
    case EDIT_ROLE_RENAME: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, edit == EDIT_CAT_RENAME ? "cat" : edit == EDIT_CH_RENAME ? "ch" : "role",
                                local.list_arg);
        cJSON_AddStringToObject(q, "name", v);
        server_job(edit == EDIT_CAT_RENAME ? "category_update"
                   : edit == EDIT_CH_RENAME ? "channel_update"
                                            : "role_update",
                   q);
        break;
    }
    }
}
enum {
    B_UP = 1,
    B_DOWN = 2,
    B_LEFT = 4,
    B_RIGHT = 8,
    B_CROSS = 16,
    B_CIRCLE = 32,
    B_SQUARE = 64,
    B_TRIANGLE = 128,
    B_SELECT = 256,
    B_START = 512,
    B_L1 = 1024,
    B_R1 = 2048,
    B_R3 = 4096,
    B_R2 = 8192,
    B_L2 = 16384
};
static void kb_input(unsigned b) {
    if (b & B_UP) {
        if (local.kb_row == 4)
            local.kb_col = (const int[]){0, 4, 7, 9}[local.kb_col];
        local.kb_row = (local.kb_row + KB_ROWS - 1) % KB_ROWS;
        if (local.kb_row == 4)
            local.kb_col = local.kb_col < 2 ? 0 : local.kb_col < 7 ? 1 : local.kb_col < 9 ? 2 : 3;
    }
    if (b & B_DOWN) {
        if (local.kb_row == 4)
            local.kb_col = (const int[]){0, 4, 7, 9}[local.kb_col];
        local.kb_row = (local.kb_row + 1) % KB_ROWS;
        if (local.kb_row == 4)
            local.kb_col = local.kb_col < 2 ? 0 : local.kb_col < 7 ? 1 : local.kb_col < 9 ? 2 : 3;
    }
    int cols = ui_kb_cols(local.kb_row);
    if (b & B_LEFT)
        local.kb_col = (local.kb_col + cols - 1) % cols;
    if (b & B_RIGHT)
        local.kb_col = (local.kb_col + 1) % cols;
    if (b & B_SQUARE)
        kb_backspace();
    if (b & B_TRIANGLE)
        kb_append(" ");
    if (b & (B_L1 | B_L2))
        local.kb_shift = !local.kb_shift;
    if (b & B_R1) /* other keyboard layout: German, English, French, Spanish */
        local.kb_layout = (local.kb_layout + 1) % LANG_COUNT;
    if (b & B_CROSS) {
        int special = ui_kb_special(local.kb_row, local.kb_col);
        if (special == KB_SHIFT)
            local.kb_shift = !local.kb_shift;
        else if (special == KB_SPACE)
            kb_append(" ");
        else if (special == KB_BACK)
            kb_backspace();
        else if (special == KB_DONE) {
            kb_finish(1);
            return;
        } else {
            const char *k = ui_kb_key(local.kb_layout, local.kb_row, local.kb_col, local.kb_shift);
            if (k)
                kb_append(k);
        }
    }
    if (b & B_START) {
        kb_finish(1);
        return;
    }
    if (b & B_CIRCLE)
        kb_finish(0);
}
static unsigned buttons(void) {
    padInfo info;
    padData pad;
    memset(&info, 0, sizeof(info));
    if (ioPadGetInfo(&info))
        return 0;
    static unsigned held[7];
    unsigned all = 0;
    /* Every connected controller can steer: no "wrong port" after reconnecting a pad. */
    for (int i = 0; i < 7; i++) {
        if (!info.status[i]) {
            held[i] = 0;
            continue;
        }
        memset(&pad, 0, sizeof(pad));
        if (!ioPadGetData(i, &pad) && pad.len > 0)
            held[i] = (pad.BTN_UP ? B_UP : 0) | (pad.BTN_DOWN ? B_DOWN : 0) |
                      (pad.BTN_LEFT ? B_LEFT : 0) | (pad.BTN_RIGHT ? B_RIGHT : 0) |
                      (pad.BTN_CROSS ? B_CROSS : 0) | (pad.BTN_CIRCLE ? B_CIRCLE : 0) |
                      (pad.BTN_SQUARE ? B_SQUARE : 0) | (pad.BTN_TRIANGLE ? B_TRIANGLE : 0) |
                      (pad.BTN_SELECT ? B_SELECT : 0) | (pad.BTN_START ? B_START : 0) |
                      (pad.BTN_L1 ? B_L1 : 0) | (pad.BTN_R1 ? B_R1 : 0) |
                      (pad.BTN_R3 ? B_R3 : 0) | (pad.BTN_R2 ? B_R2 : 0) | (pad.BTN_L2 ? B_L2 : 0);
        all |= held[i];
    }
    /* a DualSense claimed for its speaker no longer appears as a pad: read it directly */
    all |= voice_ds_buttons();
    return all;
}
/* ---------- list dialogs ---------- */
static void list_clear(const char *title) {
    snprintf(local.list_title, sizeof(local.list_title), "%s", title);
    local.list_note[0] = 0;
    local.list_count = 0;
}
static void list_add(int id, const char *text, const char *sub, int flags, uint32_t color) {
    if (local.list_count >= 24)
        return;
    typeof(local.list[0]) *it = &local.list[local.list_count++];
    it->id = id;
    it->flags = flags;
    it->color = color;
    snprintf(it->text, sizeof(it->text), "%s", text ? text : "");
    snprintf(it->sub, sizeof(it->sub), "%s", sub ? sub : "");
}
static void list_build(void);
static void list_open(int kind, const char *title, int arg, int arg2) {
    if (local.page == PAGE_LIST && list_depth < 8) {
        list_stack[list_depth][0] = local.list_kind;
        list_stack[list_depth][1] = local.list_arg;
        list_stack[list_depth][2] = local.list_arg2;
        list_depth++;
    } else if (local.page != PAGE_LIST)
        list_depth = 0;
    local.page = PAGE_LIST;
    local.list_kind = kind;
    local.list_arg = arg;
    local.list_arg2 = arg2;
    local.list_focus = local.list_top = 0;
    list_clear(title);
    list_build();
    snprintf(local.list_title, sizeof(local.list_title), "%s", title);
    while (local.list_focus < local.list_count && (local.list[local.list_focus].flags & LI_HEADER))
        local.list_focus++;
}
static void list_back(void) {
    if (list_depth > 0) {
        list_depth--;
        char title[96];
        local.list_kind = list_stack[list_depth][0];
        local.list_arg = list_stack[list_depth][1];
        local.list_arg2 = list_stack[list_depth][2];
        snprintf(title, sizeof(title), "%s", local.list_title);
        local.list_focus = local.list_top = 0;
        list_build();
    } else
        local.page = PAGE_CHAT;
}
static void list_close(void) {
    if (local.list_kind == LK_AUDIO && state.mic_test)
        app_mic_test(&application, 0); /* leaving the audio settings ends the test */
    list_depth = 0;
    local.page = PAGE_CHAT;
}
static const char *perm_names(int bit) {
    static const int keys[10] = {T_P_MANAGE_SERVER, T_P_MANAGE_CHANNELS, T_P_MANAGE_ROLES, T_P_INVITE,
                                 T_P_SEND, T_P_DELETE, T_P_KICK, T_P_BAN, T_P_MANAGE_VOICE, T_P_MUTE};
    return T(keys[bit % 10]);
}
static const UiMember *member_by_user(int user) {
    for (int i = 0; i < state.member_count; i++)
        if (state.members[i].user == user)
            return &state.members[i];
    return NULL;
}
static const UiRole *role_by_index(const UiGroup *g, int i) {
    for (int k = 0; g && k < g->nrole; k++)
        if (g->role[k].i == i)
            return &g->role[k];
    return NULL;
}
static const char *const symbol_names[12] = {"Aa", "★", "♥", "♪", "☺", "✓", "GG", "XD", "#1", "PS", "OK", "77"};
static const uint32_t pick_colors[12] = {0x4f8cff, 0x39d98a, 0xf4b445, 0xff6f91, 0xa98bff, 0x2fc6d6,
                                         0xff8a4c, 0x7bd35a, 0xe46cff, 0x5ad1ff, 0xffd166, 0xc2c7cf};
/* (re)builds the items of the open list from the current state */
static void list_build(void) {
    const UiGroup *g = sel_group();
    const UiChannel *ch = sel_channel();
    char sub[160];
    int kind = local.list_kind, a = local.list_arg;
    char title[96];
    snprintf(title, sizeof(title), "%s", local.list_title);
    local.list_count = 0;
    local.list_note[0] = 0;
    switch (kind) {
    case LK_MENU: {
        UiRow rows[96];
        const UiRow *r = current_row(rows);
        int chat = r && (r->kind == ROW_DM || r->kind == ROW_CHANNEL);
        if (chat && g) {
            list_add(1, T(g->muted_chat ? T_M_UNMUTE_CHAT : T_M_MUTE_CHAT), g->name, 0, 0);
            if (ch && ch->type == CH_TEXT)
                list_add(2, T(T_M_SEARCH), NULL, 0, 0);
            if (state.call_state != CALL_OFF && state.call_count)
                list_add(3, T(T_M_PARTICIPANTS), state.call_name, 0, 0);
        }
        if (r && r->kind == ROW_SERVER && g) {
            list_add(4, T(T_M_OPEN_SERVER), g->name, 0, 0);
            list_add(13, T(g->muted_chat ? T_M_UNMUTE_SERVER : T_M_MUTE_SERVER), NULL, 0, 0);
        }
        if (g && g->kind == GK_SERVER && (chat || (r && r->kind == ROW_SERVER))) {
            list_add(5, T(T_M_SERVER_SETTINGS), g->name, 0, 0);
            list_add(6, T(T_M_INVITE), NULL, g->perms & P_INVITE ? 0 : LI_DISABLED, 0);
            list_add(14, T(T_M_MEMBERS), NULL, 0, 0);
        }
        if (g && (chat || (r && r->kind == ROW_SERVER)))
            list_add(7, T(state.call_state != CALL_OFF ? T_M_LEAVE_VOICE : T_M_JOIN_VOICE), NULL, 0, 0);
        list_add(8, T(T_M_JOIN_CODE), NULL, 0, 0);
        list_add(9, T(T_M_CREATE_SERVER), NULL, 0, 0);
        list_add(10, T(T_SET_NETWORK), NULL, 0, 0);
        if (g && (chat || (r && r->kind == ROW_SERVER)))
            list_add(11, T(g->own && g->perms == P_ALL ? (g->kind == GK_DM ? T_M_DELETE_DM : T_M_DELETE_SERVER)
                                                        : (g->kind == GK_DM ? T_M_LEAVE_DM : T_M_LEAVE_SERVER)),
                     g->name, LI_DANGER, 0);
        list_add(12, T(T_M_QUIT), T(T_M_QUIT_SUB), 0, 0);
        break;
    }
    case LK_CREATE:
        list_add(1, T(T_CREATE_TEMPLATE), T(T_CREATE_TEMPLATE_SUB), 0, 0);
        list_add(0, T(T_CREATE_SIMPLE), T(T_CREATE_SIMPLE_SUB), 0, 0);
        break;
    case LK_FRIEND_MENU: {
        const UiFriend *f = friend_by_index(a);
        if (!f)
            break;
        if (f->state == FR_FRIENDS) {
            list_add(1, T(T_H_MESSAGE_FRIEND), f->name, 0, 0);
            list_add(2, T(T_H_CALL), NULL, 0, 0);
            list_add(3, T(T_H_TO_SERVER), NULL, 0, 0);
        }
        if (f->state == FR_INCOMING) {
            list_add(6, T(T_H_ACCEPT), NULL, 0, 0);
            list_add(7, T(T_H_DECLINE), NULL, 0, 0);
        }
        list_add(4, T(f->state == FR_OUTGOING ? T_F_CANCEL : T_F_REMOVE), NULL, LI_DANGER, 0);
        list_add(5, T(T_F_BLOCK), T(T_F_BLOCK_SUB), LI_DANGER, 0);
        break;
    }
    case LK_PICK_SERVER:
        for (int i = 0; i < state.group_count; i++)
            if (state.groups[i].kind == GK_SERVER && state.groups[i].own && (state.groups[i].perms & P_INVITE))
                list_add(state.groups[i].id, state.groups[i].name, NULL, 0, pick_colors[state.groups[i].color % 12]);
        if (!local.list_count)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_NO_OWN_SERVER));
        break;
    case LK_GROUPDM: {
        int chosen = 0;
        for (int i = 0; i < state.friend_count; i++) {
            const UiFriend *f = &state.friends[i];
            if (f->state != FR_FRIENDS)
                continue;
            int on = groupdm_pick[f->i & (ORBIT_FRIENDS - 1)] == 1;
            chosen += on;
            list_add(f->i, f->name, status_text_for(f->status), on ? LI_CHECKED : 0, 0);
        }
        char t[80];
        TF(t, sizeof(t), T_GROUPDM_CREATE, chosen);
        list_add(-1, t, NULL, chosen ? 0 : LI_DISABLED, 0);
        if (local.list_count == 1)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_NO_FRIENDS_YET));
        break;
    }
    case LK_BROWSE:
        for (int i = 0; i < state.browse_count; i++) {
            const UiBrowse *b = &state.browse[i];
            TF(sub, sizeof(sub), T_BROWSE_SUB, b->members, b->host);
            list_add(i, b->name, sub, 0, pick_colors[b->color % 12]);
        }
        snprintf(local.list_note, sizeof(local.list_note), "%s",
                 state.browse_state == 1 ? T(T_SEARCHING) : !state.browse_count ? T(T_BROWSE_NONE) : T(T_BROWSE_NOTE));
        break;
    case LK_SEARCH:
        for (int i = 0; i < state.search_count; i++)
            list_add(i, state.search[i].text, state.search[i].name, 0, 0);
        snprintf(local.list_note, sizeof(local.list_note), "%s",
                 state.search_state == 1 ? T(T_SEARCHING) : !state.search_count ? T(T_SEARCH_NONE) : "");
        break;
    case LK_MSG: {
        const UiMessage *m = a >= 0 && a < state.message_count ? &state.messages[a] : NULL;
        if (!m)
            break;
        snprintf(local.list_note, sizeof(local.list_note), "%s: %.150s", m->name, m->text);
        int alive = !(m->flags & MF_DELETED) && !m->pending;
        list_add(1, T(T_MSG_REPLY), NULL, alive ? 0 : LI_DISABLED, 0);
        list_add(2, T(T_MSG_REACT), NULL, alive ? 0 : LI_DISABLED, 0);
        if (m->mine && alive)
            list_add(3, T(T_MSG_EDIT), NULL, 0, 0);
        if (alive && (m->mine || (g && (g->perms & P_DELETE))))
            list_add(4, T(T_MSG_DELETE), NULL, LI_DANGER, 0);
        break;
    }
    case LK_REACT:
        for (int i = 0; i < ORBIT_REACTIONS; i++)
            list_add(i, ui_reaction(i), NULL, 0, 0);
        break;
    case LK_PROFILE:
        list_add(1, T(T_P_NAME), state.username, 0, 0);
        list_add(2, T(T_P_STATUS), status_text_for(state.me.status), 0, 0);
        list_add(3, T(T_P_CUSTOM), state.me.custom[0] ? state.me.custom : T(T_EMPTY), 0, 0);
        list_add(4, T(T_P_BIO), state.me.bio[0] ? state.me.bio : T(T_EMPTY), 0, 0);
        list_add(5, T(T_P_GAME), state.me.game[0] ? state.me.game : T(T_EMPTY), 0, 0);
        list_add(6, T(T_P_COLOR), NULL, 0, pick_colors[state.me.color % 12]);
        list_add(7, T(T_P_AVATAR), symbol_names[state.me.avatar % 12], 0, 0);
        snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_P_GAME_NOTE));
        break;
    case LK_STATUS:
        for (int i = ST_ONLINE; i <= ST_INVISIBLE; i++)
            list_add(i, status_text_for(i), i == ST_INVISIBLE ? T(T_INVISIBLE_SUB) : NULL,
                     LI_RADIO | (state.me.status == i ? LI_CHECKED : 0), 0);
        break;
    case LK_COLOR:
    case LK_SRV_COLOR:
    case LK_ROLE_COLOR:
        for (int i = 0; i < 12; i++) {
            char t[32];
            TF(t, sizeof(t), T_COLOR_N, i + 1);
            list_add(i, t, NULL, 0, pick_colors[i]);
        }
        break;
    case LK_AVATAR:
    case LK_SRV_ICON:
        for (int i = 0; i < 12; i++)
            list_add(i, i ? symbol_names[i] : T(T_INITIAL), NULL, 0, 0);
        break;
    case LK_SRV_BANNER:
        for (int i = 0; i < 8; i++) {
            char t[32];
            TF(t, sizeof(t), T_BANNER_N, i + 1);
            list_add(i, t, NULL, g && g->banner == i ? LI_RADIO | LI_CHECKED : LI_RADIO, 0);
        }
        break;
    case LK_LANG:
        list_add(-1, T(T_LANG_AUTO), i18n_lang_name(system_lang), LI_RADIO | (state.set.lang_auto ? LI_CHECKED : 0), 0);
        for (int i = 0; i < LANG_COUNT; i++)
            list_add(i, i18n_lang_name(i), NULL, LI_RADIO | (!state.set.lang_auto && state.set.lang == i ? LI_CHECKED : 0), 0);
        snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_LANGUAGE_NOTE));
        break;
    case LK_THEME: {
        static const int keys[THEME_COUNT] = {T_THEME_DARK, T_THEME_BLUE, T_THEME_PURPLE, T_THEME_MINIMAL};
        for (int i = 0; i < THEME_COUNT; i++)
            list_add(i, T(keys[i]), NULL, LI_RADIO | (state.set.theme == i ? LI_CHECKED : 0), 0);
        break;
    }
    case LK_NOTIFY: {
        static const int nk[N_COUNT] = {T_NT_FRIEND_REQUEST, T_NT_FRIEND_ACCEPTED, T_NT_DM, T_NT_GROUP,
                                        T_NT_INVITE, T_NT_CALL, T_NT_MENTION, T_NT_MODERATION};
        for (int i = 0; i < N_COUNT; i++)
            list_add(i, T(nk[i]), NULL, state.set.notify & (1 << i) ? LI_CHECKED : 0, 0);
        break;
    }
    case LK_AUDIO:

        list_add(5, T(T_MIC_TEST), T(T_MIC_TEST_SUB), state.mic_test ? LI_CHECKED : 0, 0);
        snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_DS_NATIVE));
        break;
    case LK_PRIVACY:
        list_add(1, T(T_PRIVACY_INVISIBLE), T(T_INVISIBLE_SUB), state.me.status == ST_INVISIBLE ? LI_CHECKED : 0, 0);
        list_add(2, T(T_BLOCKED_PEOPLE), NULL, 0, 0);
        break;
    case LK_BLOCKED:
        for (int i = 0; i < state.friend_count; i++)
            if (state.friends[i].state == FR_BLOCKED)
                list_add(state.friends[i].i, state.friends[i].name, T(T_UNBLOCK_SUB), 0, 0);
        if (!local.list_count)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_NOBODY_BLOCKED));
        break;
    case LK_SERVER:
        if (!g)
            break;
        {
            int ms = g->perms & P_MANAGE_SERVER ? 0 : LI_DISABLED;
            list_add(1, T(T_S_NAME), g->name, ms, 0);
            list_add(2, T(T_S_DESC), g->desc[0] ? g->desc : T(T_EMPTY), ms, 0);
            list_add(3, T(T_S_WELCOME), g->welcome[0] ? g->welcome : T(T_EMPTY), ms, 0);
            list_add(4, T(T_S_ICON), symbol_names[g->icon % 12], ms, 0);
            list_add(5, T(T_S_COLOR), NULL, ms, pick_colors[g->color % 12]);
            list_add(6, T(T_S_BANNER), NULL, ms, 0);
            list_add(7, T(T_S_PUBLIC), T(T_S_PUBLIC_SUB), ms | (g->open ? LI_CHECKED : 0), 0);
            list_add(8, T(T_S_CHANNELS), NULL, g->perms & (P_MANAGE_CHANNELS | P_MANAGE_VOICE) ? 0 : LI_DISABLED, 0);
            list_add(9, T(T_S_ROLES), NULL, g->perms & P_MANAGE_ROLES ? 0 : LI_DISABLED, 0);
            list_add(10, T(T_M_MEMBERS), NULL, 0, 0);
            list_add(11, T(T_S_BANNED), NULL, g->perms & P_BAN ? 0 : LI_DISABLED, 0);
            list_add(12, T(T_S_ROTATE), T(T_S_ROTATE_SUB), ms, 0);
        }
        break;
    case LK_CHANNELS:
        if (!g)
            break;
        for (int c = -1; c < g->ncat; c++) {
            if (c >= 0)
                list_add(1000 + g->cat[c].i, g->cat[c].name, T(T_CATEGORY), 0, 0x636a74);
            for (int k = 0; k < g->nch; k++)
                if (g->ch[k].cat == (c < 0 ? -1 : g->cat[c].i))
                    list_add(g->ch[k].id, g->ch[k].name, T(g->ch[k].type == CH_VOICE ? T_VOICE_CHANNEL : T_TEXT_CHANNEL),
                             0, 0);
        }
        list_add(-1, T(T_NEW_TEXT_CHANNEL), NULL, 0, 0);
        list_add(-2, T(T_NEW_VOICE_CHANNEL), NULL, 0, 0);
        list_add(-3, T(T_NEW_CATEGORY), NULL, 0, 0);
        break;
    case LK_CHANNEL:
        list_add(1, T(T_C_RENAME), NULL, 0, 0);
        list_add(2, T(T_C_TOPIC), NULL, 0, 0);
        list_add(3, T(T_C_UP), NULL, 0, 0);
        list_add(4, T(T_C_DOWN), NULL, 0, 0);
        list_add(5, T(T_C_CATEGORY), NULL, 0, 0);
        list_add(6, T(T_C_VIEW), T(T_C_VIEW_SUB), 0, 0);
        list_add(7, T(T_C_SEND), T(T_C_SEND_SUB), 0, 0);
        list_add(8, T(T_C_DELETE), NULL, LI_DANGER, 0);
        break;
    case LK_CATEGORY:
        list_add(1, T(T_C_RENAME), NULL, 0, 0);
        list_add(3, T(T_C_UP), NULL, 0, 0);
        list_add(4, T(T_C_DOWN), NULL, 0, 0);
        list_add(8, T(T_CAT_DELETE), T(T_CAT_DELETE_SUB), LI_DANGER, 0);
        break;
    case LK_PICK_CAT:
        list_add(-1, T(T_NO_CATEGORY), NULL, 0, 0);
        for (int c = 0; g && c < g->ncat; c++)
            list_add(g->cat[c].i, g->cat[c].name, NULL, 0, 0);
        break;
    case LK_CH_VIEW:
    case LK_CH_SEND: {
        const UiChannel *h = NULL;
        for (int k = 0; g && k < g->nch; k++)
            if (g->ch[k].id == a)
                h = &g->ch[k];
        int mask = h ? (kind == LK_CH_VIEW ? h->view : h->send) : 0;
        list_add(-1, T(T_EVERYONE), NULL, LI_RADIO | (!mask ? LI_CHECKED : 0), 0);
        for (int k = 0; g && k < g->nrole; k++)
            list_add(g->role[k].i, g->role[k].name, NULL, mask & (1 << g->role[k].i) ? LI_CHECKED : 0,
                     g->role[k].color);
        break;
    }
    case LK_ROLES:
        for (int k = 0; g && k < g->nrole; k++) {
            char t[48];
            TF(t, sizeof(t), T_PRIORITY_N, g->role[k].prio);
            list_add(g->role[k].i, g->role[k].name, t, 0, g->role[k].color);
        }
        list_add(-1, T(T_NEW_ROLE), NULL, 0, 0);
        break;
    case LK_ROLE:
        list_add(1, T(T_C_RENAME), NULL, 0, 0);
        list_add(2, T(T_R_COLOR), NULL, 0, 0);
        list_add(3, T(T_R_PRIORITY), NULL, 0, 0);
        list_add(4, T(T_R_PERMS), NULL, 0, 0);
        list_add(8, T(T_R_DELETE), NULL, LI_DANGER, 0);
        break;
    case LK_ROLE_PRIO:
        for (int p = 90; p >= 10; p -= 10) {
            char t[32];
            TF(t, sizeof(t), T_PRIORITY_N, p);
            list_add(p, t, NULL, 0, 0);
        }
        break;
    case LK_ROLE_PERMS: {
        const UiRole *ro = role_by_index(g, a);
        for (int b = 0; b < 10; b++)
            list_add(1 << b, perm_names(b), NULL, ro && (ro->perms & (1u << b)) ? LI_CHECKED : 0, 0);
        snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_PERMS_NOTE));
        break;
    }
    case LK_MEMBERS:
        for (int i = 0; i < state.member_count; i++) {
            const UiMember *m = &state.members[i];
            list_add(m->user, m->name, m->owner ? T(T_OWNER) : status_text_for(m->status), 0,
                     m->status == ST_OFFLINE ? 0x636a74 : 0x39d98a);
        }
        if (!state.member_count)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_LOADING));
        break;
    case LK_MEMBER: {
        const UiMember *m = member_by_user(a);
        if (!m)
            break;
        snprintf(local.list_note, sizeof(local.list_note), "%s  ·  %s%s%s", m->name, m->oid,
                 m->bio[0] ? "  ·  " : "", m->bio);
        int p = g ? g->perms : 0;
        list_add(1, T(T_MB_ROLES), NULL, p & P_MANAGE_ROLES ? 0 : LI_DISABLED, 0);
        list_add(2, T(m->smuted ? T_MB_UNMUTE : T_MB_MUTE), NULL, p & P_MUTE ? 0 : LI_DISABLED, 0);
        list_add(3, T(T_MB_KICK), NULL, (p & P_KICK ? 0 : LI_DISABLED) | LI_DANGER, 0);
        list_add(4, T(T_MB_BAN), NULL, (p & P_BAN ? 0 : LI_DISABLED) | LI_DANGER, 0);
        break;
    }
    case LK_MEMBER_ROLES: {
        const UiMember *m = member_by_user(a);
        for (int k = 0; g && m && k < g->nrole; k++)
            list_add(g->role[k].i, g->role[k].name, NULL, m->roles & (1 << g->role[k].i) ? LI_CHECKED : 0,
                     g->role[k].color);
        break;
    }
    case LK_BANNED:
        for (int i = 0; i < state.banned_count; i++)
            list_add(state.banned[i].user, state.banned[i].name, T(T_UNBAN_SUB), 0, 0);
        if (!state.banned_count)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_NOBODY_BANNED));
        break;
    case LK_CALLERS:
        for (int i = 0; i < state.call_count; i++) {
            const UiCallPerson *p = &state.call_people[i];
            if (p->self)
                continue;
            char t[48];
            if (p->local_muted)
                snprintf(t, sizeof(t), "%s", T(T_LOCAL_MUTED));
            else
                TF(t, sizeof(t), T_VOLUME_N, p->volume);
            list_add(i, p->name, t, 0, p->speaking ? 0x39d98a : 0);
        }
        if (!local.list_count)
            snprintf(local.list_note, sizeof(local.list_note), "%s", T(T_CALL_ALONE));
        break;
    case LK_CALLER: {
        const UiCallPerson *p = a >= 0 && a < state.call_count ? &state.call_people[a] : NULL;
        if (!p)
            break;
        list_add(1, T(T_LOCAL_MUTE), T(T_LOCAL_MUTE_SUB), p->local_muted ? LI_CHECKED : 0, 0);
        for (int v = 50; v <= 200; v += 50) {
            char t[32];
            TF(t, sizeof(t), T_VOLUME_N, v);
            list_add(v, t, NULL, LI_RADIO | (!p->local_muted && p->volume == v ? LI_CHECKED : 0), 0);
        }
        if (g && (g->perms & P_MUTE))
            list_add(2, T(p->server_muted ? T_MB_UNMUTE : T_MB_MUTE), T(T_SERVER_MUTE_SUB), 0, 0);
        break;
    }
    case LK_CONFIRM:
        list_add(1, T(T_CONFIRM_YES), NULL, LI_DANGER, 0);
        list_add(0, T(T_H_CANCEL), NULL, 0, 0);
        break;
    }
    snprintf(local.list_title, sizeof(local.list_title), "%s", title);
    if (local.list_focus >= local.list_count)
        local.list_focus = local.list_count ? local.list_count - 1 : 0;
}
static void confirm(const char *title, int action_id, int arg) {
    confirm_action = action_id;
    confirm_arg = arg;
    list_open(LK_CONFIRM, title, 0, 0);
}
static int has_group(void) {
    return sel_group() != NULL;
}
static void open_invite(void) {
    if (!has_group()) {
        notice(T(T_CREATE_FIRST));
        return;
    }
    app_invite(&application);
    local.page = PAGE_INVITE;
}
static void open_code(void) {
    keyboard(EDIT_INVITE, T(T_KB_INVITE_TITLE), "", 23, 1);
}
static void message_actions(void);
static void do_confirmed(void) {
    const UiGroup *g = sel_group();
    switch (confirm_action) {
    case 1:
        app_leave(&application);
        local.history_offset = 0;
        break;
    case 2: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "ch", confirm_arg);
        server_job("channel_remove", q);
        break;
    }
    case 3: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "cat", confirm_arg);
        server_job("category_remove", q);
        break;
    }
    case 4: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "role", confirm_arg);
        server_job("role_remove", q);
        break;
    }
    case 5:
    case 6: {
        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "user", confirm_arg);
        server_job(confirm_action == 5 ? "kick" : "ban", q);
        break;
    }
    case 7:
        app_friend_op(&application, confirm_arg, "friend_remove", 0);
        break;
    case 8:
        app_friend_op(&application, confirm_arg, "friend_block", 0);
        break;
    case 9:
        app_delete_message(&application, confirm_arg);
        break;
    }
    (void)g;
    list_close();
}
/* X on a list item */
static void list_choose(void) {
    if (!local.list_count)
        return;
    typeof(local.list[0]) *it = &local.list[local.list_focus];
    if (it->flags & (LI_DISABLED | LI_HEADER))
        return;
    const UiGroup *g = sel_group();
    int id = it->id, a = local.list_arg;
    UiProfile me = state.me;
    UiSettings set = state.set;
    cJSON *q;
    switch (local.list_kind) {
    case LK_MENU:
        list_close();
        switch (id) {
        case 1:
            if (g)
                app_mute_chat(&application, g->link, g->id, !g->muted_chat);
            break;
        case 2:
            keyboard(EDIT_SEARCH, T(T_M_SEARCH), "", 64, 0);
            break;
        case 3:
            list_open(LK_CALLERS, T(T_M_PARTICIPANTS), 0, 0);
            break;
        case 4:
            if (g) {
                int ch = -1;
                for (int i = 0; i < g->nch; i++)
                    if (g->ch[i].type == CH_TEXT && (ch < 0 || g->ch[i].pos < g->ch[ch].pos))
                        ch = i;
                if (ch >= 0) {
                    app_open(&application, g->link, g->id, g->ch[ch].id);
                    follow_selection = 1;
                }
            }
            break;
        case 5:
            list_open(LK_SERVER, T(T_M_SERVER_SETTINGS), 0, 0);
            break;
        case 6:
            open_invite();
            break;
        case 7:
            app_call_toggle(&application);
            break;
        case 8:
            open_code();
            break;
        case 9:
            list_open(LK_CREATE, T(T_M_CREATE_SERVER), 0, 0);
            break;
        case 10:
            local.page = PAGE_NETWORK;
            break;
        case 11:
            confirm(T(g && g->own && g->perms == P_ALL ? T_DELETE_Q : T_LEAVE_Q), 1, 0);
            break;
        case 12:
            running = 0;
            break;
        case 13:
            if (g)
                app_mute_chat(&application, g->link, g->id, !g->muted_chat);
            break;
        case 14:
            if (g)
                app_want_members(&application, g->link, g->id);
            list_open(LK_MEMBERS, T(T_M_MEMBERS), 0, 0);
            break;
        }
        return;
    case LK_CREATE:
        local.list_arg2 = id;
        list_close();
        local.list_arg2 = id;
        keyboard(EDIT_GROUP, T(T_KB_SERVER_NAME), "", 64, 0);
        return;
    case LK_FRIEND_MENU:
        list_close();
        if (id == 1 && !app_friend_message(&application, a))
            follow_selection = 1;
        if (id == 2 && !app_friend_call(&application, a))
            follow_selection = 1;
        if (id == 3)
            list_open(LK_PICK_SERVER, T(T_H_TO_SERVER), a, 0);
        if (id == 4)
            confirm(T(T_F_REMOVE_Q), 7, a);
        if (id == 5)
            confirm(T(T_F_BLOCK_Q), 8, a);
        if (id == 6)
            app_friend_op(&application, a, "friend_respond", 1);
        if (id == 7)
            app_friend_op(&application, a, "friend_respond", 0);
        return;
    case LK_PICK_SERVER:
        list_close();
        app_friend_invite(&application, a, 0, id);
        return;
    case LK_GROUPDM:
        if (id >= 0) {
            groupdm_pick[id & (ORBIT_FRIENDS - 1)] ^= 1;
        } else {
            int picked[ORBIT_FRIENDS], n = 0;
            for (int i = 0; i < ORBIT_FRIENDS; i++)
                if (groupdm_pick[i] == 1)
                    picked[n++] = i;
            list_close();
            if (!app_group_dm(&application, picked, n))
                follow_selection = 1;
            memset(groupdm_pick, 0, sizeof(groupdm_pick));
            return;
        }
        break;
    case LK_BROWSE:
        list_close();
        if (!app_join_public(&application, id))
            follow_selection = 1;
        return;
    case LK_SEARCH:
        return;
    case LK_MSG: {
        const UiMessage *m = a >= 0 && a < state.message_count ? &state.messages[a] : NULL;
        if (!m)
            return;
        list_close();
        if (id == 1) {
            local.reply_id = m->id;
            local.edit_id = 0;
            local.msg_mode = 0;
            keyboard(EDIT_MESSAGE, T(T_MSG_REPLY), local.draft, 768, 2);
        } else if (id == 2) {
            list_open(LK_REACT, T(T_MSG_REACT), a, 0);
        } else if (id == 3) {
            local.edit_id = m->id;
            local.reply_id = 0;
            local.msg_mode = 0;
            keyboard(EDIT_EDIT_MSG, T(T_MSG_EDIT), m->text, 768, 0);
        } else if (id == 4)
            confirm(T(T_MSG_DELETE_Q), 9, m->id);
        return;
    }
    case LK_REACT: {
        const UiMessage *m = a >= 0 && a < state.message_count ? &state.messages[a] : NULL;
        list_close();
        if (m)
            app_react(&application, m->id, id);
        return;
    }
    case LK_PROFILE:
        if (id == 1)
            keyboard(EDIT_NAME, T(T_P_NAME), state.username, 48, 0);
        else if (id == 2)
            list_open(LK_STATUS, T(T_P_STATUS), 0, 0);
        else if (id == 3)
            keyboard(EDIT_CUSTOM, T(T_P_CUSTOM), state.me.custom, 64, 0);
        else if (id == 4)
            keyboard(EDIT_BIO, T(T_P_BIO), state.me.bio, 96, 0);
        else if (id == 5)
            keyboard(EDIT_GAME, T(T_P_GAME), state.me.game, 48, 0);
        else if (id == 6)
            list_open(LK_COLOR, T(T_P_COLOR), 0, 0);
        else if (id == 7)
            list_open(LK_AVATAR, T(T_P_AVATAR), 0, 0);
        return;
    case LK_STATUS:
        me.status = id;
        app_set_profile(&application, &me);
        list_back();
        return;
    case LK_COLOR:
        me.color = id;
        app_set_profile(&application, &me);
        list_back();
        return;
    case LK_AVATAR:
        me.avatar = id;
        app_set_profile(&application, &me);
        list_back();
        return;
    case LK_LANG:
        set.lang_auto = id < 0;
        if (id >= 0)
            set.lang = id;
        i18n_set(set.lang_auto ? system_lang : set.lang);
        app_set_settings(&application, &set);
        list_close();
        return;
    case LK_THEME:
        set.theme = id;
        ui_set_theme(id);
        app_set_settings(&application, &set);
        list_close();
        return;
    case LK_NOTIFY:
        set.notify ^= 1 << id;
        app_set_settings(&application, &set);
        break;
    case LK_AUDIO:
        if (id == 5) {
            app_mic_test(&application, !state.mic_test);
            break;
        }
        break;
    case LK_PRIVACY:
        if (id == 1) {
            me.status = me.status == ST_INVISIBLE ? ST_ONLINE : ST_INVISIBLE;
            app_set_profile(&application, &me);
        } else
            list_open(LK_BLOCKED, T(T_BLOCKED_PEOPLE), 0, 0);
        break;
    case LK_BLOCKED:
        app_friend_op(&application, id, "friend_unblock", 0);
        list_close();
        return;
    case LK_SERVER:
        if (!g)
            return;
        switch (id) {
        case 1:
            keyboard(EDIT_SRV_NAME, T(T_S_NAME), g->name, 64, 0);
            break;
        case 2:
            keyboard(EDIT_SRV_DESC, T(T_S_DESC), g->desc, 128, 0);
            break;
        case 3:
            keyboard(EDIT_SRV_WELCOME, T(T_S_WELCOME), g->welcome, 192, 0);
            break;
        case 4:
            list_open(LK_SRV_ICON, T(T_S_ICON), 0, 0);
            break;
        case 5:
            list_open(LK_SRV_COLOR, T(T_S_COLOR), 0, 0);
            break;
        case 6:
            list_open(LK_SRV_BANNER, T(T_S_BANNER), 0, 0);
            break;
        case 7:
            q = cJSON_CreateObject();
            cJSON_AddBoolToObject(q, "open", !g->open);
            server_job("server_update", q);
            break;
        case 8:
            list_open(LK_CHANNELS, T(T_S_CHANNELS), 0, 0);
            break;
        case 9:
            list_open(LK_ROLES, T(T_S_ROLES), 0, 0);
            break;
        case 10:
            app_want_members(&application, g->link, g->id);
            list_open(LK_MEMBERS, T(T_M_MEMBERS), 0, 0);
            break;
        case 11:
            app_want_members(&application, g->link, g->id);
            list_open(LK_BANNED, T(T_S_BANNED), 0, 0);
            break;
        case 12:
            server_job("rotate", cJSON_CreateObject());
            break;
        }
        return;
    case LK_SRV_ICON:
    case LK_SRV_COLOR:
    case LK_SRV_BANNER:
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, local.list_kind == LK_SRV_ICON ? "icon" : local.list_kind == LK_SRV_COLOR ? "color" : "banner", id);
        server_job("server_update", q);
        list_back();
        return;
    case LK_CHANNELS:
        if (id == -1)
            keyboard(EDIT_CH_NEW_TEXT, T(T_NEW_TEXT_CHANNEL), "", 32, 0);
        else if (id == -2)
            keyboard(EDIT_CH_NEW_VOICE, T(T_NEW_VOICE_CHANNEL), "", 32, 0);
        else if (id == -3)
            keyboard(EDIT_CAT_NEW, T(T_NEW_CATEGORY), "", 32, 0);
        else if (id >= 1000)
            list_open(LK_CATEGORY, it->text, id - 1000, 0);
        else
            list_open(LK_CHANNEL, it->text, id, 0);
        return;
    case LK_CHANNEL:
    case LK_CATEGORY: {
        int cat = local.list_kind == LK_CATEGORY;
        const char *key = cat ? "cat" : "ch", *op = cat ? "category_update" : "channel_update";
        if (id == 1) {
            local.list_arg = a;
            keyboard(cat ? EDIT_CAT_RENAME : EDIT_CH_RENAME, T(T_C_RENAME), it->text, 32, 0);
            local.list_arg = a;
        } else if (id == 2) {
            const UiChannel *h = NULL;
            for (int k = 0; g && k < g->nch; k++)
                if (g->ch[k].id == a)
                    h = &g->ch[k];
            keyboard(EDIT_CH_TOPIC, T(T_C_TOPIC), h ? h->topic : "", 96, 0);
            local.list_arg = a;
        } else if (id == 3 || id == 4) {
            q = cJSON_CreateObject();
            cJSON_AddNumberToObject(q, key, a);
            cJSON_AddNumberToObject(q, "move", id == 3 ? -1 : 1);
            server_job(op, q);
        } else if (id == 5)
            list_open(LK_PICK_CAT, T(T_C_CATEGORY), a, 0);
        else if (id == 6)
            list_open(LK_CH_VIEW, T(T_C_VIEW), a, 0);
        else if (id == 7)
            list_open(LK_CH_SEND, T(T_C_SEND), a, 0);
        else if (id == 8)
            confirm(T(cat ? T_CAT_DELETE : T_C_DELETE), cat ? 3 : 2, a);
        return;
    }
    case LK_PICK_CAT:
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "ch", a);
        cJSON_AddNumberToObject(q, "cat", id);
        server_job("channel_update", q);
        list_back();
        return;
    case LK_CH_VIEW:
    case LK_CH_SEND: {
        int mask = 0;
        for (int k = 0; g && k < g->nch; k++)
            if (g->ch[k].id == a)
                mask = local.list_kind == LK_CH_VIEW ? g->ch[k].view : g->ch[k].send;
        mask = id < 0 ? 0 : mask ^ (1 << id);
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "ch", a);
        cJSON_AddNumberToObject(q, local.list_kind == LK_CH_VIEW ? "view" : "send", mask);
        server_job("channel_update", q);
        return;
    }
    case LK_ROLES:
        if (id < 0)
            keyboard(EDIT_ROLE_NEW, T(T_NEW_ROLE), "", 24, 0);
        else
            list_open(LK_ROLE, it->text, id, 0);
        return;
    case LK_ROLE:
        if (id == 1) {
            keyboard(EDIT_ROLE_RENAME, T(T_C_RENAME), it->text, 24, 0);
            local.list_arg = a;
        } else if (id == 2)
            list_open(LK_ROLE_COLOR, T(T_R_COLOR), a, 0);
        else if (id == 3)
            list_open(LK_ROLE_PRIO, T(T_R_PRIORITY), a, 0);
        else if (id == 4)
            list_open(LK_ROLE_PERMS, T(T_R_PERMS), a, 0);
        else if (id == 8)
            confirm(T(T_R_DELETE), 4, a);
        return;
    case LK_ROLE_COLOR:
    case LK_ROLE_PRIO:
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "role", a);
        cJSON_AddNumberToObject(q, local.list_kind == LK_ROLE_COLOR ? "color" : "prio",
                                local.list_kind == LK_ROLE_COLOR ? (double)pick_colors[id % 12] : id);
        server_job("role_update", q);
        list_back();
        return;
    case LK_ROLE_PERMS: {
        const UiRole *ro = role_by_index(g, a);
        if (!ro)
            return;
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "role", a);
        cJSON_AddNumberToObject(q, "perms", (double)(ro->perms ^ (uint32_t)id));
        server_job("role_update", q);
        return;
    }
    case LK_MEMBERS:
        list_open(LK_MEMBER, it->text, id, 0);
        return;
    case LK_MEMBER: {
        const UiMember *m = member_by_user(a);
        if (!m)
            return;
        if (id == 1)
            list_open(LK_MEMBER_ROLES, T(T_MB_ROLES), a, 0);
        else if (id == 2) {
            q = cJSON_CreateObject();
            cJSON_AddNumberToObject(q, "user", a);
            cJSON_AddBoolToObject(q, "mute", !m->smuted);
            server_job("server_mute", q);
        } else if (id == 3)
            confirm(T(T_MB_KICK), 5, a);
        else if (id == 4)
            confirm(T(T_MB_BAN), 6, a);
        return;
    }
    case LK_MEMBER_ROLES: {
        const UiMember *m = member_by_user(a);
        if (!m)
            return;
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "user", a);
        cJSON_AddNumberToObject(q, "roles", m->roles ^ (1 << id));
        server_job("member_roles", q);
        return;
    }
    case LK_BANNED:
        q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "user", id);
        server_job("unban", q);
        return;
    case LK_CALLERS:
        list_open(LK_CALLER, it->text, id, 0);
        return;
    case LK_CALLER: {
        const UiCallPerson *p = a >= 0 && a < state.call_count ? &state.call_people[a] : NULL;
        if (!p)
            return;
        if (id == 1)
            app_call_person(&application, a, !p->local_muted, p->volume);
        else if (id == 2) {
            q = cJSON_CreateObject();
            cJSON_AddNumberToObject(q, "user", p->user);
            cJSON_AddBoolToObject(q, "mute", !p->server_muted);
            server_job("server_mute", q);
        } else
            app_call_person(&application, a, 0, id);
        return;
    }
    case LK_CONFIRM:
        if (id == 1)
            do_confirmed();
        else
            list_back();
        return;
    }
    list_build();
}
static void message_actions(void) {
    if (local.msg_sel >= 0 && local.msg_sel < state.message_count)
        list_open(LK_MSG, T(T_H_ACTIONS), local.msg_sel, 0);
}
static void setting_open(int which) {
    switch (which) {
    case 0:
        list_open(LK_PROFILE, T(T_SET_PROFILE), 0, 0);
        break;
    case 1:
        list_open(LK_LANG, T(T_SET_LANGUAGE), 0, 0);
        break;
    case 2:
        list_open(LK_THEME, T(T_SET_LOOK), 0, 0);
        break;
    case 3:
        list_open(LK_NOTIFY, T(T_SET_NOTIFY), 0, 0);
        break;
    case 4:
        list_open(LK_AUDIO, T(T_SET_AUDIO), 0, 0);
        break;
    case 5:
        local.page = PAGE_NETWORK;
        break;
    case 6:
        list_open(LK_PRIVACY, T(T_SET_PRIVACY), 0, 0);
        break;
    }
}
static void go_note(const UiNote *n) {
    if (n->type == N_FRIEND_REQUEST || n->type == N_FRIEND_ACCEPTED) {
        local.tab = TAB_FRIENDS;
        sync_cursor();
        return;
    }
    if (n->group < 0)
        return;
    app_open(&application, n->link, n->group, n->channel);
    follow_selection = 1;
}
static void row_cross(const UiRow *r) {
    if (!r)
        return;
    switch (r->kind) {
    case ROW_ACTION:
        if (r->a == 1)
            keyboard(EDIT_FRIEND, T(T_KB_FRIEND_TITLE), "", 23, 1);
        else if (r->a == 2)
            app_friend_code(&application);
        else if (r->a == 3) {
            memset(groupdm_pick, 0, sizeof(groupdm_pick));
            list_open(LK_GROUPDM, T(T_NEW_GROUP_DM), 0, 0);
        } else if (r->a == 4)
            list_open(LK_CREATE, T(T_CREATE_SERVER), 0, 0);
        else if (r->a == 5)
            open_code();
        else if (r->a == 6) {
            app_browse(&application);
            list_open(LK_BROWSE, T(T_PUBLIC_SERVERS), 0, 0);
        }
        break;
    case ROW_NOTE:
        if (r->a >= 0 && r->a < state.note_count)
            go_note(&state.notes[r->a]);
        break;
    case ROW_SETTING:
        setting_open(r->a);
        break;
    case ROW_FRIEND: {
        const UiFriend *f = friend_by_index(r->a);
        if (f && f->state == FR_INCOMING)
            app_friend_op(&application, f->i, "friend_respond", 1);
        else if (f && f->state == FR_FRIENDS && !app_friend_message(&application, f->i))
            follow_selection = 1;
        break;
    }
    case ROW_SERVER: {
        const UiGroup *g = sel_group();
        if (!g)
            break;
        int best = -1;
        for (int i = 0; i < g->nch; i++)
            if (g->ch[i].type == CH_TEXT && (best < 0 || g->ch[i].pos < g->ch[best].pos))
                best = i;
        if (best >= 0) {
            app_open(&application, g->link, g->id, g->ch[best].id);
            follow_selection = 1;
        }
        break;
    }
    case ROW_DM:
    case ROW_CHANNEL: {
        const UiChannel *h = sel_channel();
        const UiGroup *g = sel_group();
        if (h && h->type == CH_VOICE)
            app_call_join(&application, g->link, g->id, h->id);
        else if (g)
            keyboard(EDIT_MESSAGE, g->name, local.draft, 768, 2);
        break;
    }
    }
}
static void action(unsigned b) {
    if (local.kb_active) {
        kb_input(b);
        return;
    }
    /* available everywhere: voice chat */
    if (b & B_R2)
        app_call_mute(&application);
    switch (local.page) {
    case PAGE_WELCOME:
        if (b & (B_UP | B_DOWN))
            local.focus = !local.focus;
        if (b & B_START)
            running = 0;
        if (b & B_CROSS) {
            if (local.focus == 0)
                keyboard(EDIT_NAME, T(T_KB_NAME_TITLE), state.username, 48, 0);
            else {
                app_set_name(&application, state.username);
                local.page = PAGE_CHAT;
            }
        }
        return;
    case PAGE_LIST:
    case PAGE_MENU:
        if (b & (B_UP | B_DOWN)) {
            int dir = b & B_UP ? -1 : 1;
            for (int step = 0; step < local.list_count; step++) {
                local.list_focus = (local.list_focus + dir + local.list_count) % (local.list_count ? local.list_count : 1);
                if (!(local.list[local.list_focus].flags & LI_HEADER))
                    break;
            }
            if (local.list_focus < local.list_top)
                local.list_top = local.list_focus;
            if (local.list_focus >= local.list_top + 9)
                local.list_top = local.list_focus - 8;
        }
        if (b & B_CROSS)
            list_choose();
        else if (b & B_CIRCLE)
            list_back();
        return;
    case PAGE_INVITE:
        if (b & (B_CIRCLE | B_TRIANGLE | B_CROSS))
            local.page = PAGE_CHAT;
        return;
    case PAGE_LEAVE:
        if (b & B_CIRCLE)
            local.page = PAGE_CHAT;
        if (b & B_CROSS) {
            app_leave(&application);
            local.page = PAGE_CHAT;
            local.history_offset = 0;
        }
        return;
    case PAGE_NETWORK:
        if (b & B_CIRCLE)
            local.page = PAGE_CHAT;
        if (b & B_SQUARE) {
            app_retry(&application);
            notice(T(T_RECONNECTING_ALL));
        }
        if (b & B_TRIANGLE)
            keyboard(EDIT_RELAY, T(T_KB_RELAY_TITLE), state.relay, 100, 0);
        return;
    default:
        break;
    }
    UiRow rows[96];
    const UiRow *r = current_row(rows);
    int chat = r && (r->kind == ROW_DM || r->kind == ROW_CHANNEL) && sel_channel() &&
               sel_channel()->type == CH_TEXT;
    /* message mode (L2): pick a message to reply, react, edit or delete */
    if (local.msg_mode) {
        if (b & B_UP && local.msg_sel > 0)
            local.msg_sel--;
        if (b & B_DOWN && local.msg_sel < state.message_count - 1)
            local.msg_sel++;
        local.history_offset = state.message_count - 1 - local.msg_sel;
        if (b & B_CROSS)
            message_actions();
        if (b & B_TRIANGLE && local.msg_sel >= 0 && local.msg_sel < state.message_count)
            list_open(LK_REACT, T(T_MSG_REACT), local.msg_sel, 0);
        if (b & (B_CIRCLE | B_L2)) {
            local.msg_mode = 0;
            local.history_offset = 0;
        }
        return;
    }
    if (b & B_LEFT)
        switch_tab(-1);
    if (b & B_RIGHT)
        switch_tab(1);
    if (b & B_UP)
        move_cursor(-1);
    if (b & B_DOWN)
        move_cursor(1);
    if (b & B_L1 && local.history_offset < state.message_count - 1)
        local.history_offset++;
    if (b & B_R1 && local.history_offset > 0)
        local.history_offset--;
    if (b & B_L2 && chat && state.message_count) {
        local.msg_mode = 1;
        local.msg_sel = state.message_count - 1 - local.history_offset;
        if (local.msg_sel < 0)
            local.msg_sel = 0;
    }
    if (b & B_CROSS)
        row_cross(r);
    if (b & B_SQUARE) {
        const UiFriend *f = r && r->kind == ROW_FRIEND ? friend_by_index(r->a) : NULL;
        if (f && f->state == FR_INCOMING)
            app_friend_op(&application, f->i, "friend_respond", 0);
        else if (local.reply_id || local.edit_id)
            local.reply_id = local.edit_id = 0;
        else
            open_code();
    }
    if (b & B_SELECT) {
        const UiFriend *f = r && r->kind == ROW_FRIEND ? friend_by_index(r->a) : NULL;
        if (f && f->state == FR_FRIENDS) {
            if (!app_friend_call(&application, f->i))
                follow_selection = 1;
        } else
            app_call_toggle(&application);
    }
    if (b & B_TRIANGLE) {
        const UiFriend *f = r && r->kind == ROW_FRIEND ? friend_by_index(r->a) : NULL;
        if (f && f->state == FR_FRIENDS)
            list_open(LK_PICK_SERVER, T(T_H_TO_SERVER), f->i, 0);
        else if (r && (r->kind == ROW_SERVER || r->kind == ROW_CHANNEL))
            open_invite();
        else if (r && r->kind == ROW_ACTION && r->a == 2)
            app_friend_code(&application);
    }
    if (b & B_CIRCLE) {
        if (local.reply_id || local.edit_id)
            local.reply_id = local.edit_id = 0;
        else if (r && r->kind == ROW_FRIEND)
            list_open(LK_FRIEND_MENU, T(T_H_MENU), r->a, 0);
        else
            list_open(LK_MENU, T(T_H_MENU), 0, 0);
    }
    if (b & B_START)
        running = 0;
}
static uint64_t micros(void) {
    return (uint64_t)sysGetSystemTime();
}
int main(void) {
    mkdir(APPDIR "/data", 0700);
    netInitialize();
    int netctl = netCtlInit();
    char ip[16] = "127.0.0.1", mask[16] = "", gateway[16] = "", user[64] = "";
    netinfo(ip, mask, gateway);
    if (sysUtilGetSystemParamString(SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME, user, sizeof(user)))
        user[0] = 0;
    user[48] = 0;
    /* interface language = PS3 system language (checked on every start); English fallback */
    s32 lang = SYSUTIL_LANG_ENGLISH_US;
    if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_LANG, &lang))
        lang = SYSUTIL_LANG_ENGLISH_US;
    system_lang = i18n_from_ps3(lang);
    i18n_set(system_lang);
    s32 tz = 0, summer = 0;
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz);
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &summer);
    ui_set_timezone(tz + (summer ? 60 : 0));
    if (app_start(&application, APPDIR "/data", ip, mask, gateway, user, ORBIT_PORT))
        return 1;
    app_set_netinfo(&application, netinfo);
    /* Internet without router settings and without a PC: built-in tunnel (open bore protocol). */
    app_set_tunnel(&application, "bore.pub");
    /* Optional own relay (USRDIR/relay.txt). The old PC relay default is replaced by the tunnel. */
    FILE *rf = fopen(APPDIR "/relay.txt", "r");
    if (rf) {
        char relay[104] = "";
        if (fgets(relay, sizeof(relay), rf)) {
            relay[strcspn(relay, " \r\n")] = 0;
            if (strncmp(relay, "bore.pub", 8))
                app_set_relay(&application, relay, 0);
        }
        fclose(rf);
    }
    if (ui_init(APPDIR "/OrbitSans.ttf")) {
        app_stop(&application);
        return 2;
    }
    void *host = memalign(1024 * 1024, HOST_SIZE);
    if (!host) {
        ui_close();
        app_stop(&application);
        return 3;
    }
    gcmContextData *context = initScreen(host, HOST_SIZE);
    if (!context) {
        ui_close();
        app_stop(&application);
        return 4;
    }
    u16 width, height;
    rsxBuffer buffers[2];
    memset(buffers, 0, sizeof(buffers));
    uint32_t *canvas = NULL;
    int ok = getResolution(&width, &height);
    for (int i = 0; ok && i < 2; i++) {
        ok = makeBuffer(&buffers[i], width, height, i);
        if (ok)
            memset(buffers[i].ptr, 0, (size_t)width * height * 4);
    }
    if (ok)
        canvas = memalign(128, (size_t)width * height * 4);
    if (!canvas) {
        app_stop(&application);
        rsxFinish(context, 1);
        ui_close();
        return 5;
    }
    ioPadInit(7);
    sysUtilRegisterCallback(0, callback, NULL);
    app_snapshot(&application, &state);
    ui_set_theme(state.set.theme);
    local.page = state.profile ? PAGE_CHAT : PAGE_WELCOME;
    /* start where the last session ended: the chat that was open */
    {
        const UiGroup *g = sel_group();
        local.tab = g ? (g->kind == GK_DM ? TAB_MESSAGES : TAB_SERVERS) : TAB_HOME;
        follow_selection = g != NULL;
    }
    int index = 0, rendered = -1, generation = 0, sent_count = state.sent_count,
        buffer_generation[2] = {-1, -1}, toast_on = 0;
    unsigned previous = 0, repeat_direction = 0;
    uint64_t repeat_at = 0, stats_at = micros(), last_flip = micros();
    int show_stats = 0, frames = 0, fps = 0, render_us = 0, frame_us = 0, worst_render = 0;
    uint64_t frame_sum = 0;
    flip(context, 1);
    /* Animated intro while the network starts in the background. Any button skips it. */
    {
        uint64_t intro_start = micros();
        unsigned before = buttons();
        int skipped = 0;
        while (running) {
            sysUtilCheckCallback();
            unsigned b = system_menu ? 0 : buttons();
            if ((b & ~before) && !skipped) {
                skipped = 1; /* jump to the final fade instead of cutting hard */
                uint64_t fade = (uint64_t)((3.2f - 0.45f) * 1e6f);
                if (micros() - intro_start < fade)
                    intro_start = micros() - fade;
            }
            before = b;
            float t = (float)(micros() - intro_start) / 1e6f;
            int done = ui_intro(canvas, width, height, t, ORBIT_VERSION);
            waitFlip();
            memcpy(buffers[index].ptr, canvas, (size_t)width * height * 4);
            if (!flip(context, index))
                break;
            index ^= 1;
            if (done)
                break;
        }
        ui_intro_done();
        buffer_generation[0] = buffer_generation[1] = -1;
        previous = buttons(); /* the skip press must not trigger an action in the app */
    }
    int last_lang = i18n_lang();
    while (running) {
        sysUtilCheckCallback();
        app_snapshot(&application, &state);
        sync_cursor();
        if (local.page == PAGE_LIST)
            list_build(); /* lists follow live data (members, results, call) */
        unsigned held = system_menu ? 0 : buttons(), pressed = held & ~previous;
        previous = held;
        unsigned direction = held & (B_UP | B_DOWN | B_LEFT | B_RIGHT | B_L1 | B_R1 | B_SQUARE);
        if (!local.kb_active)
            direction &= ~(B_SQUARE | B_LEFT | B_RIGHT); /* tabs: one step per press */
        uint64_t now = micros();
        local.now = orbit_millis();
        if (direction != repeat_direction) {
            repeat_direction = direction;
            repeat_at = now + 380000;
        } else if (direction && now >= repeat_at) {
            pressed |= direction;
            repeat_at = now + (local.kb_active ? 90000 : 110000);
        }
        int changed = 0;
        if (now - stats_at >= 1000000) {
            fps = (int)(((uint64_t)frames * 1000000 + (now - stats_at) / 2) / (now - stats_at));
            frame_us = frames ? (int)(frame_sum / (uint64_t)frames) : 0;
            frames = 0;
            frame_sum = 0;
            stats_at = now;
            if (show_stats)
                changed = 1;
        }
        if ((pressed & B_R3) && !local.kb_active) {
            show_stats = !show_stats;
            changed = 1;
        }
        if (state.sent_count != sent_count) {
            sent_count = state.sent_count;
            local.draft[0] = 0;
            local.history_offset = 0;
            changed = 1;
        }
        if (pressed) {
            action(pressed);
            changed = 1;
        }
        if (i18n_lang() != last_lang) {
            last_lang = i18n_lang();
            changed = 1;
        }
        int t = ui_toast_visible(&state, local.now);
        if (t != toast_on) {
            toast_on = t;
            changed = 1;
        }
        /* Only redraw when something visible changed; an idle screen costs almost nothing. */
        if (changed || state.revision != rendered) {
            uint64_t start = micros();
            ui_render(canvas, width, height, &state, &local);
            if (show_stats)
                ui_performance(fps, frame_us, render_us);
            render_us = (int)(micros() - start);
            if (render_us > worst_render)
                worst_render = render_us;
            rendered = state.revision;
            generation++;
        }
        waitFlip();
        uint64_t flipped = micros();
        frame_sum += flipped - last_flip;
        last_flip = flipped;
        if (buffer_generation[index] != generation) {
            memcpy(buffers[index].ptr, canvas, (size_t)width * height * 4);
            buffer_generation[index] = generation;
        }
        if (!flip(context, index))
            break;
        index ^= 1;
        frames++;
    }
    FILE *perf = fopen(APPDIR "/performance.txt", "w");
    if (perf) {
        fprintf(perf, "Orbit %s; %ux%u; fps=%d; frame=%dus; last_render=%dus; worst_render=%dus\n",
                ORBIT_VERSION, width, height, fps, frame_us, render_us, worst_render);
        fclose(perf);
    }
    app_stop(&application);
    sysUtilUnregisterCallback(0);
    ioPadEnd();
    rsxFinish(context, 1);
    free(canvas);
    ui_close();
    if (!netctl)
        netCtlTerm();
    netDeinitialize();
    crypto_wipe(&local, sizeof(local));
    return 0;
}
