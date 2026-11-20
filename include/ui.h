#ifndef ORBIT_UI_H
#define ORBIT_UI_H
#include "i18n.h"
#include "orbit.h"
#include "games.h"
#include "directory.h"
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
/* ORBIT NEXT top bar order: Home, Discover, Friends, Games, Servers, then Messages and Settings icons */
enum { TAB_HOME, TAB_DISCOVER, TAB_FRIENDS, TAB_GAMES, TAB_SERVERS, TAB_MESSAGES, TAB_SETTINGS, TAB_COUNT };
/* full-screen ("spotlight") tabs draw the top bar instead of the sidebar */
#define TAB_SPOTLIGHT(t) ((t) == TAB_HOME || (t) == TAB_DISCOVER || (t) == TAB_GAMES)

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
/* stored ids stay stable (0-3 existed before 1.2); Performance also reduces effects and motion */
enum { THEME_DARK, THEME_BLUE, THEME_PURPLE, THEME_MINIMAL, THEME_GLASS, THEME_SPACE, THEME_CRYSTAL, THEME_PERF,
       THEME_COUNT };
#define THEME_KEYS {T_THEME_DARK, T_THEME_BLUE, T_THEME_PURPLE, T_THEME_MINIMAL, T_THEME_GLASS, T_THEME_SPACE, T_THEME_CRYSTAL, T_THEME_PERF}
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
    char name[49], oid[ORBIT_ID_TEXT], pic[33], anim[33];
} UiPerson;
typedef struct {
    int user, ch, muted;
    char name[49];
} UiCaller;
typedef struct {
    int link, id, members, unread, state, own, call, kind, icon, color, banner, open, perms,
        myroles, smuted, online, ncat, nch, nrole, ndm, ncall, muted_chat, reports;
    char name[65], host[49], people[200], desc[129], welcome[193];
    char pic[33], anim[33]; /* 1.2: server icon picture / animation */
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
    char name[49], oid[ORBIT_ID_TEXT], text[ORBIT_TEXT], rname[49], rtext[97], pic[33], anim[33];
} UiMessage;
typedef struct {
    int speaking, self, user, local_muted, volume, server_muted;
    char name[49];
} UiCallPerson;
typedef struct {
    int i, state, user, status, color, avatar, online, nmutual;
    uint32_t since, created;
    char name[49], fid[ORBIT_ID_TEXT], code[ORBIT_INVITE_TEXT], custom[65], bio[97], pub[65],
        game[49], mutual[200], pic[33], anim[33];
    int playing;                       /* 1.2: game detected by the friend's presence plugin */
    char playing_id[10], playing_name[65];
} UiFriend;
typedef struct {
    int user, status, color, avatar, roles, top, owner, smuted, call;
    uint32_t created;
    char name[49], oid[ORBIT_ID_TEXT], custom[65], bio[97], game[49], pic[33], anim[33];
} UiMember;
typedef struct {
    int type, link, group, channel;
    uint32_t time;
    char text[200];
} UiNote;
/* public community as shown in Discover. Comes from the persistent directory cache, so a
 * community stays listed (as "host offline") when its PS3 is switched off. */
#define UI_BROWSE 48
enum { BROWSE_IDLE, BROWSE_ASKING, BROWSE_DONE, BROWSE_NO_CONNECTION };
typedef struct {
    int link, id, members, icon, color;
    int online, member, live; /* live: its host PS3 is reachable right now */
    uint32_t seen;            /* last time the host confirmed it (unix time) */
    int central;              /* 1.2: listed by the public directory */
    char name[65], desc[129], host[49], server[17], invite[ORBIT_INVITE_TEXT];
    char pic[33], anim[33];
} UiBrowse;
/* 1.2: a report as a moderator sees it (fetched on demand) */
#define UI_REPORTS 32
typedef struct {
    int id, user, reason, state, banned;
    uint32_t msg, time;
    char target[49], reporter[49], text[129], detail[97];
} UiReport;
/* 1.2: server appearance change log entry */
#define UI_AUDIT 40
typedef struct {
    int what;
    uint32_t time;
    char user[49];
} UiAudit;
/* 1.2 Discover search and filter */
enum { DIR_F_ALL, DIR_F_LIVE, DIR_F_JOINED, DIR_F_NEW, DIR_F_COUNT };
enum { DIR_S_POPULAR, DIR_S_NAME, DIR_S_SEEN, DIR_S_COUNT };
/* persistent directory entry (data/directory.json) */
#define APP_DIR 64
typedef struct {
    int id, members, online, icon, color, member;
    int hidden;   /* 1.2: hidden by the user ("report and hide") */
    int reported; /* 1.2: report reason + 1 for the public directory, 0 = none */
    int report_sent, central;
    uint32_t seen;
    char name[65], desc[129], host[49], server[17], invite[ORBIT_INVITE_TEXT];
    char pic[33], anim[33];
} AppDirEntry;
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
    char pic[33]; /* 1.2: own profile picture (data/pics/<pic>.pic), "" = symbol avatar */
    char anim[33]; /* 1.2: own animated picture (data/pics/<anim>.ani), pic = its first frame */
} UiProfile;
/* settings */
typedef struct {
    int lang_auto, lang, theme, notify, ds_audio, ds_mic;
    int bg, bg_preview; /* 1.1: background notifications (XMB plugin) and message text in them */
    int sharp;          /* 1.2: 1 = draw at the full output resolution (1080p), 0 = fast 720p + upscale */
    int sounds;         /* 1.2: UI sound volume in percent (0 = off) */
    int suggest;        /* 1.2: word suggestions in the keyboard (learned locally) */
    int dnd;            /* 1.2: do not disturb: no event sounds, no XMB pop-ups */
    int update_auto;    /* 1.2: check the signed update manifest at start (off by default) */
    int presence;       /* 1.2: let the presence plugin tell friends what I play (off by default) */
    int dir_publish;    /* 1.2: list my open servers in the public directory (off by default) */
    int anim_off;       /* 1.2: show animated pictures as still pictures */
    int reduce_motion;  /* 1.2: no animations at all (pictures, transitions where possible) */
} UiSettings;
typedef struct {
    int pic_gen; /* 1.2: profile pictures arrived */
    int import_state, import_err, import_target, import_anchor; /* 1.2 picture import: IMPORT_* / IMG_E_*, -1 = profile */
    char import_pic[33], import_anim[33];                       /* prepared picture for the preview */
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
        browse_state, banned_count, report_count, report_state, report_group;
    UiFriend friends[ORBIT_FRIENDS];
    UiNote notes[UI_NOTES];
    UiMember members[ORBIT_USERS];
    UiPerson banned[ORBIT_USERS];
    UiBrowse browse[UI_BROWSE];
    UiMessage search[UI_RESULTS];
    UiOutbox outbox[UI_OUTBOX];
    UiReport reports[UI_REPORTS];
    UiAudit audit[UI_AUDIT];
    int audit_count, audit_state; /* 0 none, 1 loading, 2 done, 3 failed */
    char dir_query[33];
    int dir_filter, dir_sort, dir_total; /* dir_total: communities before search/filter */
    int dir_central;  /* 1.2: public directory: 0 not set up, 1 set up, 2 reached, 3 unreachable */
    int dir_listed;   /* own open servers currently listed there */
    char dir_host[64];
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
    char wid[17], wkey[65]; /* 1.1: background-notification credential on that host */
    int watch_renew;        /* 1.1: ask the host for a new credential (old one locked out) */
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
    int pic_gen;
    char pic_want[8][33]; /* pictures being fetched */
    uint64_t pic_when[8];
    /* 1.2 animations: one download at a time (in pieces), uploads waiting for their pieces */
    char anim_want[8][33];
    uint64_t anim_when[8];
    char dl_hash[33];
    int dl_link;
    uint8_t *dl_buf;
    size_t dl_n, dl_total;
    uint64_t dl_since;
    struct {
        int link;
        char hash[33];
        cJSON *then; /* sent when the host has every piece */
    } upl[4];
    int import_state, import_err, import_target, import_link, import_running, import_thread_used, import_anchor;
    pthread_t import_thread;
    uint8_t *import_src; /* the user's file, kept until confirm/cancel (re-cropping) */
    size_t import_n;
    char import_path[300], prep_pic[33], prep_anim[33];
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
    UiMessage search[UI_RESULTS];
    UiReport reports[UI_REPORTS];
    int report_count, report_state, report_group; /* 1.2: 0 none, 1 loading, 2 done, 3 failed */
    UiAudit audit[UI_AUDIT];
    int audit_count, audit_state;
    char outbox_server[UI_OUTBOX][17];
    /* ORBIT NEXT: persistent public community directory */
    AppDirEntry dir[APP_DIR];
    int dir_count, dir_filter, dir_sort, dir_total;
    char dir_query[33];
    /* 1.2 public directory (directory.h) */
    DirConfig dircfg;
    int dir_ok, dir_fetch_now, dir_announce_now, dir_central, dir_listed, dir_joinable;
    pthread_t dir_thread;
    uint64_t browse_since;
    AppLink links[APP_LINKS];
} OrbitApp;
#define KB_TEXT 769
typedef struct {
    int page, focus, history_offset, menu_focus, tab, side, msg_mode, msg_sel, reply_id, edit_id;
    int list_focus, list_count, list_kind, list_arg, list_arg2, list_top;
    uint64_t now;
    char draft[ORBIT_TEXT];
    /* in-app keyboard (replaces the system dialog: no dimmed/black background) */
    int kb_active, kb_row, kb_col, kb_shift, kb_mode, kb_max, kb_layout, kb_emoji;
    int kb_sugg_n;          /* word suggestions shown above the keys; kb_row -1 = suggestion bar */
    char kb_sugg[3][32];
    char kb_title[96], kb_text[KB_TEXT];
    /* ORBIT NEXT spotlight screens: focus zone + item, hero game (-1 = automatic), grid cursor */
    int home_zone, home_item, home_game, home_strip_top, grid_focus, grid_top;
    /* generic list dialog (menus, settings, management) */
    char list_title[96], list_note[200];
    char qr[160]; /* 1.2: QR code shown next to the list (phone upload address) */
    char preview_pic[33], preview_anim[33]; /* 1.2: picture shown next to the list (import preview) */
    int preview_square;                      /* server icon (rounded square) instead of a circle */
    struct {
        int id, flags;
        uint32_t color;
        char text[80], sub[120];
    } list[48]; /* 1.2: 48 (the language page lists every registry language) */
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
/* 1.2 moderation: report a person (msg 0 = no message) to the selected server's moderators;
 * load the selected server's reports; mark one handled (REPORT_DONE) or dismissed. */
int app_report(OrbitApp *app, int user, uint32_t msg, int reason);
int app_reports(OrbitApp *app);
int app_audit(OrbitApp *app); /* 1.2: server appearance change log of the selected server */
int app_report_resolve(OrbitApp *app, int id, int state);
/* 1.2: hide a Discover entry for good and remember a report for the public directory */
int app_dir_hide(OrbitApp *app, int index, int reason);
/* 1.2: Discover search (case-insensitive in name, description and host), filter and order.
 * query NULL keeps the current text; indexes of the list follow the filtered order. */
void app_dir_filter(OrbitApp *app, const char *query, int filter, int sort);
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
/* 1.2 profile pictures (avatar.h): set from a decoded picture (NULL removes it); ask the
 * meeting points for a picture by name (stored in data/pics, state.pic_gen counts arrivals) */
int app_set_picture(OrbitApp *app, const void *orbit_image);
void app_pic_request(OrbitApp *app, const char *hash);
/* 1.2: import a picture file (PNG, JPEG or GIF) the user brought (USB, phone) on a worker thread,
 * never on the interface thread. gid < 0: own profile picture; else the server icon of group gid
 * on link (the host checks the permission). Takes ownership of data. state.import_state follows it. */
enum { IMPORT_IDLE, IMPORT_WORKING, IMPORT_READY, IMPORT_SENDING, IMPORT_DONE, IMPORT_FAILED };
int app_import_picture(OrbitApp *app, uint8_t *data, size_t n, int link, int gid);
int app_import_file(OrbitApp *app, const char *path, int link, int gid);
/* anchor: 0 centre, 1 top/left, 2 bottom/right (which part of a non-square picture is kept) */
int app_import_crop(OrbitApp *app, int anchor);
int app_import_confirm(OrbitApp *app);
void app_import_cancel(OrbitApp *app);
void app_import_reset(OrbitApp *app);
void app_anim_request(OrbitApp *app, const char *hash);
void app_set_settings(OrbitApp *app, const UiSettings *settings);
/* 1.1 background-notification plugin: 0 = done, 1 = nothing to do, -1 = failed */
int app_plugin_install(const char *appdir);
int app_plugin_remove(void);
int app_plugin_installed(void);
/* 1.2: the separate, experimental game-detection plugin (orbit_presence.sprx) */
int app_presence_install(const char *appdir);
int app_presence_remove(void);
int app_presence_installed(void);
/* new background-notification keys on every friend's PS3 (old ones stop working) */
void app_watch_renew(OrbitApp *app);
/* voice chat */
int app_call_toggle(OrbitApp *app);
int app_call_join(OrbitApp *app, int link, int group, int channel);
void app_call_mute(OrbitApp *app);
void app_call_person(OrbitApp *app, int index, int local_muted, int volume);
/* microphone test: hear your own voice after the codec; nothing is sent */
int app_mic_test(OrbitApp *app, int on);
/* ORBIT NEXT: locally detected games (owned by the caller, must stay valid while shown) */
void ui_set_games(const OrbitGame *games, int count, int scanning);
/* 1 while the renderer still has lazy work (e.g. icons to decode): draw again next frame */
int ui_pending(void);
/* home focus model shared with the input code */
enum { HZ_HERO, HZ_ONLINE, HZ_MESSAGES, HZ_STRIP, HZ_COUNT };
int ui_home_buttons(const UiState *s, const UiLocal *l);
int ui_home_messages(const UiState *s, int out_index[3]);
int ui_home_game(const UiLocal *l);
int ui_games_count(void);
int ui_init(const char *font_path);
void ui_close(void);
void ui_set_timezone(int minutes);
void ui_set_theme(int theme);
void ui_render(uint32_t *pixels, int width, int height, const UiState *state, const UiLocal *local);
/* performance overlay (R3). All values are measured on the running console. */
typedef struct {
    int fps, frame_avg_us, frame_low1_us; /* low1: the slowest 1 % of frames (frame time) */
    int render_us, render_avg_us, render_worst_us, present_us;
    int renders, by_input, by_data, by_anim; /* full redraws in the last second and why */
    int draw_w, draw_h, out_w, out_h;        /* drawn size and output size */
    int cache_kb;                            /* CPU-side image caches */
} UiPerf;
void ui_performance(const UiPerf *p);
/* 1280x720 -> 1920x1080 (3:2) into dst (row pitch in pixels). Only writes dst, never reads it,
 * so dst may be RSX video memory. */
void ui_upscale_3_2(const uint32_t *src, int w, int h, uint32_t *dst, int dst_pitch);
int ui_cache_kb(void);
void ui_icon(uint32_t *pixels, int width, int height);
/* Animated intro; t in seconds. Returns 1 when finished. */
int ui_intro(uint32_t *pixels, int width, int height, float t, const char *version);
/* time the skip button jumps to (the hand-over into Home) */
float ui_intro_skip_time(void);
/* reduced motion / performance mode: short calm intro, no particles */
void ui_set_reduced_motion(int on);
void ui_intro_done(void);
void ui_social(uint32_t *pixels, int width, int height, const char *version);
int ui_toast_visible(const UiState *s, uint64_t now);
/* 1.2 profile pictures: folder with <hash>.pic files, and who to ask for a missing one */
void ui_set_pic_dir(const char *dir);
extern void (*ui_pic_wanted)(const char *hash);
extern void (*ui_anim_wanted)(const char *hash);
/* 1.2 animated pictures: enabled = not "Reduce motion" and not "animated pictures off";
 * load: average frame drawing time (pauses animations when the console is busy);
 * due: an animation on screen needs its next frame (ask for a redraw) */
void ui_anim_settings(int enabled);
void ui_anim_load(int render_avg_us);
int ui_anim_due(uint64_t now_ms);
void ui_pic_clear(void);
/* sidebar rows of the current tab, shared by renderer and input handling */
enum { ROW_NONE, ROW_SERVER, ROW_CHANNEL, ROW_CATEGORY, ROW_ACTION, ROW_DM, ROW_FRIEND, ROW_NOTE,
       ROW_SETTING, ROW_HEADER };
typedef struct {
    int kind, a, b, c; /* server: a=group index; channel: a=group, b=channel; action: a=id */
} UiRow;
int ui_rows(const UiState *s, int tab, UiRow *rows, int cap);
/* keyboard layout: returns the UTF-8 text of a key, or NULL for special keys */
const char *ui_kb_key(int layout, int row, int col, int shift);
/* the shift state a key row really uses: code keyboards (mode 1) always give digits and capitals */
int ui_kb_shift(int mode, int row, int shift);
int ui_kb_cols(int row);
#define KB_ROWS 5
enum { KB_SHIFT = 100, KB_SPACE, KB_BACK, KB_DONE };
int ui_kb_special(int row, int col);
const char *ui_reaction(int i);
void ui_audio_probe(uint32_t *pixels, int width, int height, const char *status,
                    const char rows[][100], int count, int player, int remaining, int level);
#endif
