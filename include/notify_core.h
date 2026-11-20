#ifndef ORBIT_NOTIFY_CORE_H
#define ORBIT_NOTIFY_CORE_H
/* ORBIT 1.1 background notifications - the plugin's logic, freestanding (no libc).
 * The same code runs in the XMB plugin (platform layer: lv2 system calls, sys_net, vshtask) and
 * in the PC tests (platform layer: POSIX), always against the real ORBIT host code.
 *
 * Config file (written by the ORBIT app, NOTIFY_CONFIG), one item per line, fields split by TAB:
 *   ORBITNOTIFY 1
 *   on <0|1>  dm <0|1>  server <0|1>  preview <0|1>  busy <0|1>     (each on its own line)
 *   t_dm <title of a private message>                e.g. "ORBIT · New message"
 *   t_server <title of a server message, %s = server, %c = channel>   "ORBIT · %s › #%c"
 *   t_from <body without preview, %u = sender>        "New message from %u"
 *   t_many_from <%n messages from %u>                  "%n new messages from %u"
 *   t_many <%n messages>                               "%n new messages"
 *   host <ipv4> <port> <relay-ipv4 or 0> <relay-port> <group tag 16 hex> <wid 16 hex>
 *        <secret 64 hex> <server id 16 hex>
 *   mute <server id> <group>
 * The app also keeps NOTIFY_ACTIVE = its unix time (decimal), renewed every few seconds while it
 * runs: the plugin stays quiet then (the app shows its own notifications). */
#include <stddef.h>
#include <stdint.h>
#define NOTIFY_CONFIG "/dev_hdd0/game/ORB000001/USRDIR/data/notify.cfg" /* app data dir */
#define NOTIFY_ACTIVE "/dev_hdd0/tmp/orbit-app-active"
#define NOTIFY_HOSTS 6
#define NOTIFY_MUTES 48
#define NOTIFY_TEMPLATE 96
#define NOTIFY_BURST_MS 2500 /* messages arriving close together become one notification */
typedef struct {
    uint32_t ip, relay_ip;
    int port, relay_port;
    char tag[17], server[17];
    uint8_t wid[8], secret[32];
} NotifyHost;
typedef struct {
    int on, dm, server, preview, busy, hosts, mutes;
    char t_dm[NOTIFY_TEMPLATE], t_server[NOTIFY_TEMPLATE], t_from[NOTIFY_TEMPLATE],
        t_many_from[NOTIFY_TEMPLATE], t_many[NOTIFY_TEMPLATE];
    NotifyHost host[NOTIFY_HOSTS];
    struct {
        char server[17];
        int group;
    } mute[NOTIFY_MUTES];
} NotifyConfig;
/* one received message */
typedef struct {
    int dm, group, channel;
    char sender[49], server[65], channel_name[33], text[124];
} NotifyEvent;
/* what the platform provides */
typedef struct {
    int (*connect)(uint32_t ipv4, int port);                 /* socket or -1, ~10 s timeout */
    int (*send)(int fd, const void *buf, size_t n);          /* 0 = all sent */
    int (*recv)(int fd, void *buf, size_t n, unsigned ms);   /* bytes, 0 = timeout, -1 = closed */
    void (*close)(int fd);
    uint64_t (*millis)(void);
    uint32_t (*unix_time)(void);
    void (*sleep_ms)(unsigned ms);
    void (*notify)(const char *utf8);
    int (*read_file)(const char *path, char *buf, size_t cap); /* bytes or -1 */
    void (*entropy)(uint8_t out[24]);                          /* nonce material, need not be secret */
    void (*log)(const char *text, unsigned long value);        /* may be NULL */
    volatile int *stop;                                         /* set to end all loops */
} NotifyPlatform;
/* 0 = valid config */
int notify_parse(NotifyConfig *c, const char *text, size_t len);
/* 0 = event decoded from a host frame */
int notify_event(NotifyEvent *e, const uint8_t *frame, size_t n);
/* does the config allow showing this event (on, not busy, kind enabled, not muted)? */
int notify_wanted(const NotifyConfig *c, const NotifyHost *h, const NotifyEvent *e);
/* text for count events starting with first; same_sender / same_place: all of them share the
 * sender / the conversation of first */
void notify_format(const NotifyConfig *c, const NotifyEvent *first, int count, int same_sender,
                   int same_place, char *out, size_t cap);
/* the app is running right now (its own notifications are on screen) */
int notify_app_active(const NotifyPlatform *p);
/* worker for config host slot `index`: connects, receives, notifies, reconnects - until *stop */
void notify_worker(const NotifyPlatform *p, int index);
#endif
