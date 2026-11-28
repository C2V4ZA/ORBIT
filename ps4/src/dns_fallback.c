/* Plain DNS A lookup over UDP (RFC 1035), used on the PS4 only when the system resolver refuses
 * a name (crossplay needs bore.pub). Only the host name is sent. POSIX only, so it is tested on
 * the PC against a local fake server (tests/test_dns.c). */
#include "dns_fallback.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int dns_build_query(unsigned char *q, size_t cap, const char *name, unsigned id) {
    size_t n = 0, len = name ? strlen(name) : 0;
    if (len == 0 || len > 250 || cap < len + 18)
        return -1;
    unsigned char head[12] = {(unsigned char)(id >> 8), (unsigned char)id, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(q, head, 12);
    n = 12;
    const char *p = name;
    while (*p) { /* labels */
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        if (l == 0 || l > 63)
            return -1;
        q[n++] = (unsigned char)l;
        memcpy(q + n, p, l);
        n += l;
        p += l + (dot ? 1 : 0);
    }
    q[n++] = 0;
    q[n++] = 0, q[n++] = 1; /* type A */
    q[n++] = 0, q[n++] = 1; /* class IN */
    return (int)n;
}
uint32_t dns_parse_answer(const unsigned char *r, size_t got, const unsigned char *q, size_t qlen) {
    if (got < 12 || qlen < 12 || got < qlen || r[0] != q[0] || r[1] != q[1] || !(r[2] & 0x80) ||
        (r[3] & 0x0f) != 0)
        return 0;
    if (memcmp(r + 12, q + 12, qlen - 12)) /* the reply repeats our question */
        return 0;
    int an = r[6] << 8 | r[7];
    size_t at = qlen;
    for (int i = 0; i < an; i++) {
        if (at >= got)
            return 0;
        if ((r[at] & 0xc0) == 0xc0)
            at += 2;
        else {
            while (at < got && r[at]) {
                if (r[at] & 0xc0)
                    return 0;
                at += (size_t)r[at] + 1;
            }
            at++;
        }
        if (at + 10 > got)
            return 0;
        int type = r[at] << 8 | r[at + 1], rdlen = r[at + 8] << 8 | r[at + 9];
        at += 10;
        if (at + (size_t)rdlen > got)
            return 0;
        if (type == 1 && rdlen == 4) {
            uint32_t a;
            memcpy(&a, r + at, 4);
            return a;
        }
        at += (size_t)rdlen;
    }
    return 0;
}
uint32_t dns_udp_lookup(const char *name, const char *server, int port, unsigned id, int timeout_ms) {
    unsigned char q[300], r[512];
    int n = dns_build_query(q, sizeof(q), name, id);
    if (n < 0)
        return 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
#ifdef ORBIT_PS4
    to.sin_len = sizeof(to);
#endif
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    uint32_t found = 0;
    if (inet_pton(AF_INET, server, &to.sin_addr) == 1 &&
        sendto(fd, q, (size_t)n, 0, (struct sockaddr *)&to, sizeof(to)) == (ssize_t)n) {
        struct pollfd pf = {fd, POLLIN, 0};
        if (poll(&pf, 1, timeout_ms) == 1) {
            ssize_t got = recv(fd, r, sizeof(r), 0);
            if (got > 0)
                found = dns_parse_answer(r, (size_t)got, q, (size_t)n);
        }
    }
    close(fd);
    return found;
}
