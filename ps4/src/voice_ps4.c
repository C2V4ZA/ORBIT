/* ORBIT PS4 port: audio device layer for voice chat and UI sounds.
 * Output: one sceAudioOut port (48 kHz, stereo float, 256-frame blocks) that mixes the voice
 * chat (voice_mix48) and the UI sounds (sfx_mix48). sceAudioOutOutput blocks until the previous
 * block is consumed, which paces the thread.
 * Microphone: sceAudioIn. OpenOrbis only ships stubs without prototypes for it; the prototype
 * below follows the open-source shadPS4 emulator's description of the call:
 *   sceAudioInOpen(userId, type, index, len, freq, param) -> handle
 *   sceAudioInInput(handle, dest) -> samples read (blocking)
 * It is UNVERIFIED on real hardware. Every open attempt and result code is shown in
 * Settings > Audio (voice_mic_info), so a hardware test tells which variant works. */
#include "voice.h"
#include "sfx.h"
#include "i18n.h"
#include "ps4_host.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <orbis/AudioOut.h>
#include <orbis/libkernel.h>

/* loaded at run time (ps4_sym): a missing libSceAudioIn only switches the microphone off */
static int32_t (*sceAudioInOpen)(int32_t user, uint32_t type, uint32_t index, uint32_t len, uint32_t freq,
                                 uint32_t param);
static int32_t (*sceAudioInInput)(int32_t handle, void *dest);
static int32_t (*sceAudioInClose)(int32_t handle);
static int audio_in_ready(void) {
    static int looked;
    if (!looked) {
        looked = 1;
        sceAudioInOpen = ps4_sym("libSceAudioIn.sprx", "sceAudioInOpen");
        sceAudioInInput = ps4_sym("libSceAudioIn.sprx", "sceAudioInInput");
        sceAudioInClose = ps4_sym("libSceAudioIn.sprx", "sceAudioInClose");
    }
    return sceAudioInOpen && sceAudioInInput && sceAudioInClose;
}

#define BLOCK 256
static pthread_mutex_t dev_mutex = PTHREAD_MUTEX_INITIALIZER;
static int out_port = -1, out_init, out_users; /* users: 1 = sfx, 2 = voice */
static volatile int out_running;
static pthread_t out_thread;
static int mic_handle = -1, mic_rate, mic_len;
static volatile int mic_running;
static pthread_t mic_thread;
static char mic_info[160];
static int mic_tries[4];
/* 1.3: how the output port was opened (shown with the microphone result in Settings > Audio) */
static char out_info[120];
static int out_float = 1;
static unsigned out_errors;

static void *out_main(void *arg) {
    (void)arg;
    static float buf[2][BLOCK * 2];
    static int16_t pcm[2][BLOCK * 2];
    int which = 0;
    while (out_running) {
        float *b = buf[which];
        memset(b, 0, sizeof(buf[0]));
        if (out_users & 2)
            voice_mix48(b, BLOCK);
        if (out_users & 1)
            sfx_mix48(b, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) /* the two mixers add up: keep it in range */
            b[i] = b[i] > 1.0f ? 1.0f : b[i] < -1.0f ? -1.0f : b[i];
        const void *block = b;
        if (!out_float) { /* the port was opened as signed 16-bit stereo */
            for (int i = 0; i < BLOCK * 2; i++)
                pcm[which][i] = (int16_t)(b[i] * 32767.0f);
            block = pcm[which];
        }
        int rc = sceAudioOutOutput(out_port, block);
        if (rc < 0) {
            if (out_errors++ < 5)
                ps4_log("audio out output: %08X", (unsigned)rc);
            sceKernelUsleep(5000);
        }
        which ^= 1;
    }
    sceAudioOutOutput(out_port, NULL); /* wait for the last block */
    return NULL;
}
/* opens the shared output port on first use */
static int out_start(int user_bit) {
    pthread_mutex_lock(&dev_mutex);
    if (out_port < 0) {
        if (!out_init) {
            int rc = sceAudioOutInit();
            if (rc < 0 && rc != (int)0x8026000E) { /* "already initialised" is fine */
                pthread_mutex_unlock(&dev_mutex);
                return -1;
            }
            out_init = 1;
        }
        /* 1.3: Test 11 was silent on a real PS4. The main port is normally opened for the system
         * user (0xFF, as PS4 homebrew does); the signed-in user and 16-bit output are fallbacks.
         * Every attempt is logged and shown in Settings > Audio. */
        int user = ps4_initial_user >= 0 ? ps4_initial_user : 0xFF;
        const struct {
            int32_t user;
            uint32_t format;
        } tries[4] = {{0xFF, ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO},
                      {0xFF, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO},
                      {user, ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO},
                      {user, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO}};
        int codes[4] = {0, 0, 0, 0}, used = -1;
        for (int i = 0; i < 4 && used < 0; i++) {
            if (i >= 2 && user == 0xFF)
                break; /* same as the first two */
            int p = sceAudioOutOpen(tries[i].user, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, BLOCK, 48000, tries[i].format);
            codes[i] = p;
            ps4_log("audio out open try %d (user %X, %s): %08X", i, (unsigned)tries[i].user,
                    tries[i].format == ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO ? "float" : "s16", (unsigned)p);
            if (p >= 0) {
                out_port = p;
                out_float = tries[i].format == ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO;
                used = i;
            }
        }
        if (used < 0) {
            snprintf(out_info, sizeof(out_info), "Audio out failed: %08X %08X %08X %08X", (unsigned)codes[0],
                     (unsigned)codes[1], (unsigned)codes[2], (unsigned)codes[3]);
            out_port = -1;
            pthread_mutex_unlock(&dev_mutex);
            return -1;
        }
        snprintf(out_info, sizeof(out_info), "Audio out: port %d, %s user, %s", out_port,
                 tries[used].user == 0xFF ? "system" : "own", out_float ? "float" : "16-bit");
        int32_t vol[2] = {32768, 32768}; /* SCE_AUDIO_VOLUME_0DB */
        sceAudioOutSetVolume(out_port, 3, vol); /* channel flags: L | R */
        out_running = 1;
        if (pthread_create(&out_thread, NULL, out_main, NULL)) {
            out_running = 0;
            sceAudioOutClose(out_port);
            out_port = -1;
            pthread_mutex_unlock(&dev_mutex);
            return -1;
        }
    }
    out_users |= user_bit;
    pthread_mutex_unlock(&dev_mutex);
    return 0;
}
static void out_stop(int user_bit) {
    pthread_mutex_lock(&dev_mutex);
    out_users &= ~user_bit;
    if (!out_users && out_port >= 0) {
        out_running = 0;
        pthread_mutex_unlock(&dev_mutex);
        pthread_join(out_thread, NULL);
        pthread_mutex_lock(&dev_mutex);
        sceAudioOutClose(out_port);
        out_port = -1;
    }
    pthread_mutex_unlock(&dev_mutex);
}

/* ---------- UI sounds ---------- */
int sfx_device_start(void) {
    return out_start(1);
}
void sfx_device_stop(void) {
    out_stop(1);
}

/* ---------- microphone ---------- */
static void *mic_main(void *arg) {
    (void)arg;
    static int16_t raw[1024], small[1024];
    while (mic_running) {
        int n = sceAudioInInput(mic_handle, raw);
        if (n < 0) {
            sceKernelUsleep(10000);
            continue;
        }
        if (n == 0 || n > mic_len)
            n = mic_len; /* some firmwares return 0 for "one full block" */
        if (mic_rate == 16000)
            voice_capture(raw, n);
        else {
            int k = voice_resample_to16k(raw, n, mic_rate, small, 1024);
            voice_capture(small, k);
        }
    }
    return NULL;
}
static int mic_open(void) {
    /* type 0 = voice chat, 1 = general; 16 kHz first (no resampling), then 48 kHz.
     * param 0 = signed 16-bit mono. len = samples per block. */
    static const struct {
        uint32_t type, freq, len;
    } tries[4] = {{0, 16000, 256}, {1, 16000, 256}, {0, 48000, 768}, {1, 48000, 768}};
    int user = ps4_initial_user >= 0 ? ps4_initial_user : 0xFF;
    if (!audio_in_ready()) {
        snprintf(mic_info, sizeof(mic_info), "libSceAudioIn not available");
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        int h = sceAudioInOpen(user, tries[i].type, 0, tries[i].len, tries[i].freq, 0);
        mic_tries[i] = h;
        if (h >= 0) {
            mic_handle = h;
            mic_rate = (int)tries[i].freq;
            mic_len = (int)tries[i].len;
            ps4_log("mic open try %d: handle %d", i, h);
            snprintf(mic_info, sizeof(mic_info), "sceAudioIn type %u  %u Hz  block %u", tries[i].type,
                     tries[i].freq, tries[i].len);
            return 0;
        }
    }
    ps4_log("mic open failed: %08X %08X %08X %08X", (unsigned)mic_tries[0], (unsigned)mic_tries[1],
            (unsigned)mic_tries[2], (unsigned)mic_tries[3]);
    snprintf(mic_info, sizeof(mic_info), "sceAudioIn failed: %08X %08X %08X %08X", (unsigned)mic_tries[0],
             (unsigned)mic_tries[1], (unsigned)mic_tries[2], (unsigned)mic_tries[3]);
    return -1;
}
const char *voice_mic_info(void) { /* output port + microphone, for Settings > Audio */
    static char both[300];
    snprintf(both, sizeof(both), "%s%s%s", out_info, out_info[0] && mic_info[0] ? " | " : "", mic_info);
    return both;
}
int voice_device_has_mic(void) {
    return mic_handle >= 0;
}
int voice_device_start(char *message, size_t capacity) {
    if (capacity)
        message[0] = 0;
    voice_reset();
    if (out_start(2)) {
        snprintf(message, capacity, "%s", "Audio output could not be opened");
        return -1;
    }
    if (mic_handle < 0 && mic_open() == 0) {
        mic_running = 1;
        if (pthread_create(&mic_thread, NULL, mic_main, NULL)) {
            mic_running = 0;
            sceAudioInClose(mic_handle);
            mic_handle = -1;
        }
    }
    if (mic_handle < 0)
        snprintf(message, capacity, "%s", T(T_NO_MIC));
    return 0;
}
void voice_device_stop(void) {
    if (mic_handle >= 0) {
        mic_running = 0;
        pthread_join(mic_thread, NULL); /* sceAudioInInput returns after one block */
        sceAudioInClose(mic_handle);
        mic_handle = -1;
    }
    out_stop(2);
}

/* the PS3-only DualSense-over-USB path does not exist on the PS4 (the DS4 is a normal pad) */
void voice_ds_configure(int audio, int mic) {
    (void)audio;
    (void)mic;
}
void voice_ds_call(int active) {
    (void)active;
}
int voice_ds_audio_active(void) {
    return 0;
}
int voice_ds_mic_active(void) {
    return 0;
}
unsigned voice_ds_buttons(void) {
    return 0;
}
const char *voice_ds_note(void) {
    return "";
}
