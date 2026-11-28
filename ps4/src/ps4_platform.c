/* ORBIT PS4 port: system services behind the PSL1GHT-style names that the shared front end
 * (orbit-ps3/src/main_ps3.c) calls. Only OpenOrbis (open-source) stubs are used.
 * Not hardware-tested by the developer: every call checks its result and falls back. */
#include "io/pad.h"
#include "lv2/systime.h"
#include "net/net.h"
#include "net/netctl.h"
#include "sysutil/sysutil.h"
#include "ps4_host.h"
#include "dns_fallback.h"
#include <arpa/inet.h>
#include "orbit.h"
#include <netdb.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
int sysctl(const int *, unsigned int, void *, size_t *, const void *, size_t); /* libkernel */
#include <pthread.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/SystemService.h>
#include <orbis/_types/net.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>

/* ---------- time ---------- */
u64 sysGetSystemTime(void) {
    return sceKernelGetProcessTime(); /* microseconds since the process started */
}

/* ---------- users and pads ---------- */
#define PS4_USERS 4
static int pad_handle[PS4_USERS] = {-1, -1, -1, -1};
static int32_t pad_user[PS4_USERS] = {-1, -1, -1, -1};
static int user_ready;
static uint64_t users_checked;
int32_t ps4_initial_user = -1;

static void users_init(void) {
    if (user_ready)
        return;
    OrbisUserServiceInitializeParams p;
    memset(&p, 0, sizeof(p));
    p.priority = 700;
    sceUserServiceInitialize(&p); /* returns "already initialized" when called twice: fine */
    if (sceUserServiceGetInitialUser(&ps4_initial_user))
        ps4_initial_user = -1;
    user_ready = 1;
}
/* every logged-in user's DualShock 4 can steer ORBIT (like every PS3 pad port) */
static void pads_refresh(void) {
    OrbisUserServiceLoginUserIdList list;
    memset(&list, 0, sizeof(list));
    if (sceUserServiceGetLoginUserIdList(&list)) {
        list.userId[0] = ps4_initial_user;
        for (int i = 1; i < PS4_USERS; i++)
            list.userId[i] = -1;
    }
    for (int i = 0; i < PS4_USERS; i++) {
        int32_t uid = list.userId[i];
        if (uid == pad_user[i] && (uid < 0 || pad_handle[i] >= 0))
            continue;
        if (pad_handle[i] >= 0)
            scePadClose(pad_handle[i]);
        pad_handle[i] = -1;
        pad_user[i] = uid;
        if (uid >= 0) {
            int h = scePadOpen(uid, 0, 0, NULL);
            if (h < 0)
                h = scePadGetHandle(uid, 0, 0); /* already opened (e.g. after a restart) */
            pad_handle[i] = h >= 0 ? h : -1;
            if (h >= 0) { /* ORBIT blue light bar while ORBIT runs (reset when it closes) */
                OrbisPadColor c = {0x2c, 0x63, 0xd8, 255};
                scePadSetLightBar(h, &c);
            }
            ps4_log("pad user %d -> handle %d", i, h);
        }
    }
}
s32 ioPadInit(u32 max) {
    (void)max;
    users_init();
    int rc = scePadInit();
    ps4_log("pad init %d, initial user %d", rc, (int)ps4_initial_user);
    if (rc < 0)
        return -1;
    pads_refresh();
    users_checked = sysGetSystemTime();
    return 0;
}
s32 ioPadEnd(void) {
    for (int i = 0; i < PS4_USERS; i++)
        if (pad_handle[i] >= 0) {
            scePadResetLightBar(pad_handle[i]);
            scePadClose(pad_handle[i]);
            pad_handle[i] = -1;
        }
    return 0;
}
s32 ioPadGetInfo(padInfo *info) {
    uint64_t now = sysGetSystemTime();
    if (now - users_checked > 2000000) { /* users log in/out: look again every 2 s */
        users_checked = now;
        pads_refresh();
    }
    memset(info, 0, sizeof(*info));
    info->max = PS4_USERS;
    for (int i = 0; i < PS4_USERS; i++)
        if (pad_handle[i] >= 0) {
            info->status[i] = 1;
            info->connected++;
        }
    return 0;
}
static unsigned ps4_buttons_last;
s32 ioPadGetData(u32 port, padData *d) {
    memset(d, 0, sizeof(*d));
    if (port >= PS4_USERS || pad_handle[port] < 0)
        return -1;
    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(pad_handle[port], &pad) || !pad.connected)
        return -1;
    uint32_t b = pad.buttons;
    /* the left stick also moves the focus (outside the dead zone) */
    int sx = (int)pad.leftStick.x - 128, sy = (int)pad.leftStick.y - 128;
    d->len = 8;
    d->BTN_UP = !!(b & ORBIS_PAD_BUTTON_UP) || sy < -90;
    d->BTN_DOWN = !!(b & ORBIS_PAD_BUTTON_DOWN) || sy > 90;
    d->BTN_LEFT = !!(b & ORBIS_PAD_BUTTON_LEFT) || sx < -90;
    d->BTN_RIGHT = !!(b & ORBIS_PAD_BUTTON_RIGHT) || sx > 90;
    d->BTN_CROSS = !!(b & ORBIS_PAD_BUTTON_CROSS);
    d->BTN_CIRCLE = !!(b & ORBIS_PAD_BUTTON_CIRCLE);
    d->BTN_SQUARE = !!(b & ORBIS_PAD_BUTTON_SQUARE);
    d->BTN_TRIANGLE = !!(b & ORBIS_PAD_BUTTON_TRIANGLE);
    d->BTN_START = !!(b & ORBIS_PAD_BUTTON_OPTIONS);
    d->BTN_SELECT = !!(b & ORBIS_PAD_BUTTON_TOUCH_PAD); /* touch pad click = PS3 SELECT */
    d->BTN_L1 = !!(b & ORBIS_PAD_BUTTON_L1);
    d->BTN_R1 = !!(b & ORBIS_PAD_BUTTON_R1);
    d->BTN_L2 = !!(b & ORBIS_PAD_BUTTON_L2);
    d->BTN_R2 = !!(b & ORBIS_PAD_BUTTON_R2);
    d->BTN_L3 = !!(b & ORBIS_PAD_BUTTON_L3);
    d->BTN_R3 = !!(b & ORBIS_PAD_BUTTON_R3);
    ps4_buttons_last = b;
    return 0;
}
/* light bar in the ORBIT blue on every open pad (cosmetic; errors ignored) */
void ps4_pad_lightbar(uint8_t r, uint8_t g, uint8_t b) {
    OrbisPadColor c = {r, g, b, 255};
    for (int i = 0; i < PS4_USERS; i++)
        if (pad_handle[i] >= 0)
            scePadSetLightBar(pad_handle[i], &c);
}
void ps4_pad_lightbar_reset(void) {
    for (int i = 0; i < PS4_USERS; i++)
        if (pad_handle[i] >= 0)
            scePadResetLightBar(pad_handle[i]);
}

/* ---------- network ---------- */
static int net_memid = -1;
s32 netInitialize(void) {
    signal(SIGPIPE, SIG_IGN);
    sceNetInit();
    if (net_memid < 0)
        net_memid = sceNetPoolCreate("orbit-net", 128 * 1024, 0);
    return 0;
}
s32 netDeinitialize(void) {
    if (net_memid >= 0)
        sceNetPoolDestroy(net_memid);
    net_memid = -1;
    return 0;
}
/* sceNetCtl is loaded at run time (ps4_sym): without it ORBIT still connects, it only does not
 * know its own LAN address (home-network discovery is then off). The info union is 256+ bytes;
 * the address texts are 16-byte strings at its start. */
static int32_t (*netctl_init)(void);
static void (*netctl_term)(void);
static int32_t (*netctl_get)(int32_t, void *);
s32 netCtlInit(void) {
    static int looked;
    if (!looked) {
        looked = 1;
        netctl_init = ps4_sym("libSceNetCtl.sprx", "sceNetCtlInit");
        netctl_term = ps4_sym("libSceNetCtl.sprx", "sceNetCtlTerm");
        netctl_get = ps4_sym("libSceNetCtl.sprx", "sceNetCtlGetInfo");
    }
    if (!netctl_init || !netctl_get)
        return -1;
    return netctl_init() < 0 ? -1 : 0;
}
void netCtlTerm(void) {
    if (netctl_term)
        netctl_term();
}
s32 netCtlGetInfo(s32 code, union net_ctl_info *info) {
    union {
        char text[16];
        uint8_t raw[1024];
    } in;
    memset(&in, 0, sizeof(in));
    if (!netctl_get || netctl_get(code, &in))
        return -1;
    memset(info, 0, sizeof(*info));
    memcpy(info->pad, in.text, 15);
    return info->pad[0] ? 0 : -1;
}
/* DNS: musl's resolver reads /etc/resolv.conf, which the PS4 does not have. This replaces the
 * libc.a member, so network.c's gethostbyname() goes through the system resolver. One lookup
 * at a time (static result, like the classic API); callers copy the address at once. */
static pthread_mutex_t dns_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct hostent dns_host;
static struct in_addr dns_addr;
static char *dns_list[2];
static char dns_name[256];
struct hostent *gethostbyname(const char *name) {
    if (!name || !name[0])
        return NULL;
    struct in_addr a;
    pthread_mutex_lock(&dns_mutex);
    if (inet_pton(AF_INET, name, &a) != 1) {
        if (net_memid < 0)
            netInitialize();
        OrbisNetId rid = sceNetResolverCreate("orbit-dns", net_memid, 0);
        OrbisNetInAddr out;
        memset(&out, 0, sizeof(out));
        int rc = rid;
        if (rid >= 0) {
            rc = sceNetResolverStartNtoa(rid, name, &out, 5 * 1000 * 1000, 2, 0);
            sceNetResolverDestroy(rid);
        }
        if (rc < 0 || !out.s_addr) { /* system resolver refused: ask a public resolver directly */
            static const char *servers[2] = {"1.1.1.1", "8.8.8.8"};
            for (int i = 0; i < 2 && !out.s_addr; i++)
                out.s_addr = dns_udp_lookup(name, servers[i], 53, (unsigned)sceKernelGetProcessTime() + (unsigned)i, 2500);
            ps4_log("dns %s: system resolver %d, fallback %s", name, rc, out.s_addr ? "ok" : "failed");
        }
        if (!out.s_addr) {
            pthread_mutex_unlock(&dns_mutex);
            return NULL;
        }
        a.s_addr = out.s_addr;
        char shown[16];
        inet_ntop(AF_INET, &a, shown, sizeof(shown));
        ps4_log("dns %s -> %s", name, shown);
    }
    dns_addr = a;
    snprintf(dns_name, sizeof(dns_name), "%s", name);
    dns_list[0] = (char *)&dns_addr;
    dns_list[1] = NULL;
    memset(&dns_host, 0, sizeof(dns_host));
    dns_host.h_name = dns_name;
    dns_host.h_addrtype = AF_INET;
    dns_host.h_length = 4;
    dns_host.h_addr_list = dns_list;
    pthread_mutex_unlock(&dns_mutex);
    return &dns_host;
}

/* ---------- random ---------- */
/* platform.c (non-PPU path) calls getrandom(). The PS4 kernel has no getrandom syscall, so it is
 * replaced with the system's random service (libSceRandom, at most 64 bytes per call). */
/* Optional system libraries are loaded at run time, not linked: if one is missing or refused on
 * some firmware, ORBIT still starts and only that feature is off. */
void *ps4_sym(const char *module, const char *name) {
    static struct {
        char name[40];
        int handle;
    } loaded[8];
    static int count;
    int h = -1, i;
    for (i = 0; i < count; i++)
        if (!strcmp(loaded[i].name, module))
            break;
    if (i < count)
        h = loaded[i].handle; /* load each library once */
    else {
        char path[96];
        snprintf(path, sizeof(path), "/system/common/lib/%s", module);
        h = (int)sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
        if (count < 8) {
            snprintf(loaded[count].name, sizeof(loaded[count].name), "%s", module);
            loaded[count++].handle = h;
        }
    }
    void *fn = NULL;
    if (h < 0 || sceKernelDlsym(h, name, &fn) < 0)
        fn = NULL;
    ps4_log("module %s: %08X, %s %s", module, (unsigned)h, name, fn ? "found" : "missing");
    return fn;
}
typedef int (*random_fn)(void *, size_t);
ssize_t getrandom(void *buf, size_t n, unsigned flags) {
    (void)flags;
    static random_fn rnd;
    static int looked;
    if (!looked) {
        looked = 1;
        rnd = (random_fn)ps4_sym("libSceRandom.sprx", "sceRandomGetRandomNumber");
    }
    unsigned char *p = buf;
    size_t left = n;
    while (left) {
        size_t k = left > 64 ? 64 : left;
        if (!rnd || rnd(p, k) < 0) {
            /* fallback: the FreeBSD kernel's arc4random sysctl (kern.arandom) */
            int mib[2] = {1, 37};
            size_t got = k;
            if (sysctl(mib, 2, p, &got, NULL, 0) || got != k)
                return -1;
        }
        p += k;
        left -= k;
    }
    return (ssize_t)n;
}

/* ---------- system parameters ---------- */
static sysutilCallback app_cb;
s32 sysUtilRegisterCallback(s32 slot, sysutilCallback cb, void *usrdata) {
    (void)slot;
    (void)usrdata;
    app_cb = cb; /* the PS4 has no exit event for apps: closing the app ends the process */
    return 0;
}
s32 sysUtilUnregisterCallback(s32 slot) {
    (void)slot;
    app_cb = NULL;
    return 0;
}
s32 sysUtilCheckCallback(void) {
    return 0;
}
s32 sysUtilGetSystemParamInt(s32 id, s32 *value) {
    int32_t v = 0, pid;
    switch (id) {
    case SYSUTIL_SYSTEMPARAM_ID_LANG:
        pid = ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG;
        break;
    case SYSUTIL_SYSTEMPARAM_ID_TIMEZONE:
        pid = ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE;
        break;
    case SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME:
        pid = ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME;
        break;
    default:
        return -1;
    }
    if (sceSystemServiceParamGetInt(pid, &v))
        return -1;
    *value = v;
    return 0;
}
s32 sysUtilGetSystemParamString(s32 id, char *buf, u32 len) {
    if (id != SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME || !len)
        return -1;
    users_init();
    buf[0] = 0;
    if (ps4_initial_user < 0 || sceUserServiceGetUserName(ps4_initial_user, buf, len))
        return -1;
    return 0;
}

/* ---------- system notification (top-right bubble) ---------- */
void ps4_notify(const char *text) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    snprintf(req.message, sizeof(req.message), "%s", text);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ---------- start-up and diagnostics ---------- */
/* A short log for hardware tests: /data/orbit/ps4-log.txt (rewritten at every start).
 * It never contains keys, codes or message text - only steps and system result codes. */
#define PS4_LOG "/data/orbit/ps4-log.txt"
void ps4_log(const char *fmt, ...) {
    FILE *f = fopen(PS4_LOG, "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(f, "[%8.3f] ", (double)sysGetSystemTime() / 1e6);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
/* runs before main(): the shared front end creates APPDIR/data and expects APPDIR to exist */
__attribute__((constructor)) static void ps4_dirs(void) {
    mkdir("/data", 0777);
    mkdir("/data/orbit", 0777);
    FILE *f = fopen(PS4_LOG, "w");
    if (f) {
        fprintf(f, "ORBIT PS4 %s start\n", ORBIT_VERSION);
        fclose(f);
    }
}

/* ---------- threads ---------- */
/* The shared code starts most threads with default attributes (fine on the PS3). The PS4
 * default stack may be smaller than a TLS handshake needs, so every thread gets at least 512 KB
 * (linked with --wrap=pthread_create). */
int __real_pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    pthread_attr_t own;
    size_t size = 0;
    if (attr && !pthread_attr_getstacksize(attr, &size) && size >= 512 * 1024)
        return __real_pthread_create(t, attr, fn, arg);
    if (attr)
        own = *attr;
    else
        pthread_attr_init(&own);
    pthread_attr_setstacksize(&own, 512 * 1024);
    int rc = __real_pthread_create(t, &own, fn, arg);
    if (!attr)
        pthread_attr_destroy(&own);
    return rc;
}
