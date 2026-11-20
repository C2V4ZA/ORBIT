/* ORBIT NEXT: animated pictures (profile pictures and server icons).
 *
 * An uploaded GIF is never passed on. The console that receives it (from USB or the phone page)
 * decodes it with the strict reader (gif.h), crops and scales every kept frame to 96x96,
 * reduces it to one shared 256-colour palette and stores this ORBIT animation:
 *
 *   zlib( "OANI" | version 1 | side 96 | frames u16 | delays u16[frames] (ms) |
 *         palette 256 x RGB | frames x 96 x 96 palette indices )
 *
 * Budgets (also enforced when a blob is read): 2..150 frames, every frame shown >= 100 ms
 * (at most 10 frames per second), total <= 15 s, blob <= ANIM_BLOB_MAX, inflated size exact.
 * The blob is named by its BLAKE2b-128 hash like a static picture (avatar.h). The first frame is
 * also stored as a normal static picture, which older clients, "Reduce motion" and lists that do
 * not animate show instead. */
#ifndef ORBIT_ANIM_H
#define ORBIT_ANIM_H
#include <stddef.h>
#include <stdint.h>

#include "image.h"

#define ANIM_SIDE 96
#define ANIM_FRAME_PX (ANIM_SIDE * ANIM_SIDE)
#define ANIM_MAX_FRAMES 150
#define ANIM_MAX_MS 15000
#define ANIM_MIN_DELAY 100
#define ANIM_BLOB_MAX (384u * 1024u)
#define ANIM_HEAD 8
#define ANIM_PIECE 32768u /* transfer piece (fits a 64 KB protocol frame as base64) */
#define ANIM_RAW_MAX (ANIM_HEAD + 2 * ANIM_MAX_FRAMES + 768 + ANIM_MAX_FRAMES * ANIM_FRAME_PX)

typedef struct {
    int frames, total_ms;
    uint16_t delay[ANIM_MAX_FRAMES];
    uint32_t pal[256]; /* 0xFFRRGGBB */
    uint8_t *idx;      /* frames * ANIM_FRAME_PX */
} OrbitAnim;

/* GIF bytes -> animation blob (malloc'd, *blob) + its name. first: the first frame as a 96x96
 * picture for the static fallback (caller frees). Returns IMG_OK, or IMG_E_UNSUPPORTED when the
 * GIF has only one frame (use it as a static picture), or another negative IMG_E_* code. */
int anim_make_gif(const uint8_t *gif, size_t len, uint8_t **blob, size_t *n, char hash[33], OrbitImage *first);
/* the same with a crop anchor (image_cover_at) */
int anim_make_gif_at(const uint8_t *gif, size_t len, int anchor, uint8_t **blob, size_t *n, char hash[33],
                     OrbitImage *first);
/* blob -> animation (a->idx allocated); hash may be NULL to skip the name check */
int anim_decode(const uint8_t *blob, size_t n, const char *hash, OrbitAnim *a);
void anim_free(OrbitAnim *a);
/* frame shown at time t_ms (loops) */
int anim_frame_at(const OrbitAnim *a, uint32_t t_ms);
/* frame k -> 96x96 ARGB pixels */
void anim_render(const OrbitAnim *a, int k, uint32_t *px);
size_t anim_bytes(const OrbitAnim *a); /* memory held, for the cache budget */
/* files: <dir>/<hash>.ani */
int anim_save(const char *dir, const char *hash, const uint8_t *blob, size_t n);
int anim_load(const char *dir, const char *hash, uint8_t **blob, size_t *n);
#endif
