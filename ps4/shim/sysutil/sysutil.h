/* PS4 port: the PSL1GHT sysutil calls used by main_ps3.c (ps4_platform.c). The PS4 language
 * ids 0-19 are in the same order as the PS3 ones, so i18n_from_ps3() maps them unchanged. */
#ifndef ORBIT_PS4_SYSUTIL_H
#define ORBIT_PS4_SYSUTIL_H
#include "../ps4_types.h"
#define SYSUTIL_EXIT_GAME 0x0101
#define SYSUTIL_MENU_OPEN 0x0131
#define SYSUTIL_MENU_CLOSE 0x0132
#define SYSUTIL_SYSTEMPARAM_ID_LANG 0x0111
#define SYSUTIL_SYSTEMPARAM_ID_TIMEZONE 0x0116
#define SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME 0x0117
#define SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME 0x0113
#define SYSUTIL_LANG_ENGLISH_US 1
typedef void (*sysutilCallback)(u64 status, u64 param, void *usrdata);
s32 sysUtilRegisterCallback(s32 slot, sysutilCallback cb, void *usrdata);
s32 sysUtilUnregisterCallback(s32 slot);
s32 sysUtilCheckCallback(void);
s32 sysUtilGetSystemParamInt(s32 id, s32 *value);
s32 sysUtilGetSystemParamString(s32 id, char *buf, u32 len);
#endif
