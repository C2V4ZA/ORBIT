/* ORBIT 1.1 background notifications, end to end on the PC: the real ORBIT host (store + network)
 * and the real plugin logic (notify_core.c) with a POSIX platform layer instead of the PS3's. */
#include "i18n.h"
#include "monocypher.h"
#include "notify_core.h"
#include "orbit.h"
#include "presence_worker.h"
#include "watch.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define PORT 31471
#define CFG "/tmp/orbit-notify-test.cfg"
#define ACTIVE "/tmp/orbit-notify-test-active"
#define PCFG "/tmp/orbit-presence-test.cfg"

/* ---------- POSIX platform for notify_core ---------- */
static pthread_mutex_t shown_lock = PTHREAD_MUTEX_INITIALIZER;
static char shown[32][400];
static int shown_count, connects;
static volatile int stop_flag;
static int p_connect(uint32_t ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(ip);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a))) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return fd;
}
static int p_send(int fd, const void *buf, size_t n) {
    return send(fd, buf, n, MSG_NOSIGNAL) == (ssize_t)n ? 0 : -1;
}
static int p_recv(int fd, void *buf, size_t n, unsigned ms) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, (int)ms) <= 0)
        return 0;
    ssize_t k = recv(fd, buf, n, 0);
    return k > 0 ? (int)k : -1;
}
static void p_close(int fd) { close(fd); }
static uint64_t p_millis(void) { return orbit_millis(); }
static uint32_t p_time(void) { return (uint32_t)time(NULL); }
static void p_sleep(unsigned ms) {
    for (unsigned t = 0; t < ms && !stop_flag; t += 50)
        usleep(50000);
}
static void p_notify(const char *text) {
    pthread_mutex_lock(&shown_lock);
    if (shown_count < 32)
        snprintf(shown[shown_count++], 400, "%s", text);
    pthread_mutex_unlock(&shown_lock);
}
static int p_read(const char *path, char *buf, size_t cap) {
    const char *real = !strcmp(path, NOTIFY_CONFIG)     ? CFG
                       : !strcmp(path, NOTIFY_ACTIVE)   ? ACTIVE
                       : !strcmp(path, PRESENCE_CONFIG) ? PCFG
                                                        : path;
    FILE *f = fopen(real, "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (int)n;
}
static void p_entropy(uint8_t out[24]) { orbit_random(out, 24); }
static int presence_stored;
static void p_log(const char *text, unsigned long v) {
    (void)v;
    if (!strcmp(text, "watch connected"))
        __sync_fetch_and_add(&connects, 1);
    if (!strcmp(text, "presence stored"))
        __sync_fetch_and_add(&presence_stored, 1);
}
/* 1.2 presence: the "running game" the stub detector reports */
static PresenceGame fake_game;
static pthread_mutex_t game_lock = PTHREAD_MUTEX_INITIALIZER;
static int fake_detect(PresenceGame *g) {
    pthread_mutex_lock(&game_lock);
    *g = fake_game;
    pthread_mutex_unlock(&game_lock);
    return 0;
}
static void set_game(int playing, const char *id, const char *name) {
    pthread_mutex_lock(&game_lock);
    fake_game.playing = playing;
    snprintf(fake_game.id, sizeof(fake_game.id), "%s", id);
    snprintf(fake_game.name, sizeof(fake_game.name), "%s", name);
    pthread_mutex_unlock(&game_lock);
}
static void *presence_thread(void *arg);
static cJSON *ask(OrbitStore *s, OrbitSession *se, const char *json);
static void write_presence(int share) {
    FILE *f = fopen(PCFG, "w");
    fprintf(f, "ORBITPRESENCE\t1\nshare\t%d\n", share);
    fclose(f);
}
/* what Anna's meeting point tells Anna about her friend Ben */
static int friend_playing(OrbitStore *s, OrbitSession *owner, char id[10], char name[65]);
/* a PARAM.SFO image with TITLE_ID and TITLE (little endian layout of the real format) */
static size_t make_sfo(uint8_t *out, const char *id, const char *title) {
    const char *keys[2] = {"TITLE", "TITLE_ID"};
    const char *vals[2] = {title, id};
    size_t kt = 20 + 2 * 16, ko = 0, dt, dofs = 0;
    memset(out, 0, 512);
    memcpy(out, "\0PSF", 4);
    out[4] = 1, out[5] = 1;
    for (int i = 0; i < 2; i++)
        memcpy(out + kt + ko, keys[i], strlen(keys[i]) + 1), ko += strlen(keys[i]) + 1;
    dt = (kt + ko + 3) & ~(size_t)3;
    for (int i = 0; i < 2; i++) {
        uint8_t *e = out + 20 + i * 16;
        size_t kofs = i ? 6 : 0, len = strlen(vals[i]) + 1, max = (len + 3) & ~(size_t)3;
        e[0] = (uint8_t)kofs, e[2] = 4, e[3] = 2;
        e[4] = (uint8_t)len, e[8] = (uint8_t)max, e[12] = (uint8_t)dofs;
        memcpy(out + dt + dofs, vals[i], len);
        dofs += max;
    }
    out[8] = (uint8_t)kt, out[12] = (uint8_t)dt;
    out[16] = 2;
    return dt + dofs;
}
static const NotifyPlatform platform = {p_connect, p_send, p_recv, p_close, p_millis, p_time,
                                        p_sleep, p_notify, p_read, p_entropy, p_log, &stop_flag};
static void *worker(void *arg) {
    notify_worker(&platform, (int)(intptr_t)arg);
    return NULL;
}
static void *presence_thread(void *arg) {
    (void)arg;
    presence_worker(&platform, fake_detect);
    return NULL;
}
static int friend_playing(OrbitStore *s, OrbitSession *owner, char id[10], char name[65]) {
    cJSON *r = ask(s, owner, "{\"op\":\"friends\"}");
    int found = 0;
    const cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(r, "friends")) {
        const cJSON *pl = cJSON_GetObjectItem(x, "playing");
        if (pl) {
            snprintf(id, 10, "%s", orbit_string(pl, "id"));
            snprintf(name, 65, "%s", orbit_string(pl, "name"));
            found = 1;
        }
    }
    cJSON_Delete(r);
    return found;
}
/* waits until `count` notifications were shown (or timeout) */
static int wait_shown(int count, int ms) {
    for (int t = 0; t < ms; t += 50) {
        pthread_mutex_lock(&shown_lock);
        int n = shown_count;
        pthread_mutex_unlock(&shown_lock);
        if (n >= count)
            return n;
        usleep(50000);
    }
    return shown_count;
}
static int wait_connects(int count, int ms) {
    for (int t = 0; t < ms && connects < count; t += 50)
        usleep(50000);
    return connects;
}

/* ---------- host helpers ---------- */
static cJSON *ask(OrbitStore *s, OrbitSession *se, const char *json) {
    cJSON *q = cJSON_Parse(json);
    cJSON *r = orbit_request(s, se, q);
    cJSON_Delete(q);
    return r;
}
static int okd(OrbitStore *s, OrbitSession *se, const char *json) {
    cJSON *r = ask(s, se, json);
    int v = cJSON_IsTrue(cJSON_GetObjectItem(r, "ok"));
    if (!v)
        printf("  (refused: %s -> %s)\n", json, orbit_string(r, "ekey"));
    cJSON_Delete(r);
    return v;
}
static int sign_in(OrbitStore *s, OrbitSession *se, const char *name, int seed) {
    uint8_t device[32], secret[64], pub[32], sig[64], t[32];
    memset(device, seed, 32);
    orbit_identity(device, secret, pub);
    orbit_bind_sign(sig, secret, pub, s->server_id);
    char token[65], p[65], g[129], json[600];
    crypto_blake2b_keyed(t, 32, device, 32, (const uint8_t *)s->server_id, 16);
    orbit_hex(token, t, 32);
    orbit_hex(p, pub, 32);
    orbit_hex(g, sig, 64);
    snprintf(json, sizeof(json),
             "{\"op\":\"device\",\"token\":\"%s\",\"name\":\"%s\",\"pub\":\"%s\",\"sig\":\"%s\",\"status\":0}",
             token, name, p, g);
    okd(s, se, "{\"op\":\"hello\",\"protocol\":11}");
    cJSON *r = ask(s, se, json);
    cJSON *u = cJSON_GetObjectItem(r, "user");
    int uid = cJSON_IsNumber(u) ? u->valueint : -1;
    cJSON_Delete(r);
    return uid;
}
static void say(OrbitStore *s, OrbitSession *se, int group, int channel, const char *text) {
    char q[1200];
    usleep(330000); /* the host allows one message per 300 ms per person */
    snprintf(q, sizeof(q), "{\"op\":\"send\",\"group\":%d,\"channel\":%d,\"text\":\"%s\"}", group,
             channel, text);
    CHECK(okd(s, se, q));
}
static void write_config(const char *server, const char *wid, const char *wkey, int preview,
                         const char *extra) {
    FILE *f = fopen(CFG, "w");
    fprintf(f, "ORBITNOTIFY\t1\non\t1\ndm\t1\nserver\t1\npreview\t%d\nbusy\t0\n", preview);
    fprintf(f, "t_dm\tORBIT \xc2\xb7 New message\n");
    fprintf(f, "t_server\tORBIT \xc2\xb7 %%s \xe2\x80\xba #%%c\n");
    fprintf(f, "t_from\tNew message from %%u\nt_many_from\t%%n new messages from %%u\nt_many\t%%n new messages\n");
    fprintf(f, "host\t127.0.0.1\t%d\t0\t0\t0123456789abcdef\t%s\t%s\t%s\n", PORT, wid, wkey, server);
    if (extra)
        fputs(extra, f);
    fclose(f);
}

/* a raw plugin session for a secret: returns the socket after the proof, or -1 */
static int raw_watch(const char *wid_hex, const char *wkey_hex, WatchKeys *k) {
    uint8_t id[8], secret[32], np[24], nh[24], hello[WATCH_HELLO], wire[64], auth[2] = {'A', '1'};
    if (orbit_unhex(id, wid_hex, 8) || orbit_unhex(secret, wkey_hex, 32))
        return -1;
    orbit_random(np, 24);
    watch_hello(hello, id, np);
    int fd = p_connect(0x7f000001, PORT);
    if (fd < 0 || p_send(fd, hello, sizeof(hello)))
        return -1;
    size_t got = 0;
    while (got < 24) {
        int n = p_recv(fd, nh + got, 24 - got, 3000);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    watch_derive(k, secret, np, nh, 0);
    if (p_send(fd, wire, watch_seal(k, wire, auth, 2))) {
        close(fd);
        return -1;
    }
    return fd;
}
/* 1 when the host closed this socket within ms (anything it sends meanwhile is skipped) */
static int closed_within(int fd, int ms) {
    uint8_t buf[256];
    for (int t = 0; t < ms; t += 100) {
        int n = p_recv(fd, buf, sizeof(buf), 100);
        if (n < 0)
            return 1;
    }
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    i18n_set(LANG_EN);
    remove(CFG);
    remove(ACTIVE);
    OrbitStore *s = calloc(1, sizeof(*s));
    remove("/tmp/orbit-notify-store.json");
    remove("/tmp/orbit-notify-store.json.bak");
    CHECK(!orbit_store_open(s, "/tmp/orbit-notify-store.json"));
    snprintf(s->server_id, sizeof(s->server_id), "a1b2c3d4e5f60718"); /* the app sets this */
    uint8_t key[32];
    orbit_random(key, 32);
    OrbitServer *srv = orbit_server_start(s, key, PORT);
    CHECK(srv != NULL);
    OrbitSession anna = {.user = -1}, ben = {.user = -1}, cleo = {.user = -1};
    int ua = sign_in(s, &anna, "Anna", 1), ub = sign_in(s, &ben, "Ben", 2),
        uc = sign_in(s, &cleo, "Cleo", 3);
    CHECK(ua >= 0 && ub >= 0 && uc >= 0);
    cJSON *cr = ask(s, &anna, "{\"op\":\"create\",\"name\":\"Clan Alpha\",\"categories\":[\"TEXT\"],"
                              "\"channels\":[{\"name\":\"general\",\"type\":0,\"cat\":0},"
                              "{\"name\":\"staff\",\"type\":0,\"cat\":0}]}");
    int gs = cJSON_GetObjectItem(cr, "group")->valueint;
    cJSON_Delete(cr);
    char q[400];
    snprintf(q, sizeof(q), "{\"op\":\"join\",\"invite\":\"%s\"}", s->data.group[gs].invite);
    CHECK(okd(s, &ben, q) && okd(s, &cleo, q));
    /* a private conversation Anna + Ben (created directly in the data: DMs need friendship) */
    pthread_mutex_lock(&s->mutex);
    int gdm = -1;
    for (int i = 0; i < ORBIT_GROUPS && gdm < 0; i++)
        if (!s->data.group[i].used)
            gdm = i;
    OrbitGroup *D = &s->data.group[gdm];
    memset(D, 0, sizeof(*D));
    D->used = 1;
    D->kind = GK_DM;
    D->owner = ua;
    D->members = 1u << ua | 1u << ub;
    D->ch[0].used = 1;
    D->ch[0].type = CH_TEXT;
    snprintf(D->invite, sizeof(D->invite), "DMDMDMDM");
    pthread_mutex_unlock(&s->mutex);

    /* ---------- 1. credential: only for the person themself, stable, renewable ---------- */
    OrbitSession nobody = {.user = -1};
    cJSON *r = ask(s, &nobody, "{\"op\":\"watch_key\"}");
    CHECK(!cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
    cJSON_Delete(r);
    r = ask(s, &ben, "{\"op\":\"watch_key\"}");
    char wid[17], wkey[65];
    snprintf(wid, sizeof(wid), "%s", orbit_string(r, "wid"));
    snprintf(wkey, sizeof(wkey), "%s", orbit_string(r, "wkey"));
    cJSON_Delete(r);
    CHECK(strlen(wid) == 16 && strlen(wkey) == 64);
    r = ask(s, &ben, "{\"op\":\"watch_key\"}");
    CHECK(!strcmp(orbit_string(r, "wkey"), wkey)); /* same secret again */
    cJSON_Delete(r);
    printf("PASS: watch_key only for a signed-in person, same secret on every request\n");

    /* ---------- 2. config + formatting (unit level) ---------- */
    write_config(s->server_id, wid, wkey, 1, NULL);
    {
        char text[8192];
        int n = p_read(NOTIFY_CONFIG, text, sizeof(text));
        NotifyConfig c;
        CHECK(n > 0 && !notify_parse(&c, text, (size_t)n));
        CHECK(c.on && c.dm && c.server && c.preview && !c.busy && c.hosts == 1 &&
              c.host[0].ip == 0x7f000001 && c.host[0].port == PORT);
        NotifyEvent e = {.dm = 0, .group = gs, .channel = 0};
        snprintf(e.sender, sizeof(e.sender), "Max");
        snprintf(e.server, sizeof(e.server), "Clan Alpha");
        snprintf(e.channel_name, sizeof(e.channel_name), "general");
        snprintf(e.text, sizeof(e.text), "who's on?");
        char out[400];
        notify_format(&c, &e, 1, 1, 1, out, sizeof(out));
        CHECK(!strcmp(out, "ORBIT \xc2\xb7 Clan Alpha \xe2\x80\xba #general\nMax: who's on?"));
        c.preview = 0;
        notify_format(&c, &e, 1, 1, 1, out, sizeof(out));
        CHECK(!strcmp(out, "ORBIT \xc2\xb7 Clan Alpha \xe2\x80\xba #general\nNew message from Max"));
        notify_format(&c, &e, 3, 1, 1, out, sizeof(out));
        CHECK(!strcmp(out, "ORBIT \xc2\xb7 Clan Alpha \xe2\x80\xba #general\n3 new messages from Max"));
        notify_format(&c, &e, 4, 0, 0, out, sizeof(out));
        CHECK(!strcmp(out, "ORBIT\n4 new messages"));
        e.dm = 1;
        c.preview = 1;
        notify_format(&c, &e, 1, 1, 1, out, sizeof(out));
        CHECK(!strcmp(out, "ORBIT \xc2\xb7 New message\nMax: who's on?"));
        /* switches */
        CHECK(notify_wanted(&c, &c.host[0], &e));
        c.dm = 0;
        CHECK(!notify_wanted(&c, &c.host[0], &e));
        c.dm = 1;
        c.busy = 1;
        CHECK(!notify_wanted(&c, &c.host[0], &e));
        c.busy = 0;
        snprintf(c.mute[0].server, 17, "%s", s->server_id);
        c.mute[0].group = gs;
        c.mutes = 1;
        e.dm = 0;
        CHECK(!notify_wanted(&c, &c.host[0], &e)); /* muted server */
        /* broken configs are refused, never half-read */
        CHECK(notify_parse(&c, "XRBITNOTIFY\t1\n", 14));
        CHECK(notify_parse(&c, "", 0));
        /* a malformed host line is skipped */
        const char *bad = "ORBITNOTIFY\t1\non\t1\nhost\t999.0.0.1\t1\t0\t0\tx\ty\tz\tw\n";
        CHECK(!notify_parse(&c, bad, strlen(bad)) && c.hosts == 0);
        /* event decoding: control characters cannot break the notification layout */
        const uint8_t frame[] = "Ms\x1f" "2\x1f" "0\x1f" "Max\x1f" "Clan\x1f" "general\x1f" "a\nb";
        CHECK(!notify_event(&e, frame, sizeof(frame) - 1) && !strcmp(e.text, "a b") && e.group == 2);
        CHECK(notify_event(&e, (const uint8_t *)"Mx", 2));
    }
    printf("PASS: config parsing, malformed input refused, text layout (title, preview on/off, bursts), switches and mutes\n");

    /* ---------- 3. live: the plugin logic receives what others write ---------- */
    pthread_t t;
    pthread_create(&t, NULL, worker, (void *)(intptr_t)0);
    CHECK(wait_connects(1, 5000) == 1);
    say(s, &anna, gs, 0, "who is playing tonight?");
    CHECK(wait_shown(1, 6000) == 1);
    CHECK(!strcmp(shown[0], "ORBIT \xc2\xb7 Clan Alpha \xe2\x80\xba #general\nAnna: who is playing tonight?"));
    /* own messages never notify */
    say(s, &ben, gs, 0, "me!");
    usleep(3200000);
    CHECK(shown_count == 1);
    /* three quick messages become one notification */
    say(s, &anna, gs, 0, "one");
    say(s, &anna, gs, 0, "two");
    say(s, &anna, gs, 0, "three");
    CHECK(wait_shown(2, 6000) == 2);
    CHECK(!strcmp(shown[1], "ORBIT \xc2\xb7 Clan Alpha \xe2\x80\xba #general\n3 new messages from Anna"));
    usleep(3000000);
    CHECK(shown_count == 2);
    /* private conversation */
    say(s, &anna, gdm, 0, "hey, are you on?");
    CHECK(wait_shown(3, 6000) == 3);
    CHECK(!strcmp(shown[2], "ORBIT \xc2\xb7 New message\nAnna: hey, are you on?"));
    /* Cleo's private chat with Anna is not Ben's business */
    pthread_mutex_lock(&s->mutex);
    D->members = 1u << ua | 1u << uc;
    pthread_mutex_unlock(&s->mutex);
    say(s, &anna, gdm, 0, "secret for Cleo");
    usleep(3200000);
    CHECK(shown_count == 3);
    pthread_mutex_lock(&s->mutex);
    D->members = 1u << ua | 1u << ub;
    pthread_mutex_unlock(&s->mutex);
    /* a channel Ben may not see */
    snprintf(q, sizeof(q), "{\"op\":\"role_add\",\"group\":%d,\"name\":\"staff\",\"perms\":0}", gs);
    CHECK(okd(s, &anna, q));
    snprintf(q, sizeof(q), "{\"op\":\"channel_update\",\"group\":%d,\"ch\":1,\"view\":1,\"send\":1}", gs);
    CHECK(okd(s, &anna, q));
    say(s, &anna, gs, 1, "staff only");
    usleep(3200000);
    CHECK(shown_count == 3);
    /* long text with umlauts is cut without breaking a character */
    char longtext[400] = "";
    for (int i = 0; i < 60; i++)
        strcat(longtext, "\xc3\xa4"); /* ä */
    say(s, &cleo, gs, 0, longtext);
    CHECK(wait_shown(4, 6000) == 4);
    {
        const char *nl = strchr(shown[3], '\n');
        const char *body = nl ? nl + 1 : "";
        size_t len = strlen(body);
        CHECK(!strncmp(body, "Cleo: ", 6) && len <= 6 + WATCH_TEXT && (len - 6) % 2 == 0);
    }
    printf("PASS: live notifications: server and private messages, bursts combined, own/hidden/foreign messages never shown, UTF-8 cut safely\n");

    /* ---------- 4. the app is open: the plugin steps aside ---------- */
    FILE *f = fopen(ACTIVE, "w");
    fprintf(f, "%u", (unsigned)time(NULL));
    fclose(f);
    usleep(6500000); /* checked every 5 s */
    say(s, &anna, gs, 0, "while the app is open");
    usleep(3200000);
    CHECK(shown_count == 4);
    remove(ACTIVE);
    int before = connects;
    CHECK(wait_connects(before + 1, 20000) == before + 1); /* comes back by itself */
    say(s, &anna, gs, 0, "app closed again");
    CHECK(wait_shown(5, 6000) == 5);
    /* a stale active file (app crashed long ago) does not silence the plugin */
    f = fopen(ACTIVE, "w");
    fprintf(f, "%u", (unsigned)time(NULL) - 600);
    fclose(f);
    CHECK(!notify_app_active(&platform));
    remove(ACTIVE);
    printf("PASS: quiet while the ORBIT app is open, reconnects by itself afterwards, stale marker ignored\n");

    /* ---------- 4b. connection housekeeping on the host ---------- */
    /* keep-alives: the plugin's session survives far longer than the idle limit */
    before = connects;
    usleep(9000000); /* > WATCH_IDLE_MS (shortened to 6.5 s in this test) */
    CHECK(connects == before);
    say(s, &anna, gs, 0, "still there?");
    CHECK(wait_shown(6, 6000) == 6);
    /* a plugin that proves its key and then goes silent is dropped */
    {
        WatchKeys k;
        int fd = raw_watch(wid, wkey, &k); /* also replaces the worker's session for a moment */
        CHECK(fd >= 0);
        CHECK(closed_within(fd, 12000)); /* replaced by the worker again, or dropped as silent */
        close(fd);
    }
    /* one watch per person: the newest one wins, the worker comes back by itself */
    CHECK(wait_connects(before + 1, 20000) >= before + 1);
    {
        WatchKeys k;
        int fd = raw_watch(wid, wkey, &k);
        CHECK(fd >= 0);
        int rejoined = wait_connects(connects + 1, 20000);
        (void)rejoined;
        CHECK(closed_within(fd, 3000)); /* the worker's new session replaced this one */
        close(fd);
    }
    /* on its own: Cleo's plugin proves its key, then never sends again -> dropped after the
     * idle limit (6.5 s here), not kept forever */
    {
        cJSON *cr2 = ask(s, &cleo, "{\"op\":\"watch_key\"}");
        char cwid[17], ckey[65];
        snprintf(cwid, sizeof(cwid), "%s", orbit_string(cr2, "wid"));
        snprintf(ckey, sizeof(ckey), "%s", orbit_string(cr2, "wkey"));
        cJSON_Delete(cr2);
        WatchKeys k;
        uint64_t t0 = orbit_millis();
        int fd = raw_watch(cwid, ckey, &k);
        CHECK(fd >= 0);
        CHECK(!closed_within(fd, 4000)); /* alive while within the limit */
        CHECK(closed_within(fd, 8000));  /* then dropped */
        uint64_t took = orbit_millis() - t0;
        CHECK(took >= 6000 && took < 12500);
        close(fd);
    }
    printf("PASS: keep-alive keeps the plugin connected, silent plugins dropped, one watch per person (newest wins)\n");

    /* ---------- 5. wrong secret, renewed secret ---------- */
    {
        uint8_t hello[WATCH_HELLO], id[8], np[24], buf[64];
        orbit_random(id, 8);
        orbit_random(np, 24);
        watch_hello(hello, id, np);
        int fd = p_connect(0x7f000001, PORT);
        CHECK(fd >= 0 && !p_send(fd, hello, sizeof(hello)));
        CHECK(p_recv(fd, buf, sizeof(buf), 3000) == -1); /* unknown plugin: closed, nothing sent */
        close(fd);
        /* right id, wrong secret: the proof fails, nothing is ever sent */
        uint8_t real[8], fake[32], nh[24];
        CHECK(!orbit_unhex(real, wid, 8));
        watch_hello(hello, real, np);
        fd = p_connect(0x7f000001, PORT);
        CHECK(fd >= 0 && !p_send(fd, hello, sizeof(hello)));
        size_t got = 0;
        while (got < 24) {
            int k = p_recv(fd, nh + got, 24 - got, 3000);
            if (k <= 0)
                break;
            got += (size_t)k;
        }
        CHECK(got == 24);
        orbit_random(fake, 32);
        WatchKeys k;
        watch_derive(&k, fake, np, nh, 0);
        uint8_t wire[64], auth[2] = {'A', '1'};
        CHECK(!p_send(fd, wire, watch_seal(&k, wire, auth, 2)));
        CHECK(p_recv(fd, buf, sizeof(buf), 3000) == -1);
        close(fd);
    }
    r = ask(s, &ben, "{\"op\":\"watch_key\",\"renew\":1}");
    CHECK(strcmp(orbit_string(r, "wkey"), wkey)); /* new secret */
    cJSON_Delete(r);
    usleep(2500000);
    say(s, &anna, gs, 0, "after renew");
    usleep(3200000);
    CHECK(shown_count == 6); /* the old plugin credential is locked out */
    printf("PASS: unknown plugin and wrong secret get nothing, renewing the secret locks the old plugin out\n");

    /* ---------- 7. 1.2 automatic game presence (presence.h) ---------- */
    {
        uint8_t sfo[512];
        PresenceGame g;
        size_t n = make_sfo(sfo, "BCES00065", "Racing\nDemo");
        CHECK(!presence_sfo(sfo, n, &g) && !strcmp(g.id, "BCES00065") && !strcmp(g.name, "Racing Demo"));
        CHECK(presence_sfo(sfo, 30, &g) != 0); /* cut */
        n = make_sfo(sfo, "bces0006x", "X");
        CHECK(presence_sfo(sfo, n, &g) != 0 && !g.id[0]); /* not a title id */
        for (int i = 0; i < 3000; i++) { /* corrupted images never crash or overread (ASan) */
            n = make_sfo(sfo, "BLUS30001", "Some Game");
            sfo[rand() % n] ^= (uint8_t)(1 + rand() % 255);
            presence_sfo(sfo, n, &g);
        }
        uint8_t fr[96];
        PresenceGame in = {1, "BLES01234", "Weltraum \xc3\x9c"}, out;
        n = presence_frame(fr, sizeof(fr), &in);
        CHECK(!presence_frame_parse(fr, n, &out) && out.playing && !strcmp(out.id, in.id) && !strcmp(out.name, in.name));
        in.playing = 0;
        n = presence_frame(fr, sizeof(fr), &in);
        CHECK(n == 1 && !presence_frame_parse(fr, n, &out) && !out.playing);
        CHECK(presence_frame_parse((const uint8_t *)"GBLES1\x1fX", 9, &out) != 0);     /* short id */
        CHECK(presence_frame_parse((const uint8_t *)"G\x1f\x01x", 4, &out) != 0);       /* control char */
        CHECK(presence_frame_parse((const uint8_t *)"G\x1f\xc3", 3, &out) != 0);        /* broken UTF-8 */
        CHECK(!presence_frame_parse((const uint8_t *)"G\x1f", 2, &out) && out.playing); /* unknown game */
    }
    {
        /* Ben is Anna's friend on Anna's meeting point; the owner session asks for friends */
        r = ask(s, &ben, "{\"op\":\"watch_key\"}");
        char bwid[17], bkey[65];
        snprintf(bwid, sizeof(bwid), "%s", orbit_string(r, "wid"));
        snprintf(bkey, sizeof(bkey), "%s", orbit_string(r, "wkey"));
        cJSON_Delete(r);
        pthread_mutex_lock(&s->mutex);
        OrbitFriend *F = &s->data.friend_[0];
        memset(F, 0, sizeof(*F));
        F->used = 1;
        F->state = FR_FRIENDS;
        F->user = ub;
        memcpy(F->pub, s->data.user[ub].pub, 32);
        snprintf(F->name, sizeof(F->name), "Ben");
        pthread_mutex_unlock(&s->mutex);
        OrbitSession owner = anna;
        owner.local = 1;
        char id[10] = "", name[65] = "";
        CHECK(!friend_playing(s, &owner, id, name));
        /* Ben's notification watch keeps running while his presence plugin reports */
        WatchKeys wk;
        int wfd = raw_watch(bwid, bkey, &wk);
        CHECK(wfd >= 0);
        write_config(s->server_id, bwid, bkey, 1, NULL);
        NotifyConfig c;
        char text[4096];
        int tn = p_read(NOTIFY_CONFIG, text, sizeof(text));
        CHECK(tn > 0 && !notify_parse(&c, text, (size_t)tn));
        PresenceGame g = {1, "BCES00065", "Racing Demo"};
        CHECK(presence_send(&platform, &c.host[0], &g) == 0);
        CHECK(friend_playing(s, &owner, id, name) && !strcmp(id, "BCES00065") && !strcmp(name, "Racing Demo"));
        CHECK(!closed_within(wfd, 1500)); /* not replaced by the presence connection */
        close(wfd);
        /* members of a public server never see it: only the friends list carries it */
        snprintf(q, sizeof(q), "{\"op\":\"members\",\"group\":%d}", gs);
        r = ask(s, &cleo, q);
        char *dump = cJSON_PrintUnformatted(r);
        CHECK(dump && !strstr(dump, "BCES00065") && !strstr(dump, "playing"));
        free(dump);
        cJSON_Delete(r);
        /* a wrong secret stores nothing */
        NotifyHost bad = c.host[0];
        bad.secret[0] ^= 1;
        PresenceGame other = {1, "BLUS30001", "Other"};
        CHECK(presence_send(&platform, &bad, &other) != 0);
        CHECK(friend_playing(s, &owner, id, name) && !strcmp(id, "BCES00065"));
        /* expiry: a report older than PRESENCE_TTL is not shown */
        pthread_mutex_lock(&s->mutex);
        s->playing[ub].until = orbit_unix_time() - 1;
        pthread_mutex_unlock(&s->mutex);
        CHECK(!friend_playing(s, &owner, id, name));
        /* the plugin loop: sharing off = nothing sent; on = game reported, then "no game" */
        write_presence(0);
        set_game(1, "BLES01234", "Space Game");
        presence_stored = 0;
        pthread_t pt;
        pthread_create(&pt, NULL, presence_thread, NULL);
        usleep(1200000);
        CHECK(presence_stored == 0 && !friend_playing(s, &owner, id, name));
        write_presence(1);
        for (int t = 0; t < 40 && !friend_playing(s, &owner, id, name); t++)
            usleep(100000);
        CHECK(friend_playing(s, &owner, id, name) && !strcmp(id, "BLES01234") && !strcmp(name, "Space Game"));
        int before = presence_stored;
        usleep(2200000); /* refreshed while the game runs (PRESENCE_REFRESH_MS = 1.5 s here) */
        CHECK(presence_stored > before);
        set_game(0, "", "");
        for (int t = 0; t < 40 && friend_playing(s, &owner, id, name); t++)
            usleep(100000);
        CHECK(!friend_playing(s, &owner, id, name));
        before = presence_stored;
        usleep(2200000); /* no game: nothing more is sent */
        CHECK(presence_stored == before);
        stop_flag = 1;
        pthread_join(pt, NULL);
        stop_flag = 0;
        remove(PCFG);
    }
    printf("PASS: game presence: PARAM.SFO reader (fuzzed), frames validated, reported over the watch key without "
           "replacing the notification watch, friends only, expires, plugin loop honours the sharing switch\n");

    stop_flag = 1;
    pthread_join(t, NULL);
    orbit_server_stop(srv);
    orbit_session_end(s, &anna);
    orbit_session_end(s, &ben);
    orbit_session_end(s, &cleo);
    orbit_store_close(s);
    free(s);
    remove(CFG);
    if (failures)
        printf("%d check(s) failed\n", failures);
    return failures != 0;
}
