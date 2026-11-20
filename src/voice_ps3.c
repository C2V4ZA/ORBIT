/* PS3 voice devices: cellMic raw capture (as verified with Orbit Audio Test 0.2) and a libaudio
 * 2-channel port. Runs in its own thread so the UI frame rate is never affected. */
#include "i18n.h"
#include "mic_probe.h"
#include "orbit.h"
#include "voice.h"
#include <audio/audio.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>
#include <unistd.h>
/* 1.0.2: the sound thread is a high-priority PS3 thread. In 1.0.0/1.0.1 it shared the CPU with the
 * much heavier 1.0 interface, fell behind, and the PS3 replayed old sound blocks: a constant buzz
 * instead of the voice. */
static sys_ppu_thread_t thread;
static volatile int running, has_mic;
static int mic_loaded, mic_init, mic_open, mic_started, mic_device = -1, audio_init, port_open;
static u32 port;
static audioPortConfig config;
static OrbitMicFormat format;
static int order_votes, big_endian = 1, data_order = -1;
static volatile int underruns;
static char mic_info[120];
static int sfx_port_open_flag(void);
const char *voice_mic_info(void) {
    if (!has_mic)
        return "";
    snprintf(mic_info, sizeof(mic_info), "#%d  %u Hz  %d ch  %d bit  type %d (%s)  data %s  gaps %d",
             mic_device, (unsigned)format.sample_rate, format.channels, format.bits, format.type,
             big_endian ? "BE" : "LE", data_order < 0 ? "?" : data_order ? "BE" : "LE", underruns);
    return mic_info;
}
static void mic_close(void) {
    if (mic_started)
        cellMicStop(mic_device);
    if (mic_open)
        cellMicClose(mic_device);
    if (mic_init)
        cellMicEnd();
    if (mic_loaded)
        sysModuleUnload(SYSMODULE_MIC);
    mic_started = mic_open = mic_init = mic_loaded = 0;
    has_mic = 0;
}
static int mic_start(void) {
    if (sysModuleLoad(SYSMODULE_MIC))
        return -1;
    mic_loaded = 1;
    if (cellMicInit())
        return -1;
    mic_init = 1;
    /* The headset can sit on any device slot: use the first one that reports a microphone. */
    for (int i = 0; i < 8 && mic_device < 0; i++)
        if (cellMicIsAttached(i) > 0)
            mic_device = i;
    if (mic_device < 0)
        return -1;
    if (cellMicOpenRaw(mic_device, 48000, 1) && cellMicOpenRaw(mic_device, 16000, 1))
        return -1;
    mic_open = 1;
    memset(&format, 0, sizeof(format));
    if (cellMicGetFormatRaw(mic_device, &format) || format.bits != 16 || format.subframe != 2)
        return -1;
    if (cellMicStart(mic_device))
        return -1;
    mic_started = 1;
    has_mic = 1;
    order_votes = format.type ? 1 : -1;
    big_endian = format.type != 0; /* exactly as in 0.9.5, which worked on real hardware */
    return 0;
}
static void capture(void) {
    static uint8_t raw[8192];
    static int16_t pcm[4096 + 3], small[2048], one[4096];
    static int carry;
    for (int round = 0; round < 4; round++) {
        int32_t n = cellMicReadRaw(mic_device, raw, sizeof(raw));
        if (n <= 0 || n > (int32_t)sizeof(raw))
            return;
        int channels = format.channels ? format.channels : 1, stride = 2 * channels;
        int count = n / stride;
        if (count > 4096 - carry)
            count = 4096 - carry;
        /* byte order as reported by the PS3 (proven in 0.9.5); the data-based guess is only
         * shown in the settings as a diagnostic */
        data_order = voice_pcm16_order(raw, count, stride, &order_votes);
        voice_pcm16_decode(raw, count, stride, big_endian, one);
        memcpy(pcm + carry, one, (size_t)count * sizeof(int16_t));
        carry += count;
        int rate = format.sample_rate ? (int)format.sample_rate : 48000;
        int usable = rate == 48000 ? carry - carry % 3 : carry;
        int k = voice_resample_to16k(pcm, usable, rate, small, 2048);
        memmove(pcm, pcm + usable, (size_t)(carry - usable) * sizeof(int16_t));
        carry -= usable;
        voice_capture(small, k);
    }
}
static void device_main(void *arg) {
    (void)arg;
    u64 last = *(volatile u64 *)(u64)config.readIndex;
    const int blocks = (int)config.numBlocks;
    float *base = (float *)(u64)config.audioDataStart;
    const size_t block_bytes = sizeof(float) * 2 * AUDIO_BLOCK_SAMPLES;
    while (running) {
        u64 now = *(volatile u64 *)(u64)config.readIndex;
        int advanced = (int)((now + (u64)blocks - last) % (u64)blocks);
        if (advanced > blocks / 2)
            underruns++; /* fell behind: the blocks below were already silenced */
        /* Fill each block when it is half a buffer ahead of the hardware read position, and
         * silence every block that has just been played: if this thread is ever late, the PS3
         * plays silence instead of repeating old sound (that repetition was the buzz). */
        while (last != now) {
            memset(base + last * 2 * AUDIO_BLOCK_SAMPLES, 0, block_bytes);
            last = (last + 1) % (u64)blocks;
            u64 target = (last + (u64)blocks / 2) % (u64)blocks;
            float *block = base + target * 2 * AUDIO_BLOCK_SAMPLES;
            if (voice_ds_audio_active()) /* the DualSense speaker pulls the mix itself */
                memset(block, 0, block_bytes);
            else
                voice_mix48(block, AUDIO_BLOCK_SAMPLES);
        }
        /* microphone after playback: playback is the time-critical part */
        if (has_mic && !voice_ds_mic_active())
            capture();
        usleep(1000);
    }
    sysThreadExit(0);
}
int voice_device_start(char *message, size_t cap) {
    if (running)
        return 0;
    voice_reset();
    message[0] = 0;
    if (mic_start()) {
        mic_close();
        snprintf(message, cap, "%s", T(T_NO_MIC));
    }
    sysModuleLoad(SYSMODULE_AUDIO);
    if (audioInit() && !audio_init) {
        mic_close();
        snprintf(message, cap, "%s", T(T_AUDIO_START_FAILED));
        return -1;
    }
    audio_init = 1;
    audioPortParam p;
    memset(&p, 0, sizeof(p));
    p.numChannels = AUDIO_PORT_2CH;
    p.numBlocks = AUDIO_BLOCK_16; /* 85 ms ring, refilled 42 ms ahead */
    p.attrib = AUDIO_PORT_INITLEVEL;
    p.level = 1.0f;
    if (audioPortOpen(&p, &port) || audioGetPortConfig(port, &config)) {
        mic_close();
        snprintf(message, cap, "%s", T(T_AUDIO_BUSY));
        return -1;
    }
    port_open = 1;
    memset((void *)(u64)config.audioDataStart, 0,
           config.numBlocks * 2 * AUDIO_BLOCK_SAMPLES * sizeof(float));
    audioPortStart(port);
    running = 1;
    underruns = 0;
    voice_ds_call(1);
    /* priority 300: far above the interface and network threads (normal is about 1000) */
    if (sysThreadCreate(&thread, device_main, NULL, 300, 64 * 1024, THREAD_JOINABLE,
                        (char *)"orbit-voice")) {
        running = 0;
        voice_device_stop();
        snprintf(message, cap, "%s", T(T_VOICE_START_FAILED));
        return -1;
    }
    return 0;
}
void voice_device_stop(void) {
    voice_ds_call(0);
    if (running) {
        running = 0;
        u64 rv;
        sysThreadJoin(thread, &rv);
    }
    if (port_open) {
        audioPortStop(port);
        audioPortClose(port);
        port_open = 0;
    }
    if (audio_init && !sfx_port_open_flag()) { /* the UI sound port may still use libaudio */
        audioQuit();
        audio_init = 0;
    }
    mic_close();
    mic_device = -1;
    voice_reset();
}
int voice_device_has_mic(void) {
    return has_mic;
}

/* ---------- 1.2: UI sounds on their own audio port ----------
 * A second 2-channel port with its own thread, so the voice code stays untouched. Both share the
 * libaudio initialisation: whoever stops last calls audioQuit. */
#include "sfx.h"
static sys_ppu_thread_t sfx_thread;
static volatile int sfx_running;
static int sfx_port_open;
static u32 sfx_port;
static audioPortConfig sfx_config;
static void sfx_main(void *arg) {
    (void)arg;
    u64 last = *(volatile u64 *)(u64)sfx_config.readIndex;
    const int blocks = (int)sfx_config.numBlocks;
    float *base = (float *)(u64)sfx_config.audioDataStart;
    const size_t block_bytes = sizeof(float) * 2 * AUDIO_BLOCK_SAMPLES;
    while (sfx_running) {
        u64 now = *(volatile u64 *)(u64)sfx_config.readIndex;
        while (last != now) { /* same scheme as the voice port: silence what was played */
            memset(base + last * 2 * AUDIO_BLOCK_SAMPLES, 0, block_bytes);
            last = (last + 1) % (u64)blocks;
            u64 target = (last + (u64)blocks / 2) % (u64)blocks;
            float *block = base + target * 2 * AUDIO_BLOCK_SAMPLES;
            memset(block, 0, block_bytes);
            sfx_mix48(block, AUDIO_BLOCK_SAMPLES);
        }
        usleep(2000);
    }
    sysThreadExit(0);
}
int sfx_device_start(void) {
    if (sfx_running)
        return 0;
    sysModuleLoad(SYSMODULE_AUDIO);
    if (!audio_init) {
        if (audioInit())
            return -1;
        audio_init = 1;
    }
    audioPortParam p;
    memset(&p, 0, sizeof(p));
    p.numChannels = AUDIO_PORT_2CH;
    p.numBlocks = AUDIO_BLOCK_8;
    p.attrib = AUDIO_PORT_INITLEVEL;
    p.level = 1.0f;
    if (audioPortOpen(&p, &sfx_port) || audioGetPortConfig(sfx_port, &sfx_config))
        return -1;
    sfx_port_open = 1;
    memset((void *)(u64)sfx_config.audioDataStart, 0, sfx_config.numBlocks * 2 * AUDIO_BLOCK_SAMPLES * sizeof(float));
    audioPortStart(sfx_port);
    sfx_running = 1;
    /* priority 600: below the voice thread (300), above the interface (about 1000) */
    if (sysThreadCreate(&sfx_thread, sfx_main, NULL, 600, 32 * 1024, THREAD_JOINABLE, (char *)"orbit-sfx")) {
        sfx_running = 0;
        sfx_device_stop();
        return -1;
    }
    return 0;
}
void sfx_device_stop(void) {
    if (sfx_running) {
        sfx_running = 0;
        u64 rv;
        sysThreadJoin(sfx_thread, &rv);
    }
    if (sfx_port_open) {
        audioPortStop(sfx_port);
        audioPortClose(sfx_port);
        sfx_port_open = 0;
    }
    if (audio_init && !port_open) {
        audioQuit();
        audio_init = 0;
    }
}
static int sfx_port_open_flag(void) {
    return sfx_port_open;
}
