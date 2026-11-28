/* ORBIT 1.2 public community directory: shared format (see directory.h). */
#include "directory.h"
#include "utf8copy.h"
#include "monocypher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *dstr(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
void dir_host_keys(const uint8_t server_key[32], uint8_t secret[64], uint8_t pub[32]) {
    uint8_t seed[32];
    static const char ctx[] = "orbit-directory-1";
    crypto_blake2b_keyed(seed, 32, server_key, 32, (const uint8_t *)ctx, sizeof(ctx) - 1);
    crypto_eddsa_key_pair(secret, pub, seed); /* wipes seed */
}
static int get_int(const cJSON *x, const char *k, int lo, int hi, int *out) {
    const cJSON *v = cJSON_GetObjectItem(x, k);
    if (!cJSON_IsNumber(v) || v->valuedouble < lo || v->valuedouble > hi || v->valuedouble != (int)v->valuedouble)
        return -1;
    *out = (int)v->valuedouble;
    return 0;
}
static int get_text(const cJSON *x, const char *k, char *out, size_t cap, size_t min) {
    const cJSON *v = cJSON_GetObjectItem(x, k);
    const char *s = cJSON_IsString(v) ? v->valuestring : NULL;
    if (!s && !min)
        s = "";
    if (!s || !orbit_text_valid(s, min, cap - 1))
        return -1;
    orbit_copy(out, cap, s);
    return 0;
}
int dir_community_parse(const cJSON *x, DirCommunity *c, int allow_private) {
    memset(c, 0, sizeof(*c));
    if (!cJSON_IsObject(x) || get_int(x, "id", 0, ORBIT_GROUPS - 1, &c->id) ||
        get_int(x, "members", 0, ORBIT_USERS, &c->members) || get_int(x, "online", -1, ORBIT_USERS, &c->online) ||
        get_int(x, "icon", 0, 255, &c->icon) || get_int(x, "color", 0, 11, &c->color) ||
        get_text(x, "name", c->name, sizeof(c->name), 1) || get_text(x, "desc", c->desc, sizeof(c->desc), 0) ||
        get_text(x, "host", c->host, sizeof(c->host), 0) ||
        get_text(x, "invite", c->invite, sizeof(c->invite), 1))
        return -1;
    if (orbit_text_offensive(c->name) || orbit_text_offensive(c->desc) || orbit_text_offensive(c->host))
        return -1;
    uint32_t ip;
    int port;
    char code[9];
    if (orbit_invite_decode(c->invite, &ip, &port, code) || (!allow_private && orbit_ipv4_private(ip)))
        return -1;
    const cJSON *srv = cJSON_GetObjectItem(x, "server");
    if (cJSON_IsString(srv)) {
        uint8_t raw[8];
        if (strlen(srv->valuestring) != 16 || orbit_unhex(raw, srv->valuestring, 8))
            return -1;
        orbit_copy(c->server, sizeof(c->server), srv->valuestring);
    }
    const cJSON *seen = cJSON_GetObjectItem(x, "seen");
    if (cJSON_IsNumber(seen) && seen->valuedouble > 0 && seen->valuedouble < 4294967295.0)
        c->seen = (uint32_t)seen->valuedouble;
    int plat; /* 1.3, optional: anything else than 1 or 2 just means "unknown" */
    if (!get_int(x, "plat", 1, 2, &plat))
        c->plat = plat;
    return 0;
}
cJSON *dir_community_json(const DirCommunity *c) {
    cJSON *x = cJSON_CreateObject();
    if (c->server[0])
        cJSON_AddStringToObject(x, "server", c->server);
    cJSON_AddNumberToObject(x, "id", c->id);
    cJSON_AddStringToObject(x, "name", c->name);
    cJSON_AddStringToObject(x, "desc", c->desc);
    cJSON_AddStringToObject(x, "host", c->host);
    cJSON_AddNumberToObject(x, "members", c->members);
    cJSON_AddNumberToObject(x, "online", c->online);
    cJSON_AddNumberToObject(x, "icon", c->icon);
    cJSON_AddNumberToObject(x, "color", c->color);
    cJSON_AddStringToObject(x, "invite", c->invite);
    if (c->seen)
        cJSON_AddNumberToObject(x, "seen", (double)c->seen);
    if (c->plat)
        cJSON_AddNumberToObject(x, "plat", c->plat);
    return x;
}
char *dir_seal(const cJSON *payload, const uint8_t secret[64], const uint8_t pub[32], int with_pub) {
    char *text = cJSON_PrintUnformatted(payload);
    if (!text)
        return NULL;
    uint8_t sig[64];
    crypto_eddsa_sign(sig, secret, (const uint8_t *)text, strlen(text));
    char sh[129], ph[65];
    orbit_hex(sh, sig, 64);
    orbit_hex(ph, pub, 32);
    cJSON *env = cJSON_CreateObject();
    if (with_pub)
        cJSON_AddStringToObject(env, "pub", ph);
    cJSON_AddStringToObject(env, "payload", text);
    cJSON_AddStringToObject(env, "sig", sh);
    free(text);
    char *out = cJSON_PrintUnformatted(env);
    cJSON_Delete(env);
    return out;
}
cJSON *dir_open(const char *text, size_t n, const uint8_t *pub, uint8_t pub_out[32]) {
    cJSON *env = cJSON_ParseWithLength(text, n);
    const char *payload = dstr(env, "payload"), *sig = dstr(env, "sig");
    uint8_t s[64], p[32];
    cJSON *out = NULL;
    if (!env || !payload[0] || strlen(sig) != 128 || orbit_unhex(s, sig, 64))
        goto done;
    if (pub)
        memcpy(p, pub, 32);
    else if (strlen(dstr(env, "pub")) != 64 || orbit_unhex(p, dstr(env, "pub"), 32))
        goto done;
    if (crypto_eddsa_check(s, p, (const uint8_t *)payload, strlen(payload)))
        goto done;
    out = cJSON_Parse(payload);
    if (out && pub_out)
        memcpy(pub_out, p, 32);
done:
    cJSON_Delete(env);
    return out;
}
int dir_config_load(const char *path, DirConfig *c) {
    memset(c, 0, sizeof(*c));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[300];
    int have_url = 0, have_pub = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "url ", 4) && (!strncmp(line + 4, "http://", 7) || !strncmp(line + 4, "https://", 8)) &&
            strlen(line + 4) < sizeof(c->url)) {
            orbit_copy(c->url, sizeof(c->url), line + 4);
            size_t l = strlen(c->url);
            while (l && c->url[l - 1] == '/')
                c->url[--l] = 0;
            have_url = 1;
        } else if (!strncmp(line, "pub ", 4) && strlen(line + 4) == 64 && !orbit_unhex(c->pub, line + 4, 32))
            have_pub = 1;
    }
    fclose(f);
    return have_url && have_pub ? 0 : -1;
}
