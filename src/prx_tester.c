/* Orbit Plugin-Test: loads the ORBIT background-plugin test modules with the system PRX loader and
 * shows the exact result codes (load, start, module_start result). Nothing is installed or changed.
 * Results are shown on screen and written to USRDIR/plugin-test.txt and /dev_usb000. */
#include "rsxutil.h"
#include "ui.h"
#include <io/pad.h>
#include <sys/prx.h>
/* PSL1GHT's lv2/prx.h and sys/prx.h share one include guard, so the functions are declared here */
sysPrxId sysPrxLoadModule(const char *path, sysPrxFlags flags, sysPrxLoadModuleOption *opt);
sysPrxId sysPrxUnloadModule(sysPrxId id, sysPrxFlags flags, sysPrxLoadModuleOption *opt);
s32 sysPrxStartModule(sysPrxId id, size_t args, void *argp, s32 *modres, sysPrxFlags flags, sysPrxStartModuleOption *opt);
s32 sysPrxStopModule(sysPrxId id, size_t args, void *argp, s32 *modres, sysPrxFlags flags, sysPrxStartModuleOption *opt);
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sysutil/sysutil.h>
#include <unistd.h>
#define DIR "/dev_hdd0/game/ORB000004/USRDIR"
static int running = 1, count, dirty = 1;
static char rows[12][100], status[192] = "Plugin-Test laeuft ...";
static FILE *logfile, *usblog;

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (count == 12) {
        memmove(rows, rows + 1, sizeof(rows[0]) * 11);
        count = 11;
    }
    snprintf(rows[count++], 100, "%.99s", line);
    FILE *files[2] = {logfile, usblog};
    for (int i = 0; i < 2; i++)
        if (files[i]) {
            fprintf(files[i], "%s\n", line);
            fflush(files[i]);
            fsync(fileno(files[i]));
        }
    dirty = 1;
}

static void callback(u64 event, u64 param, void *user) {
    (void)param;
    (void)user;
    if (event == SYSUTIL_EXIT_GAME)
        running = 0;
}

static unsigned buttons(void) {
    padInfo info = {0};
    padData data;
    unsigned all = 0;
    if (ioPadGetInfo(&info))
        return 0;
    for (int i = 0; i < 7; i++) {
        if (!info.status[i])
            continue;
        memset(&data, 0, sizeof(data));
        if (!ioPadGetData(i, &data) && data.len > 0)
            all |= (data.BTN_CROSS ? 1 : 0) | (data.BTN_CIRCLE ? 2 : 0) | (data.BTN_START ? 4 : 0);
    }
    return all;
}

/* one module: load -> start -> stop -> unload, every result code logged */
static void try_module(const char *label, const char *file) {
    char path[160];
    snprintf(path, sizeof(path), DIR "/%s", file);
    sysPrxId id = sysPrxLoadModule(path, 0, NULL);
    if (id < 0) {
        say("%s: LADEN FEHLER 0x%08X", label, (unsigned)id);
        return;
    }
    s32 res = -1;
    s32 rc = sysPrxStartModule(id, 0, NULL, &res, 0, NULL);
    say("%s: geladen (id 0x%X), start 0x%08X, module_start %d", label, (unsigned)id, (unsigned)rc, (int)res);
    if (rc == 0) {
        s32 sres = -1;
        sysPrxStopModule(id, 0, NULL, &sres, 0, NULL);
    }
    sysPrxUnloadModule(id, 0, NULL);
}

/* module K: module_start creates a background thread that sleeps 1 s and then writes a marker */
static volatile unsigned int marker;
static void try_thread(const char *label, const char *file) {
    char path[160];
    snprintf(path, sizeof(path), DIR "/%s", file);
    sysPrxId id = sysPrxLoadModule(path, 0, NULL);
    if (id < 0) {
        say("%s: LADEN FEHLER 0x%08X", label, (unsigned)id);
        return;
    }
    s32 res = -1;
    marker = 0;
    s32 rc = sysPrxStartModule(id, sizeof(marker), (void *)&marker, &res, 0, NULL);
    say("%s: start 0x%08X, module_start 0x%08X, Marke 0x%08X", label, (unsigned)rc, (unsigned)res,
        (unsigned)marker);
    if (rc != 0 || res != 0)
        return;
    for (int i = 0; i < 50 && marker != 0x4f524254; i++)
        usleep(100000);
    say("%s: Hintergrund-Thread %s (0x%08X)", label, marker == 0x4f524254 ? "LAEUFT" : "lief NICHT", marker);
    usleep(500000);
    s32 sres = -1;
    sysPrxStopModule(id, 0, NULL, &sres, 0, NULL);
    sysPrxUnloadModule(id, 0, NULL);
}

int main(void) {
    logfile = fopen(DIR "/plugin-test.txt", "w");
    usblog = fopen("/dev_usb000/orbit-plugin-test.txt", "w");
    if (ui_init(DIR "/OrbitSans.ttf"))
        return 1;
    void *host = memalign(1024 * 1024, HOST_SIZE);
    gcmContextData *context = host ? initScreen(host, HOST_SIZE) : NULL;
    u16 width, height;
    if (!context || !getResolution(&width, &height))
        return 3;
    rsxBuffer buffers[2] = {0};
    for (int i = 0; i < 2; i++) {
        if (!makeBuffer(&buffers[i], width, height, i))
            return 4;
        memset(buffers[i].ptr, 0, (size_t)width * height * 4);
    }
    uint32_t *canvas = memalign(128, (size_t)width * height * 4);
    if (!canvas)
        return 5;
    ioPadInit(7);
    sysUtilRegisterCallback(0, callback, NULL);
    say("ORBIT Plugin-Test 4");
    remove("/dev_hdd0/tmp/orbit-plugin-log.txt");
    /* same module in different containers: tells format problems apart from signature problems */
    try_thread("K Thread       ", "orbit_k.sprx");
    /* the log file the plugins wrote themselves (same file the XMB test uses) */
    FILE *plog = fopen("/dev_hdd0/tmp/orbit-plugin-log.txt", "r");
    if (!plog)
        say("Plugin-Log: NICHT gefunden");
    else {
        char line[100];
        while (fgets(line, sizeof(line), plog)) {
            line[strcspn(line, "\r\n")] = 0;
            say("Log: %s", line);
        }
        fclose(plog);
    }
    remove("/dev_hdd0/tmp/orbit-plugin-log.txt"); /* clean for the XMB test */
    snprintf(status, sizeof(status), "Fertig. Foto machen, dann KREIS zum Beenden.");
    int index = 0, generation = 0, versions[2] = {-1, -1};
    flip(context, 1);
    while (running) {
        sysUtilCheckCallback();
        if (buttons() & (2 | 4))
            running = 0;
        if (dirty) {
            dirty = 0;
            ui_audio_probe(canvas, width, height, status, (const char(*)[100])rows, count, 0, 0, -1);
            generation++;
        }
        waitFlip();
        if (versions[index] != generation) {
            memcpy(buffers[index].ptr, canvas, (size_t)width * height * 4);
            versions[index] = generation;
        }
        if (!flip(context, index))
            break;
        index ^= 1;
    }
    if (logfile)
        fclose(logfile);
    if (usblog)
        fclose(usblog);
    sysUtilUnregisterCallback(0);
    ioPadEnd();
    rsxFinish(context, 1);
    ui_close();
    return 0;
}
