/* ORBIT PS4 port: scan-out through sceVideoOut, same interface as the PS3 rsxutil.
 * Two linear A8R8G8B8 1920x1080 buffers in write-combined direct memory; the shared CPU
 * renderer (ui.c) draws at native 1080p and the canvas is copied into the free buffer.
 * The system scales 1080p to the TV (also on a 4K TV with a PS4 Pro). */
#include "rsxutil.h"
#include "ps4_host.h"
#include <string.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

#define PS4_W 1920
#define PS4_H 1080
#define PS4_BUFFERS 2
static int video = -1;
static OrbisKernelEqueue flip_queue;
static void *vmem;
static off_t vmem_off;
static size_t vmem_size;
static void *fb[PS4_BUFFERS];
static int registered;
static int64_t flip_arg;
struct gcmContextData {
    int unused;
};
static struct gcmContextData context_data;

gcmContextData *initScreen(void *host_addr, u32 size) {
    (void)host_addr;
    (void)size;
    video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    ps4_log("video open %d", video);
    if (video < 0)
        return NULL;
    if (sceKernelCreateEqueue(&flip_queue, "orbit flip") < 0)
        return NULL;
    sceVideoOutAddFlipEvent(flip_queue, video, NULL);
    const size_t align = 0x200000, one = (size_t)PS4_W * PS4_H * 4;
    size_t per = (one + align - 1) / align * align;
    vmem_size = per * PS4_BUFFERS;
    int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), vmem_size, align, 3, &vmem_off);
    ps4_log("direct memory %zu bytes: %d", vmem_size, rc);
    if (rc < 0)
        return NULL;
    rc = sceKernelMapDirectMemory(&vmem, vmem_size, 0x33, 0, vmem_off, align);
    if (rc < 0) {
        ps4_log("map direct memory: %d", rc);
        sceKernelReleaseDirectMemory(vmem_off, vmem_size);
        return NULL;
    }
    for (int i = 0; i < PS4_BUFFERS; i++) {
        fb[i] = (char *)vmem + per * i;
        memset(fb[i], 0, one);
    }
    OrbisVideoOutBufferAttribute attr;
    memset(&attr, 0, sizeof(attr));
    /* 0x80000000 = A8R8G8B8 sRGB, tiling 1 = linear, aspect 0, pitch in pixels */
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, PS4_W, PS4_H, PS4_W);
    rc = sceVideoOutRegisterBuffers(video, 0, fb, PS4_BUFFERS, &attr);
    ps4_log("register buffers: %d", rc);
    if (rc)
        return NULL;
    registered = 1;
    sceVideoOutSetFlipRate(video, 0); /* 60 Hz */
    return &context_data;
}
int getResolution(u16 *width, u16 *height) {
    *width = PS4_W;
    *height = PS4_H;
    return 1;
}
int makeBuffer(rsxBuffer *buffer, u16 width, u16 height, int id) {
    if (id < 0 || id >= PS4_BUFFERS || !fb[id] || width != PS4_W || height != PS4_H)
        return 0;
    buffer->width = width;
    buffer->height = height;
    buffer->id = id;
    buffer->ptr = fb[id];
    buffer->offset = 0;
    return 1;
}
/* wait until the last submitted flip is on screen: the other buffer is then free to draw */
void waitFlip(void) {
    if (video < 0 || !flip_arg)
        return;
    for (int guard = 0; guard < 8; guard++) {
        OrbisVideoOutFlipStatus st;
        memset(&st, 0, sizeof(st));
        sceVideoOutGetFlipStatus(video, &st);
        if (st.flipArg >= flip_arg)
            return;
        OrbisKernelEvent ev;
        int n = 0;
        OrbisKernelUseconds timeout = 100000;
        sceKernelWaitEqueue(flip_queue, &ev, 1, &n, &timeout);
    }
}
int flip(gcmContextData *context, s32 buffer) {
    (void)context;
    if (video < 0)
        return 0;
    /* a failed flip only drops one frame; never end the app because of it */
    if (sceVideoOutSubmitFlip(video, buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC, flip_arg + 1) == 0)
        flip_arg++;
    return 1;
}
void rsxFinish(gcmContextData *context, u32 arg) {
    (void)context;
    (void)arg;
    if (video < 0)
        return;
    waitFlip();
    if (registered)
        sceVideoOutUnregisterBuffers(video, 0);
    sceVideoOutClose(video);
    video = -1;
    if (vmem)
        sceKernelReleaseDirectMemory(vmem_off, vmem_size);
    vmem = NULL;
}
