#ifndef ORBIT_UTF8COPY_H
#define ORBIT_UTF8COPY_H
#include <stddef.h>
#include <string.h>

/* Length of s that fits in max bytes without splitting a UTF-8 sequence. */
static inline size_t orbit_utf8_fit(const char *s, size_t len, size_t max) {
    if (len <= max)
        return len;
    size_t n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
        n--; /* s[n] continues a character: cut before that character starts */
    return n;
}

/* Like snprintf(dst, cap, "%s", src), but a cut never ends inside a UTF-8 character.
   NULL src copies "". Overlapping dst/src is allowed (memmove). Returns bytes copied. */
static inline size_t orbit_copy(char *dst, size_t cap, const char *src) {
    if (!dst || !cap)
        return 0;
    if (!src)
        src = "";
    size_t n = orbit_utf8_fit(src, strlen(src), cap - 1);
    memmove(dst, src, n);
    dst[n] = 0;
    return n;
}

#endif
