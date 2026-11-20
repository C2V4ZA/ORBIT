/* ORBIT 1.2 presence plugin loop (see presence.h / presence_worker.h). Freestanding. */
#include "presence_worker.h"
#include "watch.h"

static int pw_recv_exact(const NotifyPlatform *p, int fd, uint8_t *buf, size_t n, unsigned ms) {
    uint64_t end = p->millis() + ms;
    size_t got = 0;
    while (got < n) {
        uint64_t now = p->millis();
        if (now >= end || *p->stop)
            return -1;
        int k = p->recv(fd, buf + got, n - got, (unsigned)(end - now));
        if (k < 0)
            return -1;
        got += (size_t)k;
    }
    return 0;
}
/* direct, or through the relay (same rules as the notification plugin) */
static int pw_dial(const NotifyPlatform *p, const NotifyHost *h) {
    if (h->ip && h->port) {
        int fd = p->connect(h->ip, h->port);
        if (fd >= 0)
            return fd;
    }
    if (!h->relay_ip || !h->relay_port)
        return -1;
    int fd = p->connect(h->relay_ip, h->relay_port);
    if (fd < 0)
        return -1;
    static const char head[] = "ORBIT-RELAY 1 JOIN ";
    char line[48];
    size_t at = 0;
    for (size_t i = 0; head[i]; i++)
        line[at++] = head[i];
    for (size_t i = 0; i < 16 && h->tag[i]; i++)
        line[at++] = h->tag[i];
    line[at++] = '\n';
    uint8_t reply[16];
    size_t got = 0;
    if (p->send(fd, line, at))
        goto fail;
    while (got < sizeof(reply)) {
        if (pw_recv_exact(p, fd, reply + got, 1, 12000))
            goto fail;
        if (reply[got++] == '\n')
            break;
    }
    if (got == 3 && reply[0] == 'O' && reply[1] == 'K')
        return fd;
fail:
    p->close(fd);
    return -1;
}
int presence_send(const NotifyPlatform *p, const NotifyHost *h, const PresenceGame *g) {
    uint8_t hello[WATCH_HELLO], nonce_p[24], nonce_h[24], body[96], wire[96 + 18];
    size_t n = presence_frame(body, sizeof(body), g);
    if (!n)
        return -1;
    int fd = pw_dial(p, h);
    if (fd < 0)
        return -1;
    int rc = -1;
    WatchKeys k;
    p->entropy(nonce_p);
    watch_hello(hello, h->wid, nonce_p);
    if (p->send(fd, hello, sizeof(hello)) || pw_recv_exact(p, fd, nonce_h, 24, 10000)) {
        p->close(fd);
        return -1;
    }
    watch_derive(&k, h->secret, nonce_p, nonce_h, 0);
    uint8_t auth[2] = {'A', '2'};
    if (!p->send(fd, wire, watch_seal(&k, wire, auth, 2)) && !p->send(fd, wire, watch_seal(&k, wire, body, n)) &&
        !pw_recv_exact(p, fd, wire, 18, 10000) && watch_frame_length(wire) == 1 &&
        !pw_recv_exact(p, fd, wire + 18, 1, 5000)) {
        uint8_t ack;
        rc = watch_open(&k, &ack, wire, wire + 18, 1) || ack != 'K' ? -1 : 0;
    }
    watch_wipe(&k);
    p->close(fd);
    return rc;
}
int presence_enabled(const NotifyPlatform *p) {
    char buf[128];
    int n = p->read_file(PRESENCE_CONFIG, buf, sizeof(buf) - 1);
    if (n < 15)
        return 0;
    buf[n] = 0;
    static const char magic[] = "ORBITPRESENCE";
    for (int i = 0; magic[i]; i++)
        if (buf[i] != magic[i])
            return 0;
    for (int i = 0; i + 7 < n; i++)
        if (buf[i] == 's' && buf[i + 1] == 'h' && buf[i + 2] == 'a' && buf[i + 3] == 'r' && buf[i + 4] == 'e' &&
            (buf[i + 5] == ' ' || buf[i + 5] == '\t') && (i == 0 || buf[i - 1] == '\n'))
            return buf[i + 6] == '1';
    return 0;
}
static int same(const PresenceGame *a, const PresenceGame *b) {
    if (a->playing != b->playing)
        return 0;
    for (size_t i = 0; i < sizeof(a->id); i++) {
        if (a->id[i] != b->id[i])
            return 0;
        if (!a->id[i])
            break;
    }
    for (size_t i = 0; i < sizeof(a->name); i++) {
        if (a->name[i] != b->name[i])
            return 0;
        if (!a->name[i])
            break;
    }
    return 1;
}
static void report(const NotifyPlatform *p, const PresenceGame *g) {
    static char text[8192];
    static NotifyConfig c;
    int n = p->read_file(NOTIFY_CONFIG, text, sizeof(text));
    if (n <= 0 || notify_parse(&c, text, (size_t)n))
        return;
    for (int i = 0; i < c.hosts && !*p->stop; i++) {
        int rc = presence_send(p, &c.host[i], g);
        if (p->log)
            p->log(rc ? "presence not stored" : "presence stored", (unsigned long)i);
    }
}
void presence_worker(const NotifyPlatform *p, PresenceDetect detect) {
    PresenceGame last = {0};
    uint64_t sent_at = 0;
    while (!*p->stop) {
        PresenceGame g = {0};
        if (!presence_enabled(p) || notify_app_active(p) || detect(&g))
            g.playing = 0; /* sharing off, ORBIT itself is open, or detection failed: no game */
        if (!g.playing)
            g.id[0] = g.name[0] = 0;
        uint64_t now = p->millis();
        if (!same(&g, &last) ? (g.playing || last.playing) : (g.playing && now - sent_at >= PRESENCE_REFRESH_MS)) {
            report(p, &g);
            sent_at = p->millis();
        }
        last = g;
        for (unsigned waited = 0; waited < PRESENCE_POLL_MS && !*p->stop; waited += 250)
            p->sleep_ms(250);
    }
}
