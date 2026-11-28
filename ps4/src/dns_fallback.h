#ifndef ORBIT_DNS_FALLBACK_H
#define ORBIT_DNS_FALLBACK_H
#include <stddef.h>
#include <stdint.h>
/* build an A query for name; returns its length or -1 */
int dns_build_query(unsigned char *q, size_t cap, const char *name, unsigned id);
/* first IPv4 address of a reply to query q (network byte order), 0 if none / invalid */
uint32_t dns_parse_answer(const unsigned char *r, size_t got, const unsigned char *q, size_t qlen);
/* ask server:port over UDP; address in network byte order, 0 on failure */
uint32_t dns_udp_lookup(const char *name, const char *server, int port, unsigned id, int timeout_ms);
#endif
