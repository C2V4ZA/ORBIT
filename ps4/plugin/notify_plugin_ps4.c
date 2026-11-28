/* ORBIT 1.3 background notifications for the PS4: a GoldHEN plugin (orbit_notify.prx).
 *
 * GoldHEN (2.3 or newer, "load plugins" switched on) loads the .prx files listed in
 * /data/GoldHEN/plugins.ini into a game when it starts and calls the exported plugin_load().
 * The ORBIT app installs this file and its plugins.ini line only when the user asks
 * (Settings > Notifications), and removes exactly that line again.
 *
 * The message logic is the same as on the PS3 (orbit-ps3/src/notify_core.c, tested on the PC
 * against the real host code): it reads /data/orbit/data/notify.cfg written by the app, keeps one
 * "watch" connection per host and shows new messages at the top of the screen. It stays quiet
 * while the ORBIT app itself is open (marker /data/orbit/app-active).
 *
 * This file is only the platform layer. It uses nothing but libkernel (sockets, threads, files,
 * time, notifications) and libSceLibcInternal (memcpy, strlen ...), which every PS4 process has;
 * no Sony SDK, no GoldHEN SDK code. NOT tested on a console yet: every step is written to
 * /data/orbit/plugin-log.txt so a test shows where it stops. */
#include "notify_core.h"
#include <netinet/in.h>
#include <orbis/libkernel.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

#define attr_public __attribute__((visibility("default")))
#define LOG_PATH "/data/orbit/plugin-log.txt"
#define START_DELAY_MS 15000 /* let the game finish starting before the network is used */
#define SO_NOSIGPIPE_BSD 0x0800
#define O_NONBLOCK_BSD 0x0004
#define F_GETFL_BSD 3
#define F_SETFL_BSD 4

/* libkernel exports these POSIX calls (FreeBSD ABI); declared here, the OpenOrbis headers for them
 * belong to the app's libc which a game process does not have */
int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int setsockopt(int fd, int level, int name, const void *value, socklen_t len);
int getsockopt(int fd, int level, int name, void *value, socklen_t *len);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
int poll(struct pollfd *fds, nfds_t n, int timeout_ms);
int fcntl(int fd, int cmd, ...);
int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
int getpid(void);


attr_public const char *g_pluginName = "orbit_notify";
attr_public const char *g_pluginDesc = "ORBIT: new chat messages as notifications during games";
attr_public const char *g_pluginAuth = "ORBIT";
attr_public uint32_t g_pluginVersion = 0x00000130; /* 1.30 */

static volatile int stop_flag;
static OrbisPthread threads[NOTIFY_HOSTS];
static int thread_count;

/* ---------- log: small, capped, never needed for the plugin to work ---------- */
static int log_lines;
static void put(char *out, size_t cap, size_t *at, const char *s) {
    while (*s && *at + 1 < cap)
        out[(*at)++] = *s++;
    out[*at] = 0;
}
static void append_log(const char *text, unsigned long value) {
    if (log_lines >= 400)
        return;
    log_lines++;
    char line[160], num[24];
    size_t at = 0, k = 0;
    put(line, sizeof(line), &at, "orbit_notify: ");
    put(line, sizeof(line), &at, text);
    do
        num[k++] = (char)('0' + value % 10);
    while ((value /= 10) && k < sizeof(num));
    put(line, sizeof(line), &at, " ");
    while (k && at + 2 < sizeof(line))
        line[at++] = num[--k];
    line[at++] = '\n';
    int fd = open(LOG_PATH, 0x0001 | 0x0008 | 0x0200, 0666); /* O_WRONLY | O_APPEND | O_CREAT */
    if (fd >= 0) {
        write(fd, line, at);
        close(fd);
    }
}

/* ---------- platform for notify_core ---------- */
static uint64_t p_millis(void) { return sceKernelGetProcessTime() / 1000; }
static int p_connect(uint32_t ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE_BSD, &one, sizeof(one)); /* a closed peer must not kill the game */
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a);
    a.sin_family = AF_INET;
    a.sin_port = (uint16_t)((port & 0xff) << 8 | (port >> 8 & 0xff));
    a.sin_addr.s_addr = (uint32_t)((ip & 0xff) << 24 | (ip >> 8 & 0xff) << 16 | (ip >> 16 & 0xff) << 8 | ip >> 24);
    int flags = fcntl(fd, F_GETFL_BSD, 0);
    fcntl(fd, F_SETFL_BSD, flags | O_NONBLOCK_BSD);
    int rc = connect(fd, (const struct sockaddr *)&a, sizeof(a));
    if (rc) {
        struct pollfd w = {fd, POLLOUT, 0};
        int err = 0;
        socklen_t len = sizeof(err);
        if (poll(&w, 1, 10000) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) || err) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL_BSD, flags); /* blocking again; recv waits through poll() */
    return fd;
}
static int p_send(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    while (n) {
        ssize_t k = send(fd, p, n, 0);
        if (k <= 0)
            return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}
static int p_recv(int fd, void *buf, size_t n, unsigned ms) {
    struct pollfd r = {fd, POLLIN, 0};
    int ready = poll(&r, 1, (int)ms);
    if (ready == 0)
        return 0;
    if (ready < 0)
        return -1;
    ssize_t k = recv(fd, buf, n, 0);
    return k > 0 ? (int)k : -1;
}
static void p_close(int fd) { close(fd); }
static uint32_t p_unix_time(void) {
    struct timeval tv;
    return gettimeofday(&tv, 0) ? 0 : (uint32_t)tv.tv_sec;
}
static void p_sleep(unsigned ms) {
    while (ms && !stop_flag) { /* in slices: unloading must not wait long */
        unsigned slice = ms < 100 ? ms : 100;
        sceKernelUsleep(slice * 1000);
        ms -= slice;
    }
}
static void p_notify(const char *text) {
    static OrbisNotificationRequest req; /* 3 KB: not on the worker stack; one at a time */
    static volatile int busy;
    while (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
        sceKernelUsleep(10000);
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    req.useIconImageUri = 1;
    size_t n = strlen(text);
    if (n > sizeof(req.message) - 1)
        n = sizeof(req.message) - 1;
    memcpy(req.message, text, n);
    static const char icon[] = "cxml://psnotification/tex_icon_system";
    memcpy(req.iconUri, icon, sizeof(icon));
    int rc = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
    append_log("notification rc", (unsigned long)(uint32_t)rc);
}
static int p_read(const char *path, char *buf, size_t cap) {
    int fd = open(path, 0, 0); /* O_RDONLY */
    if (fd < 0)
        return -1;
    size_t got = 0;
    while (got < cap) {
        ssize_t k = read(fd, buf + got, cap - got);
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    close(fd);
    return (int)got;
}
static void p_entropy(uint8_t out[24]) { /* nonce material, need not be secret (notify_core.h) */
    static uint32_t counter;
    uint64_t t = sceKernelGetProcessTime();
    uint32_t pid = (uint32_t)getpid(), c = __atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED);
    for (int i = 0; i < 24; i++)
        out[i] = (uint8_t)((t >> (i % 8 * 8)) ^ (pid >> (i % 4 * 8)) ^ (c >> (i % 4 * 8)) ^ (i * 151u));
}
static const NotifyPlatform platform = {p_connect, p_send, p_recv,   p_close,  p_millis,  p_unix_time,
                                        p_sleep,   p_notify, p_read, p_entropy, append_log, &stop_flag};

static void *worker(void *arg) {
    int index = (int)(intptr_t)arg;
    p_sleep(START_DELAY_MS + (unsigned)index * 500);
    if (!stop_flag)
        notify_worker(&platform, index);
    return 0;
}

/* GoldHEN calls these (exported, default visibility). plugin_load must return at once. */
attr_public int32_t plugin_load(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    stop_flag = 0;
    log_lines = 0;
    append_log("plugin_load, pid", (unsigned long)getpid());
    for (int i = 0; i < NOTIFY_HOSTS; i++) {
        OrbisPthreadAttr attr;
        if (scePthreadAttrInit(&attr))
            break;
        scePthreadAttrSetstacksize(&attr, 64 * 1024);
        if (!scePthreadCreate(&threads[thread_count], &attr, worker, (void *)(intptr_t)i, "orbit_notify"))
            thread_count++;
        scePthreadAttrDestroy(&attr);
    }
    append_log("workers", (unsigned long)thread_count);
    return 0;
}
attr_public int32_t plugin_unload(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    stop_flag = 1;
    for (int i = 0; i < thread_count; i++)
        scePthreadJoin(threads[i], 0);
    thread_count = 0;
    append_log("plugin_unload", 0);
    return 0;
}
