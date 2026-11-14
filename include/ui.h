#ifndef ORBIT_UI_H
#define ORBIT_UI_H
#include "i18n.h"
#include "orbit.h"
#define UI_GROUPS 64
#define UI_MESSAGES 80
#define APP_LINKS 16
#define CALL_PEOPLE 12
#define UI_NOTES 24
#define UI_OUTBOX 32
#define UI_RESULTS 20
#define UI_DM_PEOPLE 9
enum { NET_CHECKING, NET_INTERNET, NET_HOME, NET_OFFLINE };
enum { LINK_OFF, LINK_CONNECTING, LINK_ONLINE, LINK_FAILED };
enum { CALL_OFF, CALL_CONNECTING, CALL_ONLINE, CALL_RECONNECTING };
enum { TAB_HOME, TAB_FRIENDS, TAB_MESSAGES, TAB_SERVERS, TAB_SETTINGS, TAB_COUNT };
/* notification types (each can be switched off in the settings) */
enum {
    N_FRIEND_REQUEST,
    N_FRIEND_ACCEPTED,
    N_DM,
    N_GROUP,
    N_INVITE,
    N_CALL,
    N_MENTION,
    N_MODERATION,
    N_COUNT
};
enum { THEME_DARK, THEME_BLUE, THEME_PURPLE, THEME_MINIMAL, THEME_COUNT };
typedef struct {
    int id, type, cat, pos, call, view, send;
    uint32_t last;
    char name[33], topic[97], lname[49], ltext[97];
} UiChannel;
typedef struct {
    int i, pos;
    char name[33];
} UiCategory;
typedef struct {
    int i, prio;
    uint32_t color, perms;
    char name[25];
} UiRole;
typedef struct {
    int user, status, color, avatar;
    char name[49], oid[ORBIT_ID_TEXT];
} UiPerson;
typedef struct {
    int user, ch, muted;
    char name[49];
} UiCaller;
typedef struct {
    int link, id, members, unread, state, own, call, kind, icon, color, banner, open, perms,
        myroles, smuted, online, ncat, nch, nrole, ndm, ncall, muted_chat;
    char name[65], host[49], people[200], desc[129], welcome[193];
    UiCategory cat[ORBIT_CATEGORIES];
    UiChannel ch[ORBIT_CHANNELS];
    UiRole role[ORBIT_ROLES];
    UiPerson dm[UI_DM_PEOPLE];
    UiCaller caller[CALL_PEOPLE];
} UiGroup;
typedef struct {
    int id, mine, flags, reply, user, color, avatar, pending, blocked;
    uint32_t time, rev;
    uint32_t react[ORBIT_REACTIONS];
    char name[49], oid[ORBIT_ID_TEXT], text[ORBIT_TEXT], rname[49], rtext[97];
} UiMessage;
typedef struct {
    int speaking, self, user, local_muted, volume, server_muted;
    char name[49];
} UiCallPerson;
typedef struct {
    int i, state, user, status, color, avatar, online, nmutual;
    uint32_t since, created;
    char name[49], fid[ORBIT_ID_TEXT], code[ORBIT_INVITE_TEXT], custom[65], bio[97], pub[65],
        game[49], mutual[200];
} UiFriend;
typedef struct {
    int user, status, color, avatar, roles, top, owner, smuted, call;
    uint32_t created;
    char name[49], oid[ORBIT_ID_TEXT], custom[65], bio[97], game[49];
} UiMember;
typedef struct {
    int type, link, group, channel;
    uint32_t time;
    char text[200];
} UiNote;
typedef struct {
    int link, id, members, icon, color;
    char name[65], desc[129], host[49];
} UiBrowse;
typedef struct {
    int link, group, channel;
    uint32_t time;
    char text[ORBIT_TEXT];
} UiOutbox;
/* own profile (saved locally, sent to every meeting point) */
typedef struct {
    int status, color, avatar;
    uint32_t created;
    char bio[97], game[49], custom[65];
} UiProfile;
/* settings */
typedef struct {
    int lang_auto, lang, theme, notify, ds_audio, ds_mic;
} UiSettings;
typedef struct {
    int profile, busy, group_count, selected, message_count, sent_count, net, invite_state,
        loading, revision, channel;
    uint32_t status_id, speak_mask;
    char status[200], username[49], invite_code[ORBIT_INVITE_TEXT], invite_group[65],
        invite_scope[300], net_detail[300], local_ip[16], public_ip[16], server_id[17];
    char link_error[200], relay[104], tunnel[128];
    int relay_online, tunnel_online;
    /* identity, profile, settings */
    char oid[ORBIT_ID_TEXT], friend_code[ORBIT_INVITE_TEXT];
    UiProfile me;
    UiSettings set;
    /* voice chat */
    int call_state, call_muted, call_count, call_here, call_mic, call_link, call_group,
        call_channel;
    char call_name[65], call_note[200], ds_note[160], mic_info[120];
    int mic_test, mic_level, mic_floor;
    UiCallPerson call_people[CALL_PEOPLE];
    UiGroup groups[UI_GROUPS];
    UiMessage messages[UI_MESSAGES];
    /* friends, notifications, lists fetched on demand */
    int friend_count, note_count, member_count, browse_count, search_count, outbox_count,
        requests, unread_dm, unread_server, members_group, members_link, search_state,
        browse_state, banned_count;
    UiFriend friends[ORBIT_FRIENDS];
    UiNote notes[UI_NOTES];
    UiMember members[ORBIT_USERS];
    UiPerson banned[ORBIT_USERS];
    UiBrowse browse[UI_RESULTS];
    UiMessage search[UI_RESULTS];
    UiOutbox outbox[UI_OUTBOX];
} UiState;
struct OrbitApp;
typedef struct {
    struct OrbitApp *owner;
    int used, own, state, running, user, history_group, history_channel, message_count,
        group_count, port;
    int reauth, remove, code_group, cached, thread_joinable, friend_link, synced, want_members,
        want_browse, member_count, banned_count;
    char join_code[9];
    uint64_t retry_at, backoff;
    char host[16], code[9], server[17], error[200], relay[24];
    char owner_pub[65], owner_name[49], owner_id[ORBIT_ID_TEXT];
    uint32_t history_rev;
    pthread_t thread;
    cJSON *job;
    cJSON *bg[8]; /* background requests of this link (never block the interface) */
    UiGroup groups[ORBIT_GROUPS];
    UiMessage messages[UI_MESSAGES];
    UiMember members[ORBIT_USERS];
    UiPerson banned[ORBIT_USERS];
} AppLink;
typedef int (*AppNetInfo)(char ip[16], char mask[16], char gateway[16]);
typedef struct OrbitApp {
    pthread_mutex_t lock;
    pthread_t manager;
    int stop, dirty, changed, busy, sent_count, profile, net, invite_state, port;
    int selected_link, selected_group, selected_channel, invite_wait, invite_wait_link,
        invite_wait_group, nat_now, invite_kind;
    uint64_t busy_since;
    uint32_t status_id;
    char data_dir[384], ip[16], netmask[16], gateway[16], broadcast[16], name[49];
    char status[200], server_id[17], invite_code[ORBIT_INVITE_TEXT], invite_group[65],
        invite_scope[300], net_detail[300];
    uint8_t device[32], server_key[32], id_secret[64], id_pub[32];
    char oid[ORBIT_ID_TEXT], home_code[9], friend_code[ORBIT_INVITE_TEXT];
    uint32_t seen[APP_LINKS][ORBIT_GROUPS][ORBIT_CHANNELS];
    uint8_t chat_muted[APP_LINKS][ORBIT_GROUPS];
    OrbitStore *store;
    OrbitServer *server;
    OrbitNat nat;
    AppNetInfo netinfo;
    UiProfile me;
    UiSettings set;
    /* relay ("Vermittler") */
    char relay[104], relay_reason[200];
    int relay_user, relay_online, relay_port;
    uint32_t relay_ip;
    OrbitRelayHost *relay_host;
    /* built-in internet tunnel */
    OrbitBore *bore;
    int bore_online, bore_port, invite_dispatch;
    uint32_t bore_ip;
    uint64_t invite_wait_since;
    char bore_host[ORBIT_HOST], bore_reason[200];
    /* voice chat: one call at a time */
    pthread_t call_thread;
    int call_running, call_joinable, call_leave, call_state, call_link, call_group, call_channel,
        call_count, mic_test;
    char call_name[65], call_note[200];
    struct {
        int user, local_muted, volume, server_muted;
        char name[49];
    } call_people[CALL_PEOPLE];
    /* friends (from this PS3's own meeting point), notifications, offline queue */
    int friend_count, note_count, outbox_count, browse_count, search_count, search_state,
        browse_state, friends_loaded, groups_loaded[APP_LINKS];
    UiFriend friends[ORBIT_FRIENDS];
    UiNote notes[UI_NOTES];
    UiOutbox outbox[UI_OUTBOX];
    UiBrowse browse[UI_RESULTS];
    UiMessage search[UI_RESULTS];
    char outbox_server[UI_OUTBOX][17];
    AppLink links[APP_LINKS];
} OrbitApp;
#define KB_TEXT 769
typedef struct {
    int page, focus, history_offset, menu_focus, tab, side, msg_mode, msg_sel, reply_id, edit_id;
    int list_focus, list_count, list_kind, list_arg, list_arg2, list_top;
    uint64_t now;
    char draft[ORBIT_TEXT];
    /* in-app keyboard (replaces the system dialog: no dimmed/black background) */
    int kb_active, kb_row, kb_col, kb_shift, kb_mode, kb_max, kb_layout;
    char kb_title[96], kb_text[KB_TEXT];
    /* generic list dialog (menus, settings, management) */
    char list_title[96], list_note[200];
    struct {
        int id, flags;
        uint32_t color;
        char text[80], sub[120];
    } list[24];
} UiLocal;
enum { LI_DISABLED = 1, LI_DANGER = 2, LI_CHECKED = 4, LI_HEADER = 8, LI_RADIO = 16 };
/* 0.6 remembered sign-in, read once for migration. */
typedef struct {
    int valid, hosting;
    char invite[240], email[97], token[65];
} OrbitRemember;
int orbit_remember_read(const char *dir, OrbitRemember *record);
int orbit_remember_write(const char *dir, const OrbitRemember *record);
void orbit_remember_clear(const char *dir, OrbitRemember *record);
enum { PAGE_CHAT, PAGE_MENU, PAGE_INVITE, PAGE_LEAVE, PAGE_NETWORK, PAGE_WELCOME, PAGE_HELP, PAGE_LIST };
int app_start(OrbitApp *app, const char *data_dir, const char *ip, const char *netmask,
              const char *gateway, const char *suggested_name, int port);
void app_set_netinfo(OrbitApp *app, AppNetInfo fn);
void app_stop(OrbitApp *app);
void app_snapshot(OrbitApp *app, UiState *state);
void app_select(OrbitApp *app, int direction);
/* choose what is shown: link + group + channel (channel -1: first text channel) */
void app_open(OrbitApp *app, int link, int group, int channel);
void app_notice(OrbitApp *app, const char *text);
void app_notice_key(OrbitApp *app, int key);
int app_set_name(OrbitApp *app, const char *name);
int app_create_group(OrbitApp *app, const char *name);
int app_join(OrbitApp *app, const char *invite);
int app_send(OrbitApp *app, const char *text);
int app_send_reply(OrbitApp *app, const char *text, int reply);
int app_invite(OrbitApp *app);
int app_leave(OrbitApp *app);
void app_retry(OrbitApp *app);
/* user = 1: chosen in the app (saved); 0: default from the package (relay.txt) */
void app_set_relay(OrbitApp *app, const char *relay, int user);
/* Internet tunnel server (e.g. "bore.pub"); empty = off */
void app_set_tunnel(OrbitApp *app, const char *server);
/* any request to the meeting point of a link (server management, messages ...). Takes job. */
int app_job(OrbitApp *app, int link, cJSON *job);
cJSON *app_cmd(const char *op, int group);
/* 1.0: messages */
int app_edit(OrbitApp *app, int id, const char *text);
int app_delete_message(OrbitApp *app, int id);
int app_react(OrbitApp *app, int id, int reaction);
int app_search(OrbitApp *app, const char *query);
/* 1.0: friends */
int app_friend_add(OrbitApp *app, const char *code);
int app_friend_op(OrbitApp *app, int index, const char *op, int accept);
int app_friend_message(OrbitApp *app, int index);
int app_friend_call(OrbitApp *app, int index);
int app_friend_invite(OrbitApp *app, int index, int link, int group);
int app_group_dm(OrbitApp *app, const int *friends, int count);
void app_friend_code(OrbitApp *app); /* refresh this console's friend code */
/* 1.0: servers */
int app_create_server(OrbitApp *app, const char *name, int with_template);
void app_want_members(OrbitApp *app, int link, int group);
void app_browse(OrbitApp *app);
int app_join_public(OrbitApp *app, int index);
void app_mute_chat(OrbitApp *app, int link, int group, int muted);
/* 1.0: profile, settings */
void app_set_profile(OrbitApp *app, const UiProfile *profile);
void app_set_settings(OrbitApp *app, const UiSettings *settings);
/* voice chat */
int app_call_toggle(OrbitApp *app);
int app_call_join(OrbitApp *app, int link, int group, int channel);
void app_call_mute(OrbitApp *app);
void app_call_person(OrbitApp *app, int index, int local_muted, int volume);
/* microphone test: hear your own voice after the codec; nothing is sent */
int app_mic_test(OrbitApp *app, int on);
int ui_init(const char *font_path);
void ui_close(void);
void ui_set_timezone(int minutes);
void ui_set_theme(int theme);
void ui_render(uint32_t *pixels, int width, int height, const UiState *state, const UiLocal *local);
void ui_performance(int fps, int frame_us, int render_us);
void ui_icon(uint32_t *pixels, int width, int height);
/* Animated intro; t in seconds. Returns 1 when finished. */
int ui_intro(uint32_t *pixels, int width, int height, float t, const char *version);
void ui_intro_done(void);
void ui_social(uint32_t *pixels, int width, int height, const char *version);
int ui_toast_visible(const UiState *s, uint64_t now);
/* sidebar rows of the current tab, shared by renderer and input handling */
enum { ROW_NONE, ROW_SERVER, ROW_CHANNEL, ROW_CATEGORY, ROW_ACTION, ROW_DM, ROW_FRIEND, ROW_NOTE,
       ROW_SETTING, ROW_HEADER };
typedef struct {
    int kind, a, b, c; /* server: a=group index; channel: a=group, b=channel; action: a=id */
} UiRow;
int ui_rows(const UiState *s, int tab, UiRow *rows, int cap);
/* keyboard layout: returns the UTF-8 text of a key, or NULL for special keys */
const char *ui_kb_key(int layout, int row, int col, int shift);
int ui_kb_cols(int row);
#define KB_ROWS 5
enum { KB_SHIFT = 100, KB_SPACE, KB_BACK, KB_DONE };
int ui_kb_special(int row, int col);
const char *ui_reaction(int i);
void ui_audio_probe(uint32_t *pixels, int width, int height, const char *status,
                    const char rows[][100], int count, int player, int remaining, int level);
#endif
