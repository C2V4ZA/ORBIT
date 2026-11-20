/* ORBIT profile pictures.
 *
 * A picture is always exactly 96x96 RGB. It travels as a "blob": the 27648 raw bytes compressed
 * with zlib (at most AVATAR_BLOB_MAX bytes). Its name is the BLAKE2b-128 hash of the blob
 * (32 lower-case hex digits), so a picture can be cached by name and checked on arrival. A blob
 * is only accepted when it inflates to exactly 27648 bytes (no more: bombs stop at the fixed
 * buffer) and its hash matches the name it was requested by.
 *
 * Pictures from USB sticks or phones go through the strict PNG/JPEG readers (image.h) first and
 * are then cropped to a square and scaled, so the original file never leaves the console. */
#ifndef ORBIT_AVATAR_H
#define ORBIT_AVATAR_H
#include "image.h"
#include <stddef.h>
#include <stdint.h>

#define AVATAR_SIDE 96
#define AVATAR_RAW (AVATAR_SIDE * AVATAR_SIDE * 3)
#define AVATAR_BLOB_MAX 32768
#define AVATAR_B64_MAX (AVATAR_BLOB_MAX / 3 * 4 + 8)

int avatar_hash_valid(const char *hash);
void avatar_hash(const uint8_t *blob, size_t n, char hash[33]);
/* crop/scale any decoded picture into a blob; returns 0 and fills blob/n/hash */
int avatar_make(const OrbitImage *img, uint8_t blob[AVATAR_BLOB_MAX], size_t *n, char hash[33]);
/* blob -> 96x96 ARGB picture (out->px allocated); hash may be NULL to skip the name check */
int avatar_decode(const uint8_t *blob, size_t n, const char *hash, OrbitImage *out);
/* files: <dir>/<hash>.pic */
int avatar_save(const char *dir, const char *hash, const uint8_t *blob, size_t n);
int avatar_load(const char *dir, const char *hash, uint8_t blob[AVATAR_BLOB_MAX], size_t *n);
/* base64 for JSON; decode returns -1 on any invalid input or overflow */
int avatar_b64_encode(char *out, size_t cap, const uint8_t *in, size_t n);
int avatar_b64_decode(uint8_t *out, size_t cap, size_t *n, const char *in);
#endif
