/* ORBIT phone upload, see include/upload.h */
#include "upload.h"
#include "image.h"
#include "orbit.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __PPU__
#include <net/net.h>
#include <net/netdb.h>
#define CLOSE(fd) netClose((fd) & ~SOCKET_FD_MASK)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#define CLOSE close
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct OrbitUpload {
    int fd, port, stop;
    volatile int state;
    pthread_t thread;
    pthread_mutex_t lock;
    char token[33];
    char *page;
    uint8_t *data;
    size_t n;
    uint64_t until;
};

static int wait_in(int fd, int ms) {
    struct pollfd p = {fd, POLLIN, 0};
    return poll(&p, 1, ms) > 0 && (p.revents & (POLLIN | POLLHUP));
}
static void send_all(int fd, const char *s, size_t n) {
    while (n) {
        int k = (int)send(fd, s, n, MSG_NOSIGNAL);
        if (k <= 0)
            return;
        s += k;
        n -= (size_t)k;
    }
}
static void reply(int fd, const char *status, const char *type, const char *body) {
    char head[256];
    size_t n = strlen(body);
    int k = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                     status, type, n);
    send_all(fd, head, (size_t)k);
    send_all(fd, body, n);
}
/* one request; returns 1 when a picture was received */
static int serve(OrbitUpload *u, int fd) {
    char head[4096];
    int n = 0;
    char *end = NULL;
    uint64_t deadline = orbit_millis() + 10000;
    while (!end) {
        if (n >= (int)sizeof(head) - 1 || orbit_millis() > deadline || !wait_in(fd, 1000))
            return 0;
        int k = (int)recv(fd, head + n, sizeof(head) - 1 - n, 0);
        if (k <= 0)
            return 0;
        n += k;
        head[n] = 0;
        end = strstr(head, "\r\n\r\n");
    }
    char method[8] = "", path[80] = "";
    if (sscanf(head, "%7s %79s", method, path) != 2)
        return 0;
    char page[40], upload[48];
    snprintf(page, sizeof(page), "/%s", u->token);
    snprintf(upload, sizeof(upload), "/%s/upload", u->token);
    if (!strcmp(method, "GET") && !strcmp(path, page)) {
        reply(fd, "200 OK", "text/html; charset=utf-8", u->page);
        return 0;
    }
    if (strcmp(method, "POST") || strcmp(path, upload)) {
        reply(fd, "404 Not Found", "text/plain", "");
        return 0;
    }
    long length = -1;
    for (char *line = strstr(head, "\r\n"); line && line < end; line = strstr(line + 2, "\r\n"))
        if (!strncasecmp(line + 2, "content-length:", 15))
            length = strtol(line + 17, NULL, 10);
    if (length <= 0 || length > (long)IMG_MAX_UPLOAD) {
        reply(fd, "413 Payload Too Large", "text/plain", "");
        return 0;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) {
        reply(fd, "500 Error", "text/plain", "");
        return 0;
    }
    u->state = UPLOAD_RECEIVING;
    size_t got = (size_t)(n - (int)(end + 4 - head));
    if ((long)got > length)
        got = (size_t)length;
    memcpy(data, end + 4, got);
    deadline = orbit_millis() + 120000;
    while ((long)got < length && !u->stop) {
        if (orbit_millis() > deadline || !wait_in(fd, 1000))
            break;
        int k = (int)recv(fd, data + got, (size_t)length - got, 0);
        if (k <= 0)
            break;
        got += (size_t)k;
        deadline = orbit_millis() + 30000;
    }
    if ((long)got != length) {
        free(data);
        u->state = UPLOAD_WAITING;
        return 0;
    }
    reply(fd, "200 OK", "text/plain", "OK");
    pthread_mutex_lock(&u->lock);
    free(u->data);
    u->data = data;
    u->n = got;
    u->state = UPLOAD_DONE;
    pthread_mutex_unlock(&u->lock);
    return 1;
}
static void *upload_main(void *arg) {
    OrbitUpload *u = arg;
    while (!u->stop && orbit_millis() < u->until) {
        if (!wait_in(u->fd, 250))
            continue;
        int c = accept(u->fd, NULL, NULL);
        if (c < 0)
            continue;
        int done = serve(u, c);
        shutdown(c, SHUT_RDWR);
        CLOSE(c);
        if (done)
            break; /* one picture per opening of the screen */
    }
    /* the token dies with the listener: after one picture or at expiry nothing answers any more */
    CLOSE(u->fd);
    u->fd = -1;
    if (u->state != UPLOAD_DONE)
        u->state = UPLOAD_STOPPED;
    return NULL;
}
static void html_text(char *out, size_t cap, const char *s) {
    size_t o = 0;
    for (; *s && o + 7 < cap; s++) {
        const char *e = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;"
                      : *s == '\'' ? "\xE2\x80\x99" /* typographic: safe in the script too */
                      : *s == '\\' ? "/" : NULL;
        if (e) {
            size_t l = strlen(e);
            memcpy(out + o, e, l);
            o += l;
        } else
            out[o++] = *s;
    }
    out[o] = 0;
}
OrbitUpload *upload_start(int port, const char *title, const char *pick, const char *sent,
                          const char *failed) {
    OrbitUpload *u = calloc(1, sizeof(*u));
    if (!u)
        return NULL;
    uint8_t raw[16];
    if (orbit_random(raw, 16)) {
        free(u);
        return NULL;
    }
    orbit_hex(u->token, raw, 16);
    char t[4][300];
    html_text(t[0], sizeof(t[0]), title);
    html_text(t[1], sizeof(t[1]), pick);
    html_text(t[2], sizeof(t[2]), sent);
    html_text(t[3], sizeof(t[3]), failed);
    u->page = malloc(4096);
    if (!u->page) {
        free(u);
        return NULL;
    }
    snprintf(u->page, 4096,
             "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>ORBIT</title>"
             "<style>body{font-family:system-ui,sans-serif;background:#0d0f13;color:#e8eaee;text-align:center;"
             "padding:32px 20px}h1{font-size:26px;letter-spacing:4px}label{display:inline-block;margin:24px 0;"
             "padding:16px 28px;border-radius:28px;background:#4f8cff;color:#fff;font-size:19px}"
             "input{display:none}p{font-size:17px;color:#aab}</style></head><body><h1>ORBIT</h1>"
             "<p>%s</p><label>%s<input id=f type=file accept=\"image/jpeg,image/png,image/gif\"></label><p id=s></p>"
             "<script>f.onchange=async()=>{const x=f.files[0];if(!x)return;s.textContent='\\u2026';"
             "try{const r=await fetch(location.pathname+'/upload',{method:'POST',body:x});"
             "s.textContent=r.ok?'%s':'%s'}catch(e){s.textContent='%s'}}</script></body></html>",
             t[0], t[1], t[2], t[3], t[3]);
    pthread_mutex_init(&u->lock, NULL);
    u->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (u->fd < 0)
        goto fail;
    int yes = 1;
    setsockopt(u->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
#ifdef __PPU__
    addr.sin_len = sizeof(addr);
#endif
    if (bind(u->fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(u->fd, 2))
        goto fail;
    u->port = port;
    if (!port) { /* tests: any free port */
        socklen_t al = sizeof(addr);
        if (getsockname(u->fd, (struct sockaddr *)&addr, &al))
            goto fail;
        u->port = ntohs(addr.sin_port);
    }
    u->until = orbit_millis() + 10 * 60 * 1000;
    u->state = UPLOAD_WAITING;
    if (pthread_create(&u->thread, NULL, upload_main, u))
        goto fail;
    return u;
fail:
    if (u->fd >= 0)
        CLOSE(u->fd);
    pthread_mutex_destroy(&u->lock);
    free(u->page);
    free(u);
    return NULL;
}
void upload_set_lifetime(OrbitUpload *u, uint32_t ms) {
    if (u)
        u->until = orbit_millis() + ms;
}
int upload_port(const OrbitUpload *u) {
    return u ? u->port : 0;
}
const char *upload_token(const OrbitUpload *u) {
    return u ? u->token : "";
}
int upload_state(OrbitUpload *u) {
    return u ? u->state : UPLOAD_STOPPED;
}
int upload_take(OrbitUpload *u, uint8_t **data, size_t *n) {
    if (!u)
        return 0;
    pthread_mutex_lock(&u->lock);
    int got = u->data != NULL;
    if (got) {
        *data = u->data;
        *n = u->n;
        u->data = NULL;
    }
    pthread_mutex_unlock(&u->lock);
    return got;
}
void upload_stop(OrbitUpload *u) {
    if (!u)
        return;
    u->stop = 1;
    pthread_join(u->thread, NULL);
    if (u->fd >= 0)
        CLOSE(u->fd);
    pthread_mutex_destroy(&u->lock);
    free(u->data);
    free(u->page);
    free(u);
}
