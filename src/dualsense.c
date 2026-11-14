/* EXPERIMENTAL: PS5 DualSense over USB as the voice chat headset of Orbit (speaker + microphone),
 * driven directly through cellUsbd. Based on Orbit Controller-Test 0.13 (ORB000003), whose
 * results on real hardware were: claiming, lightbar, rumble and clean speaker sound work;
 * isochronous completions are not reported, so output is paced by the clock with a lead; the
 * PS3's USB threads need a higher priority (11 dropouts -> 1 in 0.11).
 * NOT verified on hardware: the controller microphone and reading the buttons while Orbit owns
 * the controller. Both detect failure and fall back (headset mic / controller handed back).
 * Off by default; switched on in Settings > Audio. */
#include "i18n.h"
#include "orbit.h"
#include "voice.h"
#include <io/pad.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysmodule/sysmodule.h>
#include <unistd.h>
#include <usb/usb.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int want_audio, want_mic, registered, attached, dev_id = -1, ready, stop_all;
static volatile int call_active, streaming, mic_ok, mic_failed, input_ok, input_failed;
static volatile unsigned ds_buttons;
static char note[160];
static pthread_t worker;
static int worker_running;
static void set_note(int key) {
    pthread_mutex_lock(&lock);
    snprintf(note, sizeof(note), "%s", key >= 0 ? T(key) : "");
    pthread_mutex_unlock(&lock);
}
const char *voice_ds_note(void) {
    return note;
}
/* ---------- LDD: Orbit claims the controller ---------- */
static s32 ldd_probe(s32 id) {
    (void)id;
    return 0;
}
static s32 ldd_attach(s32 id) {
    dev_id = id;
    attached = 1;
    return 0;
}
static s32 ldd_detach(s32 id) {
    (void)id;
    attached = 0;
    ready = 0;
    dev_id = -1;
    return 0;
}
static usbLddOps ops = {"OrbitDualSense", ldd_probe, ldd_attach, ldd_detach};
/* ---------- every USB call gets a time limit (a stuck call must never freeze Orbit) ---------- */
typedef struct {
    int kind, a, done, abandoned;
    s32 result;
    void *p;
    s32 len;
    usbDeviceRequest req;
} Call;
enum { C_OPEN, C_CONTROL, C_INTERRUPT };
static volatile int xfer_done, xfer_result, xfer_count;
static void done_cb(s32 result, s32 n, void *arg) {
    (void)arg;
    xfer_result = result;
    xfer_count = n;
    xfer_done = 1;
}
static pthread_mutex_t call_lock = PTHREAD_MUTEX_INITIALIZER;
static void *call_main(void *arg) {
    Call *c = arg;
    s32 r = -1;
    if (c->kind == C_OPEN)
        r = usbOpenPipe(c->a, (usbEndpointDescriptor *)c->p);
    else if (c->kind == C_CONTROL)
        r = usbControlTransfer(c->a, &c->req, c->p, done_cb, NULL);
    else
        r = usbInterruptTransfer(c->a, c->p, c->len, done_cb, NULL);
    pthread_mutex_lock(&call_lock);
    c->result = r;
    c->done = 1;
    int free_it = c->abandoned;
    pthread_mutex_unlock(&call_lock);
    if (free_it)
        free(c);
    return NULL;
}
static s32 timed(const Call *t, int ms) {
    Call *c = malloc(sizeof(*c));
    if (!c)
        return -997;
    *c = *t;
    c->done = c->abandoned = 0;
    pthread_t th;
    if (pthread_create(&th, NULL, call_main, c)) {
        free(c);
        return -996;
    }
    pthread_detach(th);
    uint64_t end = orbit_millis() + (uint64_t)ms;
    for (;;) {
        pthread_mutex_lock(&call_lock);
        if (c->done) {
            s32 r = c->result;
            pthread_mutex_unlock(&call_lock);
            free(c);
            return r;
        }
        if (orbit_millis() >= end) {
            c->abandoned = 1;
            pthread_mutex_unlock(&call_lock);
            return -998;
        }
        pthread_mutex_unlock(&call_lock);
        usleep(2000);
    }
}
static int wait_xfer(int ms) {
    uint64_t end = orbit_millis() + (uint64_t)ms;
    while (!xfer_done && orbit_millis() < end)
        usleep(1000);
    return xfer_done ? xfer_result : -999;
}
static s32 open_pipe(void *ep) {
    Call c = {C_OPEN, dev_id, 0, 0, 0, ep, 0, {0}};
    return timed(&c, 3000);
}
static int control(int pipe, u8 type, u8 req, u16 value, u16 index, void *data, u16 len) {
    Call c = {C_CONTROL, pipe, 0, 0, 0, data, 0, {0}};
    c.req.bmRequestType = type;
    c.req.bRequest = req;
    c.req.wValue = value;
    c.req.wIndex = index;
    c.req.wLength = len;
    xfer_done = 0;
    int rc = timed(&c, 2000);
    return rc ? rc : wait_xfer(1500);
}
/* ---------- descriptors ---------- */
static u8 *hid_in_ep, *iso_out_ep, *iso_in_ep;
static int hid_iface = -1, out_iface = -1, out_alt = 1, out_channels = 4, out_max;
static int in_iface = -1, in_alt = 1, in_channels = 2, in_max;
static void scan(void) {
    int cur_if = -1, cur_alt = 0, cur_class = 0, cur_sub = 0, n = 0, last_fmt = 2;
    void *p = NULL;
    hid_in_ep = iso_out_ep = iso_in_ep = NULL;
    while (n < 96 && (p = usbScanStaticDescriptor(dev_id, p, 0))) {
        u8 *d = p;
        n++;
        if (d[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
            cur_if = d[2];
            cur_alt = d[3];
            cur_class = d[5];
            cur_sub = d[6];
        } else if (d[1] == 0x24 && cur_class == 1 && cur_sub == 2 && d[2] == 0x02 && d[0] >= 8)
            last_fmt = d[4]; /* channels of this alternate setting */
        else if (d[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
            int addr = d[2], attr = d[3] & 3, maxp = d[4] | d[5] << 8;
            if (cur_class == 3 && attr == 3 && (addr & 0x80) && !hid_in_ep) {
                hid_in_ep = d;
                hid_iface = cur_if;
            }
            if (cur_class == 3 && attr == 3 && !(addr & 0x80))
                hid_iface = cur_if;
            if (cur_class == 1 && attr == 1 && !(addr & 0x80) && !iso_out_ep) {
                iso_out_ep = d;
                out_iface = cur_if, out_alt = cur_alt, out_max = maxp, out_channels = last_fmt;
            }
            if (cur_class == 1 && attr == 1 && (addr & 0x80) && !iso_in_ep) {
                iso_in_ep = d;
                in_iface = cur_if, in_alt = cur_alt, in_max = maxp, in_channels = last_fmt;
            }
        }
    }
}
/* speaker on, lightbar Orbit blue (shows that Orbit owns the controller) */
static int send_report(int ctrl, int rumble) {
    u8 *r = memalign(128, 128); /* usbAllocateMemory blocks on real hardware (test 0.4) */
    if (!r)
        return -1;
    memset(r, 0, 64);
    r[0] = 0x02;
    r[1] = 0x01 | 0x02 | 0x10 | 0x20 | 0x80;
    r[2] = 0x04 | 0x10;
    r[3] = r[4] = (u8)rumble;
    r[5] = 0x7C;
    r[6] = 0xFF;
    r[8] = 0x30; /* built-in speaker */
    r[39] = 0x02;
    r[42] = 0x02;
    r[44] = 0x04;
    r[45] = 0x30, r[46] = 0x70, r[47] = 0xFF;
    int rc = hid_iface >= 0 ? control(ctrl, 0x21, 0x09, 0x0202, (u16)hid_iface, r, 48) : -1;
    if (rc != -998)
        free(r);
    return rc;
}
/* ---------- speaker: clock-paced isochronous output (Controller-Test 0.11-0.13) ---------- */
#define REQS 32
static usbIsochRequest oreq[REQS];
static usbHSIsochRequest ohs[REQS];
static u8 *obuf[REQS];
static float mixbuf[2 * 8 * 384];
static int opipe = -1, hs, packet_samples, packet_bytes, submitted, filled = -1;
static double req_ms, lead_ms = 300.0;
static uint64_t stream_start;
static void out_done(s32 r, usbIsochRequest *q, void *a) {
    (void)r, (void)q, (void)a;
}
static void out_done_hs(s32 r, usbHSIsochRequest *q, void *a) {
    (void)r, (void)q, (void)a;
}
static void fill(int i) {
    int frames = packet_samples * USB_MAX_ISOCH_PACKETS;
    if (frames > 8 * 384)
        frames = 8 * 384;
    voice_mix48(mixbuf, frames); /* the same mixer as the TV output: codec, jitter buffer, volumes */
    u8 *b = obuf[i];
    for (int s = 0; s < frames; s++) {
        float m = 0.5f * (mixbuf[2 * s] + mixbuf[2 * s + 1]);
        int v = (int)(m * 30000.0f);
        for (int c = 0; c < out_channels; c++) {
            int x = c < 2 ? v : 0; /* channels 3+4 are the haptic actuators: silent */
            *b++ = (u8)x;
            *b++ = (u8)(x >> 8);
        }
    }
}
static int submit(int i) {
    oreq[i].relative_start_frame = ohs[i].relative_start_frame = 0;
    return hs ? usbHSIsochronousTransfer(opipe, &ohs[i], out_done_hs, NULL)
              : usbIsochronousTransfer(opipe, &oreq[i], out_done, NULL);
}
static void pump(int prefill) {
    double elapsed = (double)(orbit_millis() - stream_start);
    for (int guard = 0; guard < REQS; guard++) {
        if (!prefill && submitted * req_ms > elapsed + lead_ms)
            return;
        if (prefill && submitted * req_ms >= lead_ms)
            return;
        int i = submitted % REQS;
        if (filled != submitted) {
            fill(i);
            filled = submitted;
        }
        if (submit(i))
            return; /* queue full: offered again unchanged on the next round */
        submitted++;
    }
}
static int out_start(int ctrl) {
    control(ctrl, 0x01, 0x0B, (u16)out_alt, (u16)out_iface, NULL, 0);
    u8 *freq = memalign(128, 128);
    if (freq) {
        freq[0] = 0x80, freq[1] = 0xBB, freq[2] = 0x00; /* 48 kHz */
        control(ctrl, 0x22, 0x01, 0x0100, iso_out_ep[2], freq, 3);
        free(freq);
    }
    u8 speed = 0;
    usbGetDeviceSpeed(dev_id, &speed);
    hs = speed == 2;
    int interval = iso_out_ep[6] ? iso_out_ep[6] : 1, period = 1 << (interval - 1);
    int per_second = (hs ? 8000 : 1000) / period;
    packet_samples = 48000 / per_second;
    packet_bytes = packet_samples * out_channels * 2;
    if (packet_bytes > out_max || packet_samples * USB_MAX_ISOCH_PACKETS > 8 * 384)
        return -1;
    opipe = open_pipe(iso_out_ep);
    if (opipe < 0)
        return -1;
    for (int i = 0; i < REQS; i++) {
        if (!obuf[i] && !(obuf[i] = memalign(128, (size_t)packet_bytes * USB_MAX_ISOCH_PACKETS + 128)))
            return -1;
        memset(&oreq[i], 0, sizeof(oreq[i]));
        memset(&ohs[i], 0, sizeof(ohs[i]));
        oreq[i].buffer_base = ohs[i].buffer_base = obuf[i];
        oreq[i].num_packets = ohs[i].num_packets = USB_MAX_ISOCH_PACKETS;
        for (int p = 0; p < USB_MAX_ISOCH_PACKETS; p++)
            oreq[i].packets[p].len = ohs[i].packets[p].len = (u16)packet_bytes;
    }
    req_ms = USB_MAX_ISOCH_PACKETS * 1000.0 / per_second;
    usbSetThreadPriority2(50, 50, 50); /* 0.11: removes almost all dropouts */
    submitted = 0;
    filled = -1;
    stream_start = orbit_millis();
    for (int t = 0; t < 40; t++) { /* prefill the lead before the clock starts (0.13) */
        int before = submitted;
        pump(1);
        if (submitted * req_ms >= lead_ms || submitted == before)
            break;
        usleep(1000);
    }
    stream_start = orbit_millis();
    return submitted ? 0 : -1;
}
static void out_stop(int ctrl) {
    if (opipe >= 0)
        usbClosePipe(opipe);
    opipe = -1;
    if (out_iface >= 0)
        control(ctrl, 0x01, 0x0B, 0, (u16)out_iface, NULL, 0);
}
/* ---------- microphone: isochronous input (not verified on hardware) ---------- */
static usbIsochRequest ireq[4];
static usbHSIsochRequest ihs[4];
static u8 *ibuf[4];
static volatile int in_done[4], in_result[4];
static int ipipe = -1, in_packet, noise_blocks;
static uint64_t mic_since;
static void in_cb(s32 r, usbIsochRequest *q, void *a) {
    (void)q;
    int i = (int)(intptr_t)a;
    in_result[i] = r;
    in_done[i] = 1;
}
static void in_cb_hs(s32 r, usbHSIsochRequest *q, void *a) {
    (void)q;
    int i = (int)(intptr_t)a;
    in_result[i] = r;
    in_done[i] = 1;
}
static int in_submit(int i) {
    in_done[i] = 0;
    for (int p = 0; p < USB_MAX_ISOCH_PACKETS; p++)
        ireq[i].packets[p].len = ihs[i].packets[p].len = (u16)in_packet;
    return hs ? usbHSIsochronousTransfer(ipipe, &ihs[i], in_cb_hs, (void *)(intptr_t)i)
              : usbIsochronousTransfer(ipipe, &ireq[i], in_cb, (void *)(intptr_t)i);
}
static int in_start(int ctrl) {
    if (!iso_in_ep)
        return -1;
    control(ctrl, 0x01, 0x0B, (u16)in_alt, (u16)in_iface, NULL, 0);
    in_packet = iso_in_ep[4] | iso_in_ep[5] << 8;
    if (in_packet > in_max)
        in_packet = in_max;
    ipipe = open_pipe(iso_in_ep);
    if (ipipe < 0)
        return -1;
    for (int i = 0; i < 4; i++) {
        if (!ibuf[i] && !(ibuf[i] = memalign(128, (size_t)in_packet * USB_MAX_ISOCH_PACKETS + 128)))
            return -1;
        memset(&ireq[i], 0, sizeof(ireq[i]));
        memset(&ihs[i], 0, sizeof(ihs[i]));
        ireq[i].buffer_base = ihs[i].buffer_base = ibuf[i];
        ireq[i].num_packets = ihs[i].num_packets = USB_MAX_ISOCH_PACKETS;
        in_submit(i);
    }
    mic_since = orbit_millis();
    return 0;
}
static void in_poll(void) {
    static int16_t pcm[4096], small[2048];
    for (int i = 0; i < 4; i++) {
        if (!in_done[i])
            continue;
        int n = 0;
        if (!in_result[i]) {
            const u8 *b = ibuf[i];
            for (int p = 0; p < USB_MAX_ISOCH_PACKETS; p++) {
                int len = hs ? ihs[i].packets[p].len : ireq[i].packets[p].len;
                int frames = len / (2 * (in_channels ? in_channels : 1));
                for (int f = 0; f < frames && n < 4096; f++) {
                    const u8 *s = b + f * 2 * in_channels;
                    pcm[n++] = (int16_t)(s[0] | s[1] << 8); /* little endian, first channel */
                }
                b += in_packet;
            }
        }
        if (n > 2) {
            /* reject streams that are only noise (mean |x| near random): headset instead */
            double sum = 0;
            for (int k = 0; k < n; k++)
                sum += pcm[k] < 0 ? -pcm[k] : pcm[k];
            if (sum / n > 9000) {
                if (++noise_blocks > 60 && !mic_failed) {
                    mic_failed = 1;
                    mic_ok = 0;
                    set_note(T_DS_MIC_FAILED);
                }
            } else {
                noise_blocks = 0;
                int usable = n - n % 3;
                int k = voice_resample_to16k(pcm, usable, 48000, small, 2048);
                voice_capture(small, k);
                mic_ok = 1;
            }
        }
        in_submit(i);
    }
    /* nothing ever arrived: this PS3 does not report input completions -> headset mic */
    if (!mic_ok && !mic_failed && orbit_millis() - mic_since > 3000) {
        mic_failed = 1;
        set_note(T_DS_MIC_FAILED);
    }
}
static void in_stop(int ctrl) {
    if (ipipe >= 0)
        usbClosePipe(ipipe);
    ipipe = -1;
    if (in_iface >= 0)
        control(ctrl, 0x01, 0x0B, 0, (u16)in_iface, NULL, 0);
}
/* ---------- buttons: HID input report 0x01 (not verified on hardware) ---------- */
static u8 *hid_buf;
static int hid_pipe = -1;
static volatile int hid_pending;
static uint64_t hid_since;
static void hid_cb(s32 r, s32 n, void *a) {
    (void)a;
    if (!r && n >= 11 && hid_buf[0] == 0x01) {
        const u8 *b = hid_buf;
        unsigned v = 0;
        static const unsigned dpad[9] = {1, 1 | 8, 8, 2 | 8, 2, 2 | 4, 4, 1 | 4, 0};
        v |= dpad[(b[8] & 15) > 8 ? 8 : b[8] & 15];
        if (b[8] & 0x10)
            v |= 64; /* square */
        if (b[8] & 0x20)
            v |= 16; /* cross */
        if (b[8] & 0x40)
            v |= 32; /* circle */
        if (b[8] & 0x80)
            v |= 128; /* triangle */
        if (b[9] & 0x01)
            v |= 1024; /* L1 */
        if (b[9] & 0x02)
            v |= 2048; /* R1 */
        if (b[9] & 0x04)
            v |= 16384; /* L2 */
        if (b[9] & 0x08)
            v |= 8192; /* R2 */
        if (b[9] & 0x10)
            v |= 256; /* Create = SELECT */
        if (b[9] & 0x20)
            v |= 512; /* Options = START */
        if (b[9] & 0x80)
            v |= 4096; /* R3 */
        ds_buttons = v;
        input_ok = 1;
    }
    hid_pending = 0;
}
static void hid_poll(void) {
    if (hid_pipe < 0 || hid_pending)
        return;
    hid_pending = 1;
    if (usbInterruptTransfer(hid_pipe, hid_buf, 64, hid_cb, NULL))
        hid_pending = 0;
}
unsigned voice_ds_buttons(void) {
    return ready && input_ok ? ds_buttons : 0;
}
static int other_pad_connected(void) {
    padInfo info;
    memset(&info, 0, sizeof(info));
    if (ioPadGetInfo(&info))
        return 0;
    for (int i = 0; i < 7; i++)
        if (info.status[i])
            return 1;
    return 0;
}
/* ---------- worker ---------- */
static void unregister(void) {
    if (registered) {
        usbUnregisterExtraLdd(&ops);
        registered = 0;
    }
}
static void *worker_main(void *arg) {
    (void)arg;
    int ctrl = -1, out_on = 0, in_on = 0;
    while (!stop_all) {
        if (!attached) {
            if (ctrl >= 0) {
                usbClosePipe(ctrl);
                ctrl = -1;
            }
            out_on = in_on = 0;
            ready = 0;
            if (want_audio || want_mic)
                set_note(T_DS_PLUG_IN);
            usleep(100000);
            continue;
        }
        if (ctrl < 0) {
            scan();
            ctrl = open_pipe(NULL);
            if (ctrl < 0) {
                set_note(T_DS_FAILED);
                usleep(1000000);
                continue;
            }
            control(ctrl, 0x00, 0x09, 1, 0, NULL, 0);
            send_report(ctrl, 0x60);
            usleep(150000);
            send_report(ctrl, 0);
            if (!hid_buf)
                hid_buf = memalign(128, 128);
            hid_pipe = hid_in_ep && hid_buf ? open_pipe(hid_in_ep) : -1;
            hid_since = orbit_millis();
            input_ok = input_failed = 0;
            ready = 1;
            set_note(T_DS_READY);
        }
        hid_poll();
        if (!input_ok && !input_failed && orbit_millis() - hid_since > 5000) {
            input_failed = 1;
            if (!other_pad_connected()) {
                /* buttons unreadable and no other controller: give the DualSense back */
                set_note(T_DS_RELEASED);
                out_stop(ctrl);
                usbClosePipe(ctrl);
                ctrl = -1;
                unregister();
                attached = 0;
                want_audio = want_mic = 0;
                continue; /* idle until the setting changes */
            }
            set_note(T_DS_NO_BUTTONS);
        }
        int need_out = want_audio && call_active, need_in = want_mic && call_active && !mic_failed;
        if (need_out && !out_on) {
            out_on = !out_start(ctrl);
            streaming = out_on;
            set_note(out_on ? T_DS_PLAYING : T_DS_FAILED);
        } else if (!need_out && out_on) {
            out_stop(ctrl);
            out_on = streaming = 0;
        }
        if (need_in && !in_on) {
            mic_ok = 0;
            in_on = !in_start(ctrl);
            if (!in_on) {
                mic_failed = 1;
                set_note(T_DS_MIC_FAILED);
            }
        } else if (!need_in && in_on) {
            in_stop(ctrl);
            in_on = 0;
        }
        if (out_on)
            pump(0);
        if (in_on)
            in_poll();
        usleep(1000);
    }
    if (ctrl >= 0) {
        out_stop(ctrl);
        in_stop(ctrl);
        if (hid_pipe >= 0)
            usbClosePipe(hid_pipe);
        usbClosePipe(ctrl);
    }
    hid_pipe = -1;
    streaming = ready = 0;
    return NULL;
}
static int usb_loaded;
void voice_ds_configure(int audio, int mic) {
    want_audio = audio;
    want_mic = mic;
    mic_failed = 0;
    if ((audio || mic) && !registered) {
        if (!usb_loaded) {
            if (sysModuleLoad(SYSMODULE_USB) || usbInit()) {
                set_note(T_DS_FAILED);
                return;
            }
            usb_loaded = 1;
        }
        if (usbRegisterExtraLdd2(&ops, 0x054C, 0x0CE6, 0x0DF2)) {
            set_note(T_DS_FAILED);
            return;
        }
        registered = 1;
        set_note(T_DS_PLUG_IN);
    }
    if ((audio || mic) && !worker_running) {
        stop_all = 0;
        worker_running = !pthread_create(&worker, NULL, worker_main, NULL);
    }
    if (!audio && !mic && worker_running) {
        stop_all = 1;
        pthread_join(worker, NULL);
        worker_running = 0;
        unregister();
        set_note(-1);
    }
}
void voice_ds_call(int active) {
    call_active = active;
}
int voice_ds_audio_active(void) {
    return streaming;
}
int voice_ds_mic_active(void) {
    return mic_ok && !mic_failed;
}
