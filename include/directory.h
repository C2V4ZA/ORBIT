#ifndef ORBIT_DIRECTORY_H
#define ORBIT_DIRECTORY_H
/* ORBIT 1.2 public community directory (03) - shared by the app and orbit-dird.
 *
 * HTTP/1.0, JSON bodies (at most DIR_BODY_MAX bytes):
 *   POST /v1/announce  {"pub":<hex32>,"payload":"<json>","sig":<hex64>}
 *        payload {"v":1,"server":<16 hex>,"time":<unix>,"communities":[DirCommunity...]}
 *        signed by the host's directory key (derived from its server key, dir_host_keys).
 *        A server id is bound to the first key that announced it (first come, first served).
 *        An empty list removes the server's listing. Listings expire DIR_TTL s after the last one.
 *   GET  /v1/list?q=<text>  -> {"payload":"<json>","sig":<hex64>}  signed by the directory's own
 *        key, which the app pins (data/directory.cfg). payload {"v":1,"time":..,"communities":[..]}
 *   POST /v1/report    {"server":..,"id":..,"reason":0..5}  -> hidden after DIR_HIDE_REPORTS
 *        reports from different addresses, kept for the operator to review (reports.log).
 * Community fields: id, name, desc, host, members, online, icon, color, plat (1.3, optional: 1 PS3,
 * 2 PS4), invite (the join code
 * for this public server: listed only when its owner switched "List in the public directory" on).
 * The directory never receives messages, member lists, presence or anything private. */
#include <stddef.h>
#include <stdint.h>
#include "orbit.h"
#define DIR_BODY_MAX 65536
#define DIR_LIST_MAX 200
#define DIR_PER_SERVER 16
#define DIR_TTL 1800
#define DIR_LIVE 900 /* announced within this many seconds = "host online" */
#define DIR_HIDE_REPORTS 3
#define DIR_ANNOUNCE_MS 600000 /* the app repeats its announcement every 10 minutes */
typedef struct {
    int id, members, online, icon, color;
    int plat; /* 1.3: host console, 1 PS3, 2 PS4, 0 unknown (optional field) */
    uint32_t seen;
    char server[17], name[65], desc[129], host[49], invite[ORBIT_INVITE_TEXT];
} DirCommunity;
/* the host's directory signing key: derived from server.key, never leaves the PS3 */
void dir_host_keys(const uint8_t server_key[32], uint8_t secret[64], uint8_t pub[32]);
/* checks one community object (from either side); 0 = valid. allow_private: test setups only */
int dir_community_parse(const cJSON *x, DirCommunity *c, int allow_private);
cJSON *dir_community_json(const DirCommunity *c);
/* signed envelope {"pub"?,"payload","sig"}; with_pub adds the public key (announcements) */
char *dir_seal(const cJSON *payload, const uint8_t secret[64], const uint8_t pub[32], int with_pub);
/* opens an envelope signed by `pub` (or by the "pub" inside when pub is NULL, then copied to
 * pub_out); returns the parsed payload or NULL */
cJSON *dir_open(const char *text, size_t n, const uint8_t *pub, uint8_t pub_out[32]);
/* data/directory.cfg:  url <http(s)://host[:port]>  and  pub <64 hex>  (both required) */
typedef struct {
    char url[200];
    uint8_t pub[32];
} DirConfig;
int dir_config_load(const char *path, DirConfig *c);
#endif
