/* PS4 port: PSL1GHT netCtl names mapped to sceNetCtl (ps4_platform.c). */
#ifndef ORBIT_PS4_NETCTL_H
#define ORBIT_PS4_NETCTL_H
#include "../ps4_types.h"
#define NET_CTL_INFO_IP_ADDRESS 14
#define NET_CTL_INFO_NETMASK 15
#define NET_CTL_INFO_DEFAULT_ROUTE 16
union net_ctl_info {
    char ip_address[16];
    char netmask[16];
    char default_route[16];
    char pad[256];
};
s32 netCtlInit(void);
void netCtlTerm(void);
s32 netCtlGetInfo(s32 code, union net_ctl_info *info);
#endif
