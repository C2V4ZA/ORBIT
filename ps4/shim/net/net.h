#ifndef ORBIT_PS4_NET_H
#define ORBIT_PS4_NET_H
#include "../ps4_types.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
s32 netInitialize(void);
s32 netDeinitialize(void);
#endif
