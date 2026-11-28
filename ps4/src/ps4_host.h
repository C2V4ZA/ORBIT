/* ORBIT PS4 port: extra platform functions (beyond the PSL1GHT-style shims). */
#ifndef ORBIT_PS4_HOST_H
#define ORBIT_PS4_HOST_H
#include <stdint.h>
extern int32_t ps4_initial_user;
void ps4_pad_lightbar(uint8_t r, uint8_t g, uint8_t b);
void ps4_pad_lightbar_reset(void);
/* system notification bubble (top right), UTF-8, shown also over games */
void ps4_notify(const char *text);
/* symbol from an optional system library in /system/common/lib, NULL when unavailable */
void *ps4_sym(const char *module, const char *name);
/* diagnostic log /data/orbit/ps4-log.txt (no secrets, no message text) */
void ps4_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
