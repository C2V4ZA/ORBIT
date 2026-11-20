#define _GNU_SOURCE
/* 07b: tests for the sealed-message core (X3DH + Double Ratchet). Not a substitute for the
 * external review listed in docs/next/E2EE.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sealed.h"

static unsigned rng_state = 12345;
static void rnd(uint8_t *p, size_t n) { /* deterministic test randomness */
    for (size_t i = 0; i < n; i++) {
        rng_state = rng_state * 1103515245u + 12345u;
        p[i] = (uint8_t)(rng_state >> 16);
    }
}

typedef struct {
    SealIdentity id;
    SealPrekeys pk;
} Person;

static void person(Person *p, uint16_t spk) {
    uint8_t seed[64], r[32 * (1 + SEAL_OPK)];
    rnd(seed, 64);
    rnd(r, sizeof r);
    seal_identity_new(&p->id, seed);
    seal_prekeys_new(&p->pk, &p->id, spk, 100, r);
}

static int seal(SealSession *s, const char *msg, uint8_t *out) {
    int n = seal_encrypt(s, (const uint8_t *)msg, strlen(msg), out, 2048);
    assert(n > 0);
    return n;
}
static int open_msg(SealSession *s, Person *me, const uint8_t *in, int n, char *text) {
    uint8_t r[32];
    rnd(r, 32);
    int k = seal_decrypt(s, &me->id, &me->pk, in, (size_t)n, (uint8_t *)text, 2048, r);
    if (k >= 0)
        text[k] = 0;
    return k;
}

static void test_basic(void) {
    static Person a, b;
    static SealSession sa, sb;
    SealBundle bundle;
    uint8_t buf[2048], r[64];
    char text[2048];
    person(&a, 1);
    person(&b, 7);
    seal_bundle_make(&bundle, &b.id, &b.pk);
    assert(seal_bundle_check(&bundle) == SEAL_OK && bundle.opk_count == SEAL_OPK);
    rnd(r, 64);
    assert(seal_session_start(&sa, &a.id, &bundle, r) == SEAL_OK);

    /* first message carries the session start; no plaintext in the wire bytes */
    int n = seal(&sa, "hello bob, secret text", buf);
    assert(n == 1 + SEAL_INIT + SEAL_HEADER + 16 + 22);
    assert(!memmem(buf, (size_t)n, "secret", 6));
    memset(&sb, 0, sizeof sb);
    assert(open_msg(&sb, &b, buf, n, text) == 22 && !strcmp(text, "hello bob, secret text"));
    assert(b.pk.opk_used[0] == 1); /* one-time prekey consumed and wiped */
    assert(!memcmp(sb.peer_ik_sign, a.id.sign_pk, 32));

    /* replay of the first message: refused, session unchanged */
    SealSession copy = sb;
    assert(open_msg(&sb, &b, buf, n, text) < 0);
    assert(!memcmp(&copy, &sb, sizeof sb));

    /* a second message before the reply still carries the prefix, and opens */
    n = seal(&sa, "second", buf);
    assert(buf[0] == 1);
    assert(open_msg(&sb, &b, buf, n, text) == 6 && !strcmp(text, "second"));

    /* reply: Alice drops the prefix afterwards */
    n = seal(&sb, "hi alice", buf);
    assert(buf[0] == 2);
    assert(open_msg(&sa, &a, buf, n, text) == 8 && !strcmp(text, "hi alice"));
    n = seal(&sa, "third", buf);
    assert(buf[0] == 2 && n == 1 + SEAL_HEADER + 16 + 5);
    assert(open_msg(&sb, &b, buf, n, text) == 5);

    /* many round trips, both directions, ratchet keeps turning */
    for (int i = 0; i < 50; i++) {
        char m[64];
        snprintf(m, sizeof m, "round %d", i);
        SealSession *from = i % 3 ? &sa : &sb, *to = i % 3 ? &sb : &sa;
        Person *who = i % 3 ? &b : &a;
        n = seal(from, m, buf);
        assert(open_msg(to, who, buf, n, text) >= 0 && !strcmp(text, m));
    }

    /* safety numbers agree on both sides and differ for another peer */
    char s1[72], s2[72], s3[72];
    seal_safety_number(a.id.sign_pk, b.id.sign_pk, s1);
    seal_safety_number(b.id.sign_pk, a.id.sign_pk, s2);
    seal_safety_number(a.id.sign_pk, a.id.sign_pk, s3);
    assert(!strcmp(s1, s2) && strcmp(s1, s3) && strlen(s1) == 71);
    printf("PASS: sealed DMs: session start from a signed bundle, one-time prekey used once, both directions, safety numbers\n");
}

static void test_order_and_attacks(void) {
    static Person a, b;
    static SealSession sa, sb;
    SealBundle bundle;
    uint8_t r[64], msgs[10][256], buf[2048];
    int len[10];
    char text[2048];
    person(&a, 1);
    person(&b, 2);
    seal_bundle_make(&bundle, &b.id, &b.pk);
    rnd(r, 64);
    assert(seal_session_start(&sa, &a.id, &bundle, r) == SEAL_OK);
    memset(&sb, 0, sizeof sb);
    int n = seal(&sa, "start", buf);
    assert(open_msg(&sb, &b, buf, n, text) == 5);
    n = seal(&sb, "ok", buf);
    assert(open_msg(&sa, &a, buf, n, text) == 2);

    /* out of order: 10 messages delivered as 9, 0, 5, 1..4, 6..8 */
    for (int i = 0; i < 10; i++) {
        char m[16];
        snprintf(m, sizeof m, "m%d", i);
        len[i] = seal_encrypt(&sa, (const uint8_t *)m, strlen(m), msgs[i], sizeof msgs[i]);
    }
    int order[10] = {9, 0, 5, 1, 2, 3, 4, 6, 7, 8};
    for (int k = 0; k < 10; k++) {
        char m[16];
        snprintf(m, sizeof m, "m%d", order[k]);
        assert(open_msg(&sb, &b, msgs[order[k]], len[order[k]], text) >= 0 && !strcmp(text, m));
    }
    assert(sb.skipped_count == 0);
    /* replays of in-order and of skipped messages fail */
    assert(open_msg(&sb, &b, msgs[9], len[9], text) == SEAL_EAUTH);
    assert(open_msg(&sb, &b, msgs[3], len[3], text) == SEAL_EAUTH);

    /* tampering with header, MAC, ciphertext or type: refused, state unchanged */
    n = seal(&sa, "tamper me", buf);
    SealSession before = sb;
    int spots[4] = {1 + 5, 1 + 35, 1 + SEAL_HEADER + 3, n - 1};
    for (int i = 0; i < 4; i++) {
        buf[spots[i]] ^= 0x40;
        assert(open_msg(&sb, &b, buf, n, text) < 0);
        assert(!memcmp(&before, &sb, sizeof sb));
        buf[spots[i]] ^= 0x40;
    }
    buf[0] = 9;
    assert(open_msg(&sb, &b, buf, n, text) == SEAL_EFORMAT);
    buf[0] = 2;
    assert(open_msg(&sb, &b, buf, 20, text) == SEAL_EFORMAT);
    assert(open_msg(&sb, &b, buf, n, text) == 9);

    /* too many skipped messages in one chain: refused instead of deriving keys forever */
    for (int i = 0; i < SEAL_MAX_SKIP + 1; i++)
        seal(&sa, "lost", buf);
    n = seal(&sa, "far ahead", buf);
    assert(open_msg(&sb, &b, buf, n, text) == SEAL_ESKIP);

    /* wrong recipient: Carol cannot open a message for Bob */
    static Person c;
    static SealSession sc;
    person(&c, 3);
    seal_bundle_make(&bundle, &b.id, &b.pk);
    static SealSession s2;
    rnd(r, 64);
    assert(seal_session_start(&s2, &a.id, &bundle, r) == SEAL_OK);
    n = seal(&s2, "for bob only", buf);
    memset(&sc, 0, sizeof sc);
    assert(open_msg(&sc, &c, buf, n, text) < 0 && !sc.ready);

    /* forged bundle: a prekey not signed by the identity is refused */
    SealBundle forged = bundle;
    forged.spk_pk[0] ^= 1;
    assert(seal_bundle_check(&forged) == SEAL_EBADSIG);
    rnd(r, 64);
    assert(seal_session_start(&s2, &a.id, &forged, r) == SEAL_EBADSIG);
    forged = bundle;
    memcpy(forged.ik_dh, c.id.dh_pk, 32); /* swap in another X25519 key under Bob's name */
    assert(seal_bundle_check(&forged) == SEAL_EBADSIG);
    /* impersonation: Mallory claims Alice's signing key with her own DH key */
    static Person m;
    person(&m, 4);
    rnd(r, 64);
    static SealSession sm;
    seal_bundle_make(&bundle, &b.id, &b.pk);
    assert(seal_session_start(&sm, &m.id, &bundle, r) == SEAL_OK);
    n = seal(&sm, "i am alice", buf);
    memcpy(buf + 1, a.id.sign_pk, 32);
    static SealSession sb2;
    memset(&sb2, 0, sizeof sb2);
    assert(open_msg(&sb2, &b, buf, n, text) == SEAL_EBADSIG && !sb2.ready);
    printf("PASS: sealed DMs: out-of-order delivery, replay, tampering, skip bound, wrong recipient, forged bundles and identities refused\n");
}

static void test_key_change(void) {
    static Person a, a2, b;
    static SealSession sa, sb;
    SealBundle bundle;
    uint8_t r[64], buf[2048];
    char text[2048];
    person(&a, 1);
    person(&b, 2);
    seal_bundle_make(&bundle, &b.id, &b.pk);
    rnd(r, 64);
    assert(seal_session_start(&sa, &a.id, &bundle, r) == SEAL_OK);
    memset(&sb, 0, sizeof sb);
    int n = seal(&sa, "hi", buf);
    assert(open_msg(&sb, &b, buf, n, text) == 2);
    uint8_t old_peer[32];
    memcpy(old_peer, sb.peer_ik_sign, 32);

    /* Alice reinstalls (lost device): new identity, new session from the published bundle */
    person(&a2, 9);
    seal_bundle_make(&bundle, &b.id, &b.pk);
    assert(bundle.opk_count == SEAL_OPK - 1); /* the used one-time prekey is gone */
    rnd(r, 64);
    static SealSession sa2;
    assert(seal_session_start(&sa2, &a2.id, &bundle, r) == SEAL_OK);
    n = seal(&sa2, "new phone", buf);
    assert(open_msg(&sb, &b, buf, n, text) == 9);
    assert(memcmp(old_peer, sb.peer_ik_sign, 32)); /* the app shows "key changed" */
    /* the old session no longer opens on Bob's side */
    n = seal(&sa, "old device", buf);
    assert(open_msg(&sb, &b, buf, n, text) < 0);

    /* bundle without one-time prekeys still works (3-DH) */
    for (int i = 0; i < SEAL_OPK; i++)
        b.pk.opk_used[i] = 1;
    seal_bundle_make(&bundle, &b.id, &b.pk);
    assert(bundle.opk_count == 0);
    static SealSession sa3, sb3;
    rnd(r, 64);
    assert(seal_session_start(&sa3, &a.id, &bundle, r) == SEAL_OK);
    n = seal(&sa3, "no opk", buf);
    memset(&sb3, 0, sizeof sb3);
    assert(open_msg(&sb3, &b, buf, n, text) == 6);
    /* its replay (no prekey to consume) does not restart or disturb the session */
    SealSession keep = sb3;
    assert(open_msg(&sb3, &b, buf, n, text) < 0);
    assert(!memcmp(&keep, &sb3, sizeof keep));

    /* stored state round trip (fixed-size blob) and wipe */
    uint8_t *blob = malloc(seal_session_size());
    memcpy(blob, &sb3, seal_session_size());
    static SealSession restored;
    memcpy(&restored, blob, seal_session_size());
    n = seal(&sa3, "after restore", buf);
    assert(open_msg(&restored, &b, buf, n, text) == 13);
    seal_session_wipe(&restored);
    static const SealSession zero;
    assert(!memcmp(&restored, &zero, sizeof zero));
    free(blob);
    printf("PASS: sealed DMs: key change after reinstall detected, old session refused, 3-DH start, replay-safe, state stored and wiped\n");
}

int main(void) {
    test_basic();
    test_order_and_attacks();
    test_key_change();
    return 0;
}
