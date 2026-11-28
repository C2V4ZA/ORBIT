/* ORBIT fuzzing (docs/next/THREAT-MODEL.md, "Fuzzing"). Built with AddressSanitizer and
 * UndefinedBehaviorSanitizer by `make fuzz`; every target is also a libFuzzer entry point
 * (build with -DFUZZ_LIBFUZZER -DFUZZ_ONE=<n> -fsanitize=fuzzer when libFuzzer is installed).
 *
 *   ./build/fuzz <target|all> [iterations] [seed]
 *
 * The driver mutates seed inputs (bit flips, byte changes, inserts, deletes, splices, "interesting"
 * numbers, truncation) with a fixed seed, so a run is repeatable. A crash prints the sanitizer
 * report and writes the input to build/fuzz-crash-<target>.bin. */
#include "avatar.h"
#include "cJSON.h"
#include "covers.h"
#include "directory.h"
#include "gif.h"
#include "i18n.h"
#include "image.h"
#include "monocypher.h"
#include "orbit.h"
#include "sealed.h"
#include "update.h"
#include "update_key.h"
#include <sanitizer/common_interface_defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ---------------- targets ---------------- */
static int t_invite(const uint8_t *d, size_t n) {
    char s[64], code[9];
    size_t k = n < sizeof(s) - 1 ? n : sizeof(s) - 1;
    memcpy(s, d, k);
    s[k] = 0;
    uint32_t ip;
    int port;
    if (!orbit_invite_decode(s, &ip, &port, code)) {
        char back[ORBIT_INVITE_TEXT];
        orbit_invite_encode(back, ip, port, code); /* round trip must not fault */
    }
    return 0;
}
static int t_image(const uint8_t *d, size_t n) {
    OrbitImage img;
    memset(&img, 0, sizeof(img));
    if (!image_decode_any(d, n, &img))
        image_free(&img);
    memset(&img, 0, sizeof(img));
    if (!image_jpeg_decode(d, n, &img, 512 * 512))
        image_free(&img);
    memset(&img, 0, sizeof(img));
    if (!image_png_decode(d, n, &img, 512, 512))
        image_free(&img);
    return 0;
}
static int frame_cb(void *ctx, const uint32_t *canvas, int w, int h, int index, int delay_ms) {
    (void)ctx, (void)index, (void)delay_ms;
    volatile uint32_t x = 0;
    if (w > 0 && h > 0)
        x = canvas[0] ^ canvas[(size_t)w * (size_t)h - 1]; /* the whole canvas must be readable */
    (void)x;
    return 0;
}
static int t_gif(const uint8_t *d, size_t n) {
    gif_sniff(d, n);
    gif_decode(d, n, frame_cb, NULL);
    OrbitImage img;
    memset(&img, 0, sizeof(img));
    if (!image_gif_first(d, n, &img))
        image_free(&img);
    return 0;
}
static int t_avatar(const uint8_t *d, size_t n) {
    char hash[33];
    avatar_hash(d, n, hash);
    OrbitImage img;
    memset(&img, 0, sizeof(img));
    if (!avatar_decode(d, n, hash, &img))
        image_free(&img);
    return 0;
}
static int t_b64(const uint8_t *d, size_t n) {
    char *s = malloc(n + 1);
    memcpy(s, d, n);
    s[n] = 0;
    uint8_t out[AVATAR_BLOB_MAX];
    size_t m;
    avatar_b64_decode(out, sizeof(out), &m, s);
    free(s);
    return 0;
}
static int t_covers(const uint8_t *d, size_t n) {
    OrbitImage img;
    memset(&img, 0, sizeof(img));
    if (!covers_decode(d, n, &img))
        image_free(&img);
    CoverConfig cfg;
    covers_config_parse((const char *)d, n, &cfg);
    return 0;
}
static int t_update(const uint8_t *d, size_t n) {
    static const uint8_t key[32] = UPDATE_PUBLIC_KEY;
    UpdateInfo info;
    update_parse((const char *)d, n, key, "de", &info);
    char a[32], b[32];
    size_t k = n < 31 ? n : 31;
    memcpy(a, d, k), a[k] = 0;
    memcpy(b, d + n - k, k), b[k] = 0;
    update_version_cmp(a, b);
    return 0;
}
static int t_directory(const uint8_t *d, size_t n) {
    cJSON *x = cJSON_ParseWithLength((const char *)d, n);
    DirCommunity c;
    if (x) {
        dir_community_parse(x, &c, 0);
        dir_community_parse(x, &c, 1);
        const cJSON *e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(x, "communities")) dir_community_parse(e, &c, 1);
    }
    cJSON_Delete(x);
    return 0;
}
/* sealed messages: a responder with a real identity receives attacker bytes */
static SealIdentity seal_me;
static SealPrekeys seal_pk;
static uint8_t seal_valid[512];
static int seal_valid_n;
static void seal_setup(void) {
    static int done;
    if (done)
        return;
    done = 1;
    uint8_t seed[64] = {7}, rnd[64] = {9};
    seal_identity_new(&seal_me, seed);
    static uint8_t prnd[32 * (1 + SEAL_OPK)];
    memset(prnd, 5, sizeof(prnd));
    seal_prekeys_new(&seal_pk, &seal_me, 1, 1, prnd);
    SealIdentity peer;
    seed[0] = 8;
    seal_identity_new(&peer, seed);
    SealBundle b;
    seal_bundle_make(&b, &seal_me, &seal_pk);
    static SealSession s;
    memset(&s, 0, sizeof(s));
    if (!seal_session_start(&s, &peer, &b, rnd))
        seal_valid_n = seal_encrypt(&s, (const uint8_t *)"hallo", 5, seal_valid, sizeof(seal_valid));
}
static int t_sealed(const uint8_t *d, size_t n) {
    seal_setup();
    static SealSession s;
    static SealPrekeys pk;
    memset(&s, 0, sizeof(s));
    pk = seal_pk;
    uint8_t out[1024], rnd[32] = {3};
    seal_decrypt(&s, &seal_me, &pk, d, n, out, sizeof(out), rnd);
    return 0;
}
/* the host's request handler: what any connected (signed-in) console can send */
static OrbitStore *rq_store;
static OrbitSession rq_owner, rq_guest;
static int rq_count;
static long rq_parsed, rq_ok, rq_signed; /* evidence that the run reaches past sign-in */
static void rq_sign_in(OrbitSession *se, const char *name, uint8_t dev0) {
    uint8_t device[32] = {dev0}, secret[64], pub[32], sig[64], t[32];
    orbit_identity(device, secret, pub);
    orbit_bind_sign(sig, secret, pub, rq_store->server_id);
    crypto_blake2b_keyed(t, 32, device, 32, (const uint8_t *)rq_store->server_id, 16);
    char token[65], p[65], g[129], json[700];
    orbit_hex(token, t, 32);
    orbit_hex(p, pub, 32);
    orbit_hex(g, sig, 64);
    memset(se, 0, sizeof(*se));
    se->user = -1, se->proto = 9;
    const char *steps[2] = {"{\"op\":\"hello\",\"protocol\":11}", json};
    snprintf(json, sizeof(json),
             "{\"op\":\"device\",\"token\":\"%s\",\"name\":\"%s\",\"pub\":\"%s\",\"sig\":\"%s\",\"status\":0}",
             token, name, p, g);
    for (int i = 0; i < 2; i++) {
        cJSON *q = cJSON_Parse(steps[i]);
        cJSON_Delete(orbit_request(rq_store, se, q));
        cJSON_Delete(q);
    }
}
static void rq_reset(void) {
    if (rq_store) {
        orbit_session_end(rq_store, &rq_owner);
        orbit_session_end(rq_store, &rq_guest);
        orbit_store_close(rq_store);
        free(rq_store);
    }
    rq_store = calloc(1, sizeof(*rq_store));
    orbit_store_open(rq_store, NULL); /* in memory, nothing written */
    memcpy(rq_store->server_id, "00112233aabbccdd", 17);
    rq_sign_in(&rq_owner, "Alex", 1);
    rq_sign_in(&rq_guest, "Mika", 2);
    const char *setup[] = {"{\"op\":\"create\",\"name\":\"Spieleabend\"}",
                           "{\"op\":\"send\",\"group\":0,\"channel\":0,\"text\":\"hallo\"}"};
    for (int i = 0; i < 2; i++) {
        cJSON *q = cJSON_Parse(setup[i]);
        cJSON_Delete(orbit_request(rq_store, &rq_owner, q));
        cJSON_Delete(q);
    }
    rq_count = 0;
}
static const char *const ops[] = {
    "add_member", "audit", "ban", "bind_device", "blob_put", "browse", "category_add", "category_remove",
    "category_update", "channel_add", "channel_remove", "channel_update", "create", "delete", "device",
    "dm_create", "edit", "forget_token", "friend", "friend_block", "friend_block_pub", "friend_out",
    "friend_remove", "friend_respond", "friend_unblock", "friends", "groups", "hello", "history", "home",
    "invitations", "invite", "invite_user", "join", "join_public", "kick", "leave", "login", "logout",
    "member_roles", "members", "pic", "profile", "react", "register", "remember", "remove_msg", "report",
    "report_resolve", "reports", "respond_invite", "resume", "role_add", "role_remove", "role_update",
    "rotate", "search", "send", "server_mute", "server_update", "unban", "watch_key"};
#define nops ((int)(sizeof(ops) / sizeof(ops[0])))
static cJSON *rq_value(const uint8_t *d, size_t n, size_t *i, int depth) {
    uint8_t k = *i < n ? d[(*i)++] : 0, v = *i < n ? d[(*i)++] : 0;
    static const double nums[] = {0, 1, 2, 3, -1, 15, 16, 63, 64, 255, 65535, 2147483647.0, -2147483648.0,
                                  4294967296.0, 1e300, -1e300, 0.5};
    switch (k % 7) {
    case 0: case 1: return cJSON_CreateNumber(nums[v % 17]);
    case 2: return cJSON_CreateNumber(v);
    case 3: { /* string: short, long, odd UTF-8, controls */
        static char s[1200];
        size_t len = v % 4 == 0 ? 1100 : v % 40;
        for (size_t j = 0; j < len; j++)
            s[j] = *i < n ? (char)(d[(*i)++] | (v & 1 ? 0x80 : 0)) : 'a';
        s[len] = 0;
        for (size_t j = 0; j < len; j++)
            if (!s[j])
                s[j] = 'x';
        return cJSON_CreateString(s);
    }
    case 4: return v & 1 ? cJSON_CreateTrue() : cJSON_CreateNull();
    case 5: {
        cJSON *a = cJSON_CreateArray();
        for (int j = 0; depth < 4 && j < v % 6; j++)
            cJSON_AddItemToArray(a, rq_value(d, n, i, depth + 1));
        return a;
    }
    default: {
        cJSON *o = cJSON_CreateObject();
        if (depth < 4)
            cJSON_AddItemToObject(o, "id", rq_value(d, n, i, depth + 1));
        return o;
    }
    }
}
static cJSON *rq_build(const uint8_t *d, size_t n) {
    static const char *const keys[] = {"group", "channel", "ch", "id", "user", "text", "name", "code", "after",
                                       "emoji", "roles", "reason", "hash", "data", "desc", "view", "send", "cat",
                                       "kind", "perms", "color", "icon", "to", "reply", "pub", "sig", "token",
                                       "status", "protocol", "invite", "people", "target", "role", "until", "game",
                                       "bio", "lang", "public", "accept", "pos", "limit", "before", "msg"};
    if (!n)
        return NULL;
    size_t i = 1;
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "op", ops[d[0] % nops]);
    while (i + 1 < n && cJSON_GetArraySize(q) < 12) {
        const char *key = keys[d[i++] % (sizeof(keys) / sizeof(keys[0]))];
        if (!cJSON_GetObjectItem(q, key))
            cJSON_AddItemToObject(q, key, rq_value(d, n, &i, 0));
    }
    return q;
}
static int t_request(const uint8_t *d, size_t n) {
    if (!rq_store || ++rq_count > 3000 || rq_owner.user < 0 || rq_guest.user < 0)
        rq_reset(); /* keep the store small and the run fast */
    cJSON *q = cJSON_ParseWithLength((const char *)d, n);
    if (!q)
        q = rq_build(d, n); /* structure-aware: the bytes pick op, fields and value types */
    if (!q)
        return 0;
    OrbitSession anon = {-1, 9, 0, 0, 0}; /* like a fresh connection (src/network.c) */
    OrbitSession *who = n % 3 == 0 ? &anon : n % 3 == 1 ? &rq_owner : &rq_guest;
    cJSON *r = orbit_request(rq_store, who, q);
    rq_parsed++;
    if (cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")))
        rq_ok++;
    if (who->user >= 0)
        rq_signed++;
    if (r) { /* the reply must serialise (no dangling or cyclic items) */
        char *s = cJSON_PrintUnformatted(r);
        free(s);
    }
    cJSON_Delete(r);
    cJSON_Delete(q);
    if (who == &anon)
        orbit_session_end(rq_store, &anon);
    return 0;
}

/* raw bytes into the host's TCP port (before and instead of a TLS handshake); afterwards the host
 * must still accept a real group connection (checked at the end of the run) */
static OrbitStore *wire_store;
static OrbitServer *wire_srv;
#define WIRE_PORT 31545
static void wire_setup(void) {
    if (wire_srv)
        return;
    wire_store = calloc(1, sizeof(*wire_store));
    orbit_store_open(wire_store, NULL);
    wire_store->data.group[0].used = 1;
    wire_store->data.group[0].members = 1;
    strcpy(wire_store->data.group[0].invite, "QRST2345");
    static const uint8_t key[32] = {1};
    wire_srv = orbit_server_start(wire_store, key, WIRE_PORT);
}
static int t_wire(const uint8_t *d, size_t n) {
    wire_setup();
    if (!wire_srv)
        return 0;
    int fd = orbit_tcp_connect("127.0.0.1", WIRE_PORT, NULL, 0);
    if (fd < 0)
        return 0;
    send(fd, d, n, MSG_NOSIGNAL);
    close(fd);
    return 0;
}
static int wire_check(void) {
    OrbitLink *l = NULL;
    for (int i = 0; i < 40 && !l; i++) { /* peers time out after their slot is used up */
        l = orbit_connect_group("127.0.0.1", WIRE_PORT, "QRST-2345", NULL, 0);
        if (!l)
            usleep(250000);
    }
    if (!l)
        return -1;
    orbit_disconnect(l);
    return 0;
}

typedef struct {
    const char *name;
    int (*fn)(const uint8_t *, size_t);
} Target;
static const Target targets[] = {
    {"request", t_request}, {"invite", t_invite}, {"image", t_image},   {"gif", t_gif},
    {"avatar", t_avatar},   {"b64", t_b64},       {"covers", t_covers}, {"update", t_update},
    {"directory", t_directory}, {"sealed", t_sealed}, {"wire", t_wire},
};
#define NT ((int)(sizeof(targets) / sizeof(targets[0])))

#ifdef FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) { return targets[FUZZ_ONE].fn(d, n); }
#else
/* ---------------- seeds ---------------- */
typedef struct {
    uint8_t *d;
    size_t n;
} Buf;
static Buf seeds[256];
static int nseeds;
static void add_seed(const void *d, size_t n) {
    if (nseeds >= 256)
        return;
    seeds[nseeds].d = malloc(n ? n : 1);
    memcpy(seeds[nseeds].d, d, n);
    seeds[nseeds++].n = n;
}
static void add_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    add_seed(buf, n);
}
static void seeds_for(const char *t) {
    nseeds = 0;
    if (!strcmp(t, "request")) {
        char q[400];
        for (int i = 0; i < nops; i++) {
            snprintf(q, sizeof(q),
                     "{\"op\":\"%s\",\"group\":0,\"channel\":0,\"ch\":1,\"id\":1,\"user\":1,\"text\":\"hi\","
                     "\"name\":\"Neu\",\"code\":\"QRST2345\",\"after\":0,\"emoji\":1,\"roles\":[1,2],"
                     "\"reason\":1,\"hash\":\"00000000000000000000000000000000\",\"data\":\"AAAA\"}",
                     ops[i]);
            add_seed(q, strlen(q));
        }
    } else if (!strcmp(t, "invite")) {
        char code[ORBIT_INVITE_TEXT];
        orbit_invite_encode(code, 0x93b9dd11, 61234, "ABCDEF23");
        add_seed(code, strlen(code));
        orbit_invite_encode(code, 0x7f000001, ORBIT_INVITE_RELAY | 31400, "QRST2345");
        add_seed(code, strlen(code));
    } else if (!strcmp(t, "image") || !strcmp(t, "covers")) {
        const char *f[] = {"tests/fixtures/base420.jpg", "tests/fixtures/base444.jpg", "tests/fixtures/gray.jpg",
                           "tests/fixtures/prog.jpg", "tests/fixtures/exif6.jpg", "tests/fixtures/small.png"};
        for (int i = 0; i < 6; i++)
            add_file(f[i]);
    } else if (!strcmp(t, "gif")) {
        const char *f[] = {"tests/fixtures/anim.gif", "tests/fixtures/interlaced.gif", "tests/fixtures/transparent.gif",
                           "tests/fixtures/noise.gif"};
        for (int i = 0; i < 4; i++)
            add_file(f[i]);
    } else if (!strcmp(t, "avatar") || !strcmp(t, "b64")) {
        OrbitImage img;
        memset(&img, 0, sizeof(img));
        if (!image_load_any("tests/fixtures/base420.jpg", &img)) {
            static uint8_t blob[AVATAR_BLOB_MAX];
            size_t n;
            char hash[33];
            if (!avatar_make(&img, blob, &n, hash)) {
                if (!strcmp(t, "avatar"))
                    add_seed(blob, n);
                else {
                    static char b64[AVATAR_BLOB_MAX * 2];
                    avatar_b64_encode(b64, sizeof(b64), blob, n);
                    add_seed(b64, strlen(b64));
                }
            }
            image_free(&img);
        }
    } else if (!strcmp(t, "update")) {
        const char *m = "{\"version\":\"1.3.0\",\"url\":\"https://example.invalid/o.pkg\",\"size\":100,"
                        "\"sha256\":\"00\",\"notes\":{\"de\":\"x\"},\"sig\":\"00\"}";
        add_seed(m, strlen(m));
    } else if (!strcmp(t, "directory")) {
        const char *m = "{\"communities\":[{\"server\":\"00112233aabbccdd\",\"id\":1,\"name\":\"Spieleabend\","
                        "\"desc\":\"x\",\"members\":3,\"games\":[\"BLES00001\"],\"invite\":\"EZB3C-R9EJH-45KKC-FUR94\","
                        "\"lang\":\"de\",\"seen\":1}]}";
        add_seed(m, strlen(m));
    } else if (!strcmp(t, "wire")) {
        static const uint8_t hello[] = {0x16, 0x03, 0x01, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x2b, 0x03, 0x03, 1, 2, 3, 4, 5, 6,
                                        7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                                        29, 30, 31, 32, 0x00, 0x00, 0x02, 0x00, 0xa8, 0x01, 0x00};
        add_seed(hello, sizeof(hello));
        add_seed("{\"op\":\"hello\"}\n", 16);
        add_seed("ORBIT7?0123456789abcdef", 23);
    } else if (!strcmp(t, "sealed")) {
        seal_setup();
        add_seed(seal_valid, (size_t)seal_valid_n);
    }
    if (!nseeds)
        add_seed("{}", 2);
}

/* ---------------- mutator ---------------- */
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return (uint32_t)(rng >> 11);
}
static size_t mutate(uint8_t *b, size_t n, size_t cap) {
    static const int64_t interesting[] = {0, 1, -1, 127, 128, 255, 256, 32767, 65535, 65536, 0x7fffffff,
                                          -2147483648ll, 4294967295ll};
    int rounds = 1 + (int)(rnd() % 8);
    for (int r = 0; r < rounds; r++) {
        switch (rnd() % 9) {
        case 0: if (n) b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: if (n) b[rnd() % n] = (uint8_t)rnd(); break;
        case 2: if (n < cap) { size_t p = rnd() % (n + 1); memmove(b + p + 1, b + p, n - p); b[p] = (uint8_t)rnd(); n++; } break;
        case 3: if (n) { size_t p = rnd() % n, k = 1 + rnd() % (n - p); memmove(b + p, b + p + k, n - p - k); n -= k; } break;
        case 4: if (n) n = rnd() % n; break; /* truncate */
        case 5: if (n >= 4) { size_t p = rnd() % (n - 3); int64_t v = interesting[rnd() % 13]; memcpy(b + p, &v, 4); } break;
        case 6: if (n && n * 2 <= cap) { size_t p = rnd() % n, k = 1 + rnd() % (n - p); if (n + k <= cap) { memmove(b + p + k, b + p, n - p); n += k; } } break; /* duplicate a block */
        case 7: if (nseeds > 1 && n) { const Buf *o = &seeds[rnd() % nseeds]; if (o->n) { size_t p = rnd() % n, q = rnd() % o->n, k = 1 + rnd() % (o->n - q); if (p + k > cap) k = cap - p; memcpy(b + p, o->d + q, k); if (p + k > n) n = p + k; } } break; /* splice */
        case 8: { /* JSON-ish: replace a digit run by a big or negative number */
            if (!n) break;
            size_t p = rnd() % n;
            const char *nums[] = {"-1", "99999999999", "2147483648", "1e308", "-0", "0.5", "\"\"", "null", "[]", "{}"};
            const char *s = nums[rnd() % 10];
            size_t k = strlen(s);
            if (n + k <= cap) { memmove(b + p + k, b + p, n - p); memcpy(b + p, s, k); n += k; }
        } break;
        }
    }
    return n;
}

static const char *current;
static uint8_t *cur_d;
static size_t cur_n;
static void on_death(void) {
    char path[128];
    snprintf(path, sizeof(path), "build/fuzz-crash-%s.bin", current);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(cur_d, 1, cur_n, f);
        fclose(f);
        fprintf(stderr, "fuzz: crashing input saved to %s\n", path);
    }
}
static void run(const Target *t, long iters) {
    seeds_for(t->name);
    current = t->name;
    size_t cap = 1 << 18;
    uint8_t *b = malloc(cap);
    clock_t start = clock();
    for (int i = 0; i < nseeds; i++) { /* the seeds themselves first */
        cur_d = seeds[i].d, cur_n = seeds[i].n;
        t->fn(seeds[i].d, seeds[i].n);
    }
    if (t->fn == t_wire && iters > 3000)
        iters = 3000;
    for (long i = 0; i < iters; i++) {
        const Buf *s = &seeds[rnd() % nseeds];
        size_t n = s->n < cap ? s->n : cap;
        memcpy(b, s->d, n);
        n = mutate(b, n, cap);
        /* exact-size copy so ASan sees any read past the end */
        uint8_t *exact = malloc(n ? n : 1);
        memcpy(exact, b, n);
        cur_d = exact, cur_n = n;
        t->fn(exact, n);
        free(exact);
    }
    printf("fuzz %-9s %8ld inputs, %3d seeds, %.1f s: no crash, no sanitizer report\n", t->name, iters, nseeds,
           (double)(clock() - start) / CLOCKS_PER_SEC);
    if (t->fn == t_wire) {
        if (wire_check()) {
            printf("          wire: FAIL - the host no longer accepts a real connection\n");
            exit(1);
        }
        printf("          wire: host still accepts a real group connection afterwards\n");
        orbit_server_stop(wire_srv);
        wire_srv = NULL;
    }
    if (!strcmp(t->name, "request"))
        printf("          request: %ld parsed as JSON, %ld sent by a signed-in session, %ld answered ok\n", rq_parsed,
               rq_signed, rq_ok);
    fflush(stdout);
    free(b);
    for (int i = 0; i < nseeds; i++)
        free(seeds[i].d);
}
int main(int argc, char **argv) {
    i18n_set(LANG_EN);
    __sanitizer_set_death_callback(on_death);
    const char *which = argc > 1 ? argv[1] : "all";
    long iters = argc > 2 ? atol(argv[2]) : 20000;
    if (argc > 3)
        rng = strtoull(argv[3], NULL, 10) | 1;
    int found = 0;
    for (int i = 0; i < NT; i++)
        if (!strcmp(which, "all") || !strcmp(which, targets[i].name)) {
            run(&targets[i], iters);
            found = 1;
        }
    if (!found) {
        fprintf(stderr, "unknown target %s\n", which);
        return 2;
    }
    return 0;
}
#endif
