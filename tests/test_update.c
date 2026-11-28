/* signed updates: version order, manifest signature/app/format checks, HTTP redirects,
 * download size and hash verification against a local test server */
#include "cJSON.h"
#include "monocypher.h"
#include "orbit.h"
#include "update.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint8_t secret[64], pub[32];
static int port;
static char pkg[200000];
static char manifest_good[4096], manifest_bad_sig[4096], manifest_bad_hash[4096],
    manifest_other_app[4096];

static void make_manifest(char *out, size_t cap, const char *app, const char *version,
                          const char *url, long size, const uint8_t hash[32], int break_sig) {
    char h[65], payload[1024], sig[129];
    for (int i = 0; i < 32; i++)
        sprintf(h + 2 * i, "%02x", hash[i]);
    snprintf(payload, sizeof(payload),
             "{\"app\":\"%s\",\"version\":\"%s\",\"url\":\"%s\",\"size\":%ld,\"blake2b\":\"%s\","
             "\"date\":\"2026-11-20\",\"notes\":{\"de\":\"Neu: Tests\",\"en\":\"New: tests\"}}",
             app, version, url, size, h);
    uint8_t s[64];
    crypto_eddsa_sign(s, secret, (const uint8_t *)payload, strlen(payload));
    if (break_sig)
        s[5] ^= 1;
    for (int i = 0; i < 64; i++)
        sprintf(sig + 2 * i, "%02x", s[i]);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "payload", payload);
    cJSON_AddStringToObject(m, "sig", sig);
    char *t = cJSON_PrintUnformatted(m);
    snprintf(out, cap, "%s", t);
    free(t);
    cJSON_Delete(m);
}

static void reply(int c, int status, const char *extra, const char *body, long n, long claimed) {
    char head[512];
    int k = snprintf(head, sizeof(head), "HTTP/1.1 %d X\r\n%sContent-Length: %ld\r\n\r\n", status,
                     extra, claimed);
    send(c, head, (size_t)k, MSG_NOSIGNAL);
    for (long sent = 0; sent < n;) {
        long w = send(c, body + sent, (size_t)(n - sent > 7000 ? 7000 : n - sent), MSG_NOSIGNAL);
        if (w <= 0)
            break;
        sent += w;
    }
}
static void *server(void *arg) {
    int s = *(int *)arg;
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0)
            return NULL;
        char req[2048];
        int n = 0, k;
        while (n < (int)sizeof(req) - 1 && (k = (int)recv(c, req + n, sizeof(req) - 1 - n, 0)) > 0) {
            n += k;
            req[n] = 0;
            if (strstr(req, "\r\n\r\n"))
                break;
        }
        req[n] = 0;
        char path[256] = "";
        sscanf(req, "GET %255s", path);
        if (!strcmp(path, "/latest/orbit-update.json"))
            reply(c, 302, "Location: /real/orbit-update.json\r\n", "", 0, 0);
        else if (!strcmp(path, "/real/orbit-update.json"))
            reply(c, 200, "", manifest_good, (long)strlen(manifest_good), (long)strlen(manifest_good));
        else if (!strcmp(path, "/badsig.json"))
            reply(c, 200, "", manifest_bad_sig, (long)strlen(manifest_bad_sig), (long)strlen(manifest_bad_sig));
        else if (!strcmp(path, "/badhash.json"))
            reply(c, 200, "", manifest_bad_hash, (long)strlen(manifest_bad_hash), (long)strlen(manifest_bad_hash));
        else if (!strcmp(path, "/other.json"))
            reply(c, 200, "", manifest_other_app, (long)strlen(manifest_other_app), (long)strlen(manifest_other_app));
        else if (!strcmp(path, "/dl/orbit.pkg")) {
            char loc[128];
            snprintf(loc, sizeof(loc), "Location: http://127.0.0.1:%d/files/orbit.pkg\r\n", port);
            reply(c, 301, loc, "", 0, 0);
        } else if (!strcmp(path, "/files/orbit.pkg"))
            reply(c, 200, "", pkg, sizeof(pkg), sizeof(pkg));
        else if (!strcmp(path, "/files/short.pkg"))
            reply(c, 200, "", pkg, sizeof(pkg) / 2, sizeof(pkg)); /* connection drops early */
        else if (!strcmp(path, "/loop"))
            reply(c, 302, "Location: /loop\r\n", "", 0, 0);
        else
            reply(c, 404, "", "", 0, 0);
        close(c);
    }
}

static long last_progress;
static void progress(void *ctx, long done, long total) {
    (void)ctx;
    assert(done <= total);
    last_progress = done;
}

int main(void) {
    /* versions */
    assert(update_version_cmp("1.2.0", "1.2.0") == 0);
    assert(update_version_cmp("1.2", "1.2.0") == 0);
    assert(update_version_cmp("1.10.0", "1.9.9") > 0);
    assert(update_version_cmp("1.2.1", "1.2.0") > 0);
    assert(update_version_cmp("1.2.0-test3", "1.2.0") < 0);
    assert(update_version_cmp("1.2.0", "1.2.0-test3") > 0);
    assert(update_version_cmp("1.2.0-test10", "1.2.0-test3") > 0);
    assert(update_version_cmp("2.0.0-test1", "1.9.0") > 0);

    uint8_t seed[32];
    for (int i = 0; i < 32; i++)
        seed[i] = (uint8_t)(i * 7 + 1);
    crypto_eddsa_key_pair(secret, pub, seed);
    for (size_t i = 0; i < sizeof(pkg); i++)
        pkg[i] = (char)(i * 31 + (i >> 8));
    uint8_t hash[32], wrong[32];
    crypto_blake2b(hash, 32, (const uint8_t *)pkg, sizeof(pkg));
    memcpy(wrong, hash, 32);
    wrong[0] ^= 0x80;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(s, (struct sockaddr *)&a, sizeof(a)) && !listen(s, 8));
    socklen_t al = sizeof(a);
    getsockname(s, (struct sockaddr *)&a, &al);
    port = ntohs(a.sin_port);
    char url[200], base[64];
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", port);
    snprintf(url, sizeof(url), "%s/dl/orbit.pkg", base);
    make_manifest(manifest_good, sizeof(manifest_good), "orbit-ps3", "1.3.9", url, sizeof(pkg), hash, 0);
    make_manifest(manifest_bad_sig, sizeof(manifest_bad_sig), "orbit-ps3", "1.3.9", url, sizeof(pkg), hash, 1);
    make_manifest(manifest_bad_hash, sizeof(manifest_bad_hash), "orbit-ps3", "1.3.9", url, sizeof(pkg), wrong, 0);
    make_manifest(manifest_other_app, sizeof(manifest_other_app), "other", "1.3.9", url, sizeof(pkg), hash, 0);
    pthread_t t;
    pthread_create(&t, NULL, server, &s);

    /* manifest: offline parsing */
    UpdateInfo info;
    assert(update_parse(manifest_good, strlen(manifest_good), pub, "de", &info) == UPD_OK);
    assert(!strcmp(info.version, "1.3.9") && info.size == (long)sizeof(pkg));
    assert(!strcmp(info.notes, "Neu: Tests") && !strcmp(info.date, "2026-11-20"));
    assert(update_parse(manifest_good, strlen(manifest_good), pub, "it", &info) == UPD_OK);
    assert(!strcmp(info.notes, "New: tests")); /* English fallback */
    assert(update_parse(manifest_bad_sig, strlen(manifest_bad_sig), pub, "en", &info) == UPD_E_SIGNATURE);
    assert(update_parse(manifest_other_app, strlen(manifest_other_app), pub, "en", &info) == UPD_E_APP);
    uint8_t other_pub[32];
    memcpy(other_pub, pub, 32);
    other_pub[3] ^= 4;
    assert(update_parse(manifest_good, strlen(manifest_good), other_pub, "en", &info) == UPD_E_SIGNATURE);
    {
        /* a single changed payload byte (here: the version) breaks the signature */
        char tampered[4096];
        snprintf(tampered, sizeof(tampered), "%s", manifest_good);
        char *v = strstr(tampered, "1.3.9");
        v[4] = '8';
        assert(update_parse(tampered, strlen(tampered), pub, "en", &info) == UPD_E_SIGNATURE);
    }
    assert(update_parse("{}", 2, pub, "en", &info) == UPD_E_FORMAT);
    assert(update_parse("not json", 8, pub, "en", &info) == UPD_E_FORMAT);

    /* over HTTP, with a relative redirect */
    char murl[200];
    snprintf(murl, sizeof(murl), "%s/latest/orbit-update.json", base);
    assert(update_check(murl, pub, "en", &info) == UPD_OK);
    assert(update_version_cmp(info.version, ORBIT_VERSION) > 0);
    snprintf(murl, sizeof(murl), "%s/badsig.json", base);
    UpdateInfo bad;
    assert(update_check(murl, pub, "en", &bad) == UPD_E_SIGNATURE);
    snprintf(murl, sizeof(murl), "%s/missing.json", base);
    assert(update_check(murl, pub, "en", &bad) == UPD_E_HTTP);
    snprintf(murl, sizeof(murl), "%s/loop", base);
    assert(update_check(murl, pub, "en", &bad) == UPD_E_HTTP); /* redirect loop ends */
    assert(update_check("ftp://example/x", pub, "en", &bad) == UPD_E_HTTP);

    /* download: absolute redirect, size and hash verified, then renamed into place */
    const char *out = "build/test-update.pkg";
    remove(out);
    assert(update_download(&info, out, progress, NULL, NULL) == UPD_OK);
    assert(last_progress == (long)sizeof(pkg));
    FILE *f = fopen(out, "rb");
    static char back[sizeof(pkg) + 1];
    assert(f && fread(back, 1, sizeof(back), f) == sizeof(pkg));
    fclose(f);
    assert(!memcmp(back, pkg, sizeof(pkg)));
    /* wrong hash: nothing is left behind */
    remove(out);
    snprintf(murl, sizeof(murl), "%s/badhash.json", base);
    assert(update_check(murl, pub, "en", &bad) == UPD_OK);
    assert(update_download(&bad, out, NULL, NULL, NULL) == UPD_E_HASH);
    assert(!fopen(out, "rb") && !fopen("build/test-update.pkg.part", "rb"));
    /* server sends less than it announced */
    UpdateInfo shortinfo = info;
    snprintf(shortinfo.url, sizeof(shortinfo.url), "%s/files/short.pkg", base);
    assert(update_download(&shortinfo, out, NULL, NULL, NULL) == UPD_E_SIZE);
    assert(!fopen(out, "rb"));
    /* signed size differs from the served Content-Length */
    UpdateInfo sized = info;
    sized.size -= 1;
    assert(update_download(&sized, out, NULL, NULL, NULL) == UPD_E_SIZE);
    /* cancel */
    volatile int cancel = 1;
    assert(update_download(&info, out, NULL, NULL, &cancel) == UPD_E_CANCEL);
    assert(!fopen(out, "rb"));
    printf("PASS: signed updates: versions, signature/app/tamper checks, redirects, size+hash "
           "verified downloads, cancel\n");
    return 0;
}
