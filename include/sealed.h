/* ORBIT NEXT 07b: sealed 1:1 messages (X3DH session start + Double Ratchet), built from the
 * published Signal specifications on Monocypher primitives (X25519, EdDSA, XChaCha20-Poly1305,
 * keyed BLAKE2b as the KDF). See docs/next/E2EE.md.
 *
 * STATUS: core library + tests only. It is NOT wired into DMs yet and has NOT been externally
 * reviewed, so nothing in the app may call anything "end-to-end encrypted" because of it.
 *
 * Freestanding: no allocation, no I/O; randomness comes from the caller (orbit_random). */
#ifndef ORBIT_SEALED_H
#define ORBIT_SEALED_H

#include <stddef.h>
#include <stdint.h>

#define SEAL_VERSION 1
#define SEAL_MAX_SKIP 64      /* most message keys skipped inside one chain */
#define SEAL_SKIPPED 64       /* most skipped message keys kept per session */
#define SEAL_OPK 16           /* one-time prekeys per bundle */
#define SEAL_HEADER 41        /* version, dh 32, pn 4, n 4 */
#define SEAL_INIT 164         /* type 1 prefix: ik_sign 32, ik_dh 32, ik_sig 64, ek 32, spk id 2, opk id 2 */
#define SEAL_OVERHEAD (1 + SEAL_HEADER + 16) /* plus SEAL_INIT until the peer replied */
#define SEAL_NO_OPK 0xFFFF

/* Long-term identity: an EdDSA signing key (signs the prekeys) and an X25519 key for X3DH. */
typedef struct {
    uint8_t sign_sk[64], sign_pk[32];
    uint8_t dh_sk[32], dh_pk[32];
} SealIdentity;

/* What the owner keeps; the public half goes into a SealBundle on their meeting point. */
typedef struct {
    uint16_t spk_id;
    uint8_t spk_sk[32], spk_pk[32], spk_sig[64];
    uint16_t opk_id[SEAL_OPK];
    uint8_t opk_sk[SEAL_OPK][32], opk_pk[SEAL_OPK][32];
    uint8_t opk_used[SEAL_OPK];
} SealPrekeys;

typedef struct {
    uint8_t ik_sign[32], ik_dh[32], ik_sig[64]; /* ik_sig: sign_pk signs "ORBIT-IK" || ik_dh */
    uint16_t spk_id;
    uint8_t spk_pk[32], spk_sig[64];
    int opk_count;
    uint16_t opk_id[SEAL_OPK];
    uint8_t opk_pk[SEAL_OPK][32];
} SealBundle;

typedef struct {
    uint8_t dh[32];
    uint32_t n;
    uint8_t mk[32];
} SealSkipped;

typedef struct {
    int ready, initiator, have_init;
    uint8_t ad[64];           /* initiator ik_sign || responder ik_sign */
    uint8_t peer_ik_sign[32]; /* for safety numbers / key-change detection */
    uint8_t peer_ek[32];      /* responder: base key of the session start (replay guard) */
    uint8_t rk[32];
    uint8_t dhs_sk[32], dhs_pk[32], dhr[32];
    int have_dhr, have_cks, have_ckr;
    uint8_t cks[32], ckr[32];
    uint32_t ns, nr, pn;
    uint8_t init_prefix[SEAL_INIT]; /* repeated on every message until the peer has replied */
    int skipped_count;
    SealSkipped skipped[SEAL_SKIPPED];
} SealSession;

enum { SEAL_OK = 0, SEAL_EBADSIG = -1, SEAL_EFORMAT = -2, SEAL_EAUTH = -3, SEAL_ESKIP = -4,
       SEAL_ENOOPK = -5, SEAL_ESPACE = -6, SEAL_ESTATE = -7 };

/* seed: 64 random bytes */
void seal_identity_new(SealIdentity *id, const uint8_t seed[64]);
/* rnd: 32 * (1 + SEAL_OPK) random bytes */
void seal_prekeys_new(SealPrekeys *pk, const SealIdentity *id, uint16_t spk_id, uint16_t first_opk,
                      const uint8_t *rnd);
void seal_bundle_make(SealBundle *b, const SealIdentity *id, const SealPrekeys *pk);
int seal_bundle_check(const SealBundle *b);

/* Initiator: start a session from the peer's bundle (signatures are checked). rnd: 64 bytes. */
int seal_session_start(SealSession *s, const SealIdentity *me, const SealBundle *peer, const uint8_t rnd[64]);
/* Seal: out needs len + SEAL_OVERHEAD + SEAL_INIT bytes. Returns the size or < 0. */
int seal_encrypt(SealSession *s, const uint8_t *msg, size_t len, uint8_t *out, size_t cap);
/* Open: works for the first message (responder, with me/prekeys; s must be zeroed) and for every
 * later one. One-time prekeys are consumed. rnd: 32 fresh bytes (used when the peer's ratchet key
 * changed). A first message from a new session of the same or another peer replaces the session;
 * compare peer_ik_sign before and after to show a "key changed" warning.
 * Returns the plaintext size or < 0; on failure the session is left unchanged. */
int seal_decrypt(SealSession *s, const SealIdentity *me, SealPrekeys *prekeys, const uint8_t *in,
                 size_t len, uint8_t *out, size_t cap, const uint8_t rnd[32]);

/* 60-digit safety number for two identity signing keys (same on both sides). */
void seal_safety_number(const uint8_t a[32], const uint8_t b[32], char out[72]);

/* Fixed-size state blobs for storage (sealed by the caller's storage key). */
size_t seal_session_size(void);
void seal_session_wipe(SealSession *s);

#endif
