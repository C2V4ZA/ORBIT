/* ORBIT UI sounds: every sound is a few sine partials with a short attack and an exponential
 * decay, computed while mixing. Original sounds, no sample files. sfx_play() may be called from
 * the UI thread while the audio thread mixes: requests go through a small lock-free queue. */
#include "sfx.h"
#include <math.h>
#include <string.h>

typedef struct {
    float start, dur, f0, f1, amp; /* seconds, Hz, linear */
} Note;
typedef struct {
    int count;
    Note n[4];
} Sound;
static const Sound sounds[SFX_COUNT] = {
    [SFX_MOVE] = {2, {{0, .05f, 1320, 1320, .06f}, {0, .03f, 2640, 2640, .015f}}},
    [SFX_SELECT] = {2, {{0, .11f, 880, 880, .1f}, {.05f, .16f, 1320, 1320, .11f}}},
    [SFX_BACK] = {2, {{0, .1f, 1320, 1320, .09f}, {.05f, .14f, 880, 880, .09f}}},
    [SFX_TAB] = {2, {{0, .09f, 600, 900, .07f}, {0, .09f, 1200, 1800, .02f}}},
    [SFX_MESSAGE] = {2, {{0, .25f, 1046.5f, 1046.5f, .12f}, {.07f, .3f, 1568, 1568, .12f}}},
    [SFX_NOTIFY] = {3, {{0, .3f, 784, 784, .11f}, {.08f, .3f, 988, 988, .11f}, {.16f, .42f, 1175, 1175, .12f}}},
    [SFX_ERROR] = {2, {{0, .18f, 220, 220, .12f}, {0, .18f, 233, 233, .1f}}},
    [SFX_SEND] = {2, {{0, .12f, 700, 1400, .09f}, {0, .12f, 1400, 2800, .02f}}},
};
#define VOICES 6
#define QUEUE 16
static struct {
    int id;
    int pos; /* samples since start */
} voice[VOICES];
static volatile int queue[QUEUE];
static volatile unsigned q_write, q_read;
static volatile int volume = 60, duck;

void sfx_reset(void) {
    memset(voice, 0, sizeof(voice));
    for (int i = 0; i < VOICES; i++)
        voice[i].id = -1;
    q_read = q_write;
    volume = 60;
    duck = 0;
}
void sfx_play(int id) {
    if (id < 0 || id >= SFX_COUNT || volume <= 0)
        return;
    unsigned w = q_write;
    if (w - q_read >= QUEUE)
        return; /* full: drop, a missed click is better than a burst */
    queue[w % QUEUE] = id;
    __sync_synchronize();
    q_write = w + 1;
}
void sfx_set_volume(int percent) {
    volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}
int sfx_volume(void) {
    return volume;
}
void sfx_set_duck(int in_call) {
    duck = in_call != 0;
}
static float length(int id) {
    float end = 0;
    for (int k = 0; k < sounds[id].count; k++)
        end = fmaxf(end, sounds[id].n[k].start + sounds[id].n[k].dur);
    return end;
}
int sfx_active(void) {
    if (q_write != q_read)
        return 1;
    for (int i = 0; i < VOICES; i++)
        if (voice[i].id >= 0)
            return 1;
    return 0;
}
void sfx_mix48(float *out, int frames) {
    static int init;
    if (!init) {
        for (int i = 0; i < VOICES; i++)
            voice[i].id = -1;
        init = 1;
    }
    while (q_read != q_write) { /* start queued sounds; the same sound restarts instead of stacking */
        int id = queue[q_read % QUEUE];
        __sync_synchronize();
        q_read++;
        int slot = -1;
        for (int i = 0; i < VOICES && slot < 0; i++)
            if (voice[i].id == id && voice[i].pos < 2400)
                slot = i;
        for (int i = 0; i < VOICES && slot < 0; i++)
            if (voice[i].id < 0)
                slot = i;
        if (slot < 0) { /* all busy: replace the oldest */
            slot = 0;
            for (int i = 1; i < VOICES; i++)
                if (voice[i].pos > voice[slot].pos)
                    slot = i;
        }
        voice[slot].id = id;
        voice[slot].pos = 0;
    }
    float gain = volume / 100.0f * (duck ? 0.35f : 1.0f);
    if (gain <= 0) {
        for (int i = 0; i < VOICES; i++)
            voice[i].id = -1;
        return;
    }
    for (int v = 0; v < VOICES; v++) {
        if (voice[v].id < 0)
            continue;
        const Sound *s = &sounds[voice[v].id];
        float end = length(voice[v].id);
        for (int f = 0; f < frames; f++) {
            float t = (voice[v].pos + f) / 48000.0f, sum = 0;
            if (t >= end)
                break;
            for (int k = 0; k < s->count; k++) {
                const Note *n = &s->n[k];
                float u = t - n->start;
                if (u < 0 || u >= n->dur)
                    continue;
                float env = u < .004f ? u / .004f : expf(-(u - .004f) * 5.0f / n->dur);
                float tail = n->dur - u;
                if (tail < .006f)
                    env *= tail / .006f; /* no click at the end */
                float phase = 6.2831853f * (n->f0 * u + (n->f1 - n->f0) * u * u / (2 * n->dur));
                sum += sinf(phase) * n->amp * env;
            }
            sum *= gain;
            out[2 * f] += sum;
            out[2 * f + 1] += sum;
        }
        voice[v].pos += frames;
        if (voice[v].pos / 48000.0f >= end)
            voice[v].id = -1;
    }
}
#if !defined(__PPU__) && !defined(ORBIT_PS4) /* PS3: voice_ps3.c, PS4: voice_ps4.c */
int sfx_device_start(void) {
    return 0;
}
void sfx_device_stop(void) {
}
#endif
