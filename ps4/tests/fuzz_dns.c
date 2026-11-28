/* Fuzzing of the DNS reply parser (src/dns_fallback.c) with AddressSanitizer + UBSan: replies
 * from the network are attacker-controlled. Mutations of a valid reply (bit flips, byte changes,
 * pointer loops, length bytes, truncation); every input is copied to an exact-size buffer so a
 * read past the end is reported. `make fuzz` in orbit-ps4. */
#include "dns_fallback.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return (uint32_t)(rng >> 11);
}
int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 200000;
    unsigned char q[300];
    int qn = dns_build_query(q, sizeof(q), "bore.pub", 0x1234);
    /* valid reply: CNAME (pointer name) then A */
    unsigned char base[512];
    memcpy(base, q, (size_t)qn);
    base[2] = 0x81, base[3] = 0x80, base[7] = 2;
    size_t n = (size_t)qn;
    static const unsigned char cname[] = {0xc0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 11, 1, 'x', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0};
    memcpy(base + n, cname, sizeof(cname));
    n += sizeof(cname);
    static const unsigned char a[] = {0xc0, 12, 0, 1, 0, 1, 0, 0, 1, 0, 0, 4, 203, 0, 113, 7};
    memcpy(base + n, a, sizeof(a));
    n += sizeof(a);
    if (!dns_parse_answer(base, n, q, (size_t)qn)) {
        puts("FAIL: seed reply not accepted");
        return 1;
    }
    long accepted = 0;
    unsigned char b[1024];
    for (long i = 0; i < iters; i++) {
        size_t m = n;
        memcpy(b, base, n);
        int rounds = 1 + (int)(rnd() % 6);
        for (int r = 0; r < rounds; r++) {
            size_t p = m ? rnd() % m : 0;
            switch (rnd() % 7) {
            case 0: if (m) b[p] ^= (unsigned char)(1u << (rnd() % 8)); break;
            case 1: if (m) b[p] = (unsigned char)rnd(); break;
            case 2: if (m) m = rnd() % m; break;
            case 3: if (m + 1 < sizeof(b)) { memmove(b + p + 1, b + p, m - p); b[p] = (unsigned char)rnd(); m++; } break;
            case 4: if (m > 1) { b[p] = 0xc0; b[(p + 1) % m] = (unsigned char)(rnd() % m); } break; /* pointer, maybe a loop */
            case 5: if (m) b[p] = (unsigned char)(rnd() % 64); break; /* label length */
            case 6: if (m + 40 < sizeof(b)) { memset(b + m, (int)(rnd() & 0xff), 40); m += 40; } break;
            }
        }
        unsigned char *exact = malloc(m ? m : 1);
        memcpy(exact, b, m);
        if (dns_parse_answer(exact, m, q, (size_t)qn))
            accepted++;
        free(exact);
    }
    printf("PASS: dns reply fuzzing, %ld mutated replies (%ld still accepted), no crash, no sanitizer report\n",
           iters, accepted);
    return 0;
}
