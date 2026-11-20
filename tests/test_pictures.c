/* 1.2: server icons and animated pictures on the host: permissions checked by the host only,
 * animations uploaded in pieces, audit log, served in pieces, persistence, clean-up. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "anim.h"
#include "avatar.h"
#include "image.h"
#include "orbit.h"

static cJSON *request(OrbitStore *s, OrbitSession *a, cJSON *q, int expected) {
    cJSON *r = orbit_request(s, a, q);
    cJSON_Delete(q);
    assert(r);
    int good = cJSON_IsTrue(cJSON_GetObjectItem(r, "ok"));
    if (good != expected) {
        char *x = cJSON_Print(r);
        fprintf(stderr, "Unexpected response: %s\n", x);
        free(x);
        abort();
    }
    return r;
}
static void ask(OrbitStore *s, OrbitSession *a, const char *json, int ok) {
    cJSON *q = cJSON_Parse(json);
    assert(q);
    cJSON_Delete(request(s, a, q, ok));
}
static void ask_q(OrbitStore *s, OrbitSession *a, cJSON *q, int ok) {
    cJSON_Delete(request(s, a, q, ok));
}
static uint8_t *load(const char *name, size_t *n) {
    char path[256];
    snprintf(path, sizeof(path), "tests/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    assert(fread(b, 1, *n, f) == *n);
    fclose(f);
    return b;
}
/* still picture: name + base64 */
static void still(const OrbitImage *img, char hash[33], char *b64) {
    uint8_t blob[AVATAR_BLOB_MAX];
    size_t n;
    assert(!avatar_make(img, blob, &n, hash));
    assert(!avatar_b64_encode(b64, AVATAR_B64_MAX, blob, n));
}
/* send an animation in pieces; returns the number of pieces */
static int put_pieces(OrbitStore *s, OrbitSession *who, const char *hash, const uint8_t *blob, size_t n, int ok) {
    int pieces = 0;
    char *text = malloc(ANIM_PIECE / 3 * 4 + 8);
    for (size_t off = 0; off < n; off += ANIM_PIECE, pieces++) {
        size_t k = n - off < ANIM_PIECE ? n - off : ANIM_PIECE;
        assert(!avatar_b64_encode(text, ANIM_PIECE / 3 * 4 + 8, blob + off, k));
        cJSON *q = cJSON_CreateObject();
        cJSON_AddStringToObject(q, "op", "blob_put");
        cJSON_AddStringToObject(q, "hash", hash);
        cJSON_AddNumberToObject(q, "offset", (double)off);
        cJSON_AddNumberToObject(q, "total", (double)n);
        cJSON_AddStringToObject(q, "data", text);
        cJSON *r = request(s, who, q, ok);
        if (ok)
            assert((size_t)cJSON_GetObjectItem(r, "have")->valuedouble == off + k);
        cJSON_Delete(r);
        if (!ok)
            break;
    }
    free(text);
    return pieces;
}
static cJSON *server_update(int gid, const char *pic, const char *b64, const char *anim) {
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "op", "server_update");
    cJSON_AddNumberToObject(q, "group", gid);
    cJSON_AddStringToObject(q, "pic", pic);
    if (b64)
        cJSON_AddStringToObject(q, "pic_data", b64);
    if (anim)
        cJSON_AddStringToObject(q, "anim", anim);
    return q;
}
static int audit_has(OrbitStore *s, OrbitSession *who, int gid, int what) {
    char q[64];
    snprintf(q, sizeof(q), "{\"op\":\"audit\",\"group\":%d}", gid);
    cJSON *r = request(s, who, cJSON_Parse(q), 1);
    const cJSON *x;
    int found = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "audit")) found |= (int)cJSON_GetObjectItem(x, "what")->valuedouble == what;
    cJSON_Delete(r);
    return found;
}
static int file_exists(const char *store, const char *hash, const char *ext) {
    char p[300];
    snprintf(p, sizeof(p), "%s.pics/%s.%s", store, hash, ext);
    return access(p, F_OK) == 0;
}

int main(void) {
    char path[128];
    snprintf(path, sizeof(path), "/tmp/orbit-pics-%ld.json", (long)getpid());
    OrbitStore *s = calloc(1, sizeof(*s));
    assert(!orbit_store_open(s, path));
    OrbitSession owner = {.user = -1, .proto = ORBIT_PROTOCOL}, mod = {.user = -1, .proto = ORBIT_PROTOCOL}, member = {.user = -1, .proto = ORBIT_PROTOCOL};
    ask(s, &owner, "{\"op\":\"register\",\"email\":\"o@example.com\",\"name\":\"Owner\",\"password\":\"long password 1\"}", 1);
    ask(s, &mod, "{\"op\":\"register\",\"email\":\"m@example.com\",\"name\":\"Moder\",\"password\":\"long password 2\"}", 1);
    ask(s, &member, "{\"op\":\"register\",\"email\":\"p@example.com\",\"name\":\"Player\",\"password\":\"long password 3\"}", 1);
    ask(s, &owner, "{\"op\":\"create\",\"name\":\"Racing Crew\"}", 1);
    cJSON *r = request(s, &owner, cJSON_Parse("{\"op\":\"invite\",\"group\":0}"), 1);
    char code[33], join[96];
    snprintf(code, sizeof(code), "%s", orbit_string(r, "invite"));
    cJSON_Delete(r);
    snprintf(join, sizeof(join), "{\"op\":\"join\",\"invite\":\"%s\"}", code);
    ask(s, &mod, join, 1);
    ask(s, &member, join, 1);
    /* a role with "Manage server" for the moderator */
    ask(s, &owner, "{\"op\":\"role_add\",\"group\":0,\"name\":\"Design\",\"perms\":1}", 1);
    int role = -1;
    for (int i = 0; i < ORBIT_ROLES; i++)
        if (s->data.group[0].role[i].used && !strcmp(s->data.group[0].role[i].name, "Design"))
            role = i;
    assert(role >= 0);
    char q[160];
    snprintf(q, sizeof(q), "{\"op\":\"member_roles\",\"group\":0,\"user\":%d,\"roles\":%d}", mod.user, 1 << role);
    ask(s, &owner, q, 1);

    /* pictures */
    size_t n;
    uint8_t *gif = load("anim.gif", &n), *ablob;
    size_t an;
    char ahash[33], phash[33], p2[33];
    OrbitImage first;
    assert(anim_make_gif(gif, n, &ablob, &an, ahash, &first) == IMG_OK);
    free(gif);
    char *b64 = malloc(AVATAR_B64_MAX), *b64b = malloc(AVATAR_B64_MAX);
    still(&first, phash, b64);
    OrbitImage other = {4, 4, calloc(16, 4)};
    for (int i = 0; i < 16; i++)
        other.px[i] = 0xFF00FF00u;
    still(&other, p2, b64b);
    image_free(&other);

    /* 1. permissions are the host's: a plain member is refused, even when claiming permissions */
    cJSON *x = server_update(0, phash, b64, NULL);
    cJSON_AddNumberToObject(x, "perms", P_ALL);
    cJSON_AddBoolToObject(x, "owner", 1);
    ask_q(s, &member, x, 0);
    assert(!s->data.group[0].pic[0]);
    /* the owner sets a still picture */
    ask_q(s, &owner, server_update(0, phash, b64, NULL), 1);
    assert(!strcmp(s->data.group[0].pic, phash) && !s->data.group[0].anim[0]);
    assert(audit_has(s, &owner, 0, AUD_ICON_PIC));
    ask(s, &member, "{\"op\":\"audit\",\"group\":0}", 0); /* the log is for managers */
    /* a broken picture is refused and changes nothing */
    b64b[10] = b64b[10] == 'A' ? 'B' : 'A';
    ask_q(s, &owner, server_update(0, p2, b64b, NULL), 0);
    assert(!strcmp(s->data.group[0].pic, phash));
    printf("PASS: server icons: only owner/\"Manage server\" (checked on the host, client claims ignored), "
           "broken pictures refused, change logged\n");

    /* 2. the moderator sets an animated icon: pieces first, then the update */
    int pieces = put_pieces(s, &mod, ahash, ablob, an, 1);
    assert(pieces >= 1);
    ask_q(s, &mod, server_update(0, phash, b64, ahash), 1);
    assert(!strcmp(s->data.group[0].anim, ahash) && file_exists(path, ahash, "ani"));
    assert(audit_has(s, &mod, 0, AUD_ICON_ANIM));
    /* someone else's upload cannot be used, and a name never uploaded keeps only the still picture */
    s->data.group[0].anim[0] = 0;
    char path2[300];
    snprintf(path2, sizeof(path2), "%s.pics/%s.ani", path, ahash);
    unlink(path2);
    put_pieces(s, &member, ahash, ablob, an, 1);
    ask_q(s, &mod, server_update(0, phash, b64, ahash), 1);
    assert(!s->data.group[0].anim[0]);
    /* pieces out of order are refused */
    {
        char *text = malloc(ANIM_PIECE / 3 * 4 + 8);
        assert(!avatar_b64_encode(text, ANIM_PIECE / 3 * 4 + 8, ablob, 100));
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "op", "blob_put");
        cJSON_AddStringToObject(p, "hash", ahash);
        cJSON_AddNumberToObject(p, "offset", 500);
        cJSON_AddNumberToObject(p, "total", (double)an);
        cJSON_AddStringToObject(p, "data", text);
        ask_q(s, &owner, p, 0);
        /* oversized total */
        p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "op", "blob_put");
        cJSON_AddStringToObject(p, "hash", ahash);
        cJSON_AddNumberToObject(p, "offset", 0);
        cJSON_AddNumberToObject(p, "total", (double)ANIM_BLOB_MAX + 1);
        cJSON_AddStringToObject(p, "data", text);
        ask_q(s, &owner, p, 0);
        free(text);
    }
    /* a damaged animation (right size, wrong content) is refused when it is used */
    uint8_t *bad = malloc(an);
    memcpy(bad, ablob, an);
    bad[an / 2] ^= 0x55;
    put_pieces(s, &owner, ahash, bad, an, 1);
    ask_q(s, &owner, server_update(0, phash, b64, ahash), 1);
    assert(!s->data.group[0].anim[0] && !file_exists(path, ahash, "ani"));
    free(bad);
    /* upload again properly */
    put_pieces(s, &owner, ahash, ablob, an, 1);
    ask_q(s, &owner, server_update(0, phash, b64, ahash), 1);
    assert(!strcmp(s->data.group[0].anim, ahash));
    /* probe: the host already has it */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "op", "blob_put");
        cJSON_AddStringToObject(p, "hash", ahash);
        cJSON_AddNumberToObject(p, "offset", 0);
        cJSON_AddNumberToObject(p, "total", (double)an);
        cJSON_AddBoolToObject(p, "probe", 1);
        cJSON *rr = request(s, &member, p, 1);
        assert((size_t)cJSON_GetObjectItem(rr, "have")->valuedouble == an);
        cJSON_Delete(rr);
    }
    /* 3. everybody gets it in pieces, byte for byte */
    {
        uint8_t *got = malloc(an);
        size_t have = 0;
        while (have < an) {
            char qq[160];
            snprintf(qq, sizeof(qq), "{\"op\":\"pic\",\"hash\":\"%s\",\"kind\":\"ani\",\"offset\":%zu}", ahash, have);
            cJSON *rr = request(s, &member, cJSON_Parse(qq), 1);
            size_t k = 0;
            assert((size_t)cJSON_GetObjectItem(rr, "total")->valuedouble == an);
            assert(!avatar_b64_decode(got + have, an - have, &k, orbit_string(rr, "data")) && k);
            have += k;
            cJSON_Delete(rr);
        }
        assert(!memcmp(got, ablob, an));
        free(got);
        char qq[160];
        snprintf(qq, sizeof(qq), "{\"op\":\"pic\",\"hash\":\"%032d\",\"kind\":\"ani\",\"offset\":0}", 0);
        ask(s, &member, qq, 0); /* names nobody here uses are not served */
    }
    /* the group list carries the names */
    r = request(s, &member, cJSON_Parse("{\"op\":\"groups\"}"), 1);
    char *text = cJSON_PrintUnformatted(r);
    assert(strstr(text, phash) && strstr(text, ahash));
    free(text);
    cJSON_Delete(r);
    printf("PASS: animated server icon: %d piece(s), only the uploader's own complete upload, out-of-order/oversized/"
           "damaged refused, probe, served in pieces byte for byte, listed with the group\n", pieces);

    /* 3b. a hardly compressible animation needs several pieces both ways */
    {
        size_t gn, nn;
        uint8_t *ng = load("noise.gif", &gn), *nblob;
        char nhash[33], nphash[33];
        OrbitImage nf;
        assert(anim_make_gif(ng, gn, &nblob, &nn, nhash, &nf) == IMG_OK);
        free(ng);
        char *nb64 = malloc(AVATAR_B64_MAX);
        still(&nf, nphash, nb64);
        int np = put_pieces(s, &owner, nhash, nblob, nn, 1);
        assert(np >= 3 && nn <= ANIM_BLOB_MAX);
        cJSON *pp = cJSON_CreateObject();
        cJSON_AddStringToObject(pp, "op", "profile");
        cJSON_AddStringToObject(pp, "pic", nphash);
        cJSON_AddStringToObject(pp, "pic_data", nb64);
        cJSON_AddStringToObject(pp, "anim", nhash);
        ask_q(s, &owner, pp, 1);
        assert(!strcmp(s->data.user[owner.user].anim, nhash));
        uint8_t *got = malloc(nn);
        size_t have = 0;
        int down = 0;
        while (have < nn) {
            char qq[160];
            snprintf(qq, sizeof(qq), "{\"op\":\"pic\",\"hash\":\"%s\",\"kind\":\"ani\",\"offset\":%zu}", nhash, have);
            cJSON *rr = request(s, &member, cJSON_Parse(qq), 1);
            size_t k = 0;
            assert(!avatar_b64_decode(got + have, nn - have, &k, orbit_string(rr, "data")) && k);
            have += k;
            down++;
            cJSON_Delete(rr);
        }
        assert(!memcmp(got, nblob, nn) && down == np);
        OrbitAnim af;
        assert(anim_decode(got, nn, nhash, &af) == IMG_OK && af.frames == 30);
        anim_free(&af);
        printf("PASS: chunked transfer: %zu-byte animation up in %d pieces, down in %d pieces, decodes (30 frames)\n",
               nn, np, down);
        free(got);
        free(nblob);
        free(nb64);
        image_free(&nf);
    }

    /* 4. animated profile picture */
    put_pieces(s, &member, ahash, ablob, an, 1);
    cJSON *pr = cJSON_CreateObject();
    cJSON_AddStringToObject(pr, "op", "profile");
    cJSON_AddStringToObject(pr, "pic", phash);
    cJSON_AddStringToObject(pr, "pic_data", b64);
    cJSON_AddStringToObject(pr, "anim", ahash);
    ask_q(s, &member, pr, 1);
    assert(!strcmp(s->data.user[member.user].anim, ahash));
    /* persistence */
    orbit_store_close(s);
    assert(!orbit_store_open(s, path));
    assert(!strcmp(s->data.group[0].pic, phash) && !strcmp(s->data.group[0].anim, ahash));
    assert(!strcmp(s->data.user[member.user].anim, ahash));
    assert(audit_has(s, &owner, 0, AUD_ICON_ANIM) && audit_has(s, &owner, 0, AUD_ICON_PIC));
    /* 5. reset to the default icon: logged; files stay while the profile still uses them */
    ask(s, &owner, "{\"op\":\"server_update\",\"group\":0,\"pic\":\"\",\"icon\":0}", 1);
    assert(!s->data.group[0].pic[0] && !s->data.group[0].anim[0]);
    assert(audit_has(s, &owner, 0, AUD_ICON_RESET));
    assert(file_exists(path, ahash, "ani") && file_exists(path, phash, "pic"));
    pr = cJSON_CreateObject();
    cJSON_AddStringToObject(pr, "op", "profile");
    cJSON_AddStringToObject(pr, "pic", "");
    ask_q(s, &member, pr, 1);
    assert(!s->data.user[member.user].anim[0]);
    assert(!file_exists(path, ahash, "ani") && !file_exists(path, phash, "pic")); /* nobody uses them */
    /* old clients (no "anim") keep working: a still picture only */
    pr = cJSON_CreateObject();
    cJSON_AddStringToObject(pr, "op", "profile");
    cJSON_AddStringToObject(pr, "pic", phash);
    cJSON_AddStringToObject(pr, "pic_data", b64);
    ask_q(s, &member, pr, 1);
    assert(!strcmp(s->data.user[member.user].pic, phash) && !s->data.user[member.user].anim[0]);
    printf("PASS: animated profile picture, persistence of icons/animations/audit log, reset logged, files "
           "removed when unused, old clients still set still pictures\n");

    orbit_store_close(s);
    free(s);
    free(ablob);
    free(b64);
    free(b64b);
    image_free(&first);
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s.bak %s.pics", path, path, path);
    assert(system(cmd) == 0);
    return 0;
}
