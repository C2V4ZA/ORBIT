/* ORBIT 1.1 background notifications: shared protocol code (see watch.h). No libc. */
#include "watch.h"
#include "monocypher.h"

static const uint8_t magic[4] = {'O', 'W', 'C', 'H'};

void orbit_watch_id(uint8_t id[8], const uint8_t secret[32]) {
    static const uint8_t label[] = "orbit-watch-id";
    crypto_blake2b_keyed(id, 8, secret, 32, label, sizeof(label) - 1);
}

void watch_hello(uint8_t out[WATCH_HELLO], const uint8_t id[8], const uint8_t nonce_p[24]) {
    for (int i = 0; i < 4; i++)
        out[i] = magic[i];
    out[4] = 1;
    for (int i = 0; i < 8; i++)
        out[5 + i] = id[i];
    for (int i = 0; i < 24; i++)
        out[13 + i] = nonce_p[i];
}

int watch_hello_parse(const uint8_t in[WATCH_HELLO], uint8_t id[8], uint8_t nonce_p[24]) {
    for (int i = 0; i < 4; i++)
        if (in[i] != magic[i])
            return -1;
    if (in[4] != 1)
        return -1;
    for (int i = 0; i < 8; i++)
        id[i] = in[5 + i];
    for (int i = 0; i < 24; i++)
        nonce_p[i] = in[13 + i];
    return 0;
}

void watch_derive(WatchKeys *k, const uint8_t secret[32], const uint8_t nonce_p[24],
                  const uint8_t nonce_h[24], int host) {
    static const uint8_t label[] = "orbit-watch-1";
    uint8_t msg[13 + 48];
    for (int i = 0; i < 13; i++)
        msg[i] = label[i];
    for (int i = 0; i < 24; i++) {
        msg[13 + i] = nonce_p[i];
        msg[37 + i] = nonce_h[i];
    }
    crypto_blake2b_keyed(k->key, 32, secret, 32, msg, sizeof(msg));
    k->sent = k->received = 0;
    k->host = host;
}

static void nonce_for(uint8_t nonce[24], int direction, uint64_t counter) {
    for (int i = 0; i < 24; i++)
        nonce[i] = 0;
    nonce[0] = (uint8_t)direction;
    for (int i = 0; i < 8; i++)
        nonce[8 + i] = (uint8_t)(counter >> (56 - 8 * i));
}

size_t watch_seal(WatchKeys *k, uint8_t *out, const uint8_t *plain, size_t n) {
    uint8_t nonce[24];
    nonce_for(nonce, k->host ? 1 : 2, k->sent++);
    out[0] = (uint8_t)(n >> 8);
    out[1] = (uint8_t)n;
    crypto_aead_lock(out + 18, out + 2, k->key, nonce, out, 2, plain, n);
    return n + 18;
}

int watch_frame_length(const uint8_t hdr[18]) {
    int n = (hdr[0] << 8) | hdr[1];
    return n <= WATCH_FRAME ? n : -1;
}

int watch_open(WatchKeys *k, uint8_t *plain, const uint8_t hdr[18], const uint8_t *cipher, size_t n) {
    uint8_t nonce[24];
    nonce_for(nonce, k->host ? 2 : 1, k->received);
    if (crypto_aead_unlock(plain, hdr + 2, k->key, nonce, hdr, 2, cipher, n))
        return -1;
    k->received++;
    return 0;
}

void watch_wipe(WatchKeys *k) {
    crypto_wipe(k, sizeof(*k));
}

size_t watch_utf8_cut(const char *s, size_t len, size_t max) {
    if (len <= max)
        return len;
    size_t n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
        n--; /* s[n] continues a character: cut before that character starts */
    return n;
}
