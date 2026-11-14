/* Orbit Controller-Test: drives a DualSense plugged into the PS3 by USB directly (cellUsbd).
 * 1. claims the controller as an extra logical device driver and lists its descriptors
 * 2. HID output report: lightbar green, player LED, short rumble, built-in speaker on
 * 3. USB audio: isochronous stream to the controller (melody on channels 1+2, buzz on 3+4)
 * Everything is shown on screen and written to USRDIR/controller-test.txt (and a USB stick). */
#include "rsxutil.h"
#include "ui.h"
#include <io/pad.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sysmodule/sysmodule.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <unistd.h>
#include <usb/usb.h>
#define DIR "/dev_hdd0/game/ORB000003/USRDIR"
static int running = 1, count;
static char rows[12][100], status[192] = "DualSense per USB-Kabel anstecken - der Test startet von selbst.";
static FILE *logfile, *usblog;
static pthread_mutex_t rows_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int step, test_running;
static volatile uint64_t step_since;
static char step_name[80];
static volatile int dev_id = -1, attached, probes;
static int dirty = 1;
static uint64_t millis(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (uint64_t)t.tv_sec * 1000 + t.tv_usec / 1000;
}
static void say(const char *fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&rows_lock);
    if (count == 12) {
        memmove(rows, rows + 1, sizeof(rows[0]) * 11);
        count = 11;
    }
    snprintf(rows[count++], 100, "%.99s", line);
    /* written immediately, also to a USB stick: survives even a frozen console */
    FILE *files[2] = {logfile, usblog};
    for (int i = 0; i < 2; i++)
        if (files[i]) {
            fprintf(files[i], "%s\n", line);
            fflush(files[i]);
            fsync(fileno(files[i]));
        }
    dirty = 1;
    pthread_mutex_unlock(&rows_lock);
}
static void begin_step(int n, const char *name) {
    step = n;
    step_since = millis();
    snprintf(step_name, sizeof(step_name), "%s", name);
    say("Schritt %d: %s ...", n, name);
}
static void callback(u64 event, u64 param, void *user) {
    (void)param;
    (void)user;
    if (event == SYSUTIL_EXIT_GAME)
        running = 0;
}
/* ---------- LDD ---------- */
static s32 ldd_probe(s32 id) {
    probes++;
    (void)id;
    return 0; /* claim it */
}
static s32 ldd_attach(s32 id) {
    dev_id = id;
    attached = 1;
    return 0;
}
static s32 ldd_detach(s32 id) {
    (void)id;
    attached = 0;
    dev_id = -1;
    return 0;
}
static usbLddOps ops = {"OrbitDualSense", ldd_probe, ldd_attach, ldd_detach};
/* ---------- every USB call gets its own time limit ---------- */
typedef struct {
    int kind, a, done, abandoned;
    s32 result;
    void *p, *q;
    usbDeviceRequest req;
} Call;
enum { CALL_OPEN_PIPE, CALL_INTERRUPT, CALL_CONTROL };
static void done_cb(s32 result, s32 n, void *arg);
static pthread_mutex_t call_lock = PTHREAD_MUTEX_INITIALIZER;
static void *call_main(void *arg) {
    Call *c = arg;
    s32 r = -1;
    if (c->kind == CALL_OPEN_PIPE)
        r = usbOpenPipe(c->a, (usbEndpointDescriptor *)c->p);
    else if (c->kind == CALL_INTERRUPT)
        r = usbInterruptTransfer(c->a, c->p, 48, done_cb, NULL);
    else
        r = usbControlTransfer(c->a, &c->req, c->p, done_cb, NULL);
    pthread_mutex_lock(&call_lock);
    c->result = r;
    c->done = 1;
    int free_it = c->abandoned;
    pthread_mutex_unlock(&call_lock);
    if (free_it)
        free(c);
    return NULL;
}
/* returns the call's result, or -998 when it did not return within ms */
static s32 timed(Call *template, int ms) {
    Call *c = malloc(sizeof(*c));
    if (!c)
        return -997;
    *c = *template;
    c->done = c->abandoned = 0;
    pthread_t t;
    if (pthread_create(&t, NULL, call_main, c)) {
        free(c);
        return -996;
    }
    pthread_detach(t);
    uint64_t end = millis() + (uint64_t)ms;
    for (;;) {
        pthread_mutex_lock(&call_lock);
        if (c->done) {
            s32 r = c->result;
            pthread_mutex_unlock(&call_lock);
            free(c);
            return r;
        }
        if (millis() >= end) {
            c->abandoned = 1; /* the helper thread frees it if the call ever returns */
            pthread_mutex_unlock(&call_lock);
            return -998;
        }
        pthread_mutex_unlock(&call_lock);
        usleep(2000);
    }
}
static s32 open_pipe(void *ep) {
    Call c = {CALL_OPEN_PIPE, dev_id, 0, 0, 0, ep, NULL, {0}};
    return timed(&c, 3000);
}
/* ---------- synchronous helpers around the async API ---------- */
static volatile int xfer_done, xfer_result, xfer_count;
static void done_cb(s32 result, s32 n, void *arg) {
    (void)arg;
    xfer_result = result;
    xfer_count = n;
    xfer_done = 1;
}
static int wait_xfer(int ms) {
    uint64_t end = millis() + (uint64_t)ms;
    while (!xfer_done && millis() < end)
        usleep(1000);
    return xfer_done ? xfer_result : -999;
}
static int control(int pipe, u8 type, u8 req, u16 value, u16 index, void *data, u16 len) {
    usbDeviceRequest r;
    r.bmRequestType = type;
    r.bRequest = req;
    r.wValue = value;
    r.wIndex = index;
    r.wLength = len;
    xfer_done = 0;
    Call c = {CALL_CONTROL, pipe, 0, 0, 0, data, NULL, r};
    int rc = timed(&c, 2000);
    if (rc)
        return rc;
    return wait_xfer(1500);
}
static u16 swap16(u16 v) {
    return (u16)(v << 8 | v >> 8);
}
/* tries host order first, then byte-swapped fields (endianness of the setup packet unknown) */
static int control2(int pipe, u8 type, u8 req, u16 value, u16 index, void *data, u16 len,
                    const char *what) {
    int rc = control(pipe, type, req, value, index, data, len);
    if (!rc) {
        say("%s: OK", what);
        return 0;
    }
    /* length stays as is: a swapped length could make the host send past the buffer */
    int rc2 = control(pipe, type, req, swap16(value), swap16(index), data, len);
    say("%s: 0x%08X / getauscht 0x%08X", what, (unsigned)rc, (unsigned)rc2);
    return rc2;
}
/* ---------- descriptors ---------- */
static u8 *hid_out_ep, *iso_out_ep;
static int hid_iface = -1, audio_iface = -1, audio_alt = 1, audio_channels = 4, iso_max;
static void scan_descriptors(void) {
    int cur_if = -1, cur_alt = 0, cur_class = 0, cur_sub = 0, n = 0;
    void *p = NULL;
    while (n < 80 && (p = usbScanStaticDescriptor(dev_id, p, 0))) {
        u8 *d = p;
        n++;
        if (d[1] == USB_DESCRIPTOR_TYPE_DEVICE && d[0] >= 18) {
            say("Geraet %02X%02X:%02X%02X USB %X.%02X", d[9], d[8], d[11], d[10], d[3], d[2]);
        } else if (d[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
            cur_if = d[2];
            cur_alt = d[3];
            cur_class = d[5];
            cur_sub = d[6];
            if (logfile)
                fprintf(logfile, "IF %d alt %d class %02X/%02X eps %d\n", cur_if, cur_alt,
                        cur_class, cur_sub, d[4]);
        } else if (d[1] == 0x24 && cur_class == 1 && cur_sub == 2 && d[2] == 0x02 && d[0] >= 8) {
            /* AudioStreaming FORMAT_TYPE: channels, subframe size, bits */
            if (logfile)
                fprintf(logfile, "  format: %d ch, %d bytes, %d bit\n", d[4], d[5], d[6]);
            if (cur_if == audio_iface || audio_iface < 0)
                audio_channels = d[4];
        } else if (d[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
            int addr = d[2], attr = d[3] & 3, maxp = d[4] | d[5] << 8;
            if (logfile)
                fprintf(logfile, "  EP %02X type %d max %d interval %d\n", addr, attr, maxp, d[6]);
            if (cur_class == 3 && attr == 3 && !(addr & 0x80)) {
                hid_out_ep = d;
                hid_iface = cur_if;
            }
            if (cur_class == 1 && attr == 1 && !(addr & 0x80) && !iso_out_ep) {
                iso_out_ep = d;
                audio_iface = cur_if;
                audio_alt = cur_alt;
                iso_max = maxp;
            }
        }
    }
    say("%d Deskriptoren | HID-OUT %s | Audio-OUT %s (IF %d alt %d, %d ch, max %d)", n,
        hid_out_ep ? "ja" : "nein", iso_out_ep ? "ja" : "nein", audio_iface, audio_alt,
        audio_channels, iso_max);
}
/* ---------- HID: lightbar, rumble, speaker ---------- */
static int send_report(int pipe_ctrl, int pipe_hid, int rumble) {
    u8 *r;
    /* plain aligned memory: usbAllocateMemory blocked on real hardware (test 0.4, step 4a) */
    r = memalign(128, 128);
    if (!r)
        return -1;
    memset(r, 0, 64);
    r[0] = 0x02;
    r[1] = 0x01 | 0x02 | 0x10 | 0x20 | 0x80; /* vibration, haptics, headphone+speaker vol, audio */
    r[2] = 0x04 | 0x10;                      /* lightbar + player LEDs */
    r[3] = rumble;                           /* right motor */
    r[4] = rumble;                           /* left motor */
    r[5] = 0x7C;                             /* headphone volume */
    r[6] = 0xFF;                             /* speaker volume */
    r[8] = 0x30;                             /* audio path: built-in speaker */
    r[39] = 0x02;                            /* valid_flag2: lightbar setup */
    r[42] = 0x02;                            /* lightbar setup: take over from default animation */
    r[44] = 0x04;                            /* middle player LED */
    r[45] = 0x00, r[46] = 0xFF, r[47] = 0x40; /* lightbar: green */
    say("4a2: Befehl vorbereitet");
    int rc = -1;
    if (hid_iface >= 0) {
        say("4b: Befehl ueber Steuerleitung (SET_REPORT) ...");
        rc = control2(pipe_ctrl, 0x21, 0x09, 0x0202, (u16)hid_iface, r, 48, "4b SET_REPORT");
    }
    if (rc && pipe_hid >= 0) {
        say("4c: Befehl ueber HID-Leitung ...");
        xfer_done = 0;
        Call c = {CALL_INTERRUPT, pipe_hid, 0, 0, 0, r, NULL, {0}};
        rc = timed(&c, 2000);
        if (!rc)
            rc = wait_xfer(1000);
        say("4c HID-Leitung: %s (0x%08X)", rc == -998 ? "haengt" : rc ? "Fehler" : "OK", (unsigned)rc);
    }
    if (rc == -998)
        return rc; /* the buffer may still be in use by the stuck call: do not free it */
    free(r);
    return rc;
}
/* ---------- isochronous audio ---------- */
#define REQS 32 /* ring: a buffer is reused only after ~250 ms */
static usbIsochRequest reqs[REQS];
static usbHSIsochRequest hsreqs[REQS];
static u8 *bufs[REQS];
static int iso_pipe = -1, packet_bytes, packet_samples, hs;
static volatile int iso_running, iso_errors, iso_ok;
static volatile uint64_t sample_pos;
static uint64_t sample_end;
static float sample_at(uint64_t k, int ch) {
    double t = k / 48000.0;
    if (ch < 2) { /* melody: C E G C, 0.3 s each, then pause */
        static const double f[4] = {523.25, 659.25, 783.99, 1046.5};
        int note = (int)(t / 0.3) % 5;
        if (note == 4)
            return 0;
        double local = fmod(t, 0.3), env = local < 0.02 ? local / 0.02 : local > 0.27 ? (0.3 - local) / 0.03 : 1;
        return (float)(0.5 * env * sin(2 * M_PI * f[note] * t));
    }
    return t < 0.6 ? (float)(0.9 * sin(2 * M_PI * 80 * t)) : 0; /* haptic buzz at the start */
}
static void fill(int i) {
    u8 *b = bufs[i];
    for (int p = 0; p < USB_MAX_ISOCH_PACKETS; p++) {
        for (int s = 0; s < packet_samples; s++) {
            uint64_t k = sample_pos + (uint64_t)s;
            for (int c = 0; c < audio_channels; c++) {
                int v = k < sample_end ? (int)(sample_at(k, c) * 30000) : 0;
                *b++ = (u8)v; /* USB audio is little endian */
                *b++ = (u8)(v >> 8);
            }
        }
        sample_pos += (uint64_t)packet_samples;
    }
}
static void iso_done(s32 result, usbIsochRequest *req, void *arg);
static void iso_done_hs(s32 result, usbHSIsochRequest *req, void *arg);
static int variant;
static int submit(int i) {
    reqs[i].relative_start_frame = hsreqs[i].relative_start_frame = 0;
    return hs ? usbHSIsochronousTransfer(iso_pipe, &hsreqs[i], iso_done_hs, (void *)(intptr_t)i)
              : usbIsochronousTransfer(iso_pipe, &reqs[i], iso_done, (void *)(intptr_t)i);
}
/* The completion callback only marks the request as free. Submitting from inside the callback
 * did not work on real hardware (test 0.5: sound stopped after the first packets), so the test
 * thread refills and resubmits (pump_audio). */
static volatile int req_free[REQS], last_result;
static void resubmit(s32 result, int i) {
    if (result) {
        iso_errors++;
        last_result = result;
    } else
        iso_ok++;
    req_free[i] = 1;
}
static int submit_errors, first_submit_error, submitted;
static uint64_t stream_start;
static double req_ms; /* audio time covered by one request */
/* Paced by the clock: the PS3 did not report finished audio packets (test 0.6: 0 errors, no
 * completions), so requests are submitted every req_ms with ~50 ms lead instead. */
static uint64_t sub_time[REQS];
static int last_submit_error;
/* Variant 1: paced by the clock, each request scheduled for an explicit future frame.
 * Variant 2: at most 4 requests in flight; a request counts as finished on its completion or
 *            after 3x its play time (the PS3 did not report completions in test 0.6).
 * Variant 3: like 2, but only 1 request in flight. */
static int filled = -1;
static int finished_estimate(double timeout) {
    uint64_t now = millis();
    int done = iso_ok + iso_errors, start = submitted > REQS ? submitted - REQS : 0, aged = start;
    for (int k = start; k < submitted; k++)
        if ((double)(now - sub_time[k % REQS]) > timeout)
            aged = k + 1;
    return done > aged ? done : aged;
}
static int underruns, behind, max_gap, under_at[4];
static double lead_ms = 60.0;
static int prefilling;
static uint64_t last_accept;
static void pump_audio(void) {
    double elapsed = (double)(millis() - stream_start);
    /* underrun = everything sent so far is already played: the controller runs dry -> crackle */
    if (!prefilling && submitted && submitted * req_ms < elapsed - 1.0) {
        if (!behind) {
            if (underruns < 4)
                under_at[underruns] = (int)elapsed; /* when it happened (ms after start) */
            underruns++;
        }
        behind = 1;
    } else
        behind = 0;
    uint64_t limit = sample_end + 9600;
    while (iso_running &&
           (uint64_t)submitted * (uint64_t)(packet_samples * USB_MAX_ISOCH_PACKETS) < limit) {
        /* never run more than 60 ms ahead of real time */
        if (!prefilling && submitted * req_ms > elapsed + lead_ms)
            return;
        if (prefilling && submitted * req_ms >= lead_ms)
            return; /* prefill up to the lead */
        /* Variant 1 (A): offer packets as fast as possible, retry at once when the PS3 says "full".
         * Variant 2 (B): 2 in flight, a packet counts as finished after its play time + 2 ms.
         * Variant 3 (C): 1 in flight, finished after its play time + 1 ms (0.8 waited 3x). */
        if (variant == 2 && submitted - finished_estimate(req_ms + 2) >= 2)
            return;
        if (variant == 3 && submitted - finished_estimate(req_ms + 1) >= 1)
            return;
        int i = submitted % REQS;
        if (filled != submitted) { /* a rejected packet is offered again unchanged */
            fill(i);
            filled = submitted;
        }
        int rc = submit(i);
        if (rc) {
            submit_errors++;
            last_submit_error = rc;
            if (!first_submit_error)
                first_submit_error = rc;
            return; /* try again on the next round, never give up early */
        }
        sub_time[i] = millis();
        if (last_accept && (int)(sub_time[i] - last_accept) > max_gap)
            max_gap = (int)(sub_time[i] - last_accept);
        last_accept = sub_time[i];
        submitted++;
    }
}
static void iso_done(s32 result, usbIsochRequest *req, void *arg) {
    (void)req;
    resubmit(result, (int)(intptr_t)arg);
}
static void iso_done_hs(s32 result, usbHSIsochRequest *req, void *arg) {
    (void)req;
    resubmit(result, (int)(intptr_t)arg);
}
static int stream_audio(int pipe_ctrl) {
    /* select the alternate setting with the audio endpoint, set 48 kHz */
    control2(pipe_ctrl, 0x01, 0x0B, (u16)audio_alt, (u16)audio_iface, NULL, 0, "Audio-Schnittstelle an");
    u8 *freq;
    freq = memalign(128, 128);
    if (freq) {
        freq[0] = 0x80, freq[1] = 0xBB, freq[2] = 0x00; /* 48000 */
        control2(pipe_ctrl, 0x22, 0x01, 0x0100, iso_out_ep[2], freq, 3, "48 kHz setzen");
        free(freq);
    }
    u8 speed = 0;
    usbGetDeviceSpeed(dev_id, &speed);
    hs = speed == 2; /* 0 low, 1 full, 2 high */
    int interval = iso_out_ep[6] ? iso_out_ep[6] : 1, period = 1 << (interval - 1);
    int per_second = (hs ? 8000 : 1000) / period; /* packets per second from bInterval */
    packet_samples = 48000 / per_second;
    packet_bytes = packet_samples * audio_channels * 2;
    say("USB-Tempo %s, Paket %d Bytes (max %d)", hs ? "High-Speed" : "Full-Speed", packet_bytes,
        iso_max);
    if (packet_bytes > iso_max) {
        say("Paket zu gross fuer den Endpunkt - Abbruch.");
        return -1;
    }
    iso_pipe = open_pipe(iso_out_ep);
    if (iso_pipe < 0) {
        say("Audio-Pipe: Fehler 0x%08X", (unsigned)iso_pipe);
        return -1;
    }
    for (int i = 0; i < REQS; i++)
        if (!(bufs[i] = memalign(128, (size_t)packet_bytes * USB_MAX_ISOCH_PACKETS + 128))) {
            say("Kein USB-Speicher.");
            return -1;
        }
    sample_pos = 0;
    sample_end = 48000 * 4; /* 4 seconds */
    iso_errors = iso_ok = submit_errors = first_submit_error = last_result = 0;
    for (int i = 0; i < REQS; i++)
        req_free[i] = 0;
    iso_running = 1;
    for (int i = 0; i < REQS; i++) {
        memset(&reqs[i], 0, sizeof(reqs[i]));
        memset(&hsreqs[i], 0, sizeof(hsreqs[i]));
        reqs[i].buffer_base = hsreqs[i].buffer_base = bufs[i];
        reqs[i].num_packets = hsreqs[i].num_packets = USB_MAX_ISOCH_PACKETS;
        for (int p = 0; p < USB_MAX_ISOCH_PACKETS; p++) {
            reqs[i].packets[p].len = (u16)packet_bytes;
            hsreqs[i].packets[p].len = (u16)packet_bytes;
        }
    }
    req_ms = USB_MAX_ISOCH_PACKETS * 1000.0 / per_second;
    submitted = 0;
    stream_start = millis();
    pump_audio(); /* first ~50 ms */
    if (!submitted) {
        say("Audio-Start: Fehler 0x%08X", (unsigned)first_submit_error);
        iso_running = 0;
        return -1;
    }
    say("Ton laeuft 4 s (%.0f ms je Paketgruppe): erst Brummen, dann Melodie ...", req_ms);
    return 0;
}
static int restart_variant(int ctrl, int v) {
    if (iso_pipe >= 0)
        usbClosePipe(iso_pipe); /* drops anything still queued from the previous variant */
    control(ctrl, 0x01, 0x0B, 0, (u16)audio_iface, NULL, 0);
    usleep(50000);
    control(ctrl, 0x01, 0x0B, (u16)audio_alt, (u16)audio_iface, NULL, 0);
    iso_pipe = open_pipe(iso_out_ep);
    if (iso_pipe < 0) {
        say("Variante %d: Audio-Pipe Fehler 0x%08X", v, (unsigned)iso_pipe);
        return -1;
    }
    variant = v;
    sample_pos = 0;
    sample_end = 48000 * 3;
    submitted = iso_ok = iso_errors = submit_errors = 0;
    first_submit_error = last_submit_error = last_result = 0;
    filled = -1;
    underruns = behind = max_gap = 0;
    memset(under_at, 0, sizeof(under_at));
    last_accept = 0;
    iso_running = 1;
    /* Prefill: offer packets until the PS3 says "full" (or 40 tries), then start the clock.
     * Avoids the start-up dropout seen in 0.11/0.12. */
    prefilling = 1;
    stream_start = millis();
    for (int t = 0; t < 40 && iso_running; t++) {
        int before = submitted, errs = submit_errors;
        pump_audio();
        if (submitted * req_ms >= lead_ms || (submitted == before && submit_errors > errs))
            break; /* lead reached or queue full */
        usleep(1000);
    }
    prefilling = 0;
    stream_start = millis();
    say("Vorgefuellt: %d Pakete (%.0f ms)", submitted, submitted * req_ms);
    return 0;
}
/* mode 1: usleep(1000) between rounds, 2: only yield, 3: yield in a high-priority thread */
static volatile int pump_mode, pump_done;
static uint64_t pump_end;
static void pump_loop(void) {
    uint64_t report = 0;
    while (millis() < pump_end && running) {
        pump_audio();
        if (pump_mode == 1)
            usleep(1000);
        else
            sysThreadYield();
        if (millis() >= report) {
            report = millis() + 500;
            int pct = (int)(submitted * req_ms * 100.0 / (double)(millis() - stream_start + 1));
            snprintf(status, sizeof(status), "VARIANTE %d | Echtzeit %d %% | Aussetzer %d | Luecke max %d ms",
                     pump_mode, pct > 100 ? 100 : pct, underruns, max_gap);
            dirty = 1;
        }
    }
}
static void pump_thread(void *arg) {
    (void)arg;
    pump_loop();
    pump_done = 1;
    sysThreadExit(0);
}
static void stop_audio(void) {
    iso_running = 0;
    usleep(100000);
    say("Ton: %d gesendet, %d Rueckmeldungen, %d Fehler (0x%08X), %d Sendefehler (0x%08X)",
        submitted, iso_ok, iso_errors, (unsigned)last_result, submit_errors,
        (unsigned)first_submit_error);
    if (iso_pipe >= 0)
        usbClosePipe(iso_pipe);
    iso_pipe = -1;
    for (int i = 0; i < REQS; i++)
        if (bufs[i]) {
            free(bufs[i]);
            bufs[i] = NULL;
        }
}
/* ---------- test sequence ---------- */
static void run_test(void) {
    count = 0;
    memset(rows, 0, sizeof(rows));
    if (!attached) {
        say("Kein DualSense uebernommen (probe-Aufrufe: %d).", probes);
        say("Controller ab- und wieder anstecken, dann nochmal KREUZ.");
        snprintf(status, sizeof(status), "DualSense nicht gefunden.");
        return;
    }
    say("DualSense uebernommen (Geraet %d).", dev_id);
    hid_out_ep = iso_out_ep = NULL;
    begin_step(1, "Controller auslesen");
    scan_descriptors();
    begin_step(2, "Steuerleitung oeffnen");
    int ctrl = open_pipe(NULL);
    say("Steuer-Pipe: %d", ctrl);
    if (ctrl < 0)
        return;
    begin_step(3, "Konfiguration setzen");
    control2(ctrl, 0x00, 0x09, 1, 0, NULL, 0, "Konfiguration 1");
    begin_step(4, "Lichtleiste gruen + Vibration");
    int hidp = -1;
    say("4a: HID-Schnittstelle %d, HID-Leitung %s", hid_iface, hid_out_ep ? "vorhanden" : "fehlt");
    int rc4 = send_report(ctrl, -1, 0xA0);
    if (rc4 && hid_out_ep) {
        say("4a: HID-Leitung oeffnen ...");
        hidp = open_pipe(hid_out_ep);
        say("4a HID-Leitung: %s (%d)", hidp == -998 ? "haengt" : hidp < 0 ? "Fehler" : "offen", hidp);
        if (hidp < 0)
            hidp = -1;
        else
            rc4 = send_report(ctrl, hidp, 0xA0);
    }
    say("Stufe 1: %s", rc4 ? "Befehl NICHT angekommen" : "Befehl angekommen");
    usleep(500000);
    send_report(ctrl, hidp, 0x00);
    snprintf(status, sizeof(status),
             "Stufe 1 fertig: Lichtleiste GRUEN? Vibriert? In 3 s kommt der Ton ...");
    usleep(3000000);
    begin_step(5, "Ton per USB");
    if (iso_out_ep && !stream_audio(ctrl)) {
        iso_running = 0;
        /* 0.10: identical 11 dropouts / 168 ms gaps in every mode -> the PS3's own USB threads.
         * 0.11 raises the priority of cellUsbd's event/transfer/callback threads. */
        /* 0.11: very high cellUsbd priority + 200 ms lead -> 1 dropout (from 11). Fine tuning: */
        static const char *names[4] = {"", "Prio 50 + Vorrat 200 ms", "Prio 50 + Vorrat 300 ms",
                                       "Prio 50 + Vorrat 500 ms"};
        say("USB-Thread-Prioritaeten vorher: %d / %d / %d", usbGetThreadPriority(0),
            usbGetThreadPriority(1), usbGetThreadPriority(2));
        int best = 0, best_pct = -1, best_under = -1;
        for (int v = 1; v <= 3 && running; v++) {
            int prio = 50;
            int prc = usbSetThreadPriority2(prio, prio, prio);
            lead_ms = v == 1 ? 200.0 : v == 2 ? 300.0 : 500.0;
            if (restart_variant(ctrl, 1)) /* all use the A logic; prefill happens in here */
                continue;
            say("Variante %d (%s): Prioritaet %d -> 0x%08X", v, names[v], prio, (unsigned)prc);
            pump_mode = 1; /* sleeping between rounds was best in 0.10 */
            pump_end = millis() + 3500;
            if (0) {
                sys_ppu_thread_t t;
                pump_done = 0;
                /* lower number = higher priority (normal threads run around 1000) */
                if (sysThreadCreate(&t, pump_thread, NULL, 100, 64 * 1024, 0, (char *)"orbit-audio")) {
                    say("Prioritaets-Thread nicht moeglich, normal weiter.");
                    pump_loop();
                } else
                    while (!pump_done && running)
                        usleep(20000);
            } else
                pump_loop();
            iso_running = 0;
            int pct = (int)(submitted * req_ms * 100.0 / 3000.0);
            if (pct > 100)
                pct = 100;
            if (pct > best_pct)
                best_pct = pct;
            say("V%d: Aussetzer %d (bei %d/%d/%d ms) | Luecke max %d ms | Echtzeit %d %%", v,
                underruns, under_at[0], under_at[1], under_at[2], max_gap, pct);
            if (underruns < best_under || best_under < 0)
                best_under = underruns, best = v;
            usleep(800000); /* short silence between the variants */
        }
        say("Beste Variante: %d (%d Aussetzer)", best, best_under);
        begin_step(6, "Ton beenden");
        stop_audio();
    }
    if (hidp >= 0)
        usbClosePipe(hidp);
    usbClosePipe(ctrl);
    snprintf(status, sizeof(status),
             "Fertig. Gruen? Vibration? Melodie? Wiederholen: neu anstecken. Beenden: PS-Taste.");
}
static void *test_thread(void *arg) {
    (void)arg;
    run_test();
    step = 0;
    test_running = 0;
    dirty = 1;
    return NULL;
}
/* All connected controllers count (the DualSense may be pad 0 and is taken over by the test). */
static unsigned buttons(void) {
    static unsigned held[7];
    padInfo info = {0};
    padData data;
    if (ioPadGetInfo(&info))
        return 0;
    unsigned all = 0;
    for (int i = 0; i < 7; i++) {
        if (!info.status[i]) {
            held[i] = 0;
            continue;
        }
        memset(&data, 0, sizeof(data));
        if (!ioPadGetData(i, &data) && data.len > 0)
            held[i] = (data.BTN_CROSS ? 1 : 0) | (data.BTN_START ? 4 : 0) | (data.BTN_CIRCLE ? 2 : 0);
        all |= held[i];
    }
    return all;
}
int main(void) {
    logfile = fopen(DIR "/controller-test.txt", "w");
    usblog = fopen("/dev_usb000/orbit-controller-test.txt", "w");
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
    int rc = sysModuleLoad(SYSMODULE_USB);
    say("USB-Modul laden: 0x%08X", (unsigned)rc);
    rc = usbInit();
    say("USB starten: 0x%08X", (unsigned)rc);
    /* DualSense 0CE6 .. DualSense Edge 0DF2 (the PS3's own DualShock 3 is 0268: untouched) */
    rc = usbRegisterExtraLdd2(&ops, 0x054C, 0x0CE6, 0x0DF2);
    say("Treiber anmelden: 0x%08X", (unsigned)rc);
    say("Jetzt DualSense per USB anstecken (oder ab + wieder an). Der Test startet von selbst.");
    say("Beenden: PS-Taste -> Spiel beenden (oder KREIS / START).");
    int index = 0, generation = 0, versions[2] = {-1, -1}, was_attached = 0;
    uint64_t auto_at = 0;
    pthread_t worker;
    unsigned previous = 0;
    flip(context, 1);
    while (running) {
        sysUtilCheckCallback();
        unsigned held = buttons(), pressed = held & ~previous;
        previous = held;
        if (pressed & (4 | 2))
            running = 0;
        if (attached != was_attached) {
            was_attached = attached;
            say(attached ? "DualSense erkannt! Test startet automatisch in 2 Sekunden ..."
                         : "DualSense abgesteckt. Zum Wiederholen neu anstecken.");
            auto_at = attached ? millis() + 2000 : 0;
        }
        /* No button needed: the test runs by itself once the controller is attached. */
        int automatic = auto_at && millis() >= auto_at;
        if (automatic)
            auto_at = 0;
        if (((pressed & 1) || automatic) && !test_running) {
            snprintf(status, sizeof(status), "Test laeuft ...");
            dirty = 1;
            test_running = 1;
            if (pthread_create(&worker, NULL, test_thread, NULL)) {
                test_running = 0;
                say("Test-Thread konnte nicht starten.");
            } else
                pthread_detach(worker);
        }
        /* the screen keeps running while the USB steps work; a stuck step is shown */
        if (test_running && step && millis() - step_since > 12000) {
            snprintf(status, sizeof(status), "Schritt %d (%s) haengt seit %d s - Foto machen!", step,
                     step_name, (int)((millis() - step_since) / 1000));
            dirty = 1;
        }
        if (dirty) {
            pthread_mutex_lock(&rows_lock);
            dirty = 0;
            ui_audio_probe(canvas, width, height, status, (const char(*)[100])rows, count,
                           attached, 0, -1);
            pthread_mutex_unlock(&rows_lock);
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
    for (int t = 0; t < 50 && test_running; t++)
        usleep(100000); /* let a running test finish its current step */
    iso_running = 0;
    if (logfile)
        fclose(logfile);
    if (usblog)
        fclose(usblog);
    usbUnregisterExtraLdd(&ops);
    usbEnd();
    sysUtilUnregisterCallback(0);
    ioPadEnd();
    rsxFinish(context, 1);
    ui_close();
    return 0;
}
