/* ORBIT 1.2 automatic game presence: wire format and PARAM.SFO reader (see presence.h).
 * Freestanding: no libc calls, so the same file builds into the XMB plugin. */
#include "presence.h"

static size_t p_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}
static void p_copy(char *dst, size_t cap, const uint8_t *src, size_t n) {
    if (n >= cap)
        n = cap - 1;
    for (size_t i = 0; i < n; i++)
        dst[i] = (char)src[i];
    dst[n] = 0;
}
int presence_id_valid(const char *id) {
    for (int i = 0; i < 9; i++) {
        char c = id[i];
        if (i < 4 ? (c < 'A' || c > 'Z') : (c < '0' || c > '9'))
            return 0;
    }
    return id[9] == 0;
}
/* UTF-8 well-formed, no control characters, no 0x1f separator */
static int p_text_ok(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c < 0x20 || c == 0x7f)
            return 0;
        size_t k = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!k || i + k > n || (k == 2 && c < 0xc2))
            return 0;
        for (size_t j = 1; j < k; j++)
            if ((s[i + j] & 0xc0) != 0x80)
                return 0;
        i += k;
    }
    return 1;
}
/* cut at a character boundary */
static size_t p_cut(const uint8_t *s, size_t n, size_t max) {
    if (n <= max)
        return n;
    while (max && (s[max] & 0xc0) == 0x80)
        max--;
    return max;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
int presence_sfo(const uint8_t *sfo, size_t n, PresenceGame *g) {
    g->id[0] = g->name[0] = 0;
    if (n < 20 || sfo[0] != 0 || sfo[1] != 'P' || sfo[2] != 'S' || sfo[3] != 'F')
        return -1;
    uint32_t keys = le32(sfo + 8), data = le32(sfo + 12), count = le32(sfo + 16);
    if (keys > n || data > n || count > 256 || 20 + (uint64_t)count * 16 > keys)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = sfo + 20 + i * 16;
        uint32_t kofs = keys + le16(e), len = le32(e + 4), dofs = data + le32(e + 12);
        if (kofs >= n || dofs > n || len > n - dofs)
            continue;
        const char *key = (const char *)sfo + kofs;
        size_t klen = p_len(key, n - kofs);
        if (klen == n - kofs)
            continue; /* key not terminated */
        size_t vlen = p_len((const char *)sfo + dofs, len);
        if (klen == 8 && !__builtin_memcmp(key, "TITLE_ID", 8)) {
            if (vlen == 9)
                p_copy(g->id, sizeof(g->id), sfo + dofs, 9);
        } else if (klen == 5 && !__builtin_memcmp(key, "TITLE", 5)) {
            /* titles may hold line breaks: turn them into spaces */
            uint8_t t[PRESENCE_NAME * 2];
            size_t m = vlen < sizeof(t) ? vlen : sizeof(t);
            for (size_t k = 0; k < m; k++)
                t[k] = sfo[dofs + k] == '\n' || sfo[dofs + k] == '\r' ? ' ' : sfo[dofs + k];
            m = p_cut(t, m, PRESENCE_NAME);
            if (p_text_ok(t, m))
                p_copy(g->name, sizeof(g->name), t, m);
        }
    }
    if (!presence_id_valid(g->id)) {
        g->id[0] = g->name[0] = 0;
        return -1;
    }
    return 0;
}
size_t presence_frame(uint8_t *out, size_t cap, const PresenceGame *g) {
    size_t at = 0;
    if (cap < 1)
        return 0;
    out[at++] = 'G';
    if (!g->playing)
        return at;
    size_t il = p_len(g->id, sizeof(g->id)), nl = p_len(g->name, sizeof(g->name));
    if (at + il + 1 + nl > cap)
        return 0;
    for (size_t i = 0; i < il; i++)
        out[at++] = (uint8_t)g->id[i];
    out[at++] = 0x1f;
    for (size_t i = 0; i < nl; i++)
        out[at++] = (uint8_t)g->name[i];
    return at;
}
int presence_frame_parse(const uint8_t *in, size_t n, PresenceGame *g) {
    g->playing = 0;
    g->id[0] = g->name[0] = 0;
    if (n < 1 || in[0] != 'G')
        return -1;
    if (n == 1)
        return 0;
    size_t sep = 1;
    while (sep < n && in[sep] != 0x1f)
        sep++;
    if (sep == n)
        return -1;
    size_t il = sep - 1, nl = n - sep - 1;
    if (il && il != 9)
        return -1;
    if (nl > PRESENCE_NAME || !p_text_ok(in + sep + 1, nl))
        return -1;
    p_copy(g->id, sizeof(g->id), in + 1, il);
    if (il && !presence_id_valid(g->id))
        return -1;
    p_copy(g->name, sizeof(g->name), in + sep + 1, nl);
    g->playing = 1;
    return 0;
}
