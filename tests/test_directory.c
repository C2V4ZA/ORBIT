/* ORBIT 1.2 public community directory (03), end to end on loopback:
 * the real orbit-dird binary, the shared format (directory.c) and two complete apps.
 * Nothing leaves the machine: the service only listens on 127.0.0.1 (--allow-private). */
#include "directory.h"
#include "monocypher.h"
#include "ui.h"
#include "update.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
pid_t waitpid(pid_t pid, int *status, int options); /* sys/wait.h clashes with P_ALL */
#include <unistd.h>

#define DPORT 31480
#define DDIR "/tmp/orbit-dird-test"
static const char *URL = "http://127.0.0.1:31480";
static UiState view;
static pid_t dird = -1;
static int wait_for(OrbitApp *a, int (*cond)(const UiState *), int ms) {
    for (int t = 0; t < ms; t += 100) {
        view.revision = 0;
        app_snapshot(a, &view);
        if (cond(&view))
            return 1;
        usleep(100000);
    }
    return 0;
}
static void run_dird(void) {
    char exe[512];
    assert(getcwd(exe, sizeof(exe)));
    strcat(exe, "/build/orbit-dird");
    dird = fork();
    if (!dird) {
        if (chdir(DDIR))
            _exit(1);
        execl(exe, exe, "--port", "31480", "--state", "state.json", "--key", "dird.key", "--allow-private",
              (char *)NULL);
        _exit(1);
    }
    for (int i = 0; i < 50; i++) { /* wait until it listens */
        char buf[64];
        size_t n;
        if (http_get("http://127.0.0.1:31480/v1/list", buf, 1, &n) != UPD_E_NET)
            return;
        usleep(100000);
    }
    assert(!"orbit-dird did not start");
}
static void stop_dird(void) {
    if (dird > 0) {
        kill(dird, SIGTERM);
        waitpid(dird, NULL, 0);
        dird = -1;
    }
}
/* POST from a chosen loopback source address (127.0.0.x), returns the HTTP status */
static int post_from(int x, const char *path, const char *body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in src = {0}, dst = {0};
    src.sin_family = dst.sin_family = AF_INET;
    src.sin_addr.s_addr = htonl(0x7f000000u | (uint32_t)x);
    dst.sin_addr.s_addr = htonl(0x7f000001u);
    dst.sin_port = htons(DPORT);
    if (bind(fd, (struct sockaddr *)&src, sizeof(src)) || connect(fd, (struct sockaddr *)&dst, sizeof(dst))) {
        close(fd);
        return -1;
    }
    char req[1024];
    int n = snprintf(req, sizeof(req), "POST %s HTTP/1.0\r\nContent-Length: %zu\r\n\r\n%s", path, strlen(body), body);
    assert(send(fd, req, (size_t)n, MSG_NOSIGNAL) == n);
    char reply[512] = "";
    int got = 0, k;
    while ((k = (int)recv(fd, reply + got, sizeof(reply) - 1 - got, 0)) > 0)
        got += k;
    close(fd);
    reply[got > 0 ? got : 0] = 0;
    int status = 0;
    sscanf(reply, "HTTP/1.0 %d", &status);
    return status;
}
static uint8_t dir_pub[32];
static cJSON *list(const char *q) {
    static char buf[256 * 1024];
    char url[200];
    size_t n;
    snprintf(url, sizeof(url), "%s/v1/list%s%s", URL, q ? "?q=" : "", q ? q : "");
    if (http_get(url, buf, sizeof(buf), &n))
        return NULL;
    return dir_open(buf, n, dir_pub, NULL);
}
static int listed(cJSON *p, const char *name) {
    const cJSON *x;
    int n = 0;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(p, "communities")) n +=
        !strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(x, "name")), name);
    return n;
}
static char *announcement(const uint8_t server_key[32], const char *server, uint32_t t, const char *name,
                          const char *invite) {
    uint8_t secret[64], pub[32];
    dir_host_keys(server_key, secret, pub);
    cJSON *p = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(p, "communities");
    cJSON_AddNumberToObject(p, "v", 1);
    cJSON_AddStringToObject(p, "server", server);
    cJSON_AddNumberToObject(p, "time", t);
    if (name) {
        DirCommunity c = {.id = 3, .members = 12, .online = 4, .icon = 2, .color = 5};
        snprintf(c.name, sizeof(c.name), "%s", name);
        snprintf(c.desc, sizeof(c.desc), "Weekly races, all skill levels");
        snprintf(c.host, sizeof(c.host), "Mara");
        snprintf(c.invite, sizeof(c.invite), "%s", invite);
        cJSON_AddItemToArray(a, dir_community_json(&c));
    }
    char *env = dir_seal(p, secret, pub, 1);
    cJSON_Delete(p);
    return env;
}
static int post(const char *path, const char *body) {
    char url[200], reply[256];
    size_t n;
    snprintf(url, sizeof(url), "%s%s", URL, path);
    return http_post(url, body, strlen(body), reply, sizeof(reply), &n);
}
/* app conditions */
static int racing_live(const UiState *s) {
    for (int i = 0; i < s->browse_count; i++)
        if (!strcmp(s->browse[i].name, "Racing Crew") && !strcmp(s->browse[i].host, "Jonas") && s->browse[i].live && s->browse[i].central &&
            s->browse[i].invite[0])
            return 1;
    return 0;
}
static int listed_one(const UiState *s) {
    return s->dir_listed == 1;
}
static int listed_none(const UiState *s) {
    return s->dir_listed == 0;
}
static int joined_racing(const UiState *s) {
    for (int i = 0; i < s->group_count; i++)
        if (!strcmp(s->groups[i].name, "Racing Crew") && s->groups[i].kind == GK_SERVER &&
            s->groups[i].state == LINK_ONLINE && !s->groups[i].own)
            return 1;
    return 0;
}
static int racing_ready(const UiState *s) {
    for (int i = 0; i < s->group_count; i++)
        if (!strcmp(s->groups[i].name, "Racing Crew") && s->groups[i].kind == GK_SERVER && s->groups[i].nch > 0)
            return 1;
    return 0;
}
static int not_busy(const UiState *s) {
    return !s->busy;
}
static int has_code(const UiState *s) {
    return strlen(s->friend_code) == 23;
}
static int dir_reached(const UiState *s) {
    return s->dir_central == 2;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    i18n_set(LANG_EN);
    assert(!system("rm -rf " DDIR " /tmp/orbit-da /tmp/orbit-db && mkdir -p " DDIR " /tmp/orbit-da /tmp/orbit-db"));

    /* ---------- 1. format: keys, envelopes, community checks ---------- */
    uint8_t key1[32], key2[32], s1[64], p1[32], s2[64], p2[32];
    memset(key1, 1, 32);
    memset(key2, 2, 32);
    dir_host_keys(key1, s1, p1);
    dir_host_keys(key1, s2, p2);
    assert(!memcmp(p1, p2, 32)); /* derived, stable */
    dir_host_keys(key2, s2, p2);
    assert(memcmp(p1, p2, 32));
    cJSON *pl = cJSON_CreateObject();
    cJSON_AddStringToObject(pl, "hello", "world");
    char *env = dir_seal(pl, s1, p1, 1);
    uint8_t got[32];
    cJSON *o = dir_open(env, strlen(env), NULL, got);
    assert(o && !memcmp(got, p1, 32));
    cJSON_Delete(o);
    assert(!dir_open(env, strlen(env), p2, NULL)); /* wrong pinned key */
    char *t = strstr(env, "world");
    t[0] = 'W';
    assert(!dir_open(env, strlen(env), NULL, NULL)); /* changed payload */
    free(env);
    cJSON_Delete(pl);
    char inv_pub[ORBIT_INVITE_TEXT], inv_home[ORBIT_INVITE_TEXT];
    assert(!orbit_invite_encode(inv_pub, 0x5db8d822u, 3490, "ABCDEFGH"));  /* 93.184.216.34 */
    assert(!orbit_invite_encode(inv_home, 0xc0a8b21fu, 3490, "ABCDEFGH")); /* 192.168.178.31 */
    cJSON *c = cJSON_Parse("{\"id\":3,\"name\":\"Racing Crew\",\"desc\":\"\",\"host\":\"Jonas\",\"members\":2,"
                           "\"online\":1,\"icon\":0,\"color\":3,\"invite\":\"x\"}");
    DirCommunity dc;
    cJSON_ReplaceItemInObject(c, "invite", cJSON_CreateString(inv_pub));
    assert(!dir_community_parse(c, &dc, 0));
    cJSON_ReplaceItemInObject(c, "invite", cJSON_CreateString(inv_home));
    assert(dir_community_parse(c, &dc, 0) && !dir_community_parse(c, &dc, 1)); /* home address: tests only */
    cJSON_ReplaceItemInObject(c, "name", cJSON_CreateString("Puta Madre"));
    assert(dir_community_parse(c, &dc, 1));
    cJSON_ReplaceItemInObject(c, "name", cJSON_CreateString("Racing Crew"));
    cJSON_ReplaceItemInObject(c, "color", cJSON_CreateNumber(40));
    assert(dir_community_parse(c, &dc, 1));
    cJSON_Delete(c);
    printf("PASS: directory format: host key derived from the server key, signed envelopes (tamper and wrong "
           "key refused), community checks (home addresses, offensive names, ranges)\n");

    /* ---------- 2. the service ---------- */
    run_dird();
    {
        char cmd[600], exe[512];
        assert(getcwd(exe, sizeof(exe)));
        snprintf(cmd, sizeof(cmd), "cd " DDIR " && %s/build/orbit-dird --key dird.key --print-pub", exe);
        FILE *f = popen(cmd, "r");
        char hex[80] = "";
        assert(f && fgets(hex, sizeof(hex), f));
        pclose(f);
        hex[strcspn(hex, "\n")] = 0;
        assert(strlen(hex) == 64 && !orbit_unhex(dir_pub, hex, 32));
        FILE *cfg = fopen("/tmp/orbit-da/directory.cfg", "w");
        fprintf(cfg, "url %s/\npub %s\n", URL, hex);
        fclose(cfg);
        assert(!system("cp /tmp/orbit-da/directory.cfg /tmp/orbit-db/directory.cfg"));
    }
    uint32_t now = orbit_unix_time();
    char *a1 = announcement(key1, "1111222233334444", now, "Racing Crew", inv_home);
    assert(!post("/v1/announce", a1));
    cJSON *l = list(NULL);
    assert(l && listed(l, "Racing Crew") == 1);
    cJSON_Delete(l);
    assert(post("/v1/announce", a1) != 0); /* replayed */
    free(a1);
    a1 = announcement(key2, "1111222233334444", now + 1, "Racing Crew 2", inv_home);
    assert(post("/v1/announce", a1) != 0); /* someone else's key for that server id */
    free(a1);
    a1 = announcement(key2, "5555666677778888", now - 4000, "Old", inv_home);
    assert(post("/v1/announce", a1) != 0); /* stale time */
    free(a1);
    a1 = announcement(key2, "5555666677778888", now, "Fuck Off", inv_home);
    assert(post("/v1/announce", a1) != 0); /* offensive name */
    free(a1);
    a1 = announcement(key2, "5555666677778888", now, "Space Pilots", inv_home);
    assert(!post("/v1/announce", a1));
    free(a1);
    l = list("racing");
    assert(l && listed(l, "Racing Crew") == 1 && !listed(l, "Space Pilots"));
    cJSON_Delete(l);
    l = list("Weekly%20races");
    assert(l && listed(l, "Racing Crew") && listed(l, "Space Pilots"));
    cJSON_Delete(l);
    /* reports: one address counts once, hidden from three */
    const char *rep = "{\"server\":\"5555666677778888\",\"id\":3,\"reason\":0}";
    assert(post_from(2, "/v1/report", rep) == 200 && post_from(2, "/v1/report", rep) == 200);
    assert(post_from(3, "/v1/report", rep) == 200);
    l = list(NULL);
    assert(listed(l, "Space Pilots"));
    cJSON_Delete(l);
    assert(post_from(4, "/v1/report", rep) == 200);
    l = list(NULL);
    assert(!listed(l, "Space Pilots") && listed(l, "Racing Crew"));
    cJSON_Delete(l);
    assert(post_from(2, "/v1/report", "{\"server\":\"zz\",\"id\":3,\"reason\":0}") == 400);
    /* the operator undoes it; the state survives a restart */
    stop_dird();
    {
        char cmd[600], exe[512];
        assert(getcwd(exe, sizeof(exe)));
        snprintf(cmd, sizeof(cmd), "cd " DDIR " && %s/build/orbit-dird --state state.json --unhide 5555666677778888:3 >/dev/null", exe);
        assert(!system(cmd));
    }
    run_dird();
    l = list(NULL);
    assert(listed(l, "Space Pilots") && listed(l, "Racing Crew"));
    cJSON_Delete(l);
    a1 = announcement(key2, "5555666677778888", now + 30, NULL, NULL); /* empty list: gone */
    assert(!post("/v1/announce", a1));
    free(a1);
    l = list(NULL);
    assert(!listed(l, "Space Pilots"));
    cJSON_Delete(l);
    printf("PASS: orbit-dird: signed announcements (replay, foreign key, stale time, offensive names refused), "
           "search, signed list, reports hide after 3 addresses, operator unhide, state kept across restarts\n");

    /* ---------- 3. two apps: A lists its open server, B finds it only through the directory ---------- */
    static OrbitApp A, B;
    assert(!app_start(&A, "/tmp/orbit-da", "127.0.0.1", "255.0.0.0", "", "Jonas", 31521));
    assert(!app_start(&B, "/tmp/orbit-db", "127.0.0.1", "255.0.0.0", "", "Lea", 31522));
    app_set_name(&A, "Jonas");
    app_set_name(&B, "Lea");
    assert(wait_for(&A, has_code, 15000) && wait_for(&B, has_code, 15000));
    assert(wait_for(&A, not_busy, 5000));
    assert(!app_create_server(&A, "Racing Crew", 1));
    assert(wait_for(&A, racing_ready, 15000));
    int sid = -1, slink = -1;
    for (int i = 0; i < view.group_count; i++)
        if (!strcmp(view.groups[i].name, "Racing Crew"))
            sid = view.groups[i].id, slink = view.groups[i].link;
    cJSON *q = app_cmd("server_update", sid);
    cJSON_AddBoolToObject(q, "open", 1);
    assert(wait_for(&A, not_busy, 5000));
    assert(!app_job(&A, slink, q));
    assert(wait_for(&A, not_busy, 5000));
    /* off by default: nothing is listed */
    assert(wait_for(&A, dir_reached, 8000) && view.dir_listed == 0);
    l = list("Racing%20Crew");
    assert(listed(l, "Racing Crew") == 1); /* only the fake one from part 2 */
    cJSON_Delete(l);
    UiSettings set = view.set;
    set.dir_publish = 1;
    app_set_settings(&A, &set);
    assert(wait_for(&A, listed_one, 8000));
    l = list("Racing%20Crew");
    assert(listed(l, "Racing Crew") == 2);
    cJSON_Delete(l);
    app_browse(&B);
    assert(wait_for(&B, racing_live, 10000));
    int idx = -1;
    for (int i = 0; i < view.browse_count; i++)
        if (!strcmp(view.browse[i].name, "Racing Crew") && view.browse[i].live && view.browse[i].link < 0 &&
            view.browse[i].members >= 1 && !strcmp(view.browse[i].host, "Jonas"))
            idx = i;
    for (int i = 0; idx < 0 && i < view.browse_count; i++)
        printf("browse: %s host=%s live=%d link=%d members=%d central=%d\n", view.browse[i].name,
               view.browse[i].host, view.browse[i].live, view.browse[i].link, view.browse[i].members,
               view.browse[i].central);
    assert(idx >= 0);
    {
        uint32_t ip;
        int port;
        char code[9];
        assert(!orbit_invite_decode(view.browse[idx].invite, &ip, &port, code));
        printf("listed join code -> %u.%u.%u.%u:%d\n", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255, port);
    }
    assert(!app_join_public(&B, idx));
    if (!wait_for(&B, joined_racing, 20000)) {
        for (int i = 0; i < view.note_count; i++)
            printf("note: %s\n", view.notes[i].text);
        printf("status: %s\n", view.status);
        assert(0);
    }
    printf("PASS: apps: listing is opt-in, the owner's open server is announced with its join code, a stranger "
           "finds it only through the directory and joins it\n");
    /* report and hide on B goes to the directory */
    view.revision = 0;
    app_snapshot(&B, &view);
    int other = -1;
    for (int i = 0; i < view.browse_count; i++)
        if (!strcmp(view.browse[i].name, "Racing Crew") && view.browse[i].central && strcmp(view.browse[i].host, "Jonas"))
            other = i; /* the fake entry from part 2 */
    assert(other >= 0 && !app_dir_hide(&B, other, 1));
    app_browse(&B); /* the next fetch sends pending reports */
    int sent = 0;
    for (int t = 0; t < 80 && !sent; t++) {
        FILE *f = fopen(DDIR "/reports.log", "r");
        char line[300];
        while (f && fgets(line, sizeof(line), f))
            sent |= strstr(line, "1111222233334444:3") != NULL;
        if (f)
            fclose(f);
        usleep(100000);
    }
    assert(sent);
    /* switching it off removes the listing right away */
    view.revision = 0;
    app_snapshot(&A, &view);
    set = view.set;
    set.dir_publish = 0;
    app_set_settings(&A, &set);
    assert(wait_for(&A, listed_none, 8000));
    l = list("Racing%20Crew");
    assert(listed(l, "Racing Crew") == 1);
    cJSON_Delete(l);
    printf("PASS: apps: report and hide reaches the directory; switching the listing off removes it\n");
    app_stop(&B);
    app_stop(&A);
    stop_dird();
    return 0;
}
