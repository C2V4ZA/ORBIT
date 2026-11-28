#ifndef ORBIT_COVERS_H
#define ORBIT_COVERS_H
/* Game covers by Title ID (brief "GAME COVERS"), source-agnostic and opt-in.
 *
 * ORBIT ships NO cover source and NO cover images. The download only runs when the user has a
 * cover source configured (data/covers.cfg, see docs/next/COVERS.md) AND "Download game covers"
 * is on. Only the Title ID leaves the console. Downloaded files are untrusted: size limit, strict
 * decoder (PNG/JPEG), pixel limits, then re-scaled to a small COVER_W x COVER_H picture. The disk
 * cache keeps only files that decoded cleanly; a bounded RAM cache (COVER_RAM_SLOTS) loads lazily
 * and evicts the least recently used cover. Without a cover the UI uses the game's ICON0.PNG. */
#include "image.h"
#include <stddef.h>
#include <stdint.h>
#define COVER_W 256
#define COVER_H 320
#define COVER_MAX_FILE (1024u * 1024u)
#define COVER_RAM_SLOTS 18 /* 1.2.1: hero + strip + a full Games page (15), about 5.9 MB */
#define COVER_URL_MAX 256
#define COVER_DISK_MAX 200 /* files kept in the cover folder; the oldest goes first */
/* Built-in source (user decision 2026-11-23): GameTDB box art, medium size (350x408 JPEG, about
 * 65 KB). Region folders EN, US, JA, ZH; a missing cover answers 404. data/covers.cfg can replace
 * it ("url=...") or remove it ("url=none"). GameTDB asks to be credited and linked. */
#define COVER_DEFAULT_URL "https://art.gametdb.com/ps3/coverM/{REGION}/{ID}.jpg"
#define COVER_DEFAULT_CREDIT "Covers: GameTDB (www.gametdb.com)"
enum { COVER_OK = 0, COVER_E_OFF = -1, COVER_E_ID = -2, COVER_E_URL = -3, COVER_E_NET = -4,
       COVER_E_IMAGE = -5, COVER_E_FILE = -6, COVER_E_NONE = -7 };
typedef struct {
    int enabled;               /* user setting "Download game covers" */
    char url[COVER_URL_MAX];   /* https template with {ID}, optional {REGION}; empty = no source */
    char credit[96];           /* attribution shown in About / Settings, e.g. "Covers: example.org" */
} CoverConfig;
/* covers.cfg: "url=...", "credit=...", "enabled=0|1"; '#' comments. Unknown keys are ignored.
 * An invalid url leaves cfg->url empty (no source). Returns 0, or -1 for bad arguments. */
int covers_config_parse(const char *text, size_t n, CoverConfig *cfg);
/* template check: https only, exactly one {ID}, at most one {REGION}, no other braces,
 * no spaces/quotes/control characters/backslashes. 1 = valid. */
int covers_template_valid(const char *tpl);
/* region code for {REGION}: EN (Europe), US, JA, KO, ZH (Asia/Hong Kong); NULL when unknown */
const char *covers_region_code(const char *id);
/* expanded URL for a game Title ID. COVER_E_OFF when disabled or no source. */
int covers_url(const CoverConfig *cfg, const char *id, char *out, size_t cap);
/* same with an explicit {REGION} folder (two capital letters) */
int covers_url_region(const CoverConfig *cfg, const char *id, const char *region, char *out, size_t cap);
/* 1.2.1: {REGION} folders to try for a game, best first; pref = the console language ("DE") or NULL.
 * PAL: pref (if a PAL language), EN, FR, DE, ES, IT, RU, AU, then US. Returns the count. */
int covers_regions(const char *id, const char *pref, const char *out[], int cap);
#define COVER_TRIES 10
/* untrusted bytes -> COVER_W x COVER_H picture (crop to fill). Refuses > COVER_MAX_FILE, unknown
 * formats, broken files and pictures smaller than 32 px or wider than tall by more than 2:1. */
int covers_decode(const uint8_t *data, size_t n, OrbitImage *out);
/* <dir>/<ID>.cov ; COVER_E_ID for anything that is not a game Title ID */
int covers_path(const char *dir, const char *id, char *out, size_t cap);
/* keep bytes that decoded cleanly: temp file, remove old, rename (PS3-safe replace) */
int covers_store(const char *dir, const char *id, const uint8_t *data, size_t n);
/* remove every cached cover (Settings > "Refresh covers"); returns files removed */
int covers_clear_disk(const char *dir);
/* keep at most keep files in dir (oldest modification time removed first); returns files removed */
int covers_trim_disk(const char *dir, int keep);
/* download + decode check + store. COVER_E_NONE when the source has no cover (HTTP error).
 * No cover / unusable picture leaves "<ID>.nil" so it is not asked again for COVER_RETRY s. */
int covers_fetch(const CoverConfig *cfg, const char *dir, const char *id, const char *pref);
#define COVER_RETRY (24L * 3600L)
/* 1 when a download is worth trying now: no cached cover and no recent "nothing there" */
int covers_need(const char *dir, const char *id, long now);
void covers_nil_path(const char *dir, const char *id, char *out, size_t cap);
void covers_mark_missing(const char *dir, const char *id);
/* cached file -> COVER_W x COVER_H picture (caller frees). COVER_E_NONE: no file; a broken file
 * is deleted. Used by the RAM cache and by the UI's single hero slot. */
int covers_load(const char *dir, const char *id, OrbitImage *out);
/* 1.3 stage 1: folders where the user already has covers (multiMAN, webMAN, ManaGunZ, USB):
 * <folder>/<TitleID>.JPG / .PNG / .jpg / .png. Read only: such a file is decoded with the same
 * strict checks as a download, never deleted, copied or sent anywhere, and it wins over the
 * download cache. Folders that do not exist are dropped (no lookups there). Call before the UI. */
#define COVER_LOCAL_MAX 12
void covers_set_local(const char *const *dirs, int n);
int covers_local_count(void);
/* 1 when a local cover file exists for id (no decode); path filled when found */
int covers_local_find(const char *id, char *path, size_t cap);

/* bounded RAM cache */
enum { COVER_SLOT_EMPTY, COVER_SLOT_READY, COVER_SLOT_MISSING };
typedef struct {
    char id[10];
    int state;
    uint32_t used;
    OrbitImage img;
} CoverSlot;
typedef struct {
    CoverSlot slot[COVER_RAM_SLOTS];
    uint32_t tick;
} CoverCache;
/* cached picture or NULL (no file / broken file; remembered as missing until covers_forget) */
const OrbitImage *covers_get(CoverCache *c, const char *dir, const char *id);
void covers_forget(CoverCache *c, const char *id); /* after a new download */
void covers_cache_free(CoverCache *c);
int covers_cache_count(const CoverCache *c);       /* READY slots (tests / R3 overlay) */
/* slot state for id without loading (-1 = not in RAM) */
int covers_peek(const CoverCache *c, const char *id);
/* "no cover" answers are asked again (a download finished) */
void covers_forget_missing(CoverCache *c);
/* called with the pixels just before a cached cover is freed (the UI drops scaled copies) */
extern void (*covers_evict_hook)(const uint32_t *px);
#endif
