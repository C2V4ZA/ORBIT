/* Microphone path: byte order detection, noise gate against constant noise, microphone test. */
#include "voice.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void bytes(uint8_t *out, const int16_t *pcm, int n, int big_endian) {
    for (int i = 0; i < n; i++) {
        uint16_t v = (uint16_t)pcm[i];
        out[2 * i] = big_endian ? (uint8_t)(v >> 8) : (uint8_t)v;
        out[2 * i + 1] = big_endian ? (uint8_t)v : (uint8_t)(v >> 8);
    }
}
static int16_t pcm[48000];
static uint8_t raw[96000];
static int detect(int big_endian, double amplitude) {
    /* "speech": a few harmonics with a slow envelope, plus a little hiss */
    for (int i = 0; i < 48000; i++) {
        double t = i / 48000.0, env = 0.5 + 0.5 * sin(2 * M_PI * 3 * t);
        double v = amplitude * env * (sin(2 * M_PI * 180 * t) + 0.5 * sin(2 * M_PI * 360 * t) + 0.25 * sin(2 * M_PI * 720 * t));
        v += (rand() % 41) - 20;
        pcm[i] = (int16_t)v;
    }
    bytes(raw, pcm, 48000, big_endian);
    int state = big_endian ? -1 : 1, order = !big_endian; /* start with the WRONG guess */
    for (int off = 0; off + 4800 <= 48000; off += 4800)
        order = voice_pcm16_order(raw + 2 * off, 4800, 2, &state);
    return order;
}
int main(void) {
    srand(7);
    assert(detect(1, 6000) == 1);
    assert(detect(0, 6000) == 0);
    assert(detect(1, 30) == 1); /* nearly silent room */
    assert(detect(0, 30) == 0);
    int16_t out[8];
    uint8_t b[4] = {0x12, 0x34, 0xfe, 0xdc};
    voice_pcm16_decode(b, 2, 2, 1, out);
    assert(out[0] == 0x1234 && out[1] == (int16_t)0xfedc);
    voice_pcm16_decode(b, 2, 2, 0, out);
    assert(out[0] == 0x3412);
    printf("PASS: microphone byte order detected from the data (big and little endian, speech and near silence), starting from a wrong guess\n");

    /* constant noise (the "fart"): after a moment it is no longer sent */
    voice_reset();
    uint8_t packet[VOICE_PACKET];
    int16_t frame[VOICE_FRAME];
    int sent_late = 0;
    for (int f = 0; f < 50 * 20; f++) { /* 20 s of rumble at rms ~2000 */
        for (int i = 0; i < VOICE_FRAME; i++)
            frame[i] = (int16_t)(2800 * sin(2 * M_PI * 70 * (f * VOICE_FRAME + i) / 16000.0) + (rand() % 400) - 200);
        voice_capture(frame, VOICE_FRAME);
        while (voice_take_packet(packet))
            if (f > 50 * 12)
                sent_late++;
    }
    assert(sent_late == 0);
    /* speech on top of that rumble still gets through */
    int sent_speech = 0;
    for (int f = 0; f < 50; f++) {
        for (int i = 0; i < VOICE_FRAME; i++) {
            double t = (f * VOICE_FRAME + i) / 16000.0;
            frame[i] = (int16_t)(2800 * sin(2 * M_PI * 70 * t) + 9000 * sin(2 * M_PI * 220 * t));
        }
        voice_capture(frame, VOICE_FRAME);
        while (voice_take_packet(packet))
            sent_speech++;
    }
    assert(sent_speech > 40);
    printf("PASS: noise gate learns a constant hum and stops sending it; speech above it is still sent\n");

    /* microphone test: hear yourself, nothing goes out */
    voice_reset();
    voice_set_loopback(1);
    for (int f = 0; f < 10; f++) {
        for (int i = 0; i < VOICE_FRAME; i++)
            frame[i] = (int16_t)(8000 * sin(2 * M_PI * 300 * (f * VOICE_FRAME + i) / 16000.0));
        voice_capture(frame, VOICE_FRAME);
    }
    assert(!voice_take_packet(packet));
    assert(voice_speaking(VOICE_USERS - 1));
    static float mix[2 * 48 * 100];
    voice_mix48(mix, 48 * 100);
    double peak = 0;
    for (int i = 0; i < 2 * 48 * 100; i++)
        peak = fabs(mix[i]) > peak ? fabs(mix[i]) : peak;
    assert(peak > 0.1);
    voice_set_loopback(0);
    printf("PASS: microphone test plays your own voice back through the codec and sends nothing\n");
    return 0;
}
