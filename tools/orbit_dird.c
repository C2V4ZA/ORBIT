/* orbit-dird: the ORBIT 1.2 public community directory (see include/directory.h).
 * Small, single-threaded HTTP/1.0 server for a Linux/Windows-WSL machine. Local use and tests only
 * until the project owner decides where (and whether) to run it publicly; put it behind a TLS
 * reverse proxy there. Nothing here contacts the internet.
 *
 *   orbit-dird --port 8470 --state dird.json --key dird.key [--allow-private]
 *   orbit-dird --state dird.json --unhide <server>:<id>     (operator: undo a report-hide)
 *   orbit-dird --key dird.key --print-pub                   (for the app's directory.cfg)
 * The key file holds a 32-byte seed (created on first start, mode 600). Keep it out of the repo. */
#include "directory.h"
#include "monocypher.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_SERVERS 1000
#define MAX_HIDDEN 2000
#define MAX_REPORTERS 8
#define RATE_PER_MIN 60
#define ANNOUNCE_GAP 20

typedef struct {
    char server[17];
    uint8_t pub[32];
    uint32_t last_time, last_seen;
    int count;
    DirCommunity c[DIR_PER_SERVER];
} Host;
typedef struct {
    char key[24]; /* server:id */
    int hidden, reports;
    uint8_t who[MAX_REPORTERS][8]; /* hashed reporter addresses */
} Flag;
static Host hosts[MAX_SERVERS];
static int host_count;
static Flag flags[MAX_HIDDEN];
static int flag_count;
static uint8_t dir_secret[64], dir_pub[32];
static const char *state_path = "dird.json";
static int allow_private;
static struct {
    uint8_t who[8];
    uint32_t minute;
    int n;
} rate[256];

static uint32_t now_s(void) {
    return (uint32_t)time(NULL);
}
/* ---------- state ---------- */
static void save_state(void) {
    cJSON *root = cJSON_CreateObject(), *hs = cJSON_AddArrayToObject(root, "hosts"),
          *fs = cJSON_AddArrayToObject(root, "flags");
    for (int i = 0; i < host_count; i++) {
        cJSON *h = cJSON_CreateObject();
        char pub[65];
        orbit_hex(pub, hosts[i].pub, 32);
        cJSON_AddStringToObject(h, "server", hosts[i].server);
        cJSON_AddStringToObject(h, "pub", pub);
        cJSON_AddNumberToObject(h, "last_time", hosts[i].last_time);
        cJSON_AddNumberToObject(h, "last_seen", hosts[i].last_seen);
        cJSON *cs = cJSON_AddArrayToObject(h, "communities");
        for (int k = 0; k < hosts[i].count; k++)
            cJSON_AddItemToArray(cs, dir_community_json(&hosts[i].c[k]));
        cJSON_AddItemToArray(hs, h);
    }
    for (int i = 0; i < flag_count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "key", flags[i].key);
        cJSON_AddBoolToObject(f, "hidden", flags[i].hidden);
        cJSON_AddNumberToObject(f, "reports", flags[i].reports);
        char who[MAX_REPORTERS * 16 + 1];
        orbit_hex(who, flags[i].who[0], (size_t)8 * MAX_REPORTERS);
        cJSON_AddStringToObject(f, "who", who);
        cJSON_AddItemToArray(fs, f);
    }
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", state_path);
    FILE *f = fopen(tmp, "w");
    if (f && text) {
        fputs(text, f);
        if (!fclose(f))
            rename(tmp, state_path);
    } else if (f)
        fclose(f);
    free(text);
}
static void load_state(void) {
    FILE *f = fopen(state_path, "rb");
    if (!f)
        return;
    static char buf[4 << 20];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    cJSON *root = cJSON_Parse(buf);
    const cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "hosts")) {
        if (host_count >= MAX_SERVERS)
            break;
        Host *h = &hosts[host_count];
        memset(h, 0, sizeof(*h));
        const cJSON *s = cJSON_GetObjectItem(x, "server"), *p = cJSON_GetObjectItem(x, "pub");
        if (!cJSON_IsString(s) || strlen(s->valuestring) != 16 || !cJSON_IsString(p) ||
            orbit_unhex(h->pub, p->valuestring, 32))
            continue;
        snprintf(h->server, sizeof(h->server), "%.16s", s->valuestring);
        h->last_time = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(x, "last_time"));
        h->last_seen = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(x, "last_seen"));
        const cJSON *c;
        cJSON_ArrayForEach(c, cJSON_GetObjectItem(x, "communities")) {
            if (h->count < DIR_PER_SERVER && !dir_community_parse(c, &h->c[h->count], 1))
                h->count++;
        }
        host_count++;
    }
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(root, "flags")) {
        if (flag_count >= MAX_HIDDEN)
            break;
        Flag *g = &flags[flag_count];
        memset(g, 0, sizeof(*g));
        const cJSON *k = cJSON_GetObjectItem(x, "key"), *w = cJSON_GetObjectItem(x, "who");
        if (!cJSON_IsString(k) || strlen(k->valuestring) >= sizeof(g->key))
            continue;
        snprintf(g->key, sizeof(g->key), "%.23s", k->valuestring);
        g->hidden = cJSON_IsTrue(cJSON_GetObjectItem(x, "hidden"));
        g->reports = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(x, "reports"));
        if (cJSON_IsString(w))
            orbit_unhex(g->who[0], w->valuestring, (size_t)8 * MAX_REPORTERS);
        flag_count++;
    }
    cJSON_Delete(root);
}
static Flag *flag_of(const char *server, int id, int create) {
    char key[24];
    snprintf(key, sizeof(key), "%.16s:%d", server, id);
    for (int i = 0; i < flag_count; i++)
        if (!strcmp(flags[i].key, key))
            return &flags[i];
    if (!create || flag_count >= MAX_HIDDEN)
        return NULL;
    Flag *g = &flags[flag_count++];
    memset(g, 0, sizeof(*g));
    snprintf(g->key, sizeof(g->key), "%s", key);
    return g;
}
/* ---------- HTTP ---------- */
static void reply(int fd, int status, const char *body) {
    const char *reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden"
                       : status == 404 ? "Not Found" : status == 409 ? "Conflict" : status == 413 ? "Too Large"
                       : status == 429 ? "Too Many Requests" : "Error";
    char head[256];
    size_t bl = body ? strlen(body) : 0;
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.0 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     status, reason, bl);
    if (send(fd, head, (size_t)n, MSG_NOSIGNAL) < 0)
        return;
    for (size_t at = 0; at < bl;) {
        ssize_t k = send(fd, body + at, bl - at, MSG_NOSIGNAL);
        if (k <= 0)
            return;
        at += (size_t)k;
    }
}
static void fail(int fd, int status, const char *why) {
    char b[160];
    snprintf(b, sizeof(b), "{\"ok\":false,\"error\":\"%s\"}", why);
    reply(fd, status, b);
}
static int read_some(int fd, char *buf, size_t n, int ms) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, ms) <= 0)
        return -1;
    return (int)recv(fd, buf, n, 0);
}
static int rate_ok(const uint8_t who[8]) {
    uint32_t minute = now_s() / 60;
    int slot = who[0];
    if (memcmp(rate[slot].who, who, 8) || rate[slot].minute != minute) {
        memcpy(rate[slot].who, who, 8);
        rate[slot].minute = minute;
        rate[slot].n = 0;
    }
    return ++rate[slot].n <= RATE_PER_MIN;
}
static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '+')
            *o++ = ' ';
        else if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else
            *o++ = *s;
    }
    *o = 0;
}
static int ci_has(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!n)
        return 1;
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n))
            return 1;
    return 0;
}
/* ---------- endpoints ---------- */
static void do_list(int fd, const char *query) {
    char q[100] = "";
    const char *p = strstr(query, "q=");
    if (p && (p == query || p[-1] == '&')) {
        snprintf(q, sizeof(q), "%.*s", (int)strcspn(p + 2, "&"), p + 2);
        url_decode(q);
    }
    uint32_t now = now_s();
    cJSON *payload = cJSON_CreateObject(), *list;
    cJSON_AddNumberToObject(payload, "v", 1);
    cJSON_AddNumberToObject(payload, "time", now);
    list = cJSON_AddArrayToObject(payload, "communities");
    int n = 0;
    for (int i = 0; i < host_count && n < DIR_LIST_MAX; i++) {
        const Host *h = &hosts[i];
        if (now - h->last_seen > DIR_TTL)
            continue;
        for (int k = 0; k < h->count && n < DIR_LIST_MAX; k++) {
            const DirCommunity *c = &h->c[k];
            const Flag *g = flag_of(h->server, c->id, 0);
            if ((g && g->hidden) || (!ci_has(c->name, q) && !ci_has(c->desc, q) && !ci_has(c->host, q)))
                continue;
            DirCommunity out = *c;
            snprintf(out.server, sizeof(out.server), "%.16s", h->server);
            out.seen = h->last_seen;
            cJSON_AddItemToArray(list, dir_community_json(&out));
            n++;
        }
    }
    char *env = dir_seal(payload, dir_secret, dir_pub, 0);
    cJSON_Delete(payload);
    reply(fd, 200, env);
    free(env);
}
static void do_announce(int fd, const char *body, size_t n) {
    uint8_t pub[32];
    cJSON *p = dir_open(body, n, NULL, pub);
    if (!p) {
        fail(fd, 403, "signature");
        return;
    }
    const cJSON *srv = cJSON_GetObjectItem(p, "server"), *tm = cJSON_GetObjectItem(p, "time"),
                *list = cJSON_GetObjectItem(p, "communities");
    uint8_t raw[8];
    uint32_t now = now_s();
    if (cJSON_GetNumberValue(cJSON_GetObjectItem(p, "v")) != 1 || !cJSON_IsString(srv) ||
        strlen(srv->valuestring) != 16 || orbit_unhex(raw, srv->valuestring, 8) || !cJSON_IsNumber(tm) ||
        !cJSON_IsArray(list) || cJSON_GetArraySize(list) > DIR_PER_SERVER) {
        cJSON_Delete(p);
        fail(fd, 400, "format");
        return;
    }
    uint32_t t = (uint32_t)tm->valuedouble;
    if (t + 600 < now || t > now + 600) {
        cJSON_Delete(p);
        fail(fd, 400, "time");
        return;
    }
    Host *h = NULL;
    for (int i = 0; i < host_count && !h; i++)
        if (!strcmp(hosts[i].server, srv->valuestring))
            h = &hosts[i];
    if (h && crypto_verify32(h->pub, pub)) {
        cJSON_Delete(p);
        fail(fd, 409, "server id belongs to another key");
        return;
    }
    /* removing a listing (empty list) is always allowed; updates at most every ANNOUNCE_GAP s */
    if (h && (t <= h->last_time || (cJSON_GetArraySize(list) && now - h->last_seen < ANNOUNCE_GAP))) {
        cJSON_Delete(p);
        fail(fd, 429, "too often or replayed");
        return;
    }
    DirCommunity tmp[DIR_PER_SERVER];
    int count = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, list) {
        if (dir_community_parse(x, &tmp[count], allow_private)) {
            cJSON_Delete(p);
            fail(fd, 400, "community");
            return;
        }
        tmp[count].server[0] = 0;
        tmp[count].seen = 0;
        for (int k = 0; k < count; k++)
            if (tmp[k].id == tmp[count].id) {
                cJSON_Delete(p);
                fail(fd, 400, "duplicate id");
                return;
            }
        count++;
    }
    if (!h) {
        if (host_count >= MAX_SERVERS) { /* full: replace the stalest */
            int old = 0;
            for (int i = 1; i < host_count; i++)
                if (hosts[i].last_seen < hosts[old].last_seen)
                    old = i;
            if (now - hosts[old].last_seen <= DIR_TTL) {
                cJSON_Delete(p);
                fail(fd, 429, "directory full");
                return;
            }
            h = &hosts[old];
        } else
            h = &hosts[host_count++];
        memset(h, 0, sizeof(*h));
        snprintf(h->server, sizeof(h->server), "%.16s", srv->valuestring);
        memcpy(h->pub, pub, 32);
    }
    h->last_time = t;
    h->last_seen = now;
    h->count = count;
    memcpy(h->c, tmp, sizeof(DirCommunity) * (size_t)count);
    cJSON_Delete(p);
    save_state();
    reply(fd, 200, "{\"ok\":true}");
}
static void do_report(int fd, const char *body, size_t n, const uint8_t who[8]) {
    cJSON *r = cJSON_ParseWithLength(body, n);
    const cJSON *srv = cJSON_GetObjectItem(r, "server"), *id = cJSON_GetObjectItem(r, "id"),
                *why = cJSON_GetObjectItem(r, "reason");
    uint8_t raw[8];
    if (!cJSON_IsString(srv) || strlen(srv->valuestring) != 16 || orbit_unhex(raw, srv->valuestring, 8) ||
        !cJSON_IsNumber(id) || id->valuedouble < 0 || id->valuedouble >= ORBIT_GROUPS || !cJSON_IsNumber(why) ||
        why->valuedouble < 0 || why->valuedouble > 5) {
        cJSON_Delete(r);
        fail(fd, 400, "format");
        return;
    }
    Flag *g = flag_of(srv->valuestring, id->valueint, 1);
    if (!g) {
        cJSON_Delete(r);
        fail(fd, 429, "full");
        return;
    }
    int known = 0;
    for (int i = 0; i < MAX_REPORTERS && i < g->reports; i++)
        known |= !memcmp(g->who[i], who, 8);
    if (!known) {
        if (g->reports < MAX_REPORTERS)
            memcpy(g->who[g->reports], who, 8);
        g->reports++;
        if (g->reports >= DIR_HIDE_REPORTS)
            g->hidden = 1;
        FILE *log = fopen("reports.log", "a");
        if (log) {
            fprintf(log, "{\"time\":%u,\"key\":\"%s\",\"reason\":%d,\"reports\":%d,\"hidden\":%s}\n", now_s(), g->key,
                    why->valueint, g->reports, g->hidden ? "true" : "false");
            fclose(log);
        }
        save_state();
    }
    cJSON_Delete(r);
    reply(fd, 200, "{\"ok\":true}");
}
static void serve(int fd, const struct sockaddr_storage *from) {
    /* reporter / rate identity: a keyed hash of the address, never the address itself */
    static uint8_t salt[32];
    static int salted;
    if (!salted)
        salted = !orbit_random(salt, sizeof(salt));
    char addr[64] = "";
    if (from->ss_family == AF_INET)
        inet_ntop(AF_INET, &((const struct sockaddr_in *)from)->sin_addr, addr, sizeof(addr));
    else if (from->ss_family == AF_INET6)
        inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)from)->sin6_addr, addr, sizeof(addr));
    uint8_t who[8];
    crypto_blake2b_keyed(who, 8, salt, 32, (const uint8_t *)addr, strlen(addr));
    static char buf[8192 + DIR_BODY_MAX];
    size_t have = 0;
    char *end = NULL;
    while (!end) {
        if (have >= 8192) {
            fail(fd, 413, "header");
            return;
        }
        int k = read_some(fd, buf + have, 8192 - have, 5000);
        if (k <= 0)
            return;
        have += (size_t)k;
        buf[have] = 0;
        end = strstr(buf, "\r\n\r\n");
    }
    if (!rate_ok(who)) {
        fail(fd, 429, "rate");
        return;
    }
    char method[8] = "", path[300] = "";
    if (sscanf(buf, "%7s %299s", method, path) != 2) {
        fail(fd, 400, "request");
        return;
    }
    long length = 0;
    for (char *l = strstr(buf, "\r\n"); l && l < end; l = strstr(l + 2, "\r\n"))
        if (!strncasecmp(l + 2, "content-length:", 15))
            length = strtol(l + 17, NULL, 10);
    char *body = end + 4;
    size_t got = have - (size_t)(body - buf);
    if (length < 0 || length > DIR_BODY_MAX) {
        fail(fd, 413, "body");
        return;
    }
    while (got < (size_t)length) {
        int k = read_some(fd, body + got, (size_t)length - got, 5000);
        if (k <= 0)
            return;
        got += (size_t)k;
    }
    char *query = strchr(path, '?');
    if (query)
        *query++ = 0;
    if (!strcmp(method, "GET") && !strcmp(path, "/v1/list"))
        do_list(fd, query ? query : "");
    else if (!strcmp(method, "POST") && !strcmp(path, "/v1/announce"))
        do_announce(fd, body, (size_t)length);
    else if (!strcmp(method, "POST") && !strcmp(path, "/v1/report"))
        do_report(fd, body, (size_t)length, who);
    else
        fail(fd, 404, "unknown");
}
static int load_key(const char *path) {
    uint8_t seed[32];
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(seed, 1, 32, f);
        fclose(f);
        if (n != 32)
            return -1;
    } else {
        if (orbit_random(seed, 32))
            return -1;
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || write(fd, seed, 32) != 32) {
            if (fd >= 0)
                close(fd);
            return -1;
        }
        close(fd);
    }
    crypto_eddsa_key_pair(dir_secret, dir_pub, seed);
    return 0;
}
int main(int argc, char **argv) {
    int port = 8470, print_pub = 0;
    const char *key_path = "dird.key", *unhide = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--state") && i + 1 < argc)
            state_path = argv[++i];
        else if (!strcmp(argv[i], "--key") && i + 1 < argc)
            key_path = argv[++i];
        else if (!strcmp(argv[i], "--allow-private"))
            allow_private = 1;
        else if (!strcmp(argv[i], "--print-pub"))
            print_pub = 1;
        else if (!strcmp(argv[i], "--unhide") && i + 1 < argc)
            unhide = argv[++i];
        else {
            fprintf(stderr, "usage: orbit-dird [--port N] [--state FILE] [--key FILE] [--allow-private] "
                            "[--print-pub] [--unhide SERVER:ID]\n");
            return 2;
        }
    }
    load_state();
    if (unhide) {
        for (int i = 0; i < flag_count; i++)
            if (!strcmp(flags[i].key, unhide)) {
                flags[i].hidden = 0;
                flags[i].reports = 0;
                memset(flags[i].who, 0, sizeof(flags[i].who));
                save_state();
                printf("unhidden %s\n", unhide);
                return 0;
            }
        printf("not found: %s\n", unhide);
        return 1;
    }
    if (load_key(key_path)) {
        fprintf(stderr, "cannot read or create the key file\n");
        return 1;
    }
    char ph[65];
    orbit_hex(ph, dir_pub, 32);
    if (print_pub) {
        printf("%s\n", ph);
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(allow_private ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 64)) {
        perror("listen");
        return 1;
    }
    printf("orbit-dird on port %d, directory key %s\n", port, ph);
    fflush(stdout);
    for (;;) {
        struct sockaddr_storage from;
        socklen_t fl = sizeof(from);
        int fd = accept(s, (struct sockaddr *)&from, &fl);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        serve(fd, &from);
        close(fd);
    }
    return 0;
}
