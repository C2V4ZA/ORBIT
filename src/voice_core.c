/* Portable voice core: VAD, IMA-ADPCM, jitter buffers and a 48 kHz mixer. */
#include "voice.h"
#include "orbit.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static const int steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int index_delta[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
typedef struct {
    int predictor, index;
} Adpcm;
static int clamp16(int v) {
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}
static int adpcm_step(Adpcm *s, int code) {
    int step = steps[s->index], diff = step >> 3;
    if (code & 4)
        diff += step;
    if (code & 2)
        diff += step >> 1;
    if (code & 1)
        diff += step >> 2;
    s->predictor = clamp16(code & 8 ? s->predictor - diff : s->predictor + diff);
    s->index += index_delta[code];
    s->index = s->index < 0 ? 0 : s->index > 88 ? 88 : s->index;
    return s->predictor;
}
static int adpcm_code(Adpcm *s, int sample) {
    int diff = sample - s->predictor, code = 0, step = steps[s->index];
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    if (diff >= step >> 1) {
        code |= 2;
        diff -= step >> 1;
    }
    if (diff >= step >> 2)
        code |= 1;
    adpcm_step(s, code);
    return code;
}
static void receive_locked(const uint8_t *p);
/* ---------- byte order of microphone data ----------
 * Read with the wrong byte order, real audio turns into near-random 16-bit values (mean |x| about
 * 16000): loud constant noise instead of a voice. Read correctly, microphone audio is quiet on
 * average. The decoder compares both readings over ~0.1 s blocks and keeps the quieter one. */
int voice_pcm16_order(const uint8_t *raw, int samples, int stride, int *state) {
    double sum_be = 0, sum_le = 0;
    for (int i = 0; i < samples; i++) {
        const uint8_t *s = raw + i * stride;
        int be = (int16_t)(s[0] << 8 | s[1]), le = (int16_t)(s[1] << 8 | s[0]);
        sum_be += be < 0 ? -be : be;
        sum_le += le < 0 ? -le : le;
    }
    /* state: votes for big endian (>0) or little endian (<0), with hysteresis */
    if (samples >= 64) {
        if (sum_be * 2 < sum_le && *state < 8)
            (*state)++;
        else if (sum_le * 2 < sum_be && *state > -8)
            (*state)--;
    }
    return *state >= 0; /* 1 = big endian */
}
void voice_pcm16_decode(const uint8_t *raw, int samples, int stride, int big_endian, int16_t *out) {
    for (int i = 0; i < samples; i++) {
        const uint8_t *s = raw + i * stride;
        out[i] = (int16_t)(big_endian ? (s[0] << 8 | s[1]) : (s[1] << 8 | s[0]));
    }
}
/* ---------- capture ---------- */
static int16_t frame[VOICE_FRAME];
static int frame_fill, muted, level, hangover, loopback, floor_rms = 200, noisy_frames;
static int rms_hist[150], hist_pos, hist_fill;
static Adpcm encoder;
static uint16_t sequence;
static uint8_t outbox[16][VOICE_PACKET];
static int out_head, out_count;
static uint64_t self_until;
void voice_set_muted(int m) {
    pthread_mutex_lock(&lock);
    muted = m;
    pthread_mutex_unlock(&lock);
}
int voice_muted(void) {
    return muted;
}
int voice_mic_level(void) {
    return level;
}
int voice_self_speaking(void) {
    return !muted && orbit_millis() < self_until;
}
static void encode_frame(void) {
    long long energy = 0;
    for (int i = 0; i < VOICE_FRAME; i++)
        energy += (long long)frame[i] * frame[i];
    int rms = (int)sqrt((double)energy / VOICE_FRAME);
    level = rms * 100 / 6000 > 100 ? 100 : rms * 100 / 6000;
    /* Background noise = the quietest frame of the last 3 seconds. Speech always has pauses, a
     * constant hum or hiss does not: so a hum never keeps the microphone "open". */
    rms_hist[hist_pos] = rms;
    hist_pos = (hist_pos + 1) % 150;
    if (hist_fill < 150)
        hist_fill++;
    /* the first 3 seconds have no reliable history: a fixed low floor (speech at once works) */
    floor_rms = 180;
    if (hist_fill == 150) {
        floor_rms = rms;
        for (int i = 0; i < hist_fill; i++)
            if (rms_hist[i] < floor_rms)
                floor_rms = rms_hist[i];
    }
    /* Mostly random-looking frames (e.g. a broken stream) count as noise, not speech */
    noisy_frames = rms > 12000 ? noisy_frames + 1 : 0;
    /* Voice activity: send only while speaking (plus 300 ms), saves bandwidth and noise. */
    if (rms > 450 && rms * 2 > floor_rms * 5 && noisy_frames < 25)
        hangover = 15;
    else if (hangover)
        hangover--;
    if (muted || !hangover)
        return;
    self_until = orbit_millis() + 250;
    uint8_t *p = outbox[(out_head + out_count) % 16];
    if (out_count == 16)
        out_head = (out_head + 1) % 16; /* network stalled: drop the oldest */
    else
        out_count++;
    p[0] = VF_AUDIO;
    p[1] = 0;
    p[2] = (uint8_t)(sequence >> 8);
    p[3] = (uint8_t)sequence++;
    p[4] = (uint8_t)((unsigned)encoder.predictor >> 8);
    p[5] = (uint8_t)encoder.predictor;
    p[6] = (uint8_t)encoder.index;
    p[7] = 0;
    for (int i = 0; i < VOICE_FRAME; i += 2)
        p[8 + i / 2] = (uint8_t)(adpcm_code(&encoder, frame[i]) | adpcm_code(&encoder, frame[i + 1]) << 4);
    if (loopback) {
        /* microphone test: hear yourself exactly as your friends hear you (after the codec) */
        uint8_t copy[VOICE_PACKET];
        memcpy(copy, p, VOICE_PACKET);
        copy[1] = VOICE_USERS - 1;
        receive_locked(copy);
        out_count--; /* nothing is sent during the test */
    }
}
void voice_set_loopback(int on) {
    pthread_mutex_lock(&lock);
    loopback = on;
    pthread_mutex_unlock(&lock);
}
int voice_loopback(void) {
    return loopback;
}
int voice_noise_floor(void) {
    return floor_rms;
}
void voice_capture(const int16_t *pcm, int n) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < n; i++) {
        frame[frame_fill++] = pcm[i];
        if (frame_fill == VOICE_FRAME) {
            encode_frame();
            frame_fill = 0;
        }
    }
    pthread_mutex_unlock(&lock);
}
int voice_take_packet(uint8_t out[VOICE_PACKET]) {
    pthread_mutex_lock(&lock);
    int ok = out_count > 0;
    if (ok) {
        memcpy(out, outbox[out_head], VOICE_PACKET);
        out_head = (out_head + 1) % 16;
        out_count--;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
/* ---------- playback ---------- */
#define RING 16000
typedef struct {
    int16_t *ring;
    int read, count, playing, phase, prev;
    uint64_t heard;
} Speaker;
static Speaker speakers[VOICE_USERS];
/* local volume per person in percent (0 = muted for me only, 100 = normal, up to 200) */
static int gains[VOICE_USERS];
void voice_set_gain(int user, int percent) {
    if (user < 0 || user >= VOICE_USERS)
        return;
    pthread_mutex_lock(&lock);
    gains[user] = 1 + (percent < 0 ? 0 : percent > 200 ? 200 : percent); /* 0 = never set */
    pthread_mutex_unlock(&lock);
}
int voice_gain(int user) {
    return user >= 0 && user < VOICE_USERS && gains[user] ? gains[user] - 1 : 100;
}
static void receive_locked(const uint8_t *p) {
    Speaker *s = &speakers[p[1]];
    if (!s->ring)
        s->ring = calloc(RING, sizeof(int16_t));
    if (s->ring) {
        Adpcm d = {(int16_t)(p[4] << 8 | p[5]), p[6]};
        if (s->count > 4800) { /* more than 300 ms behind: catch up */
            s->read = (s->read + s->count - 1600) % RING;
            s->count = 1600;
        }
        for (int i = 0; i < VOICE_FRAME / 2 && s->count < RING - 1; i++) {
            int lo = adpcm_step(&d, p[8 + i] & 15), hi;
            s->ring[(s->read + s->count++) % RING] = (int16_t)lo;
            hi = adpcm_step(&d, p[8 + i] >> 4);
            s->ring[(s->read + s->count++) % RING] = (int16_t)hi;
        }
        s->heard = orbit_millis();
    }
}
void voice_receive(const uint8_t *p, int n) {
    if (n != VOICE_PACKET || p[0] != VF_AUDIO || p[1] >= VOICE_USERS || p[6] > 88)
        return;
    pthread_mutex_lock(&lock);
    receive_locked(p);
    pthread_mutex_unlock(&lock);
}
int voice_speaking(int user) {
    return user >= 0 && user < VOICE_USERS && speakers[user].heard &&
           orbit_millis() - speakers[user].heard < 300;
}
void voice_mix48(float *out, int frames) {
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    pthread_mutex_lock(&lock);
    for (int u = 0; u < VOICE_USERS; u++) {
        Speaker *s = &speakers[u];
        if (!s->ring)
            continue;
        if (!s->playing && s->count >= 960) /* 60 ms jitter buffer */
            s->playing = 1;
        if (!s->playing)
            continue;
        float gain = gains[u] ? (float)(gains[u] - 1) / 100.0f : 1.0f;
        for (int i = 0; i < frames; i++) {
            if (!s->count) {
                s->playing = 0;
                break;
            }
            int next = s->ring[s->read];
            float v = gain * (float)(s->prev + (next - s->prev) * s->phase / 3) / 32768.0f;
            out[2 * i] += v;
            out[2 * i + 1] += v;
            if (++s->phase == 3) {
                s->phase = 0;
                s->prev = next;
                s->read = (s->read + 1) % RING;
                s->count--;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    for (int i = 0; i < frames * 2; i++)
        out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
}
void voice_reset(void) {
    pthread_mutex_lock(&lock);
    for (int u = 0; u < VOICE_USERS; u++) {
        free(speakers[u].ring);
        memset(&speakers[u], 0, sizeof(speakers[u]));
        gains[u] = 0;
    }
    frame_fill = out_head = out_count = hangover = level = noisy_frames = 0;
    floor_rms = 200;
    hist_pos = hist_fill = 0;
    memset(&encoder, 0, sizeof(encoder));
    self_until = 0;
    pthread_mutex_unlock(&lock);
}
int voice_resample_to16k(const int16_t *in, int n, int rate, int16_t *out, int cap) {
    int k = 0;
    if (rate == 48000) {
        for (int i = 0; i + 2 < n && k < cap; i += 3)
            out[k++] = (int16_t)((in[i] + in[i + 1] + in[i + 2]) / 3);
    } else if (rate == 16000) {
        for (int i = 0; i < n && k < cap; i++)
            out[k++] = in[i];
    } else if (rate > 0) {
        double step = (double)rate / VOICE_RATE;
        for (double pos = 0; (int)pos < n && k < cap; pos += step)
            out[k++] = in[(int)pos];
    }
    return k;
}
