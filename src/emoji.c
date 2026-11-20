/* ORBIT emoji pictures, drawn from simple signed-distance shapes (circles, capsules, polygons,
 * arcs). Everything here is original ORBIT artwork; no emoji font or image set is used. */
#include "emoji.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t *px;
    int n;
} Pic;

/* ---------- painter: shapes in unit coordinates (0..1, y down) ---------- */
static void put(Pic *c, int i, int j, uint32_t col, float a) {
    if (a <= 0)
        return;
    if (a > 1)
        a = 1;
    uint32_t *p = &c->px[j * c->n + i], d = *p;
    float da = (d >> 24) / 255.0f, oa = a + da * (1 - a);
    if (oa <= 0)
        return;
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        float sc = (col >> s & 255), dc = (d >> s & 255);
        float v = (sc * a + dc * da * (1 - a)) / oa;
        out |= (uint32_t)(v + 0.5f) << s;
    }
    *p = out | (uint32_t)(oa * 255.0f + 0.5f) << 24;
}
typedef float (*Sdf)(float x, float y, const float *k);
static void paint(Pic *c, Sdf f, const float *k, uint32_t col, float alpha) {
    float px = 1.0f / c->n;
    for (int j = 0; j < c->n; j++)
        for (int i = 0; i < c->n; i++) {
            float d = f((i + 0.5f) * px, (j + 0.5f) * px, k);
            float cover = 0.5f - d / px;
            if (cover > 0)
                put(c, i, j, col, (cover > 1 ? 1 : cover) * alpha);
        }
}
static float sd_circle(float x, float y, const float *k) {
    return hypotf(x - k[0], y - k[1]) - k[2];
}
static float sd_ellipse(float x, float y, const float *k) { /* approximate, fine for small sizes */
    float dx = (x - k[0]) / k[2], dy = (y - k[1]) / k[3];
    float r = hypotf(dx, dy);
    return (r - 1) * fminf(k[2], k[3]);
}
static float seg(float x, float y, float ax, float ay, float bx, float by) {
    float px = x - ax, py = y - ay, vx = bx - ax, vy = by - ay;
    float l = vx * vx + vy * vy, t = l > 0 ? (px * vx + py * vy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(px - vx * t, py - vy * t);
}
static float sd_line(float x, float y, const float *k) {
    return seg(x, y, k[0], k[1], k[2], k[3]) - k[4];
}
static float sd_box(float x, float y, const float *k) { /* x0 y0 x1 y1 radius */
    float cx = (k[0] + k[2]) / 2, cy = (k[1] + k[3]) / 2, hx = (k[2] - k[0]) / 2 - k[4], hy = (k[3] - k[1]) / 2 - k[4];
    float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy;
    return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - k[4];
}
/* arc: centre, radius, start/end angle in degrees (0 = right, 90 = down), half width */
static float sd_arc(float x, float y, const float *k) {
    float a = atan2f(y - k[1], x - k[0]) * 57.29578f;
    float a0 = k[3], a1 = k[4];
    while (a < a0)
        a += 360;
    while (a > a0 + 360)
        a -= 360;
    if (a <= a1)
        return fabsf(hypotf(x - k[0], y - k[1]) - k[2]) - k[5];
    float r = k[2], e0x = k[0] + r * cosf(a0 / 57.29578f), e0y = k[1] + r * sinf(a0 / 57.29578f);
    float e1x = k[0] + r * cosf(a1 / 57.29578f), e1y = k[1] + r * sinf(a1 / 57.29578f);
    return fminf(hypotf(x - e0x, y - e0y), hypotf(x - e1x, y - e1y)) - k[5];
}
/* polygon: k[0] = point count, then x/y pairs */
static float sd_poly(float x, float y, const float *k) {
    int n = (int)k[0];
    const float *v = k + 1;
    float d = 1e9f;
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = v[2 * i], yi = v[2 * i + 1], xj = v[2 * j], yj = v[2 * j + 1];
        d = fminf(d, seg(x, y, xi, yi, xj, yj));
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside ? -d : d;
}
/* crescent: circle minus a shifted circle */
static float sd_crescent(float x, float y, const float *k) {
    float a = hypotf(x - k[0], y - k[1]) - k[2], b = hypotf(x - k[3], y - k[4]) - k[5];
    return fmaxf(a, -b);
}
/* half disc below (k[3] > 0) or above (k[3] < 0) the centre line */
static float sd_halfdisc(float x, float y, const float *k) {
    float c = hypotf(x - k[0], y - k[1]) - k[2];
    float h = k[3] > 0 ? k[1] - y : y - k[1];
    return fmaxf(c, h);
}
#define CIRCLE(x, y, r, col) paint(c, sd_circle, (const float[]){x, y, r}, col, 1)
#define ELLIPSE(x, y, rx, ry, col) paint(c, sd_ellipse, (const float[]){x, y, rx, ry}, col, 1)
#define LINE(x0, y0, x1, y1, w, col) paint(c, sd_line, (const float[]){x0, y0, x1, y1, w}, col, 1)
#define BOX(x0, y0, x1, y1, r, col) paint(c, sd_box, (const float[]){x0, y0, x1, y1, r}, col, 1)
#define ARC(x, y, r, a0, a1, w, col) paint(c, sd_arc, (const float[]){x, y, r, a0, a1, w}, col, 1)
#define POLY(col, ...) paint(c, sd_poly, (const float[]){__VA_ARGS__}, col, 1)
#define HALF(x, y, r, dir, col) paint(c, sd_halfdisc, (const float[]){x, y, r, dir}, col, 1)

enum {
    YELLOW = 0xffc93c,
    YEDGE = 0xe39a18,
    INK = 0x5b3714,
    RED = 0xef3b4a,
    BLUE = 0x4aa6ff,
    WHITE = 0xffffff,
    PINK = 0xff7aa8,
    ORANGE = 0xff8a1f,
    GOLD = 0xffc53a,
    GREEN = 0x2fbf71,
    GREY = 0x8d96a3,
    DARK = 0x2a2f38
};
static void face(Pic *c, uint32_t fill, uint32_t edge) {
    CIRCLE(.5f, .5f, .46f, edge);
    CIRCLE(.5f, .5f, .425f, fill);
}
static void eyes(Pic *c) {
    ELLIPSE(.35f, .4f, .045f, .075f, INK);
    ELLIPSE(.65f, .4f, .045f, .075f, INK);
}
static void happy_eyes(Pic *c) {
    ARC(.35f, .44f, .08f, 200, 340, .025f, INK);
    ARC(.65f, .44f, .08f, 200, 340, .025f, INK);
}
static void closed_eyes(Pic *c) {
    LINE(.28f, .42f, .42f, .42f, .022f, INK);
    LINE(.58f, .42f, .72f, .42f, .022f, INK);
}
static void smile(Pic *c, float r) {
    ARC(.5f, .5f, r, 30, 150, .028f, INK);
}
static void frown(Pic *c) {
    ARC(.5f, .86f, .2f, 220, 320, .028f, INK);
}
static void grin(Pic *c) {
    HALF(.5f, .56f, .25f, 1, INK);
    BOX(.3f, .56f, .7f, .63f, .02f, WHITE);
}
static void heart(Pic *c, float x, float y, float s, uint32_t col) {
    CIRCLE(x - .17f * s, y - .1f * s, .19f * s, col);
    CIRCLE(x + .17f * s, y - .1f * s, .19f * s, col);
    POLY(col, 3, x - .35f * s, y - .04f * s, x + .35f * s, y - .04f * s, x, y + .4f * s);
}
static void star(Pic *c, float x, float y, float r, float inner, uint32_t col) {
    float k[21];
    k[0] = 10;
    for (int i = 0; i < 10; i++) {
        float a = (-90 + i * 36) / 57.29578f, rr = i & 1 ? r * inner : r;
        k[1 + 2 * i] = x + rr * cosf(a);
        k[2 + 2 * i] = y + rr * sinf(a);
    }
    paint(c, sd_poly, k, col, 1);
}
static void sparkle(Pic *c, float x, float y, float r, uint32_t col) {
    POLY(col, 8, x, y - r, x + r * .22f, y - r * .22f, x + r, y, x + r * .22f, y + r * .22f, x, y + r,
         x - r * .22f, y + r * .22f, x - r, y, x - r * .22f, y - r * .22f);
}
static void tear(Pic *c, float x, float y, float s) {
    CIRCLE(x, y + .05f * s, .06f * s, BLUE);
    POLY(BLUE, 3, x - .055f * s, y + .04f * s, x + .055f * s, y + .04f * s, x, y - .09f * s);
}

static void draw(Pic *c, int i) {
    switch (i) {
    case 0: /* grinning */
        face(c, YELLOW, YEDGE), eyes(c), grin(c);
        break;
    case 1: /* tears of joy */
        face(c, YELLOW, YEDGE), happy_eyes(c), grin(c);
        tear(c, .14f, .5f, 1.3f), tear(c, .86f, .5f, 1.3f);
        break;
    case 2: /* smiling, blushing */
        face(c, YELLOW, YEDGE), happy_eyes(c), smile(c, .24f);
        paint(c, sd_circle, (const float[]){.25f, .58f, .07f}, PINK, .55f);
        paint(c, sd_circle, (const float[]){.75f, .58f, .07f}, PINK, .55f);
        break;
    case 3: /* wink */
        face(c, YELLOW, YEDGE);
        ELLIPSE(.35f, .4f, .045f, .075f, INK);
        ARC(.65f, .46f, .08f, 200, 340, .025f, INK);
        smile(c, .24f);
        break;
    case 4: /* heart eyes */
        face(c, YELLOW, YEDGE), grin(c);
        heart(c, .33f, .4f, .38f, RED), heart(c, .67f, .4f, .38f, RED);
        break;
    case 5: /* sunglasses */
        face(c, YELLOW, YEDGE), smile(c, .24f);
        BOX(.16f, .32f, .46f, .5f, .06f, DARK), BOX(.54f, .32f, .84f, .5f, .06f, DARK);
        LINE(.44f, .36f, .56f, .36f, .025f, DARK);
        break;
    case 6: /* thinking */
        face(c, YELLOW, YEDGE), eyes(c);
        LINE(.27f, .27f, .42f, .24f, .022f, INK);
        LINE(.4f, .7f, .64f, .65f, .028f, INK);
        break;
    case 7: /* surprised */
        face(c, YELLOW, YEDGE), eyes(c);
        ELLIPSE(.5f, .68f, .08f, .1f, INK);
        break;
    case 8: /* sad, one tear */
        face(c, YELLOW, YEDGE), eyes(c), frown(c);
        tear(c, .3f, .55f, 1.0f);
        break;
    case 9: /* crying loudly */
        face(c, YELLOW, YEDGE);
        ARC(.35f, .36f, .08f, 20, 160, .025f, INK), ARC(.65f, .36f, .08f, 20, 160, .025f, INK);
        BOX(.27f, .46f, .37f, .92f, .04f, BLUE), BOX(.63f, .46f, .73f, .92f, .04f, BLUE);
        ELLIPSE(.5f, .66f, .12f, .09f, INK);
        break;
    case 10: /* angry */
        face(c, 0xff7043, 0xd84315), eyes(c), frown(c);
        LINE(.24f, .26f, .42f, .32f, .028f, INK), LINE(.58f, .32f, .76f, .26f, .028f, INK);
        break;
    case 11: /* sleeping */
        face(c, YELLOW, YEDGE), closed_eyes(c);
        ELLIPSE(.5f, .68f, .05f, .06f, INK);
        LINE(.7f, .1f, .86f, .1f, .02f, BLUE), LINE(.86f, .1f, .7f, .26f, .02f, BLUE),
            LINE(.7f, .26f, .86f, .26f, .02f, BLUE);
        break;
    case 12: /* slight smile */
        face(c, YELLOW, YEDGE), eyes(c), smile(c, .2f);
        break;
    case 13: /* slight frown */
        face(c, YELLOW, YEDGE), eyes(c), frown(c);
        break;
    case 14: /* neutral */
        face(c, YELLOW, YEDGE), eyes(c);
        LINE(.36f, .68f, .64f, .68f, .028f, INK);
        break;
    case 15: /* tongue */
        face(c, YELLOW, YEDGE), eyes(c);
        ELLIPSE(.5f, .72f, .1f, .1f, PINK);
        LINE(.32f, .64f, .68f, .64f, .028f, INK);
        break;
    case 16: /* halo */
        face(c, YELLOW, YEDGE), happy_eyes(c), smile(c, .24f);
        paint(c, sd_ellipse, (const float[]){.5f, .07f, .3f, .06f}, BLUE, 1);
        paint(c, sd_ellipse, (const float[]){.5f, .07f, .22f, .03f}, WHITE, 1);
        break;
    case 17: /* sweat smile */
        face(c, YELLOW, YEDGE), happy_eyes(c), grin(c);
        tear(c, .82f, .22f, 1.4f);
        break;
    case 18: /* kiss */
        face(c, YELLOW, YEDGE);
        ELLIPSE(.35f, .4f, .045f, .075f, INK);
        ARC(.65f, .46f, .08f, 200, 340, .025f, INK);
        ARC(.52f, .62f, .05f, 270, 450, .022f, INK), ARC(.52f, .72f, .05f, 270, 450, .022f, INK);
        heart(c, .8f, .74f, .3f, RED);
        break;
    case 19: /* scream */
        face(c, 0xd8e4f0, 0x9fb3c8), eyes(c);
        ELLIPSE(.5f, .7f, .1f, .16f, INK);
        break;
    case 20: /* skull */
        CIRCLE(.5f, .42f, .36f, 0xd9dde3);
        BOX(.3f, .55f, .7f, .9f, .08f, 0xd9dde3);
        CIRCLE(.36f, .44f, .1f, DARK), CIRCLE(.64f, .44f, .1f, DARK);
        POLY(DARK, 3, .5f, .58f, .45f, .67f, .55f, .67f);
        LINE(.42f, .76f, .42f, .86f, .015f, DARK), LINE(.5f, .76f, .5f, .86f, .015f, DARK),
            LINE(.58f, .76f, .58f, .86f, .015f, DARK);
        break;
    case 21: /* eyes */
        ELLIPSE(.3f, .5f, .17f, .27f, WHITE), ELLIPSE(.7f, .5f, .17f, .27f, WHITE);
        paint(c, sd_ellipse, (const float[]){.3f, .5f, .17f, .27f}, GREY, .15f);
        CIRCLE(.36f, .56f, .08f, DARK), CIRCLE(.76f, .56f, .08f, DARK);
        break;
    case 22: /* heart */
        heart(c, .5f, .48f, 1.25f, RED);
        break;
    case 23: /* broken heart */
        heart(c, .5f, .48f, 1.25f, RED);
        POLY(WHITE, 6, .5f, .22f, .43f, .38f, .55f, .5f, .45f, .66f, .5f, .66f, .58f, .5f);
        break;
    case 24: /* star */
        star(c, .5f, .53f, .46f, .45f, GOLD);
        break;
    case 25: /* glowing star */
        star(c, .5f, .55f, .36f, .45f, GOLD);
        sparkle(c, .14f, .18f, .1f, GOLD), sparkle(c, .86f, .2f, .08f, GOLD), sparkle(c, .85f, .86f, .07f, GOLD);
        break;
    case 26: /* sparkles */
        sparkle(c, .4f, .55f, .34f, GOLD), sparkle(c, .78f, .24f, .16f, GOLD), sparkle(c, .8f, .8f, .11f, GOLD);
        break;
    case 27: /* fire */
        CIRCLE(.5f, .64f, .3f, ORANGE);
        POLY(ORANGE, 4, .2f, .6f, .38f, .2f, .5f, .04f, .8f, .6f);
        CIRCLE(.52f, .72f, .17f, GOLD);
        POLY(GOLD, 3, .36f, .7f, .52f, .38f, .68f, .7f);
        break;
    case 28: /* lightning */
        POLY(GOLD, 6, .58f, .04f, .22f, .56f, .46f, .56f, .38f, .96f, .78f, .4f, .54f, .4f);
        break;
    case 29: /* sun */
        for (int k = 0; k < 8; k++) {
            float a = k * 45 / 57.29578f;
            LINE(.5f + .3f * cosf(a), .5f + .3f * sinf(a), .5f + .44f * cosf(a), .5f + .44f * sinf(a), .04f, ORANGE);
        }
        CIRCLE(.5f, .5f, .24f, GOLD);
        break;
    case 30: /* crescent moon */
        paint(c, sd_crescent, (const float[]){.5f, .5f, .4f, .68f, .38f, .34f}, GOLD, 1);
        break;
    case 31: /* thumbs up */
        BOX(.14f, .46f, .3f, .9f, .03f, BLUE);
        BOX(.3f, .44f, .82f, .9f, .1f, YELLOW);
        LINE(.44f, .46f, .5f, .12f, .085f, YELLOW);
        LINE(.34f, .6f, .78f, .6f, .012f, YEDGE), LINE(.34f, .74f, .78f, .74f, .012f, YEDGE);
        break;
    case 32: /* check mark */
        BOX(.08f, .08f, .92f, .92f, .16f, GREEN);
        LINE(.27f, .52f, .43f, .68f, .06f, WHITE), LINE(.43f, .68f, .74f, .32f, .06f, WHITE);
        break;
    case 33: /* cross mark */
        LINE(.18f, .18f, .82f, .82f, .1f, RED), LINE(.82f, .18f, .18f, .82f, .1f, RED);
        break;
    case 34: /* game controller */
        CIRCLE(.27f, .62f, .2f, 0x59616e), CIRCLE(.73f, .62f, .2f, 0x59616e);
        BOX(.12f, .3f, .88f, .66f, .16f, 0x59616e);
        BOX(.2f, .45f, .36f, .51f, .01f, DARK), BOX(.25f, .4f, .31f, .56f, .01f, DARK);
        CIRCLE(.68f, .42f, .045f, GREEN), CIRCLE(.76f, .5f, .045f, RED), CIRCLE(.6f, .5f, .045f, BLUE),
            CIRCLE(.68f, .58f, .045f, GOLD);
        break;
    case 35: /* trophy */
        ARC(.22f, .34f, .12f, 90, 270, .035f, GOLD), ARC(.78f, .34f, .12f, -90, 90, .035f, GOLD);
        HALF(.5f, .2f, .3f, 1, GOLD);
        BOX(.2f, .12f, .8f, .22f, .02f, GOLD);
        BOX(.45f, .48f, .55f, .72f, .01f, GOLD);
        BOX(.3f, .72f, .7f, .9f, .03f, 0xb7791f);
        break;
    case 36: /* crown */
        POLY(GOLD, 7, .1f, .8f, .1f, .3f, .3f, .52f, .5f, .18f, .7f, .52f, .9f, .3f, .9f, .8f);
        CIRCLE(.5f, .6f, .06f, RED), CIRCLE(.27f, .66f, .05f, BLUE), CIRCLE(.73f, .66f, .05f, GREEN);
        break;
    case 37: /* party popper */
        POLY(ORANGE, 3, .1f, .92f, .3f, .38f, .62f, .7f);
        LINE(.18f, .74f, .44f, .62f, .025f, 0xa66bff);
        CIRCLE(.62f, .22f, .045f, RED), CIRCLE(.82f, .38f, .04f, BLUE), CIRCLE(.46f, .14f, .035f, GREEN),
            CIRCLE(.86f, .14f, .035f, GOLD), CIRCLE(.74f, .56f, .035f, PINK);
        ARC(.6f, .4f, .2f, 220, 300, .02f, 0xa66bff);
        break;
    case 38: /* headphones */
        ARC(.5f, .55f, .34f, 180, 360, .05f, DARK);
        BOX(.1f, .52f, .3f, .86f, .07f, RED), BOX(.7f, .52f, .9f, .86f, .07f, RED);
        break;
    case 39: /* musical note */
        ELLIPSE(.36f, .76f, .15f, .11f, BLUE);
        LINE(.49f, .76f, .49f, .14f, .035f, BLUE);
        POLY(BLUE, 4, .49f, .1f, .78f, .24f, .78f, .38f, .49f, .26f);
        break;
    case 40: /* speech bubble */
        BOX(.08f, .14f, .92f, .7f, .16f, 0xe6eaf0);
        POLY(0xe6eaf0, 3, .24f, .64f, .44f, .64f, .18f, .9f);
        CIRCLE(.32f, .42f, .055f, GREY), CIRCLE(.5f, .42f, .055f, GREY), CIRCLE(.68f, .42f, .055f, GREY);
        break;
    case 41: /* football */
        CIRCLE(.5f, .5f, .44f, DARK), CIRCLE(.5f, .5f, .41f, WHITE);
        {
            float k[11];
            k[0] = 5;
            for (int q = 0; q < 5; q++) {
                float a = (-90 + q * 72) / 57.29578f;
                k[1 + 2 * q] = .5f + .14f * cosf(a);
                k[2 + 2 * q] = .5f + .14f * sinf(a);
            }
            paint(c, sd_poly, k, DARK, 1);
            for (int q = 0; q < 5; q++) {
                float a = (-90 + q * 72) / 57.29578f;
                CIRCLE(.5f + .36f * cosf(a), .5f + .36f * sinf(a), .07f, DARK);
            }
        }
        break;
    case 42: /* pizza */
        POLY(GOLD, 3, .14f, .14f, .86f, .3f, .38f, .9f);
        LINE(.14f, .14f, .86f, .3f, .06f, 0xc9822b);
        CIRCLE(.45f, .38f, .07f, RED), CIRCLE(.6f, .44f, .06f, RED), CIRCLE(.44f, .6f, .06f, RED);
        break;
    case 43: /* ghost */
        CIRCLE(.5f, .42f, .32f, 0xeef1f6);
        BOX(.18f, .42f, .82f, .8f, .01f, 0xeef1f6);
        CIRCLE(.29f, .8f, .11f, 0xeef1f6), CIRCLE(.5f, .8f, .11f, 0xeef1f6), CIRCLE(.71f, .8f, .11f, 0xeef1f6);
        ELLIPSE(.4f, .4f, .05f, .08f, DARK), ELLIPSE(.6f, .4f, .05f, .08f, DARK);
        ELLIPSE(.5f, .58f, .05f, .05f, DARK);
        break;
    }
}

static const struct {
    uint32_t cp;
    const char *name;
} table[EMOJI_COUNT] = {
    {0x1F600, "grinning"},      {0x1F602, "joy"},          {0x1F60A, "blush"},
    {0x1F609, "wink"},          {0x1F60D, "heart eyes"},   {0x1F60E, "sunglasses"},
    {0x1F914, "thinking"},      {0x1F62E, "surprised"},    {0x1F622, "sad"},
    {0x1F62D, "crying"},        {0x1F621, "angry"},        {0x1F634, "sleeping"},
    {0x1F642, "slight smile"},  {0x1F641, "slight frown"}, {0x1F610, "neutral"},
    {0x1F61B, "tongue"},        {0x1F607, "halo"},         {0x1F605, "sweat smile"},
    {0x1F618, "kiss"},          {0x1F631, "scream"},       {0x1F480, "skull"},
    {0x1F440, "eyes"},          {0x2764, "heart"},         {0x1F494, "broken heart"},
    {0x2B50, "star"},           {0x1F31F, "glowing star"}, {0x2728, "sparkles"},
    {0x1F525, "fire"},          {0x26A1, "lightning"},     {0x2600, "sun"},
    {0x1F319, "moon"},          {0x1F44D, "thumbs up"},    {0x2705, "check"},
    {0x274C, "cross"},          {0x1F3AE, "controller"},   {0x1F3C6, "trophy"},
    {0x1F451, "crown"},         {0x1F389, "party"},        {0x1F3A7, "headphones"},
    {0x1F3B5, "music"},         {0x1F4AC, "speech"},       {0x26BD, "football"},
    {0x1F355, "pizza"},         {0x1F47B, "ghost"},
};

int emoji_find(uint32_t cp) {
    for (int i = 0; i < EMOJI_COUNT; i++)
        if (table[i].cp == cp)
            return i;
    return -1;
}
uint32_t emoji_codepoint(int i) {
    return i >= 0 && i < EMOJI_COUNT ? table[i].cp : 0;
}
const char *emoji_name(int i) {
    return i >= 0 && i < EMOJI_COUNT ? table[i].name : "";
}
const char *emoji_utf8(int i) {
    static char text[EMOJI_COUNT][5];
    if (i < 0 || i >= EMOJI_COUNT)
        return "";
    if (!text[i][0]) {
        uint32_t c = table[i].cp;
        unsigned char *o = (unsigned char *)text[i];
        if (c < 0x10000) {
            o[0] = (unsigned char)(0xe0 | c >> 12);
            o[1] = (unsigned char)(0x80 | (c >> 6 & 63));
            o[2] = (unsigned char)(0x80 | (c & 63));
            o[3] = 0;
        } else {
            o[0] = (unsigned char)(0xf0 | c >> 18);
            o[1] = (unsigned char)(0x80 | (c >> 12 & 63));
            o[2] = (unsigned char)(0x80 | (c >> 6 & 63));
            o[3] = (unsigned char)(0x80 | (c & 63));
            o[4] = 0;
        }
    }
    return text[i];
}
int emoji_is_modifier(uint32_t cp) {
    return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}
void emoji_render(int index, int size, uint32_t *argb) {
    if (size <= 0 || !argb)
        return;
    memset(argb, 0, (size_t)size * size * 4);
    if (index < 0 || index >= EMOJI_COUNT)
        return;
    Pic c = {argb, size};
    draw(&c, index);
}
