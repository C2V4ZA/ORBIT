/* ORBIT 1.1 background notifications - plugin logic (see notify_core.h). No libc: everything
 * this file needs comes from watch.c, monocypher and the platform functions. */
#include "notify_core.h"
#include "monocypher.h"
#include "watch.h"

/* ---------- small helpers (no libc) ---------- */
static size_t text_len(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
static int same_text(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void copy_text(char *dst, size_t cap, const char *src, size_t len) {
    size_t n = watch_utf8_cut(src, len, cap - 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = c < 0x20 || c == 0x7f ? ' ' : (char)c; /* no line breaks inside a field */
    }
    dst[n] = 0;
}
static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int parse_hex(uint8_t *out, const char *s, size_t len, size_t n) {
    if (len != n * 2)
        return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_value(s[2 * i]), lo = hex_value(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}
static int parse_uint(const char *s, size_t len, unsigned long max, unsigned long *out) {
    unsigned long v = 0;
    if (!len)
        return -1;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (unsigned long)(s[i] - '0');
        if (v > max)
            return -1;
    }
    *out = v;
    return 0;
}
static int parse_ipv4(const char *s, size_t len, uint32_t *out) {
    uint32_t ip = 0;
    size_t start = 0;
    int parts = 0;
    for (size_t i = 0; i <= len; i++)
        if (i == len || s[i] == '.') {
            unsigned long part;
            if (parts == 4 || parse_uint(s + start, i - start, 255, &part))
                return -1;
            ip = ip << 8 | (uint32_t)part;
            parts++;
            start = i + 1;
        }
    if (parts != 4)
        return -1;
    *out = ip;
    return 0;
}
static void put_number(char *out, size_t cap, size_t *at, unsigned long v) {
    char digits[12];
    int n = 0;
    do
        digits[n++] = (char)('0' + v % 10);
    while ((v /= 10) && n < 11);
    while (n && *at + 1 < cap)
        out[(*at)++] = digits[--n];
}
static void put_text(char *out, size_t cap, size_t *at, const char *s) {
    size_t len = text_len(s);
    size_t room = cap - 1 - *at;
    len = watch_utf8_cut(s, len, room);
    for (size_t i = 0; i < len; i++)
        out[(*at)++] = s[i];
}
/* template with %s server, %c channel, %u sender, %n count */
static void put_template(char *out, size_t cap, size_t *at, const char *t, const NotifyEvent *e,
                         int count) {
    for (; *t && *at + 1 < cap; t++) {
        if (*t == '%' && t[1]) {
            t++;
            if (*t == 's')
                put_text(out, cap, at, e->server);
            else if (*t == 'c')
                put_text(out, cap, at, e->channel_name);
            else if (*t == 'u')
                put_text(out, cap, at, e->sender);
            else if (*t == 'n')
                put_number(out, cap, at, (unsigned long)count);
            else
                out[(*at)++] = *t;
        } else
            out[(*at)++] = *t;
    }
}

/* ---------- config ---------- */
#define FIELDS 10
int notify_parse(NotifyConfig *c, const char *text, size_t len) {
    for (size_t i = 0; i < sizeof(*c); i++)
        ((char *)c)[i] = 0;
    size_t at = 0;
    int line_no = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && text[end] != '\n')
            end++;
        size_t stop = end > at && text[end - 1] == '\r' ? end - 1 : end;
        const char *f[FIELDS];
        size_t fl[FIELDS];
        int nf = 0;
        size_t s = at;
        for (size_t i = at; i <= stop && nf < FIELDS; i++)
            if (i == stop || text[i] == '\t') {
                f[nf] = text + s;
                fl[nf++] = i - s;
                s = i + 1;
            }
        size_t line_start = at;
        at = end + 1;
        if (line_no++ == 0) {
            static const char magic[] = "ORBITNOTIFY";
            if (nf < 2 || fl[0] != 11 || fl[1] != 1 || f[1][0] != '1')
                return -1;
            for (int i = 0; i < 11; i++)
                if (f[0][i] != magic[i])
                    return -1;
            continue;
        }
        if (nf < 2 || stop == line_start)
            continue;
        char key[16];
        copy_text(key, sizeof(key), f[0], fl[0]);
        unsigned long v;
        int *flag = same_text(key, "on")        ? &c->on
                    : same_text(key, "dm")      ? &c->dm
                    : same_text(key, "server")  ? &c->server
                    : same_text(key, "preview") ? &c->preview
                    : same_text(key, "busy")    ? &c->busy
                                                : 0;
        if (flag) {
            if (!parse_uint(f[1], fl[1], 1, &v))
                *flag = (int)v;
            continue;
        }
        char *tmpl = same_text(key, "t_dm")          ? c->t_dm
                     : same_text(key, "t_server")    ? c->t_server
                     : same_text(key, "t_from")      ? c->t_from
                     : same_text(key, "t_many_from") ? c->t_many_from
                     : same_text(key, "t_many")      ? c->t_many
                                                     : 0;
        if (tmpl) {
            copy_text(tmpl, NOTIFY_TEMPLATE, f[1], fl[1]);
            continue;
        }
        if (same_text(key, "host") && nf >= 9 && c->hosts < NOTIFY_HOSTS) {
            NotifyHost *h = &c->host[c->hosts];
            unsigned long port, relay_port;
            if (parse_ipv4(f[1], fl[1], &h->ip) || parse_uint(f[2], fl[2], 65535, &port) ||
                (fl[3] == 1 && f[3][0] == '0' ? (h->relay_ip = 0, 0)
                                              : parse_ipv4(f[3], fl[3], &h->relay_ip)) ||
                parse_uint(f[4], fl[4], 65535, &relay_port) || fl[5] != 16 ||
                parse_hex(h->wid, f[6], fl[6], 8) || parse_hex(h->secret, f[7], fl[7], 32) ||
                fl[8] != 16)
                continue;
            h->port = (int)port;
            h->relay_port = (int)relay_port;
            copy_text(h->tag, sizeof(h->tag), f[5], 16);
            copy_text(h->server, sizeof(h->server), f[8], 16);
            if ((h->ip && h->port) || (h->relay_ip && h->relay_port))
                c->hosts++;
            continue;
        }
        if (same_text(key, "mute") && nf >= 3 && c->mutes < NOTIFY_MUTES && fl[1] == 16 &&
            !parse_uint(f[2], fl[2], 1000, &v)) {
            copy_text(c->mute[c->mutes].server, 17, f[1], 16);
            c->mute[c->mutes++].group = (int)v;
        }
    }
    if (line_no == 0)
        return -1;
    if (!c->t_dm[0])
        copy_text(c->t_dm, NOTIFY_TEMPLATE, "ORBIT", 5);
    if (!c->t_server[0])
        copy_text(c->t_server, NOTIFY_TEMPLATE, "ORBIT - %s #%c", 14);
    if (!c->t_from[0])
        copy_text(c->t_from, NOTIFY_TEMPLATE, "%u", 2);
    if (!c->t_many_from[0])
        copy_text(c->t_many_from, NOTIFY_TEMPLATE, "%n x %u", 7);
    if (!c->t_many[0])
        copy_text(c->t_many, NOTIFY_TEMPLATE, "%n", 2);
    return 0;
}

/* ---------- events ---------- */
int notify_event(NotifyEvent *e, const uint8_t *frame, size_t n) {
    if (n < 2 || frame[0] != 'M')
        return -1;
    const char *f[8];
    size_t fl[8];
    int nf = 0;
    size_t s = 1;
    for (size_t i = 1; i <= n && nf < 8; i++)
        if (i == n || frame[i] == 0x1f) {
            f[nf] = (const char *)frame + s;
            fl[nf++] = i - s;
            s = i + 1;
        }
    unsigned long g, ch;
    if (nf != 7 || fl[0] != 1 || (f[0][0] != 'd' && f[0][0] != 's') ||
        parse_uint(f[1], fl[1], 1000, &g) || parse_uint(f[2], fl[2], 1000, &ch) || !fl[3])
        return -1;
    e->dm = f[0][0] == 'd';
    e->group = (int)g;
    e->channel = (int)ch;
    copy_text(e->sender, sizeof(e->sender), f[3], fl[3]);
    copy_text(e->server, sizeof(e->server), f[4], fl[4]);
    copy_text(e->channel_name, sizeof(e->channel_name), f[5], fl[5]);
    copy_text(e->text, sizeof(e->text), f[6], fl[6]);
    return 0;
}

int notify_wanted(const NotifyConfig *c, const NotifyHost *h, const NotifyEvent *e) {
    if (!c->on || c->busy || (e->dm ? !c->dm : !c->server))
        return 0;
    for (int i = 0; i < c->mutes; i++)
        if (c->mute[i].group == e->group && same_text(c->mute[i].server, h->server))
            return 0;
    return 1;
}

void notify_format(const NotifyConfig *c, const NotifyEvent *first, int count, int same_sender,
                   int same_place, char *out, size_t cap) {
    size_t at = 0;
    if (same_place)
        put_template(out, cap, &at, first->dm ? c->t_dm : c->t_server, first, count);
    else
        put_text(out, cap, &at, "ORBIT");
    if (at + 1 < cap)
        out[at++] = '\n';
    if (count == 1) {
        if (c->preview) {
            put_text(out, cap, &at, first->sender);
            put_text(out, cap, &at, ": ");
            put_text(out, cap, &at, first->text);
        } else
            put_template(out, cap, &at, c->t_from, first, 1);
    } else
        put_template(out, cap, &at, same_sender ? c->t_many_from : c->t_many, first, count);
    out[at] = 0;
}

int notify_app_active(const NotifyPlatform *p) {
    char buf[24];
    int n = p->read_file(NOTIFY_ACTIVE, buf, sizeof(buf) - 1);
    unsigned long t;
    size_t len = 0;
    if (n <= 0)
        return 0;
    while (len < (size_t)n && buf[len] >= '0' && buf[len] <= '9')
        len++;
    if (parse_uint(buf, len, 0xffffffffUL, &t))
        return 0;
    uint32_t now = p->unix_time();
    return now >= t ? now - t < 30 : t - now < 30; /* stale file after a crash: not active */
}

/* ---------- connection ---------- */
static int recv_exact(const NotifyPlatform *p, int fd, uint8_t *buf, size_t n, unsigned ms) {
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
static int dial(const NotifyPlatform *p, const NotifyHost *h) {
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
    char line[48];
    size_t at = 0;
    put_text(line, sizeof(line), &at, "ORBIT-RELAY 1 JOIN ");
    put_text(line, sizeof(line), &at, h->tag);
    line[at++] = '\n';
    uint8_t reply[16];
    size_t got = 0;
    if (p->send(fd, line, at))
        goto fail;
    while (got < sizeof(reply)) { /* "OK\n" or "OFFLINE\n" */
        if (recv_exact(p, fd, reply + got, 1, 12000))
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

typedef struct {
    NotifyEvent first;
    int count, same_sender, same_place;
    uint64_t since;
} Burst;

static void burst_flush(const NotifyPlatform *p, const NotifyConfig *c, Burst *b) {
    if (!b->count)
        return;
    char text[400];
    notify_format(c, &b->first, b->count, b->same_sender, b->same_place, text, sizeof(text));
    p->notify(text);
    b->count = 0;
}
static void burst_add(const NotifyPlatform *p, Burst *b, const NotifyEvent *e) {
    if (!b->count) {
        b->first = *e;
        b->count = 1;
        b->same_sender = b->same_place = 1;
        b->since = p->millis();
        return;
    }
    b->count++;
    if (!same_text(b->first.sender, e->sender))
        b->same_sender = 0;
    if (b->first.dm != e->dm || b->first.group != e->group || b->first.channel != e->channel)
        b->same_place = 0;
}

/* one watch session; returns when the connection ends (or the app starts / stop is set) */
static void session(const NotifyPlatform *p, const NotifyConfig *c, const NotifyHost *h, int fd) {
    static uint8_t frames[NOTIFY_HOSTS][WATCH_FRAME + 18];
    uint8_t hello[WATCH_HELLO], nonce_p[24], nonce_h[24], *wire = frames[h - c->host];
    WatchKeys k;
    p->entropy(nonce_p);
    watch_hello(hello, h->wid, nonce_p);
    if (p->send(fd, hello, sizeof(hello)) || recv_exact(p, fd, nonce_h, 24, 10000))
        return;
    watch_derive(&k, h->secret, nonce_p, nonce_h, 0);
    uint8_t auth[2] = {'A', '1'};
    if (p->send(fd, wire, watch_seal(&k, wire, auth, 2))) {
        watch_wipe(&k);
        return;
    }
    if (p->log)
        p->log("watch connected", (unsigned long)(h - c->host));
    Burst burst = {0};
    size_t have = 0;
    uint64_t last_rx = p->millis(), last_check = 0, last_tx = p->millis();
    uint8_t out[24];
    while (!*p->stop) {
        uint64_t now = p->millis();
        if (now - last_tx >= WATCH_PING_MS) { /* sign of life: the host drops silent plugins */
            uint8_t ping = 'P';
            if (p->send(fd, out, watch_seal(&k, out, &ping, 1)))
                break;
            last_tx = now;
        }
        if (burst.count && now - burst.since >= NOTIFY_BURST_MS)
            burst_flush(p, c, &burst);
        if (now - last_check >= 5000) {
            last_check = now;
            if (notify_app_active(p))
                break; /* the app is open: it shows messages itself */
        }
        if (now - last_rx > WATCH_PING_MS * 5 / 2)
            break; /* host silent: reconnect */
        size_t want = have < 18 ? 18 : 18 + (size_t)watch_frame_length(wire);
        int r = p->recv(fd, wire + have, want - have, 500);
        if (r < 0)
            break;
        if (!r)
            continue;
        have += (size_t)r;
        if (have == 18 && watch_frame_length(wire) < 0)
            break;
        if (have < 18 || have < 18 + (size_t)watch_frame_length(wire))
            continue;
        size_t n = have - 18;
        have = 0;
        last_rx = p->millis();
        uint8_t plain[WATCH_FRAME];
        if (watch_open(&k, plain, wire, wire + 18, n))
            break;
        NotifyEvent e;
        if (!notify_event(&e, plain, n) && notify_wanted(c, h, &e))
            burst_add(p, &burst, &e);
    }
    if (!notify_app_active(p))
        burst_flush(p, c, &burst);
    watch_wipe(&k);
}

void notify_worker(const NotifyPlatform *p, int index) {
    static char files[NOTIFY_HOSTS][8192];
    static NotifyConfig configs[NOTIFY_HOSTS];
    char *file = files[index];
    NotifyConfig *c = &configs[index];
    unsigned backoff = 5000;
    int last_reason = -1;
    while (!*p->stop) {
        int n = p->read_file(NOTIFY_CONFIG, file, sizeof(files[0]) - 1);
        /* why this worker waits (logged once per change): 1 no config file, 2 unreadable config,
         * 3 notifications switched off, 4 no host for this slot, 5 the app is open */
        int reason = n <= 0                                  ? 1
                     : notify_parse(c, file, (size_t)n)      ? 2
                     : !c->on                                ? 3
                     : index >= c->hosts                     ? 4
                     : notify_app_active(p)                  ? 5
                                                             : 0;
        if (reason != last_reason && p->log && (index == 0 || reason == 0 || reason == 5))
            p->log(reason ? "waiting, reason" : "config ok, hosts", reason ? (unsigned long)reason
                                                                          : (unsigned long)c->hosts);
        last_reason = reason;
        if (reason) {
            p->sleep_ms(reason == 5 ? 5000 : 15000);
            continue;
        }
        const NotifyHost *h = &c->host[index];
        int fd = dial(p, h);
        if (fd < 0) {
            if (p->log)
                p->log("host unreachable", (unsigned long)index);
            p->sleep_ms(backoff);
            backoff = backoff * 2 > 120000 ? 120000 : backoff * 2;
            continue;
        }
        uint64_t began = p->millis();
        session(p, c, h, fd);
        p->close(fd);
        crypto_wipe(c, sizeof(*c));
        if (p->millis() - began > 60000)
            backoff = 5000; /* it worked for a while: retry soon */
        else {
            p->sleep_ms(backoff);
            backoff = backoff * 2 > 120000 ? 120000 : backoff * 2;
        }
    }
}
