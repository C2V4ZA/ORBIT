#ifndef ORBIT_PRESENCE_H
#define ORBIT_PRESENCE_H
/* ORBIT 1.2 automatic game presence (experimental, opt-in). Freestanding (no libc) like
 * notify_core: compiled into the ORBIT app/host, the PC tests and the separate XMB plugin
 * orbit_presence.sprx. The notification plugin orbit_notify.sprx is NOT changed by this.
 *
 * The presence plugin detects the running game and tells the meeting points listed in notify.cfg
 * over a short authenticated watch connection (watch.h):
 *   plugin -> host  hello, then the frame "A2" (presence only: the person's notification watch
 *                   keeps running), then one frame:  "G" title-id 0x1f title   (a game runs)
 *                                                    "G"                        (no game any more)
 *   host   -> plugin "K" (stored); hosts before 1.2 simply close the connection.
 * The host keeps a report PRESENCE_TTL seconds and shows it only to the person's friends (never
 * in public member lists). The plugin repeats it every PRESENCE_REFRESH_MS while the game runs.
 * presence.cfg (written by the app):  ORBITPRESENCE 1 \n share <0|1> */
#include <stddef.h>
#include <stdint.h>
#define PRESENCE_CONFIG "/dev_hdd0/game/ORB000001/USRDIR/data/presence.cfg"
#define PRESENCE_TTL 300
#ifndef PRESENCE_REFRESH_MS /* tests shorten these */
#define PRESENCE_REFRESH_MS 120000
#define PRESENCE_POLL_MS 15000
#endif
#define PRESENCE_NAME 64
typedef struct {
    int playing;                    /* a game runs */
    char id[10];                    /* "BLES01234", "" = not recognised */
    char name[PRESENCE_NAME + 1];   /* UTF-8 title, "" = not recognised */
} PresenceGame;
/* "ABCD12345": four capital letters, five digits */
int presence_id_valid(const char *id);
/* reads TITLE_ID and TITLE from a PARAM.SFO image; 0 = a valid TITLE_ID was found */
int presence_sfo(const uint8_t *sfo, size_t n, PresenceGame *g);
/* frame body ("G..."), returns its length (0 = does not fit) */
size_t presence_frame(uint8_t *out, size_t cap, const PresenceGame *g);
/* 0 = a valid frame; invalid ids/names are refused, not repaired */
int presence_frame_parse(const uint8_t *in, size_t n, PresenceGame *g);
#endif
