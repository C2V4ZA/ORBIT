#ifndef ORBIT_WATCH_H
#define ORBIT_WATCH_H
/* ORBIT 1.1 background notifications: a lean, encrypted push stream from a meeting point (host)
 * to the person's XMB plugin. Freestanding (no libc): compiled into the ORBIT app and the plugin.
 *
 * Connection (plain TCP to the normal ORBIT port, or through the relay like any other link):
 *   plugin -> host  "OWCH" | version 1 | wid[8] | nonce_p[24]          (37 bytes)
 *   host   -> plugin nonce_h[24]                                       (unknown wid: host closes)
 *   key = BLAKE2b-keyed(secret, "orbit-watch-1" | nonce_p | nonce_h)
 *   then frames both ways: u16 BE length n | mac[16] | ciphertext[n]    (XChaCha20-Poly1305)
 *   nonce = direction byte (1 host->plugin, 2 plugin->host) | 7 zero | u64 BE counter | 8 zero
 *   plugin's first frame must be "A1" (proves the secret) before the host sends anything.
 * Both sides send "P" every 20 s; a silent peer is dropped (WATCH_IDLE_MS). One watch per person:
 * a new one replaces the older (a PS3 that vanished without closing its connection).
 * Host frames: "P" keep-alive, or an event
 *   "M" kind 0x1f group 0x1f channel 0x1f sender 0x1f server-name 0x1f channel-name 0x1f text
 *   (kind: "s" server, "d" private conversation; text is cut to WATCH_TEXT bytes, UTF-8 safe).
 * secret = OrbitUser.watch on that host, handed to the person's own app with op "watch_key". */
#include <stddef.h>
#include <stdint.h>
#define WATCH_HELLO 37
#define WATCH_NONCE 24
#define WATCH_FRAME 1024 /* largest plaintext */
#define WATCH_TEXT 120
#ifndef WATCH_PING_MS /* tests shorten these */
#define WATCH_PING_MS 20000
#define WATCH_IDLE_MS 65000 /* host ends a watch when the plugin sent nothing for this long */
#endif
#define WATCH_MAX 12        /* watches per meeting point: normal connections always find room */
typedef struct {
    uint8_t key[32];
    uint64_t sent, received;
    int host; /* 1 on the host side, 0 in the plugin */
} WatchKeys;
/* public 8-byte name of a secret (sent in the hello instead of the secret) */
void orbit_watch_id(uint8_t id[8], const uint8_t secret[32]);
void watch_hello(uint8_t out[WATCH_HELLO], const uint8_t id[8], const uint8_t nonce_p[24]);
/* 0 = a valid hello; id and nonce_p filled */
int watch_hello_parse(const uint8_t in[WATCH_HELLO], uint8_t id[8], uint8_t nonce_p[24]);
void watch_derive(WatchKeys *k, const uint8_t secret[32], const uint8_t nonce_p[24],
                  const uint8_t nonce_h[24], int host);
/* writes header + ciphertext to out (n + 18 bytes), returns that length; n <= WATCH_FRAME */
size_t watch_seal(WatchKeys *k, uint8_t *out, const uint8_t *plain, size_t n);
/* hdr: the 18 header bytes; returns the ciphertext length announced by it, or -1 */
int watch_frame_length(const uint8_t hdr[18]);
/* decrypts n bytes (in place allowed); 0 = authentic */
int watch_open(WatchKeys *k, uint8_t *plain, const uint8_t hdr[18], const uint8_t *cipher, size_t n);
void watch_wipe(WatchKeys *k);
/* cuts UTF-8 text to at most max bytes without splitting a character; returns the new length */
size_t watch_utf8_cut(const char *s, size_t len, size_t max);
#endif
