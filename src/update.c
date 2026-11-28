/* Signed updates: manifest verification, version comparison and a small HTTP(S) downloader.
 * See include/update.h for the trust model. */
#include "update.h"
#include "utf8copy.h"
#include "cJSON.h"
#include "monocypher.h"
#include "orbit.h"
#include "update_key.h"
#include <ctype.h>
#include <errno.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __PPU__
#include <net/net.h>
#include <net/netdb.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

const uint8_t update_public_key[32] = UPDATE_PUBLIC_KEY;

/* ---------- versions ---------- */
/* natural order: digit runs compare as numbers */
static int natural(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            char *ea, *eb;
            unsigned long x = strtoul(a, &ea, 10), y = strtoul(b, &eb, 10);
            if (x != y)
                return x < y ? -1 : 1;
            a = ea, b = eb;
            continue;
        }
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y)
            return x < y ? -1 : 1;
        a++, b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}
int update_version_cmp(const char *a, const char *b) {
    for (int part = 0; part < 4; part++) {
        unsigned long x = 0, y = 0;
        if (isdigit((unsigned char)*a))
            x = strtoul(a, (char **)&a, 10);
        if (isdigit((unsigned char)*b))
            y = strtoul(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == '.')
            a++;
        if (*b == '.')
            b++;
    }
    const char *sa = strchr(a, '-'), *sb = strchr(b, '-');
    if (!sa && !sb)
        return 0;
    if (!sa || !sb) /* a test build ("-test3") is older than the release */
        return sa ? -1 : 1;
    return natural(sa + 1, sb + 1);
}

/* ---------- manifest ---------- */
static int unhex(uint8_t *out, size_t n, const char *s) {
    if (!s || strlen(s) != n * 2)
        return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (!isxdigit((unsigned char)s[2 * i]) || !isxdigit((unsigned char)s[2 * i + 1]) ||
            sscanf(s + 2 * i, "%2x", &v) != 1)
            return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}
static const char *str(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static int safe_text(const char *s) {
    for (; *s; s++)
        if ((unsigned char)*s < 32 && *s != '\n')
            return 0;
    return 1;
}
int update_parse(const char *manifest, size_t n, const uint8_t key[32], const char *lang,
                 UpdateInfo *out) {
    memset(out, 0, sizeof(*out));
    if (!manifest || n > UPDATE_MANIFEST_MAX)
        return UPD_E_FORMAT;
    cJSON *outer = cJSON_ParseWithLength(manifest, n);
    if (!outer)
        return UPD_E_FORMAT;
    int rc = UPD_E_FORMAT;
    cJSON *inner = NULL;
    const char *payload = str(outer, "payload"), *sig = str(outer, "sig");
    uint8_t signature[64];
    if (!payload || unhex(signature, 64, sig))
        goto done;
    if (crypto_eddsa_check(signature, key, (const uint8_t *)payload, strlen(payload))) {
        rc = UPD_E_SIGNATURE;
        goto done;
    }
    /* everything below is signed */
    inner = cJSON_Parse(payload);
    if (!inner)
        goto done;
    const char *app = str(inner, "app"), *version = str(inner, "version"), *url = str(inner, "url"),
               *hash = str(inner, "blake2b"), *date = str(inner, "date");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(inner, "size");
    if (!app || strcmp(app, "orbit-ps3")) {
        rc = UPD_E_APP;
        goto done;
    }
    if (!version || strlen(version) >= sizeof(out->version) || !url ||
        strlen(url) >= sizeof(out->url) || (strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) ||
        !cJSON_IsNumber(size) || size->valuedouble < 1 || size->valuedouble > UPDATE_PKG_MAX ||
        unhex(out->hash, 32, hash) || !safe_text(version) || !safe_text(url))
        goto done;
    orbit_copy(out->version, sizeof(out->version), version);
    orbit_copy(out->url, sizeof(out->url), url);
    out->size = (long)size->valuedouble;
    if (date && strlen(date) < sizeof(out->date) && safe_text(date))
        orbit_copy(out->date, sizeof(out->date), date);
    const cJSON *notes = cJSON_GetObjectItemCaseSensitive(inner, "notes");
    const char *note = notes ? str(notes, lang ? lang : "en") : NULL;
    if (!note && notes)
        note = str(notes, "en");
    if (note && safe_text(note))
        orbit_copy(out->notes, sizeof(out->notes), note);
    rc = UPD_OK;
done:
    cJSON_Delete(inner);
    cJSON_Delete(outer);
    crypto_wipe(signature, sizeof(signature));
    return rc;
}

/* ---------- HTTP(S) ---------- */
typedef struct {
    int fd, tls;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    uint64_t deadline;
} Conn;
static int wait_fd(int fd, int writing, int ms) {
    struct pollfd p = {fd, writing ? POLLOUT : POLLIN, 0};
    return poll(&p, 1, ms) > 0 && (p.revents & (writing ? POLLOUT : POLLIN | POLLHUP));
}
static int bio_send(void *ctx, const unsigned char *buf, size_t n) {
    Conn *c = ctx;
    if (orbit_millis() > c->deadline || !wait_fd(c->fd, 1, 3000))
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int k = (int)send(c->fd, buf, n, MSG_NOSIGNAL);
    return k > 0 ? k : MBEDTLS_ERR_NET_SEND_FAILED;
}
static int bio_recv(void *ctx, unsigned char *buf, size_t n) {
    Conn *c = ctx;
    if (orbit_millis() > c->deadline || !wait_fd(c->fd, 0, 3000))
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int k = (int)recv(c->fd, buf, n, 0);
    return k >= 0 ? k : MBEDTLS_ERR_NET_RECV_FAILED;
}
static int rng(void *ctx, unsigned char *out, size_t n) {
    (void)ctx;
    return orbit_random(out, n);
}
static void conn_close(Conn *c) {
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
    }
    orbit_socket_close(c->fd);
    c->fd = -1;
}
static int conn_write(Conn *c, const char *s, size_t n) {
    while (n) {
        int k = c->tls ? mbedtls_ssl_write(&c->ssl, (const unsigned char *)s, n)
                       : bio_send(c, (const unsigned char *)s, n);
        if (k <= 0)
            return -1;
        s += k;
        n -= (size_t)k;
    }
    return 0;
}
/* >0 bytes, 0 end of stream, <0 error */
static int conn_read(Conn *c, char *buf, size_t n) {
    if (!c->tls)
        return bio_recv(c, (unsigned char *)buf, n);
    int k = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, n);
    if (k == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return k;
}
typedef struct {
    int tls, port;
    char host[ORBIT_HOST], path[UPDATE_URL_MAX];
} Url;
static int parse_url(const char *u, Url *out) {
    memset(out, 0, sizeof(*out));
    if (!strncmp(u, "https://", 8))
        out->tls = 1, u += 8, out->port = 443;
    else if (!strncmp(u, "http://", 7))
        u += 7, out->port = 80;
    else
        return -1;
    size_t h = strcspn(u, ":/?#");
    if (!h || h >= sizeof(out->host))
        return -1;
    memcpy(out->host, u, h);
    for (size_t i = 0; i < h; i++)
        if (!isalnum((unsigned char)out->host[i]) && out->host[i] != '.' && out->host[i] != '-')
            return -1;
    u += h;
    if (*u == ':') {
        char *end;
        long p = strtol(u + 1, &end, 10);
        if (p < 1 || p > 65535)
            return -1;
        out->port = (int)p;
        u = end;
    }
    if (*u == '#')
        u = "";
    snprintf(out->path, sizeof(out->path), "%s%s", *u == '/' ? "" : "/", u);
    char *frag = strchr(out->path, '#');
    if (frag)
        *frag = 0;
    for (const char *p = out->path; *p; p++)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127)
            return -1;
    return 0;
}
static int conn_open(Conn *c, const Url *u) {
    memset(c, 0, sizeof(*c));
    c->deadline = orbit_millis() + 60000;
    c->fd = orbit_tcp_connect(u->host, u->port, NULL, 0);
    if (c->fd < 0)
        return -1;
    if (!u->tls)
        return 0;
    c->tls = 1;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    /* Authenticity comes from the signed manifest, not from certificates: the console has no
     * maintained CA store, so the server certificate is not checked. */
    if (mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT))
        goto fail;
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, rng, NULL);
    mbedtls_ssl_conf_min_version(&c->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) || mbedtls_ssl_set_hostname(&c->ssl, u->host))
        goto fail;
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
    if (mbedtls_ssl_handshake(&c->ssl))
        goto fail;
    return 0;
fail:
    conn_close(c);
    return -1;
}
static int ci_prefix(const char *line, const char *name) {
    size_t n = strlen(name);
    return !strncasecmp(line, name, n);
}
/* Sends the request, follows redirects and leaves c positioned at the body.
 * body/body_n: bytes of the body already read with the header. */
static int open_body(const char *url, const char *method, const char *payload, size_t payload_n, Conn *c,
                     long *length, char *extra, int *extra_n) {
    char current[UPDATE_URL_MAX];
    orbit_copy(current, sizeof(current), url);
    for (int hop = 0; hop < 6; hop++) {
        Url u;
        if (parse_url(current, &u))
            return UPD_E_HTTP;
        if (conn_open(c, &u))
            return UPD_E_NET;
        char req[UPDATE_URL_MAX + 320];
        int rn = payload ? snprintf(req, sizeof(req),
                                    "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ORBIT-PS3/" ORBIT_VERSION
                                    "\r\nContent-Type: application/json\r\nContent-Length: %zu"
                                    "\r\nConnection: close\r\n\r\n",
                                    method, u.path, u.host, payload_n)
                         : snprintf(req, sizeof(req),
                                    "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ORBIT-PS3/" ORBIT_VERSION
                                    "\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                                    method, u.path, u.host);
        if (rn <= 0 || rn >= (int)sizeof(req) || conn_write(c, req, (size_t)rn) ||
            (payload && payload_n && conn_write(c, payload, payload_n))) {
            conn_close(c);
            return UPD_E_NET;
        }
        char head[8192];
        int n = 0;
        char *end = NULL;
        while (!end) {
            if (n >= (int)sizeof(head) - 1) {
                conn_close(c);
                return UPD_E_HTTP;
            }
            int k = conn_read(c, head + n, sizeof(head) - 1 - n);
            if (k <= 0) {
                conn_close(c);
                return UPD_E_NET;
            }
            n += k;
            head[n] = 0;
            end = strstr(head, "\r\n\r\n");
        }
        *end = 0;
        int status = 0;
        if (sscanf(head, "HTTP/%*d.%*d %d", &status) != 1) {
            conn_close(c);
            return UPD_E_HTTP;
        }
        char location[UPDATE_URL_MAX] = "";
        *length = -1;
        for (char *line = strstr(head, "\r\n"); line; line = strstr(line, "\r\n")) {
            line += 2;
            if (ci_prefix(line, "content-length:"))
                *length = strtol(line + 15, NULL, 10);
            else if (ci_prefix(line, "location:")) {
                const char *v = line + 9;
                while (*v == ' ')
                    v++;
                size_t l = strcspn(v, "\r\n");
                if (l < sizeof(location)) {
                    memcpy(location, v, l);
                    location[l] = 0;
                }
            }
        }
        if (!payload && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            conn_close(c);
            if (!location[0])
                return UPD_E_HTTP;
            if (location[0] == '/') { /* relative redirect: same scheme and host */
                char base[ORBIT_HOST + 24];
                snprintf(base, sizeof(base), "%s://%s:%d", u.tls ? "https" : "http", u.host, u.port);
                if (strlen(base) + strlen(location) >= sizeof(current))
                    return UPD_E_HTTP;
                {
                    size_t bl = strlen(base);
                    memcpy(current, base, bl);
                    memcpy(current + bl, location, strlen(location) + 1);
                }
            } else
                orbit_copy(current, sizeof(current), location);
            continue;
        }
        if (status != 200) {
            conn_close(c);
            return UPD_E_HTTP;
        }
        char *body = end + 4;
        *extra_n = n - (int)(body - head);
        memcpy(extra, body, (size_t)*extra_n);
        return UPD_OK;
    }
    return UPD_E_HTTP;
}
static int http_request(const char *url, const char *method, const char *payload, size_t payload_n, char *buf,
                        size_t cap, size_t *len) {
    Conn c;
    long length;
    char extra[8192];
    int extra_n = 0;
    *len = 0;
    int rc = open_body(url, method, payload, payload_n, &c, &length, extra, &extra_n);
    if (rc)
        return rc;
    if ((size_t)extra_n > cap || (length >= 0 && (size_t)length > cap)) {
        conn_close(&c);
        return UPD_E_SIZE;
    }
    memcpy(buf, extra, (size_t)extra_n);
    size_t n = (size_t)extra_n;
    while (length < 0 || (long)n < length) {
        if (n == cap) {
            conn_close(&c);
            return UPD_E_SIZE;
        }
        int k = conn_read(&c, buf + n, cap - n);
        if (k == 0 && length < 0)
            break;
        if (k <= 0) {
            conn_close(&c);
            return UPD_E_NET;
        }
        n += (size_t)k;
    }
    conn_close(&c);
    *len = n;
    return UPD_OK;
}
int http_get(const char *url, char *buf, size_t cap, size_t *len) {
    return http_request(url, "GET", NULL, 0, buf, cap, len);
}
int http_post(const char *url, const char *body, size_t n, char *buf, size_t cap, size_t *len) {
    return http_request(url, "POST", body, n, buf, cap, len);
}
int update_check(const char *url, const uint8_t key[32], const char *lang, UpdateInfo *out) {
    char *buf = malloc(UPDATE_MANIFEST_MAX);
    size_t n = 0;
    if (!buf)
        return UPD_E_NET;
    int rc = http_get(url, buf, UPDATE_MANIFEST_MAX, &n);
    if (!rc)
        rc = update_parse(buf, n, key, lang, out);
    free(buf);
    return rc;
}
int update_download(const UpdateInfo *info, const char *path, UpdateProgress progress, void *ctx,
                    volatile int *cancel) {
    char part[512];
    if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part))
        return UPD_E_FILE;
    Conn c;
    long length;
    static char extra[8192];
    int extra_n = 0;
    int rc = open_body(info->url, "GET", NULL, 0, &c, &length, extra, &extra_n);
    if (rc)
        return rc;
    if (length >= 0 && length != info->size) {
        conn_close(&c);
        return UPD_E_SIZE;
    }
    FILE *f = fopen(part, "wb");
    if (!f) {
        conn_close(&c);
        return UPD_E_FILE;
    }
    crypto_blake2b_ctx h;
    crypto_blake2b_init(&h, 32);
    long done = 0;
    static char chunk[16384];
    char *data = extra;
    int k = extra_n;
    rc = UPD_OK;
    for (;;) {
        if (k > 0) {
            if (done + k > info->size) {
                rc = UPD_E_SIZE;
                break;
            }
            crypto_blake2b_update(&h, (const uint8_t *)data, (size_t)k);
            if (fwrite(data, 1, (size_t)k, f) != (size_t)k) {
                rc = UPD_E_FILE;
                break;
            }
            done += k;
            if (progress)
                progress(ctx, done, info->size);
        }
        if (done == info->size)
            break;
        if (cancel && *cancel) {
            rc = UPD_E_CANCEL;
            break;
        }
        c.deadline = orbit_millis() + 30000; /* idle limit, not a total limit */
        k = conn_read(&c, chunk, sizeof(chunk));
        data = chunk;
        if (k == 0) {
            rc = UPD_E_SIZE;
            break;
        }
        if (k < 0) {
            rc = UPD_E_NET;
            break;
        }
    }
    conn_close(&c);
    uint8_t got[32];
    crypto_blake2b_final(&h, got);
    if (fclose(f) && !rc)
        rc = UPD_E_FILE;
    if (!rc && crypto_verify32(got, info->hash))
        rc = UPD_E_HASH;
    if (!rc) {
        remove(path);
        if (rename(part, path))
            rc = UPD_E_FILE;
    }
    if (rc)
        remove(part);
    return rc;
}
