/* Force-included into every PS4 translation unit (-include ps4_fix.h).
 * The OpenOrbis musl headers use FreeBSD values for socket constants, except MSG_NOSIGNAL:
 * 0x4000 is MSG_NBIO on FreeBSD (the PS4 kernel), which would make blocking sends
 * non-blocking. FreeBSD's MSG_NOSIGNAL is 0x20000. SIGPIPE is also ignored at start. */
#ifndef ORBIT_PS4_FIX_H
#define ORBIT_PS4_FIX_H
#include <sys/socket.h>
#undef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x20000
#endif
