/* ORBIT NEXT 07b: X3DH + Double Ratchet core (see include/sealed.h and docs/next/E2EE.md).
 *
 * Follows "The X3DH Key Agreement Protocol" and "The Double Ratchet Algorithm" (Signal, public
 * specifications) with these instantiations:
 *   DH       X25519 (Monocypher)
 *   SIG      Monocypher EdDSA (curve25519 + BLAKE2b) over domain-separated messages
 *   KDF_RK   BLAKE2b-512 keyed with RK over "ORBIT-RK1" || DH output -> new RK || CK
 *   KDF_CK   BLAKE2b-256 keyed with CK over 0x01 (message key) / 0x02 (next chain key)
 *   X3DH KDF BLAKE2b-256 over "ORBIT-X3DH1" || 32 x 0xFF || DH1 || DH2 || DH3 [|| DH4]
 *   ENCRYPT  XChaCha20-Poly1305, key || nonce = BLAKE2b-448 keyed with MK over "ORBIT-MK1"
 *            AD = initiator IK || responder IK || message header (|| session-start prefix)
 * Headers are authenticated, not encrypted (the host sees ratchet public keys and counters). */
#include "sealed.h"

#include <string.h>

#include "../vendor/monocypher.h"

enum { T_INIT = 1, T_MSG = 2 };

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static void kdf_rk(uint8_t rk[32], uint8_t ck[32], const uint8_t dh[32]) {
    uint8_t in[9 + 32], out[64];
    memcpy(in, "ORBIT-RK1", 9);
    memcpy(in + 9, dh, 32);
    crypto_blake2b_keyed(out, 64, rk, 32, in, sizeof in);
    memcpy(rk, out, 32);
    memcpy(ck, out + 32, 32);
    crypto_wipe(out, sizeof out);
    crypto_wipe(in, sizeof in);
}
static void kdf_ck(uint8_t ck[32], uint8_t mk[32]) {
    uint8_t one = 1, two = 2, next[32];
    crypto_blake2b_keyed(mk, 32, ck, 32, &one, 1);
    crypto_blake2b_keyed(next, 32, ck, 32, &two, 1);
    memcpy(ck, next, 32);
    crypto_wipe(next, 32);
}
static void mk_expand(const uint8_t mk[32], uint8_t key[32], uint8_t nonce[24]) {
    uint8_t out[56];
    crypto_blake2b_keyed(out, 56, mk, 32, (const uint8_t *)"ORBIT-MK1", 9);
    memcpy(key, out, 32);
    memcpy(nonce, out + 32, 24);
    crypto_wipe(out, sizeof out);
}

static void ik_message(uint8_t m[8 + 32], const uint8_t dh_pk[32]) {
    memcpy(m, "ORBIT-IK", 8);
    memcpy(m + 8, dh_pk, 32);
}
static void spk_message(uint8_t m[9 + 2 + 32], uint16_t id, const uint8_t pk[32]) {
    memcpy(m, "ORBIT-SPK", 9);
    put16(m + 9, id);
    memcpy(m + 11, pk, 32);
}

void seal_identity_new(SealIdentity *id, const uint8_t seed[64]) {
    uint8_t s[32];
    memset(id, 0, sizeof *id);
    memcpy(s, seed, 32);
    crypto_eddsa_key_pair(id->sign_sk, id->sign_pk, s); /* wipes s */
    memcpy(id->dh_sk, seed + 32, 32);
    crypto_x25519_public_key(id->dh_pk, id->dh_sk);
}

void seal_prekeys_new(SealPrekeys *pk, const SealIdentity *id, uint16_t spk_id, uint16_t first_opk,
                      const uint8_t *rnd) {
    uint8_t m[43];
    memset(pk, 0, sizeof *pk);
    pk->spk_id = spk_id;
    memcpy(pk->spk_sk, rnd, 32);
    crypto_x25519_public_key(pk->spk_pk, pk->spk_sk);
    spk_message(m, spk_id, pk->spk_pk);
    crypto_eddsa_sign(pk->spk_sig, id->sign_sk, m, sizeof m);
    for (int i = 0; i < SEAL_OPK; i++) {
        pk->opk_id[i] = (uint16_t)(first_opk + i);
        if (pk->opk_id[i] == SEAL_NO_OPK)
            pk->opk_id[i] = 0;
        memcpy(pk->opk_sk[i], rnd + 32 * (i + 1), 32);
        crypto_x25519_public_key(pk->opk_pk[i], pk->opk_sk[i]);
    }
}

void seal_bundle_make(SealBundle *b, const SealIdentity *id, const SealPrekeys *pk) {
    uint8_t m[40];
    memset(b, 0, sizeof *b);
    memcpy(b->ik_sign, id->sign_pk, 32);
    memcpy(b->ik_dh, id->dh_pk, 32);
    ik_message(m, id->dh_pk);
    crypto_eddsa_sign(b->ik_sig, id->sign_sk, m, sizeof m);
    b->spk_id = pk->spk_id;
    memcpy(b->spk_pk, pk->spk_pk, 32);
    memcpy(b->spk_sig, pk->spk_sig, 64);
    for (int i = 0; i < SEAL_OPK; i++)
        if (!pk->opk_used[i]) {
            b->opk_id[b->opk_count] = pk->opk_id[i];
            memcpy(b->opk_pk[b->opk_count], pk->opk_pk[i], 32);
            b->opk_count++;
        }
}

static int ik_check(const uint8_t sign[32], const uint8_t dh[32], const uint8_t sig[64]) {
    uint8_t m[40];
    ik_message(m, dh);
    return crypto_eddsa_check(sig, sign, m, sizeof m) == 0;
}

int seal_bundle_check(const SealBundle *b) {
    uint8_t m[43];
    if (b->opk_count < 0 || b->opk_count > SEAL_OPK || !ik_check(b->ik_sign, b->ik_dh, b->ik_sig))
        return SEAL_EBADSIG;
    spk_message(m, b->spk_id, b->spk_pk);
    return crypto_eddsa_check(b->spk_sig, b->ik_sign, m, sizeof m) == 0 ? SEAL_OK : SEAL_EBADSIG;
}

static void x3dh_secret(uint8_t sk[32], const uint8_t dh[4][32], int n) {
    crypto_blake2b_ctx c;
    uint8_t f[32];
    memset(f, 0xFF, 32);
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, (const uint8_t *)"ORBIT-X3DH1", 11);
    crypto_blake2b_update(&c, f, 32);
    for (int i = 0; i < n; i++)
        crypto_blake2b_update(&c, dh[i], 32);
    crypto_blake2b_final(&c, sk);
}

int seal_session_start(SealSession *s, const SealIdentity *me, const SealBundle *peer, const uint8_t rnd[64]) {
    if (seal_bundle_check(peer) != SEAL_OK)
        return SEAL_EBADSIG;
    uint8_t ek_sk[32], ek_pk[32], dh[4][32], sk[32], m[40];
    int n = peer->opk_count > 0 ? 4 : 3;
    memcpy(ek_sk, rnd, 32);
    crypto_x25519_public_key(ek_pk, ek_sk);
    crypto_x25519(dh[0], me->dh_sk, peer->spk_pk);
    crypto_x25519(dh[1], ek_sk, peer->ik_dh);
    crypto_x25519(dh[2], ek_sk, peer->spk_pk);
    if (n == 4)
        crypto_x25519(dh[3], ek_sk, peer->opk_pk[0]);
    x3dh_secret(sk, (const uint8_t(*)[32])dh, n);

    memset(s, 0, sizeof *s);
    s->ready = s->initiator = s->have_init = 1;
    memcpy(s->ad, me->sign_pk, 32);
    memcpy(s->ad + 32, peer->ik_sign, 32);
    memcpy(s->peer_ik_sign, peer->ik_sign, 32);
    /* session-start prefix: our identity (bound by its signature), base key, prekey ids */
    uint8_t *p = s->init_prefix;
    memcpy(p, me->sign_pk, 32);
    memcpy(p + 32, me->dh_pk, 32);
    ik_message(m, me->dh_pk);
    crypto_eddsa_sign(p + 64, me->sign_sk, m, sizeof m);
    memcpy(p + 128, ek_pk, 32);
    put16(p + 160, peer->spk_id);
    put16(p + 162, n == 4 ? peer->opk_id[0] : SEAL_NO_OPK);
    /* RatchetInitAlice */
    memcpy(s->rk, sk, 32);
    memcpy(s->dhr, peer->spk_pk, 32);
    s->have_dhr = 1;
    memcpy(s->dhs_sk, rnd + 32, 32);
    crypto_x25519_public_key(s->dhs_pk, s->dhs_sk);
    uint8_t out[32];
    crypto_x25519(out, s->dhs_sk, s->dhr);
    kdf_rk(s->rk, s->cks, out);
    s->have_cks = 1;
    crypto_wipe(out, 32);
    crypto_wipe(ek_sk, 32);
    crypto_wipe(dh, sizeof dh);
    crypto_wipe(sk, 32);
    return SEAL_OK;
}

int seal_encrypt(SealSession *s, const uint8_t *msg, size_t len, uint8_t *out, size_t cap) {
    if (!s->ready || !s->have_cks)
        return SEAL_ESTATE;
    size_t pre = s->have_init ? SEAL_INIT : 0, total = 1 + pre + SEAL_HEADER + 16 + len;
    if (cap < total || len > 1 << 20)
        return SEAL_ESPACE;
    uint8_t mk[32], key[32], nonce[24], ad[64 + SEAL_INIT + SEAL_HEADER];
    out[0] = s->have_init ? T_INIT : T_MSG;
    if (pre)
        memcpy(out + 1, s->init_prefix, pre);
    uint8_t *h = out + 1 + pre;
    h[0] = SEAL_VERSION;
    memcpy(h + 1, s->dhs_pk, 32);
    put32(h + 33, s->pn);
    put32(h + 37, s->ns);
    kdf_ck(s->cks, mk);
    s->ns++;
    mk_expand(mk, key, nonce);
    memcpy(ad, s->ad, 64);
    memcpy(ad + 64, out + 1, pre + SEAL_HEADER);
    crypto_aead_lock(h + SEAL_HEADER + 16, h + SEAL_HEADER, key, nonce, ad, 64 + pre + SEAL_HEADER, msg, len);
    crypto_wipe(mk, 32);
    crypto_wipe(key, 32);
    return (int)total;
}

static int skip_keys(SealSession *s, uint32_t until) {
    if (!s->have_ckr)
        return SEAL_OK;
    if (until > s->nr + SEAL_MAX_SKIP)
        return SEAL_ESKIP;
    while (s->nr < until) {
        if (s->skipped_count == SEAL_SKIPPED) { /* drop the oldest */
            crypto_wipe(&s->skipped[0], sizeof s->skipped[0]);
            memmove(s->skipped, s->skipped + 1, sizeof s->skipped[0] * (SEAL_SKIPPED - 1));
            s->skipped_count--;
        }
        SealSkipped *k = &s->skipped[s->skipped_count++];
        memcpy(k->dh, s->dhr, 32);
        k->n = s->nr;
        kdf_ck(s->ckr, k->mk);
        s->nr++;
    }
    return SEAL_OK;
}

static int open_with(const uint8_t mk[32], const uint8_t *ad, size_t ad_len, const uint8_t *mac,
                     const uint8_t *ct, size_t len, uint8_t *out) {
    uint8_t key[32], nonce[24];
    mk_expand(mk, key, nonce);
    int r = crypto_aead_unlock(out, mac, key, nonce, ad, ad_len, ct, len);
    crypto_wipe(key, 32);
    return r ? SEAL_EAUTH : SEAL_OK;
}

/* Double Ratchet receive on t (a scratch copy). */
static int ratchet_open(SealSession *t, const uint8_t *ad, size_t ad_len, const uint8_t *h,
                        const uint8_t *ct, size_t len, uint8_t *out, const uint8_t rnd[32]) {
    const uint8_t *dh = h + 1, *mac = h + SEAL_HEADER;
    uint32_t pn = get32(h + 33), n = get32(h + 37);
    for (int i = 0; i < t->skipped_count; i++)
        if (t->skipped[i].n == n && !memcmp(t->skipped[i].dh, dh, 32)) {
            if (open_with(t->skipped[i].mk, ad, ad_len, mac, ct, len, out))
                return SEAL_EAUTH;
            crypto_wipe(&t->skipped[i], sizeof t->skipped[i]);
            memmove(t->skipped + i, t->skipped + i + 1, sizeof t->skipped[0] * (size_t)(t->skipped_count - i - 1));
            t->skipped_count--;
            return SEAL_OK;
        }
    if (!t->have_dhr || memcmp(dh, t->dhr, 32)) {
        int r = skip_keys(t, pn);
        if (r)
            return r;
        uint8_t o[32];
        t->pn = t->ns;
        t->ns = t->nr = 0;
        memcpy(t->dhr, dh, 32);
        t->have_dhr = 1;
        crypto_x25519(o, t->dhs_sk, t->dhr);
        kdf_rk(t->rk, t->ckr, o);
        t->have_ckr = 1;
        memcpy(t->dhs_sk, rnd, 32);
        crypto_x25519_public_key(t->dhs_pk, t->dhs_sk);
        crypto_x25519(o, t->dhs_sk, t->dhr);
        kdf_rk(t->rk, t->cks, o);
        t->have_cks = 1;
        crypto_wipe(o, 32);
    }
    if (n < t->nr) /* already used (replay) and not among the skipped keys */
        return SEAL_EAUTH;
    int r = skip_keys(t, n);
    if (r)
        return r;
    uint8_t mk[32];
    kdf_ck(t->ckr, mk);
    t->nr++;
    r = open_with(mk, ad, ad_len, mac, ct, len, out);
    crypto_wipe(mk, 32);
    return r;
}

/* Responder side of X3DH from a session-start prefix into a fresh t. */
static int responder_start(SealSession *t, const SealIdentity *me, SealPrekeys *pk, const uint8_t *p, int *opk_slot) {
    const uint8_t *ik_sign = p, *ik_dh = p + 32, *ik_sig = p + 64, *ek = p + 128;
    uint16_t spk_id = get16(p + 160), opk_id = get16(p + 162);
    *opk_slot = -1;
    if (!ik_check(ik_sign, ik_dh, ik_sig))
        return SEAL_EBADSIG;
    if (spk_id != pk->spk_id)
        return SEAL_ENOOPK;
    if (opk_id != SEAL_NO_OPK) {
        for (int i = 0; i < SEAL_OPK; i++)
            if (pk->opk_id[i] == opk_id && !pk->opk_used[i])
                *opk_slot = i;
        if (*opk_slot < 0)
            return SEAL_ENOOPK;
    }
    uint8_t dh[4][32], sk[32];
    crypto_x25519(dh[0], pk->spk_sk, ik_dh);
    crypto_x25519(dh[1], me->dh_sk, ek);
    crypto_x25519(dh[2], pk->spk_sk, ek);
    if (*opk_slot >= 0)
        crypto_x25519(dh[3], pk->opk_sk[*opk_slot], ek);
    x3dh_secret(sk, (const uint8_t(*)[32])dh, *opk_slot >= 0 ? 4 : 3);
    memset(t, 0, sizeof *t);
    t->ready = 1;
    memcpy(t->ad, ik_sign, 32);
    memcpy(t->ad + 32, me->sign_pk, 32);
    memcpy(t->peer_ik_sign, ik_sign, 32);
    memcpy(t->peer_ek, ek, 32);
    /* RatchetInitBob */
    memcpy(t->rk, sk, 32);
    memcpy(t->dhs_sk, pk->spk_sk, 32);
    memcpy(t->dhs_pk, pk->spk_pk, 32);
    crypto_wipe(dh, sizeof dh);
    crypto_wipe(sk, 32);
    return SEAL_OK;
}

int seal_decrypt(SealSession *s, const SealIdentity *me, SealPrekeys *prekeys, const uint8_t *in,
                 size_t len, uint8_t *out, size_t cap, const uint8_t rnd[32]) {
    if (len < 1 + SEAL_HEADER + 16 || (in[0] != T_INIT && in[0] != T_MSG))
        return SEAL_EFORMAT;
    size_t pre = in[0] == T_INIT ? SEAL_INIT : 0;
    if (len < 1 + pre + SEAL_HEADER + 16 || in[1 + pre] != SEAL_VERSION)
        return SEAL_EFORMAT;
    size_t clen = len - 1 - pre - SEAL_HEADER - 16;
    if (cap < clen)
        return SEAL_ESPACE;
    const uint8_t *h = in + 1 + pre, *ct = h + SEAL_HEADER + 16;
    uint8_t ad[64 + SEAL_INIT + SEAL_HEADER];
    static SealSession t; /* scratch copy, keeps the PS3 thread stack small; not re-entrant */
    int r = SEAL_ESTATE;

    /* 1. the running session (a repeated session-start prefix of the same session is fine) */
    int same = s->ready && pre && !s->initiator && !memcmp(in + 1 + 128, s->peer_ek, 32);
    if (s->ready && (!pre || same)) {
        t = *s;
        memcpy(ad, t.ad, 64);
        memcpy(ad + 64, in + 1, pre + SEAL_HEADER);
        r = ratchet_open(&t, ad, 64 + pre + SEAL_HEADER, h, ct, clen, out, rnd);
        if (r == SEAL_OK) {
            if (t.initiator)
                t.have_init = 0; /* the peer has the session now */
            seal_session_wipe(s);
            *s = t;
            seal_session_wipe(&t);
            return (int)clen;
        }
        seal_session_wipe(&t);
        if (!pre || same) /* a replayed session start never restarts the session */
            return r;
    }
    /* 2. a new session started by the peer (first contact, reinstall, lost device) */
    if (pre && me && prekeys) {
        int slot;
        r = responder_start(&t, me, prekeys, in + 1, &slot);
        if (r == SEAL_OK) {
            memcpy(ad, t.ad, 64);
            memcpy(ad + 64, in + 1, pre + SEAL_HEADER);
            r = ratchet_open(&t, ad, 64 + pre + SEAL_HEADER, h, ct, clen, out, rnd);
        }
        if (r == SEAL_OK) {
            if (slot >= 0) {
                prekeys->opk_used[slot] = 1;
                crypto_wipe(prekeys->opk_sk[slot], 32);
            }
            seal_session_wipe(s);
            *s = t;
            seal_session_wipe(&t);
            return (int)clen;
        }
        seal_session_wipe(&t);
    }
    return r;
}

void seal_safety_number(const uint8_t a[32], const uint8_t b[32], char out[72]) {
    const uint8_t *lo = memcmp(a, b, 32) <= 0 ? a : b, *hi = lo == a ? b : a;
    crypto_blake2b_ctx c;
    uint8_t h[64];
    crypto_blake2b_init(&c, 64);
    crypto_blake2b_update(&c, (const uint8_t *)"ORBIT-SAFETY1", 13);
    crypto_blake2b_update(&c, lo, 32);
    crypto_blake2b_update(&c, hi, 32);
    crypto_blake2b_final(&c, h);
    char *o = out;
    for (int g = 0; g < 12; g++) {
        uint64_t v = 0;
        for (int i = 0; i < 5; i++)
            v = v << 8 | h[g * 5 + i];
        v %= 100000;
        for (int d = 4; d >= 0; d--) {
            o[d] = (char)('0' + v % 10);
            v /= 10;
        }
        o += 5;
        *o++ = g == 11 ? '\0' : ' ';
    }
}

size_t seal_session_size(void) { return sizeof(SealSession); }
void seal_session_wipe(SealSession *s) { crypto_wipe(s, sizeof *s); }
