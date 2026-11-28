/* DNS fallback (src/dns_fallback.c) on the PC: query format, reply parsing (compressed and plain
 * names, CNAME before A, errors, wrong id, truncated data), and a real UDP round trip against a
 * fake server on 127.0.0.1 (no internet). */
#include "dns_fallback.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static size_t reply(unsigned char *r, const unsigned char *q, int qlen, int rcode, int cname, int plain_name) {
    memcpy(r, q, (size_t)qlen);
    r[2] = 0x81, r[3] = (unsigned char)(0x80 | rcode);
    r[6] = 0, r[7] = (unsigned char)(cname ? 2 : 1);
    size_t n = (size_t)qlen;
    if (cname) { /* bore.pub CNAME x.example (pointer name) */
        unsigned char c[] = {0xc0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 11, 1, 'x', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0};
        c[11] = 11;
        memcpy(r + n, c, sizeof(c));
        n += sizeof(c);
    }
    if (plain_name) {
        unsigned char nm[] = {4, 'b', 'o', 'r', 'e', 3, 'p', 'u', 'b', 0};
        memcpy(r + n, nm, sizeof(nm));
        n += sizeof(nm);
    } else {
        r[n++] = 0xc0, r[n++] = 12;
    }
    unsigned char a[] = {0, 1, 0, 1, 0, 0, 1, 0, 0, 4, 203, 0, 113, 7};
    memcpy(r + n, a, sizeof(a));
    return n + sizeof(a);
}
static int fake_fd;
static void *fake_server(void *arg) {
    (void)arg;
    unsigned char q[512], r[512];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(fake_fd, q, sizeof(q), 0, (struct sockaddr *)&from, &fl);
    if (n > 0) {
        size_t rn = reply(r, q, (int)n, 0, 1, 0);
        sendto(fake_fd, r, rn, 0, (struct sockaddr *)&from, fl);
    }
    return NULL;
}
int main(void) {
    unsigned char q[300], r[512];
    int n = dns_build_query(q, sizeof(q), "bore.pub", 0x1234);
    static const unsigned char want[] = {0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 4, 'b', 'o', 'r', 'e', 3, 'p', 'u', 'b', 0, 0, 1, 0, 1};
    assert(n == (int)sizeof(want) && !memcmp(q, want, sizeof(want)));
    assert(dns_build_query(q, sizeof(q), "bad..name", 1) < 0 && dns_build_query(q, sizeof(q), "", 1) < 0);
    n = dns_build_query(q, sizeof(q), "bore.pub", 0x1234);
    char ip[16];
    size_t rn = reply(r, q, n, 0, 0, 0);
    uint32_t a = dns_parse_answer(r, rn, q, (size_t)n);
    assert(inet_ntop(AF_INET, &a, ip, sizeof(ip)) && !strcmp(ip, "203.0.113.7"));
    rn = reply(r, q, n, 0, 1, 1); /* CNAME first, plain name */
    assert(dns_parse_answer(r, rn, q, (size_t)n) == a);
    rn = reply(r, q, n, 3, 0, 0); /* NXDOMAIN */
    assert(dns_parse_answer(r, rn, q, (size_t)n) == 0);
    rn = reply(r, q, n, 0, 0, 0);
    r[1] ^= 1; /* wrong id */
    assert(dns_parse_answer(r, rn, q, (size_t)n) == 0);
    r[1] ^= 1;
    for (size_t cut = 0; cut < rn; cut++) /* every truncation is refused, never read past the end */
        assert(dns_parse_answer(r, cut, q, (size_t)n) == 0);
    printf("PASS: dns fallback query/reply (pointer and plain names, CNAME, NXDOMAIN, wrong id, truncation)\n");

    fake_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(fake_fd, (struct sockaddr *)&me, sizeof(me)));
    socklen_t ml = sizeof(me);
    getsockname(fake_fd, (struct sockaddr *)&me, &ml);
    pthread_t t;
    pthread_create(&t, NULL, fake_server, NULL);
    uint32_t got = dns_udp_lookup("bore.pub", "127.0.0.1", ntohs(me.sin_port), 0x4242, 2000);
    pthread_join(t, NULL);
    close(fake_fd);
    assert(got == a);
    assert(dns_udp_lookup("bore.pub", "not-an-ip", 53, 1, 100) == 0);
    printf("PASS: dns fallback over UDP against a local fake server\n");
    return 0;
}
