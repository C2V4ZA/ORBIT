/* PS4 port: the small part of the PSL1GHT pad API that main_ps3.c uses, backed by scePad
 * (ps4_platform.c). Up to 4 PS4 users with a DualShock 4 each are read; port i = user i. */
#ifndef ORBIT_PS4_PAD_H
#define ORBIT_PS4_PAD_H
#include "../ps4_types.h"
#define MAX_PADS 7
typedef struct {
    u32 max, connected, info;
    u8 status[MAX_PADS];
} padInfo;
typedef struct {
    s32 len;
    int BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CROSS, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE;
    int BTN_SELECT, BTN_START, BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_L3, BTN_R3;
} padData;
s32 ioPadInit(u32 max);
s32 ioPadEnd(void);
s32 ioPadGetInfo(padInfo *info);
s32 ioPadGetData(u32 port, padData *data);
#endif
