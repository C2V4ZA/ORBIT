/* ORBIT signed updates.
 *
 * The app downloads a small manifest (orbit-update.json):
 *   {"payload":"<JSON text>","sig":"<128 hex chars>"}
 * "sig" is an Ed25519-style EdDSA signature (monocypher, BLAKE2b) over the exact bytes of
 * "payload", made with the release key that never leaves the developer's PC. The payload is
 *   {"app":"orbit-ps3","version":"1.2.1","url":"https://...pkg","size":123,
 *    "blake2b":"<64 hex chars>","date":"2026-11-20","notes":{"de":"...","en":"..."}}
 * Only a manifest with a valid signature from the public key built into the app is accepted, and
 * the downloaded package must match both the signed size and the signed BLAKE2b-256 hash before
 * it is moved into place. TLS is used when the URL is https, but integrity never depends on it.
 * Nothing is installed automatically: the PKG is saved to /dev_hdd0/packages for the system's
 * Package Manager, so the user always decides. */
#ifndef ORBIT_UPDATE_H
#define ORBIT_UPDATE_H
#include <stddef.h>
#include <stdint.h>

#define UPDATE_URL_MAX 400
#define UPDATE_MANIFEST_MAX 16384
#define UPDATE_PKG_MAX (96L * 1024 * 1024)

enum {
    UPD_OK = 0,
    UPD_E_NET = -1,      /* could not connect / read */
    UPD_E_HTTP = -2,     /* unexpected HTTP status or too many redirects */
    UPD_E_FORMAT = -3,   /* manifest is not what we expect */
    UPD_E_SIGNATURE = -4,/* signature does not match the built-in key */
    UPD_E_APP = -5,      /* signed, but for another app */
    UPD_E_SIZE = -6,     /* download size differs from the signed size */
    UPD_E_HASH = -7,     /* download hash differs from the signed hash */
    UPD_E_FILE = -8,     /* could not write the file */
    UPD_E_CANCEL = -9,
};

typedef struct {
    char version[24];
    char url[UPDATE_URL_MAX];
    long size;
    uint8_t hash[32];
    char date[16];
    char notes[480]; /* in the requested language, English fallback */
} UpdateInfo;

/* <0, 0, >0 like strcmp; "1.10.0" > "1.9.2"; a "-suffix" (test build) is older than no suffix */
int update_version_cmp(const char *a, const char *b);
/* verify and parse a manifest; lang is "de", "en", ... for the notes */
int update_parse(const char *manifest, size_t n, const uint8_t public_key[32], const char *lang,
                 UpdateInfo *out);

/* minimal HTTP/1.0 GET with redirects (http and https). 0 on success. */
typedef void (*UpdateProgress)(void *ctx, long done, long total);
int http_get(const char *url, char *buf, size_t cap, size_t *len);
/* 1.2: POST a JSON body (no redirects); the reply body goes to buf */
int http_post(const char *url, const char *body, size_t n, char *buf, size_t cap, size_t *len);
/* fetch + verify the manifest */
int update_check(const char *manifest_url, const uint8_t public_key[32], const char *lang,
                 UpdateInfo *out);
/* download to path (written as path.part, renamed only after size + hash match) */
int update_download(const UpdateInfo *info, const char *path, UpdateProgress progress, void *ctx,
                    volatile int *cancel);

/* the release public key compiled into the app (include/update_key.h) */
extern const uint8_t update_public_key[32];
#endif
