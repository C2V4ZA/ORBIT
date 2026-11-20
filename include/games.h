#ifndef ORBIT_GAMES_H
#define ORBIT_GAMES_H
/* Local PS3 game detection: reads PARAM.SFO of installed titles (read-only, no game hooks).
 * "Installed" never means "playing" - live presence is a separate, explicit signal. */
#include <stddef.h>
#include <stdint.h>
#define GAMES_MAX 128
enum { GAME_SRC_HDD, GAME_SRC_DISC };
typedef struct {
    char id[10];      /* Title ID, e.g. BLES00680 */
    char title[64];   /* UTF-8 TITLE from PARAM.SFO */
    char cat[3];      /* HG, DG, GD */
    char icon[160];   /* path of ICON0.PNG (may not exist) */
    uint32_t mtime;   /* PARAM.SFO modification time (install/update, NOT play time) */
    uint32_t played;  /* last time ORBIT knows it was played (0 = unknown) */
    int source, favorite;
} OrbitGame;
/* PARAM.SFO parser. Returns 0 when TITLE_ID and TITLE were found. Untrusted input. */
int sfo_parse(const uint8_t *data, size_t len, char *title, size_t title_cap, char *id, size_t id_cap,
              char *cat, size_t cat_cap);
int sfo_read(const char *path, char *title, size_t title_cap, char *id, size_t id_cap, char *cat,
             size_t cat_cap);
/* retail/PSN game Title ID shape: 4 letters + 5 digits, known prefixes (homebrew filtered out) */
int games_is_game_id(const char *id);
/* region from the Title ID: 'E' Europe, 'U' America, 'J' Japan, 'A' Asia, 'K' Korea, 'H' Hong Kong, 0 unknown */
char games_region(const char *id);
/* Scan root (e.g. /dev_hdd0/game) and an optional disc root (/dev_bdvd). Deduplicates by Title ID.
 * Sorted: disc first, then newest mtime. Returns count. */
int games_scan(const char *root, const char *disc, OrbitGame *out, int cap);
#endif
