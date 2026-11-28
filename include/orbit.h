#ifndef ORBIT_H
#define ORBIT_H
#include "cJSON.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#define ORBIT_VERSION "1.3.0"
/* 1.3 crossplay: which console this build runs on (1 PS3, 2 PS4); sent with the profile and with
 * every public server listing, so Discover can show where a community is hosted */
#ifdef ORBIT_PS4
#define ORBIT_PLAT 2
#else
#define ORBIT_PLAT 1
#endif
/* 9 = Orbit 0.9.x (groups only). 10 = Orbit 1.0 (servers, channels, roles, friends, DMs).
 * A 1.0 host answers 0.9 clients with the old view: servers only, first channel only. */
/* 11 = Orbit 1.1: background notifications (op "watch_key" + watch connections, see watch.h) */
#define ORBIT_PROTOCOL 11
#define ORBIT_USERS 32
#define ORBIT_GROUPS 48
#define ORBIT_MESSAGES 512
#define ORBIT_CHANNELS 16
#define ORBIT_CATEGORIES 8
#define ORBIT_ROLES 8
#define ORBIT_FRIENDS 48
#define ORBIT_REACTIONS 6
/* group kinds: a community server, a private (group) conversation, the hidden friend inbox */
enum { GK_SERVER, GK_DM, GK_HOME };
enum { CH_TEXT, CH_VOICE };
/* permissions, always checked by the host */
enum {
    P_MANAGE_SERVER = 1,
    P_MANAGE_CHANNELS = 2,
    P_MANAGE_ROLES = 4,
    P_INVITE = 8,
    P_SEND = 16,
    P_DELETE = 32,
    P_KICK = 64,
    P_BAN = 128,
    P_MANAGE_VOICE = 256,
    P_MUTE = 512,
    P_ALL = 1023
};
enum { ST_ONLINE, ST_AWAY, ST_BUSY, ST_INVISIBLE, ST_OFFLINE };
/* friend entry states, kept on the owner's own PS3 */
enum { FR_NONE, FR_INCOMING, FR_OUTGOING, FR_FRIENDS, FR_BLOCKED };
enum { MF_EDITED = 1, MF_DELETED = 2 };
/* Public Orbit ID: 12 base32 characters of BLAKE2b(Ed25519 public key), "ABCD-EFGH-JKLM". */
#define ORBIT_ID_TEXT 15
void orbit_identity(const uint8_t device[32], uint8_t secret[64], uint8_t pub[32]);
void orbit_id_text(char out[ORBIT_ID_TEXT], const uint8_t pub[32]);
/* Signature that binds a public key to an account on one meeting point (server id). */
void orbit_bind_sign(uint8_t sig[64], const uint8_t secret[64], const uint8_t pub[32],
                     const char server_id[17]);
int orbit_bind_check(const uint8_t sig[64], const uint8_t pub[32], const char server_id[17]);
#define ORBIT_TEXT 769
#define ORBIT_PORT 31413
#define ORBIT_FRAME 65536
#define ORBIT_HOST 96
#define ORBIT_PENDING_EMAILS 64
/* Invite: XXXXX-XXXXX-XXXXX-XXXXX (IPv4, port, group code, checksum). */
#define ORBIT_INVITE_TEXT 24
#define ORBIT_INVITE_RELAY 0x10000
int orbit_parse_endpoint(const char *text, char host[ORBIT_HOST], int *port);
int orbit_parse_invite(const char *text, char host[ORBIT_HOST], int *port, uint8_t key[32]);
int orbit_group_code(char out[33], const char *text);
int orbit_parse_join(const char *text, char host[ORBIT_HOST], int *port, uint8_t key[32],
                     char group[33]);
int orbit_invite_encode(char out[ORBIT_INVITE_TEXT], uint32_t ipv4, int port, const char *code);
int orbit_invite_decode(const char *text, uint32_t *ipv4, int *port, char code[9]);
/* One key typed into a code field (invitation or friend code): letters become capitals, digits
 * stay, dashes/spaces/symbols are ignored, at most 20 symbols, a dash after every five.
 * text holds at most max bytes (+ NUL). Returns 0 when the key was added. */
int orbit_code_type(char *text, size_t max, const char *key);
void orbit_group_tag(char out[17], const char *code);
int orbit_group_psk(uint8_t psk[32], const char *code);
int orbit_ipv4_private(uint32_t ipv4);
typedef struct {
    char email[97], name[49];
    uint8_t salt[16], hash[32];
    uint8_t devices[4][32];
    /* 1.0 profile: shared by the person with every meeting point they use */
    int has_pub, status, color, avatar;
    int plat; /* 1.3: console of this person, 1 = PS3, 2 = PS4, 0 = unknown (older Orbit) */
    uint32_t created;
    uint8_t pub[32];
    char bio[97], game[49], custom[65];
    char pic[33]; /* 1.2: profile picture name (avatar.h), "" = symbol avatar */
    char anim[33]; /* 1.2: animated profile picture (anim.h), "" = none; pic is its first frame */
    /* 1.1: secret of this person's background-notification plugin (watch.h), 0 = none yet */
    int has_watch;
    uint8_t watch[32];
} OrbitUser;
typedef struct {
    int used, group;
    char email[97];
} OrbitPendingEmail;
typedef struct {
    int used, type, category, pos;
    uint8_t view, send; /* role masks: 0 = everyone */
    char name[33], topic[97];
} OrbitChannel;
typedef struct {
    int used, pos;
    char name[33];
} OrbitCategory;
typedef struct {
    int used, priority;
    uint32_t color, perms;
    char name[25];
} OrbitRole;
typedef struct {
    int used, owner, kind, icon, color, banner, open;
    uint32_t members, pending, banned, muted;
    char name[65], invite[33], desc[129], welcome[193];
    char pic[33], anim[33]; /* 1.2: server icon picture / animation, "" = symbol icon */
    OrbitCategory cat[ORBIT_CATEGORIES];
    OrbitChannel ch[ORBIT_CHANNELS];
    OrbitRole role[ORBIT_ROLES];
    uint8_t roles[ORBIT_USERS]; /* role bitmask per member */
    uint32_t ban_until[ORBIT_USERS]; /* 1.2: end of a timed ban (unix time), 0 = permanent */
} OrbitGroup;
/* 1.2: a report to the moderators of a server (kept on the server's host) */
#define ORBIT_REPORTS 64
enum { REPORT_OPEN, REPORT_DONE, REPORT_DISMISSED };
enum { RR_SPAM, RR_HARASSMENT, RR_HATE, RR_SEXUAL, RR_IMPERSONATION, RR_OTHER, RR_COUNT };
typedef struct {
    int used, group, reporter, target, reason, state, handler;
    uint32_t id, msg, time;
    char text[129], detail[97];
} OrbitReport;
/* 1.2: audit log of server appearance changes (kept on the server's host) */
#define ORBIT_AUDIT 64
enum { AUD_NAME, AUD_DESC, AUD_WELCOME, AUD_ICON, AUD_ICON_PIC, AUD_ICON_ANIM, AUD_ICON_RESET, AUD_COLOR,
       AUD_BANNER, AUD_OPEN, AUD_COUNT };
typedef struct {
    int used, group, user, what;
    uint32_t id, time;
} OrbitAudit;
typedef struct {
    uint32_t id, time, rev, reply;
    int group, user, channel, flags;
    uint32_t react[ORBIT_REACTIONS];
    char text[ORBIT_TEXT];
} OrbitMessage;
typedef struct {
    int used, state, user; /* user index on this PS3 once known, else -1 */
    uint32_t since;
    uint8_t pub[32];
    char name[49], code[ORBIT_INVITE_TEXT];
} OrbitFriend;
typedef struct {
    int version, users, owner_user; /* owner_user: whose PS3 this is, -1 = unknown */
    uint32_t next_id, rev;
    OrbitUser user[ORBIT_USERS];
    OrbitGroup group[ORBIT_GROUPS];
    OrbitMessage message[ORBIT_MESSAGES];
    OrbitPendingEmail pending_email[ORBIT_PENDING_EMAILS];
    OrbitFriend friend_[ORBIT_FRIENDS];
    OrbitReport report[ORBIT_REPORTS];
    uint32_t next_report;
    OrbitAudit audit[ORBIT_AUDIT];
    uint32_t next_audit;
} OrbitData;
/* 1.2: an animation being uploaded in pieces (RAM only, a few at a time, see anim.h) */
#define ORBIT_STAGED 4
typedef struct {
    int user;
    uint64_t when;
    char hash[33];
    uint8_t *data;
    size_t n, total;
} OrbitStaged;
typedef struct {
    OrbitData data;
    char path[512], server_id[17];
    int lang; /* language of host-side texts (default names of migrated channels) */
    pthread_mutex_t mutex;
    uint64_t auth_window;
    int auth_count;
    uint64_t join_window[ORBIT_USERS];
    unsigned join_count[ORBIT_USERS];
    uint64_t friend_window[ORBIT_USERS];
    unsigned friend_count[ORBIT_USERS];
    uint32_t call[ORBIT_GROUPS];                    /* members currently in a voice channel */
    uint8_t call_ch[ORBIT_GROUPS][ORBIT_USERS];     /* ... and which one */
    int online[ORBIT_USERS];                        /* open sessions per person, not saved */
    /* 1.2: game reported by the person's presence plugin (presence.h), not saved */
    struct {
        uint32_t until;
        char id[10], name[65];
    } playing[ORBIT_USERS];
    OrbitStaged staged[ORBIT_STAGED];
} OrbitStore;
typedef struct {
    int user, proto, local, counted;
    uint64_t last_write;
} OrbitSession;
/* Called when a connection ends: keeps the presence counter right. */
void orbit_session_end(OrbitStore *s, OrbitSession *session);
/* Effective permissions of a member in a group (owner: P_ALL). Store must be locked. */
uint32_t orbit_perms(const OrbitData *d, int gid, int uid);
int orbit_can_view(const OrbitData *d, int gid, int ch, int uid);
/* Voice: may this member speak in that voice channel? (not server-muted). Locks the store. */
int orbit_voice_allowed(OrbitStore *s, int gid, int ch, int uid, int *muted);
int orbit_random(void *buf, size_t n);
uint64_t orbit_millis(void);
uint32_t orbit_unix_time(void);
void orbit_hex(char *out, const uint8_t *in, size_t n);
int orbit_unhex(uint8_t *out, const char *s, size_t n);
int orbit_text_valid(const char *s, size_t min, size_t max);
/* 1.2 name rules for new and changed names: 2-24 characters, no invisible/direction characters,
 * no leading, trailing or double spaces, not a reserved staff name. 0 = fine, else an E_* key. */
int orbit_name_check(const char *name);
/* 1.2: 1 when a name contains an offensive word (slurs, sexual terms; folded for case,
 * look-alike digits and separators). Used for person names and public server names. */
int orbit_text_offensive(const char *text);
int orbit_store_open(OrbitStore *s, const char *path);
/* lang: language of default names when 0.9.5 groups become servers */
int orbit_store_open_lang(OrbitStore *s, const char *path, int lang);
void orbit_store_close(OrbitStore *s);
/* Returns the group code for a PSK tag, or -1. Thread-safe. */
int orbit_store_code_for_tag(OrbitStore *s, const char tag[17], char code[9]);
cJSON *orbit_request(OrbitStore *s, OrbitSession *session, const cJSON *req);
const char *orbit_string(const cJSON *o, const char *key);
cJSON *orbit_error(const char *text);
/* error with a stable i18n key (see i18n.h), shown in the reader's language */
cJSON *orbit_fail(int key);
int orbit_keyfile(const char *path, uint8_t key[32]);
/* 1.1 background notifications (watch.h): person whose watch secret has this id (-1: none) */
int orbit_watch_user(OrbitStore *s, const uint8_t id[8], uint8_t secret[32]);
uint32_t orbit_watch_start(OrbitStore *s);
/* next event for that person after *since: length written to out, 0 = none, -1 = end watch */
/* 1.2 presence: playing = 0 clears it; id/name already validated (presence_frame_parse) */
/* 1.2 directory: the open community servers of this meeting point, with their join codes
 * ("code") - for the owner's announcement only. Locks the store. */
cJSON *orbit_store_public(OrbitStore *s);
void orbit_presence_set(OrbitStore *s, int uid, int playing, const char *id, const char *name);
int orbit_watch_next(OrbitStore *s, int uid, const uint8_t id[8], uint32_t *since, uint8_t *out,
                     size_t cap);
typedef struct OrbitServer OrbitServer;
typedef struct OrbitLink OrbitLink;
OrbitServer *orbit_server_start(OrbitStore *store, const uint8_t key[32], int port);
void orbit_server_stop(OrbitServer *server);
/* Privacy Shield (docs/next/PRIVACY-SHIELD.md): on = accept only connections that arrive through
 * this console's own tunnel or relay splice (127.0.0.1) and stay silent to LAN discovery, so no
 * other console ever connects to this console's own IP address. */
void orbit_server_set_shield(OrbitServer *server, int on);
int orbit_server_shield_refused(OrbitServer *server); /* direct connections refused so far */
/* Privacy Shield: may an invitation address be dialled? Relay invitations always (the relay
 * splices, the peer never sees this console's IP); plain addresses only when they are one of the
 * tunnel servers in tunnels[] (then the address belongs to the tunnel, not to the other console). */
int orbit_shield_allows(uint32_t ip, int port, const uint32_t *tunnels, int n);
OrbitLink *orbit_connect(const char *ip, int port, const uint8_t key[32]);
OrbitLink *orbit_connect_detailed(const char *ip, int port, const uint8_t key[32], char *reason,
                                  size_t capacity);
/* Connects with a group invitation: PSK derived from the group code. */
OrbitLink *orbit_connect_group(const char *ip, int port, const char *code, char *reason,
                               size_t capacity);
/* LAN discovery: broadcasts the group tag, returns the answering host IPv4. */
int orbit_discover(const char tag[17], int port, const char *broadcast, char ip[16],
                   unsigned timeout_ms);
cJSON *orbit_exchange(OrbitLink *link, const cJSON *req);
/* Plain TCP with timeouts (DNS names allowed). Returns a blocking socket or -1. */
int orbit_tcp_connect(const char *host, int port, char *reason, size_t capacity);
void orbit_socket_close(int fd);
/* IPv4 of a dotted address or DNS name, 0 if unknown. */
uint32_t orbit_resolve(const char *host);
/* TLS with the group-code PSK on an already connected socket (e.g. through the relay). */
OrbitLink *orbit_wrap_group(int fd, const char *code, char *reason, size_t capacity);
/* Relay ("Vermittler"): a small server both consoles reach from inside their home network.
 * The relay only splices TCP streams; TLS stays end to end between the consoles. */
#define ORBIT_RELAY_PORT 31400
typedef struct OrbitRelayHost OrbitRelayHost;
OrbitRelayHost *orbit_relay_host_start(const char *relay, const char server_id[17],
                                       const uint8_t key[32], OrbitStore *store, int local_port);
/* 1 = registered at the relay, 0 = not (reason filled) */
int orbit_relay_host_state(OrbitRelayHost *h, char *reason, size_t capacity);
void orbit_relay_host_stop(OrbitRelayHost *h);
int orbit_relay_dial(const char *relay, const char tag[17], char *reason, size_t capacity);
int orbit_relay_discover(int port, char ip[16], unsigned timeout_ms);
/* Built-in tunnel (open bore protocol, e.g. bore.pub): makes this console's meeting point
 * reachable from the internet without router settings and without a PC. */
typedef struct OrbitBore OrbitBore;
OrbitBore *orbit_bore_start(const char *server, int want_port, int local_port);
int orbit_bore_state(OrbitBore *b, uint32_t *ip, int *port, char *host, size_t host_cap,
                     char *reason, size_t cap);
void orbit_bore_stop(OrbitBore *b);
int orbit_frame_send(OrbitLink *link, const uint8_t *buf, size_t n);
/* Returns frame length, 0 when nothing arrived within wait_ms, -1 on a broken link. */
int orbit_frame_recv(OrbitLink *link, uint8_t *buf, size_t cap, unsigned wait_ms);
void orbit_disconnect(OrbitLink *link);
/* 1 = this link uses ECDHE-PSK (forward secrecy), 0 = plain PSK (an older ORBIT on the other side) */
int orbit_link_forward_secret(OrbitLink *link);
/* tests only: 1 = this console offers only the old plain-PSK suite, like ORBIT 1.0-1.2 */
extern int orbit_tls_legacy;
/* Automatic router port opening: UPnP IGD, then PCP / NAT-PMP. No manual router setup. */
typedef struct {
    int ok, port, cgnat, fritz;
    char external[16], method[16], reason[300], router[64];
    char control[256], service[96]; /* UPnP mapping, kept for removal */
} OrbitNat;
int orbit_nat_open(const char *local_ip, const char *gateway, int internal_port, OrbitNat *out);
void orbit_nat_close(OrbitNat *nat, const char *gateway);
#endif
