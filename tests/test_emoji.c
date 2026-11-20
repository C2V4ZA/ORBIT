/* emoji pictures and UI sounds: lookup, UTF-8, drawing coverage, sound sanity */
#include "emoji.h"
#include "sfx.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t decode(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    uint32_t c = *p++;
    int n = c < 128 ? 0 : c < 224 ? 1 : c < 240 ? 2 : 3;
    c &= n ? (1u << (6 - n)) - 1 : 127;
    while (n--)
        c = c << 6 | (*p++ & 63);
    return c;
}

int main(void) {
    enum { S = 64, COLS = 11 };
    static uint32_t px[S * S], sheet[COLS * S * 4 * S];
    for (int i = 0; i < EMOJI_COUNT; i++) {
        uint32_t cp = emoji_codepoint(i);
        assert(cp && emoji_find(cp) == i);
        assert(decode(emoji_utf8(i)) == cp);
        for (int j = 0; j < i; j++)
            assert(emoji_codepoint(j) != cp);
        emoji_render(i, S, px);
        int covered = 0;
        for (int k = 0; k < S * S; k++)
            covered += (px[k] >> 24) > 128;
        if (covered < S * S / 20 || covered > S * S * 95 / 100) {
            printf("emoji %d (%s) coverage %d\n", i, emoji_name(i), covered);
            return 1;
        }
        int ox = (i % COLS) * S, oy = (i / COLS) * S;
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                uint32_t p = px[y * S + x], a = p >> 24, bg = 0x1a1d24;
                uint32_t o = 0;
                for (int s = 0; s < 24; s += 8)
                    o |= (((p >> s & 255) * a + (bg >> s & 255) * (255 - a)) / 255) << s;
                sheet[(oy + y) * COLS * S + ox + x] = o;
            }
    }
    assert(emoji_find(0x41) < 0 && emoji_is_modifier(0xFE0F) && !emoji_is_modifier(0x1F600));
    FILE *f = fopen("build/emoji-sheet.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", COLS * S, 4 * S);
        for (int k = 0; k < COLS * S * 4 * S; k++) {
            unsigned char rgb[3] = {(unsigned char)(sheet[k] >> 16), (unsigned char)(sheet[k] >> 8), (unsigned char)sheet[k]};
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    printf("PASS: %d original emoji pictures: unique code points, UTF-8, drawn coverage\n", EMOJI_COUNT);

    /* UI sounds: every sound is short, audible, never clips; volume 0 is silent; mixing adds */
    static float buf[2 * 48000];
    for (int id = 0; id < SFX_COUNT; id++) {
        sfx_reset();
        sfx_set_volume(100);
        sfx_play(id);
        memset(buf, 0, sizeof(buf));
        sfx_mix48(buf, 48000);
        float peak = 0;
        int last = 0;
        for (int k = 0; k < 2 * 48000; k++) {
            float v = fabsf(buf[k]);
            if (v > peak)
                peak = v;
            if (v > 1e-4f)
                last = k / 2;
        }
        assert(peak > 0.02f && peak <= 0.5f);
        assert(last < 48000 * 9 / 10); /* every sound ends within 0.9 s */
        assert(!sfx_active());
    }
    sfx_reset();
    sfx_set_volume(0);
    sfx_play(SFX_SELECT);
    memset(buf, 0, sizeof(buf));
    sfx_mix48(buf, 4800);
    for (int k = 0; k < 9600; k++)
        assert(buf[k] == 0);
    /* ducking during a call: quieter but still audible */
    sfx_reset();
    sfx_set_volume(100);
    float full = 0, ducked = 0;
    sfx_play(SFX_NOTIFY);
    memset(buf, 0, sizeof(buf));
    sfx_mix48(buf, 24000);
    for (int k = 0; k < 48000; k++)
        full = fmaxf(full, fabsf(buf[k]));
    sfx_reset();
    sfx_set_duck(1);
    sfx_play(SFX_NOTIFY);
    memset(buf, 0, sizeof(buf));
    sfx_mix48(buf, 24000);
    for (int k = 0; k < 48000; k++)
        ducked = fmaxf(ducked, fabsf(buf[k]));
    assert(ducked > 0.005f && ducked < full * 0.6f);
    /* many presses at once: bounded voices, no runaway level */
    sfx_reset();
    sfx_set_duck(0);
    for (int k = 0; k < 50; k++)
        sfx_play(SFX_MOVE);
    memset(buf, 0, sizeof(buf));
    sfx_mix48(buf, 4800);
    for (int k = 0; k < 9600; k++)
        assert(fabsf(buf[k]) <= 0.6f);
    printf("PASS: UI sounds: %d synthesized sounds, short, no clipping, mute, ducking in calls, voice limit\n", SFX_COUNT);
    return 0;
}
