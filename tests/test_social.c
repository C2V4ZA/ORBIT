/* Orbit 1.0 host logic: migration from 0.9.5, protocol versions, identity binding, friends,
 * private chats, servers with channels, roles and permissions, message features, presence. */
#include "i18n.h"
#include "monocypher.h"
#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static cJSON *ask(OrbitStore *s, OrbitSession *se, const char *json) {
    cJSON *q = cJSON_Parse(json);
    cJSON *r = orbit_request(s, se, q);
    cJSON_Delete(q);
    return r;
}
static int ok(cJSON *r) {
    int v = cJSON_IsTrue(cJSON_GetObjectItem(r, "ok"));
    return v;
}
static int okd(OrbitStore *s, OrbitSession *se, const char *json) {
    cJSON *r = ask(s, se, json);
    int v = ok(r);
    if (!v)
        printf("  (refused: %s -> %s)\n", json, orbit_string(r, "ekey"));
    cJSON_Delete(r);
    return v;
}
static const char *ekey(OrbitStore *s, OrbitSession *se, const char *json) {
    static char k[64];
    cJSON *r = ask(s, se, json);
    snprintf(k, sizeof(k), "%s", ok(r) ? "OK" : orbit_string(r, "ekey"));
    cJSON_Delete(r);
    return k;
}
static int num(cJSON *r, const char *key) {
    cJSON *v = cJSON_GetObjectItem(r, key);
    return cJSON_IsNumber(v) ? v->valueint : -1;
}
/* signs in like Orbit 1.0: device token + signed public key */
static int sign_in(OrbitStore *s, OrbitSession *se, const char *name, uint8_t device[32], uint8_t pub[32]) {
    uint8_t secret[64], sig[64];
    orbit_identity(device, secret, pub);
    orbit_bind_sign(sig, secret, pub, s->server_id);
    char token[65], p[65], g[129], json[600];
    uint8_t t[32];
    crypto_blake2b_keyed(t, 32, device, 32, (const uint8_t *)s->server_id, 16);
    orbit_hex(token, t, 32);
    orbit_hex(p, pub, 32);
    orbit_hex(g, sig, 64);
    snprintf(json, sizeof(json), "{\"op\":\"device\",\"token\":\"%s\",\"name\":\"%s\",\"pub\":\"%s\",\"sig\":\"%s\",\"status\":0}",
             token, name, p, g);
    okd(s, se, "{\"op\":\"hello\",\"protocol\":10}");
    cJSON *r = ask(s, se, json);
    int uid = ok(r) ? num(r, "user") : -1;
    cJSON_Delete(r);
    return uid;
}
static cJSON *find_group(cJSON *groups, int id) {
    cJSON *g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItem(groups, "groups")) if (num(g, "id") == id) return g;
    return NULL;
}
int main(void) {
    i18n_set(LANG_DE);
    const char *path = "/tmp/orbit-social.json";
    remove(path);
    char bak[64];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    remove(bak);
    /* ---------- 1. a 0.9.5 data file becomes servers, nothing is lost ---------- */
    FILE *f = fopen(path, "w");
    fprintf(f, "{\"version\":1,\"next_id\":4,\"users\":[{\"email\":\"ps3-aaaaaaaaaaaa@orbit.local\",\"name\":\"Max\","
               "\"salt\":\"%032d\",\"hash\":\"%064d\",\"devices\":[\"%064d\",\"%064d\",\"%064d\",\"%064d\"]}],"
               "\"groups\":[{\"id\":0,\"owner\":0,\"members\":1,\"pending\":0,\"name\":\"Zocker\",\"invite\":\"ABCDEFGH\"}],"
               "\"messages\":[{\"id\":1,\"group\":0,\"user\":0,\"time\":100,\"text\":\"hallo alt\"},"
               "{\"id\":3,\"group\":0,\"user\":0,\"time\":200,\"text\":\"zweite\"}],\"pending_emails\":[]}",
            0, 0, 0, 0, 0, 0);
    fclose(f);
    OrbitStore *s = calloc(1, sizeof(*s));
    CHECK(orbit_store_open_lang(s, path, LANG_DE) == 0);
    memcpy(s->server_id, "00112233445566aa", 17);
    CHECK(s->data.group[0].used && s->data.group[0].kind == GK_SERVER);
    CHECK(s->data.group[0].ch[0].used && s->data.group[0].ch[0].type == CH_TEXT &&
          !strcmp(s->data.group[0].ch[0].name, "allgemein"));
    CHECK(s->data.group[0].ch[1].used && s->data.group[0].ch[1].type == CH_VOICE);
    CHECK(s->data.message[1].id == 1 && s->data.message[1].channel == 0 && !strcmp(s->data.message[1].text, "hallo alt"));
    CHECK(s->data.message[3].id == 3);
    CHECK(s->data.group[0].role[3].used && !strcmp(s->data.group[0].role[3].name, "Member"));
    printf("PASS: 0.9.5 data converted: group -> server with text + voice channel, messages kept in the text channel, default roles\n");

    /* ---------- 2. identities ---------- */
    uint8_t dev_owner[32], dev_b[32], dev_c[32], pub_o[32], pub_b[32], pub_c[32];
    memset(dev_owner, 1, 32);
    memset(dev_b, 2, 32);
    memset(dev_c, 3, 32);
    OrbitSession owner = {.user = -1, .local = 1}, b = {.user = -1}, c = {.user = -1};
    int uo = sign_in(s, &owner, "Ben", dev_owner, pub_o);
    int ub = sign_in(s, &b, "Lea", dev_b, pub_b);
    int uc = sign_in(s, &c, "Tom", dev_c, pub_c);
    CHECK(uo >= 0 && ub >= 0 && uc >= 0 && uo != ub && ub != uc);
    CHECK(s->data.owner_user == uo);
    CHECK(s->data.user[ub].has_pub && !memcmp(s->data.user[ub].pub, pub_b, 32));
    char ida[ORBIT_ID_TEXT], idb[ORBIT_ID_TEXT];
    orbit_id_text(ida, pub_b);
    orbit_id_text(idb, pub_b);
    CHECK(strlen(ida) == 14 && !strcmp(ida, idb) && ida[4] == '-' && ida[9] == '-');
    /* a forged signature is refused (identity theft) */
    {
        uint8_t secret[64], sig[64], other[32];
        orbit_identity(dev_b, secret, other);
        orbit_bind_sign(sig, secret, other, "ffffffffffffffff"); /* signed for another PS3 */
        char p[65], g[129], json[600], token[65];
        orbit_hex(p, other, 32);
        orbit_hex(g, sig, 64);
        memset(token, 'a', 64);
        token[64] = 0;
        snprintf(json, sizeof(json), "{\"op\":\"device\",\"token\":\"%s\",\"name\":\"X\",\"pub\":\"%s\",\"sig\":\"%s\"}",
                 token, p, g);
        OrbitSession x = {.user = -1};
        CHECK(!strcmp(ekey(s, &x, json), "E_BIND"));
    }
    /* the name may change, the identity stays */
    {
        uint8_t p2[32];
        OrbitSession b2 = {.user = -1};
        CHECK(sign_in(s, &b2, "Lea2", dev_b, p2) == ub && !memcmp(p2, pub_b, 32));
        orbit_session_end(s, &b2);
    }
    printf("PASS: Ed25519 identity bound per meeting point, Orbit ID stable across renames, forged/replayed signature rejected\n");

    /* ---------- 3. protocol 9 view stays compatible ---------- */
    {
        OrbitSession old = {.user = -1};
        okd(s, &old, "{\"op\":\"hello\"}");
        CHECK(old.proto == 9);
        old.user = ub;
        CHECK(okd(s, &b, "{\"op\":\"join\",\"invite\":\"ABCDEFGH\"}"));
        cJSON *r = ask(s, &old, "{\"op\":\"groups\"}");
        cJSON *g = find_group(r, 0);
        CHECK(g && !cJSON_GetObjectItem(g, "chs") && num(g, "last") == 3);
        cJSON_Delete(r);
        CHECK(okd(s, &old, "{\"op\":\"send\",\"group\":0,\"text\":\"von 0.9\"}"));
        r = ask(s, &old, "{\"op\":\"history\",\"group\":0,\"after\":0}");
        CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(r, "messages")) == 3);
        cJSON_Delete(r);
        usleep(320000);
    }
    printf("PASS: protocol 9 (Orbit 0.9.x) clients: same group list, send/history map to the first text channel\n");

    /* ---------- 4. friends ---------- */
    CHECK(!strcmp(ekey(s, &b, "{\"op\":\"friends\"}"), "E_LOCAL_ONLY")); /* only the owner sees the list */
    CHECK(okd(s, &b, "{\"op\":\"friend\",\"code\":\"AAAAA-BBBBB-CCCCC-DDDDD\"}"));
    CHECK(okd(s, &c, "{\"op\":\"friend\",\"code\":\"EEEEE-FFFFF-GGGGG-HHHHH\"}"));
    CHECK(!strcmp(ekey(s, &owner, "{\"op\":\"friend\",\"code\":\"x\"}"), "E_SELF"));
    cJSON *fr = ask(s, &owner, "{\"op\":\"friends\"}");
    int fb = -1, fc = -1;
    cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(fr, "friends")) {
        if (num(x, "user") == ub)
            fb = num(x, "i");
        if (num(x, "user") == uc)
            fc = num(x, "i");
        CHECK(num(x, "state") == FR_INCOMING);
    }
    cJSON_Delete(fr);
    CHECK(fb >= 0 && fc >= 0);
    /* not friends yet: no private chat */
    char q[300];
    snprintf(q, sizeof(q), "{\"op\":\"dm_create\",\"users\":[%d]}", ub);
    CHECK(!strcmp(ekey(s, &owner, q), "E_NOT_FRIEND"));
    snprintf(q, sizeof(q), "{\"op\":\"friend_respond\",\"i\":%d,\"accept\":true}", fb);
    CHECK(okd(s, &owner, q));
    cJSON *st = ask(s, &b, "{\"op\":\"friend\",\"code\":\"AAAAA-BBBBB-CCCCC-DDDDD\"}");
    CHECK(!strcmp(orbit_string(st, "state"), "friends"));
    cJSON_Delete(st);
    /* outgoing request recorded by the owner, then accepted by the other side */
    {
        uint8_t devx[32], pubx[32], secx[64];
        memset(devx, 9, 32);
        orbit_identity(devx, secx, pubx);
        char p[65];
        orbit_hex(p, pubx, 32);
        snprintf(q, sizeof(q), "{\"op\":\"friend_out\",\"pub\":\"%s\",\"name\":\"Kim\",\"code\":\"KKKKK-KKKKK-KKKKK-KKKKK\"}", p);
        cJSON *r = ask(s, &owner, q);
        CHECK(ok(r) && num(r, "state") == FR_OUTGOING);
        cJSON_Delete(r);
        uint8_t pk[32];
        OrbitSession k = {.user = -1};
        int uk = sign_in(s, &k, "Kim", devx, pk);
        CHECK(uk >= 0);
        st = ask(s, &k, "{\"op\":\"friend\",\"code\":\"KKKKK-KKKKK-KKKKK-KKKKK\"}");
        CHECK(!strcmp(orbit_string(st, "state"), "friends")); /* mutual: no second confirmation */
        cJSON_Delete(st);
        orbit_session_end(s, &k);
    }
    printf("PASS: friend requests (incoming/outgoing), accept, mutual request becomes friendship, list only for the owner\n");

    /* ---------- 5. private chats ---------- */
    snprintf(q, sizeof(q), "{\"op\":\"dm_create\",\"users\":[%d]}", ub);
    cJSON *dm = ask(s, &owner, q);
    int gdm = num(dm, "group");
    cJSON_Delete(dm);
    CHECK(gdm > 0 && s->data.group[gdm].kind == GK_DM);
    dm = ask(s, &owner, q); /* the same chat again, not a second one */
    CHECK(num(dm, "group") == gdm);
    cJSON_Delete(dm);
    CHECK(!strcmp(ekey(s, &b, "{\"op\":\"dm_create\",\"users\":[0]}"), "E_LOCAL_ONLY"));
    cJSON *gl = ask(s, &b, "{\"op\":\"groups\"}");
    cJSON *gd = find_group(gl, gdm);
    CHECK(gd && num(gd, "kind") == GK_DM && cJSON_GetArraySize(cJSON_GetObjectItem(gd, "dm")) == 1);
    cJSON_Delete(gl);
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":0,\"text\":\"Yo bro wanna play some GTA?\"}", gdm);
    CHECK(okd(s, &b, q));
    /* C is not in the chat */
    snprintf(q, sizeof(q), "{\"op\":\"history\",\"group\":%d,\"channel\":0,\"after\":0}", gdm);
    CHECK(!strcmp(ekey(s, &c, q), "E_NO_ACCESS"));
    cJSON *h = ask(s, &owner, q);
    cJSON *m0 = cJSON_GetArrayItem(cJSON_GetObjectItem(h, "messages"), 0);
    CHECK(m0 && !strcmp(orbit_string(m0, "text"), "Yo bro wanna play some GTA?")); /* never translated */
    cJSON_Delete(h);
    /* blocking: removed from private chats, cannot write there */
    snprintf(q, sizeof(q), "{\"op\":\"friend_block\",\"i\":%d}", fb);
    CHECK(okd(s, &owner, q));
    CHECK(!(s->data.group[gdm].members >> ub & 1));
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":0,\"text\":\"hi\"}", gdm);
    usleep(320000);
    CHECK(!strcmp(ekey(s, &b, q), "E_NO_ACCESS"));
    st = ask(s, &b, "{\"op\":\"friend\",\"code\":\"AAAAA-BBBBB-CCCCC-DDDDD\"}");
    CHECK(!strcmp(orbit_string(st, "state"), "pending")); /* a blocked person learns nothing */
    cJSON_Delete(st);
    snprintf(q, sizeof(q), "{\"op\":\"friend_unblock\",\"i\":%d}", fb);
    CHECK(okd(s, &owner, q));
    printf("PASS: private chats only with friends, no duplicates, members only, user text untouched; blocking enforced on the host\n");

    /* ---------- 6. servers: template, channels, categories ---------- */
    cJSON *cr = ask(s, &owner, "{\"op\":\"create\",\"name\":\"ORBIT GAMING\",\"categories\":[\"INFORMATION\",\"COMMUNITY\",\"VOICE CHANNELS\"],"
                               "\"channels\":[{\"name\":\"welcome\",\"type\":0,\"cat\":0},{\"name\":\"rules\",\"type\":0,\"cat\":0},"
                               "{\"name\":\"general\",\"type\":0,\"cat\":1},{\"name\":\"General Voice\",\"type\":1,\"cat\":2},"
                               "{\"name\":\"Private Lounge\",\"type\":1,\"cat\":2}]}");
    int gs = num(cr, "group");
    cJSON_Delete(cr);
    CHECK(gs >= 0);
    OrbitGroup *G = &s->data.group[gs];
    CHECK(G->cat[2].used && !strcmp(G->ch[3].name, "General Voice") && G->ch[3].type == CH_VOICE && G->ch[3].category == 2);
    cJSON *inv = ask(s, &owner, "{\"op\":\"invite\",\"group\":1}");
    cJSON_Delete(inv);
    char join[120];
    snprintf(join, sizeof(join), "{\"op\":\"join\",\"invite\":\"%s\"}", G->invite);
    CHECK(okd(s, &b, join));
    CHECK(okd(s, &c, join));
    snprintf(q, sizeof(q), "{\"op\":\"channel_add\",\"group\":%d,\"name\":\"ps3-games\",\"cat\":1}", gs);
    CHECK(!strcmp(ekey(s, &b, q), "E_NO_PERMISSION")); /* members cannot manage channels */
    cJSON *ca = ask(s, &owner, q);
    int chg = num(ca, "ch");
    cJSON_Delete(ca);
    CHECK(chg > 0 && G->ch[chg].category == 1);
    snprintf(q, sizeof(q), "{\"op\":\"channel_update\",\"group\":%d,\"ch\":%d,\"name\":\"ps3-spiele\",\"topic\":\"Alles über PS3\",\"move\":-1}", gs, chg);
    CHECK(okd(s, &owner, q));
    CHECK(!strcmp(G->ch[chg].name, "ps3-spiele") && G->ch[chg].pos < G->ch[2].pos);
    snprintf(q, sizeof(q), "{\"op\":\"category_update\",\"group\":%d,\"cat\":2,\"move\":-1}", gs);
    CHECK(okd(s, &owner, q));
    CHECK(G->cat[2].pos < G->cat[1].pos);
    snprintf(q, sizeof(q), "{\"op\":\"server_update\",\"group\":%d,\"desc\":\"Die Community\",\"welcome\":\"Willkommen!\",\"color\":4,\"icon\":2,\"banner\":3,\"open\":true}", gs);
    CHECK(okd(s, &owner, q));
    CHECK(G->open && G->color == 4 && !strcmp(G->welcome, "Willkommen!"));
    printf("PASS: community server with categories, text/voice channels, create/rename/describe/move, server design fields\n");

    /* ---------- 7. roles and permissions, all checked on the host ---------- */
    snprintf(q, sizeof(q), "{\"op\":\"member_roles\",\"group\":%d,\"user\":%d,\"roles\":%d}", gs, ub, (1 << 2) | (1 << 3));
    CHECK(okd(s, &owner, q)); /* Lea becomes moderator */
    snprintf(q, sizeof(q), "{\"op\":\"member_roles\",\"group\":%d,\"user\":%d,\"roles\":%d}", gs, uc, (1 << 1) | (1 << 3));
    CHECK(!strcmp(ekey(s, &b, q), "E_NO_PERMISSION")); /* moderators cannot hand out roles */
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":2,\"text\":\"Hallo @Lea\"}", gs);
    usleep(320000);
    cJSON *sm = ask(s, &c, q);
    int mid = num(sm, "id");
    cJSON_Delete(sm);
    CHECK(mid > 0);
    snprintf(q, sizeof(q), "{\"op\":\"remove_msg\",\"group\":%d,\"id\":%d}", gs, mid);
    CHECK(okd(s, &b, q)); /* moderator deletes someone else's message */
    CHECK(s->data.message[mid % ORBIT_MESSAGES].flags & MF_DELETED);
    snprintf(q, sizeof(q), "{\"op\":\"kick\",\"group\":%d,\"user\":%d}", gs, uo);
    CHECK(!strcmp(ekey(s, &b, q), "E_ROLE_RANK")); /* nobody touches the owner */
    snprintf(q, sizeof(q), "{\"op\":\"role_add\",\"group\":%d,\"name\":\"VIP\",\"perms\":%d,\"prio\":20,\"color\":16711680}", gs, P_SEND | P_INVITE);
    cJSON *ra = ask(s, &owner, q);
    int vip = num(ra, "role");
    cJSON_Delete(ra);
    CHECK(vip >= 4);
    snprintf(q, sizeof(q), "{\"op\":\"role_update\",\"group\":%d,\"role\":%d,\"perms\":%d}", gs, vip, P_ALL);
    CHECK(okd(s, &owner, q));
    snprintf(q, sizeof(q), "{\"op\":\"role_add\",\"group\":%d,\"name\":\"Boss\",\"prio\":99,\"perms\":%d}", gs, P_ALL);
    CHECK(!strcmp(ekey(s, &b, q), "E_NO_PERMISSION"));
    /* private channel: only VIP sees it */
    snprintf(q, sizeof(q), "{\"op\":\"channel_update\",\"group\":%d,\"ch\":1,\"view\":%d,\"send\":%d}", gs, 1 << vip, 1 << vip);
    CHECK(okd(s, &owner, q));
    snprintf(q, sizeof(q), "{\"op\":\"history\",\"group\":%d,\"channel\":1,\"after\":0}", gs);
    CHECK(!strcmp(ekey(s, &c, q), "E_NO_CHANNEL"));
    gl = ask(s, &c, "{\"op\":\"groups\"}");
    gd = find_group(gl, gs);
    int sees_private = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(gd, "chs")) if (num(x, "i") == 1) sees_private = 1;
    CHECK(gd && !sees_private);
    cJSON_Delete(gl);
    /* server mute, ban */
    snprintf(q, sizeof(q), "{\"op\":\"server_mute\",\"group\":%d,\"user\":%d,\"mute\":true}", gs, uc);
    CHECK(okd(s, &b, q));
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":2,\"text\":\"psst\"}", gs);
    usleep(320000);
    CHECK(!strcmp(ekey(s, &c, q), "E_NO_PERMISSION"));
    int muted = 0;
    CHECK(orbit_voice_allowed(s, gs, 3, uc, &muted) == 3 && muted == 1);
    CHECK(orbit_voice_allowed(s, gs, 2, uc, &muted) < 0); /* a text channel is no voice channel */
    snprintf(q, sizeof(q), "{\"op\":\"ban\",\"group\":%d,\"user\":%d}", gs, uc);
    CHECK(okd(s, &owner, q));
    CHECK(!strcmp(ekey(s, &c, join), "E_BANNED"));
    snprintf(q, sizeof(q), "{\"op\":\"join_public\",\"group\":%d}", gs);
    CHECK(!strcmp(ekey(s, &c, q), "E_BANNED"));
    snprintf(q, sizeof(q), "{\"op\":\"unban\",\"group\":%d,\"user\":%d}", gs, uc);
    CHECK(okd(s, &owner, q));
    cJSON *br = ask(s, &c, "{\"op\":\"browse\"}");
    int listed = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(br, "servers")) if (num(x, "id") == gs) listed = 1;
    CHECK(listed);
    cJSON_Delete(br);
    snprintf(q, sizeof(q), "{\"op\":\"join_public\",\"group\":%d}", gs);
    CHECK(okd(s, &c, q));
    printf("PASS: roles Owner/Admin/Moderator/Member + custom roles, rank and right checks, private channels, server mute, ban/unban, public join\n");

    /* ---------- 8. message features ---------- */
    usleep(320000);
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":2,\"text\":\"erste Nachricht\"}", gs);
    sm = ask(s, &owner, q);
    int m1 = num(sm, "id");
    cJSON_Delete(sm);
    usleep(320000);
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":2,\"text\":\"Antwort\",\"reply\":%d}", gs, m1);
    sm = ask(s, &b, q);
    int m2 = num(sm, "id");
    cJSON_Delete(sm);
    CHECK(m1 > 0 && m2 > m1);
    snprintf(q, sizeof(q), "{\"op\":\"history\",\"group\":%d,\"channel\":2,\"after\":0}", gs);
    h = ask(s, &c, q);
    uint32_t rev = (uint32_t)cJSON_GetObjectItem(h, "mrev")->valuedouble;
    cJSON *last = NULL;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(h, "messages")) last = x;
    CHECK(last && num(last, "reply") == m1 && !strcmp(orbit_string(last, "rtext"), "erste Nachricht"));
    cJSON_Delete(h);
    snprintf(q, sizeof(q), "{\"op\":\"edit\",\"group\":%d,\"id\":%d,\"text\":\"geändert\"}", gs, m1);
    CHECK(!strcmp(ekey(s, &b, q), "E_NO_PERMISSION")); /* only your own messages */
    CHECK(okd(s, &owner, q));
    snprintf(q, sizeof(q), "{\"op\":\"react\",\"group\":%d,\"id\":%d,\"r\":0}", gs, m1);
    CHECK(okd(s, &b, q));
    CHECK(okd(s, &c, q));
    snprintf(q, sizeof(q), "{\"op\":\"history\",\"group\":%d,\"channel\":2,\"after\":%d,\"since\":%u}", gs, m2, rev);
    h = ask(s, &c, q);
    cJSON *chg1 = cJSON_GetArrayItem(cJSON_GetObjectItem(h, "changed"), 0);
    CHECK(chg1 && num(chg1, "id") == m1 && (num(chg1, "flags") & MF_EDITED) &&
          !strcmp(orbit_string(chg1, "text"), "geändert") &&
          __builtin_popcount((unsigned)cJSON_GetArrayItem(cJSON_GetObjectItem(chg1, "react"), 0)->valueint) == 2);
    cJSON_Delete(h);
    snprintf(q, sizeof(q), "{\"op\":\"search\",\"group\":%d,\"q\":\"ANTWORT\"}", gs);
    h = ask(s, &c, q);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(h, "messages")) == 1);
    cJSON_Delete(h);
    gl = ask(s, &c, "{\"op\":\"groups\"}");
    gd = find_group(gl, gs);
    int preview = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(gd, "chs")) if (num(x, "i") == 2 && !strcmp(orbit_string(x, "ltext"), "Antwort")) preview = 1;
    CHECK(preview);
    cJSON_Delete(gl);
    printf("PASS: replies with quote, edit own only, delete, reactions, change sync via revision, search, last-message preview\n");

    /* ---------- 9. presence and profile ---------- */
    CHECK(okd(s, &b, "{\"op\":\"profile\",\"status\":2,\"custom\":\"Am Zocken\",\"bio\":\"Hi!\",\"game\":\"Gran Turismo 5\"}"));
    snprintf(q, sizeof(q), "{\"op\":\"members\",\"group\":%d}", gs);
    cJSON *mb = ask(s, &c, q);
    int seen_busy = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(mb, "members")) if (num(x, "user") == ub && num(x, "status") == ST_BUSY &&
                                                                 !strcmp(orbit_string(x, "game"), "Gran Turismo 5")) seen_busy = 1;
    CHECK(seen_busy);
    cJSON_Delete(mb);
    CHECK(okd(s, &b, "{\"op\":\"profile\",\"status\":3}")); /* invisible */
    mb = ask(s, &c, q);
    int hidden = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(mb, "members")) if (num(x, "user") == ub && num(x, "status") == ST_OFFLINE) hidden = 1;
    CHECK(hidden);
    cJSON_Delete(mb);
    orbit_session_end(s, &c);
    CHECK(s->online[uc] == 0);
    printf("PASS: profile (status, custom status, bio, favourite game), invisible shown as offline, presence counter\n");

    /* ---------- 10. everything persists ---------- */
    orbit_store_close(s);
    memset(s, 0, sizeof(*s));
    CHECK(orbit_store_open_lang(s, path, LANG_EN) == 0);
    G = &s->data.group[gs];
    CHECK(G->used && G->open && !strcmp(G->ch[chg].name, "ps3-spiele") && G->role[vip].used && G->ch[1].view == (1 << vip));
    CHECK(s->data.group[gdm].kind == GK_DM && s->data.owner_user == uo);
    CHECK(!strcmp(s->data.group[0].ch[0].name, "allgemein")); /* converted names stay, even in English */
    int friends = 0;
    for (int i = 0; i < ORBIT_FRIENDS; i++)
        friends += s->data.friend_[i].used;
    CHECK(friends == 2); /* unblocking removes the entry: the friendship must be asked for again */
    CHECK(!strcmp(s->data.user[ub].game, "Gran Turismo 5"));
    CHECK(s->data.message[m1 % ORBIT_MESSAGES].react[0] != 0);
    printf("PASS: servers, channels, roles, private chats, friends, profiles and reactions survive a restart\n");
    orbit_store_close(s);
    free(s);
    if (failures)
        printf("%d check(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
