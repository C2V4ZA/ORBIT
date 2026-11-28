/* PS4 port: same interface as the PS3 rsxutil.h, implemented with sceVideoOut and linear
 * A8R8G8B8 scan-out buffers in direct memory (ps4_video.c). The CPU renderer draws into them. */
#ifndef ORBIT_PS4_RSXUTIL_H
#define ORBIT_PS4_RSXUTIL_H
#include "ps4_types.h"
#define HOST_SIZE 4096
typedef struct gcmContextData gcmContextData;
typedef struct {
    int height, width, id;
    uint32_t *ptr;
    uint32_t offset;
} rsxBuffer;
void waitFlip(void);
int flip(gcmContextData *context, s32 buffer);
int makeBuffer(rsxBuffer *buffer, u16 width, u16 height, int id);
int getResolution(u16 *width, u16 *height);
gcmContextData *initScreen(void *host_addr, u32 size);
void rsxFinish(gcmContextData *context, u32 arg);
#endif
