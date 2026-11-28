/* Screen transitions: curve shape, end frames exact, threaded result identical, choice rules,
 * hint bar fades through, tab push moves the page sideways without overlap. */
#include "transition.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 321
#define H 181
static uint32_t prev[W * H], next[W * H], back[W * H], a[W * H], b[W * H];

static int same_rgb(const uint32_t *x, const uint32_t *y) {
    for (int i = 0; i < W * H; i++)
        if ((x[i] & 0xffffff) != (y[i] & 0xffffff))
            return 0;
    return 1;
}
int main(void) {
    /* curve: starts at 0, ends exactly at 1, small overshoot, ease-out monotonic */
    assert(tr_spring(0) == 0 && tr_spring(1) == 1 && tr_spring(2) == 1);
    float peak = 0;
    for (int i = 0; i <= 1000; i++) {
        float v = tr_spring(i / 1000.0f);
        if (v > peak)
            peak = v;
        assert(v >= -0.001f);
    }
    assert(peak > 1.0f && peak < 1.05f);
    assert(fabsf(tr_spring(0.999f) - 1.0f) < 0.01f);
    for (int i = 1; i <= 100; i++)
        assert(tr_ease_out(i / 100.0f) >= tr_ease_out((i - 1) / 100.0f));
    printf("PASS: transition curves (spring 0 -> 1, overshoot %.1f %%, ease-out monotonic)\n", (peak - 1) * 100);

    srand(7);
    for (int i = 0; i < W * H; i++) {
        prev[i] = (uint32_t)rand() & 0xffffff;
        next[i] = (uint32_t)rand() & 0xffffff;
        back[i] = (uint32_t)rand() & 0xffffff;
    }
    TrPlan p;
    for (int k = TR_SLIDE_LEFT; k < TR_COUNT; k++) {
        for (int with_back = 0; with_back < 2; with_back++) {
            tr_plan(&p, k, prev, next, with_back ? back : NULL, W, H);
            tr_compose(a, prev, next, &p, 1.0f, 1);
            assert(same_rgb(a, next)); /* last frame = the new screen exactly */
            tr_compose(a, prev, next, &p, 0.0f, 1);
            assert(same_rgb(a, prev)); /* first frame = the old screen */
            for (int step = 0; step <= 10; step++) { /* threads give exactly the same picture */
                float t = step / 10.0f;
                tr_compose(a, prev, next, &p, t, 1);
                tr_compose(b, prev, next, &p, t, 4);
                assert(!memcmp(a, b, sizeof(a)));
                tr_compose(b, prev, next, &p, t, 8);
                assert(!memcmp(a, b, sizeof(a)));
            }
        }
        assert(tr_duration_ms(k) > 0 && tr_duration_ms(k) <= 450); /* never slows the user down */
    }
    /* mid-way frames really mix both screens */
    tr_plan(&p, TR_FADE, prev, next, NULL, W, H);
    tr_compose(a, prev, next, &p, 0.5f, 3);
    assert(!same_rgb(a, prev) && !same_rgb(a, next));
    tr_shutdown();
    tr_compose(b, prev, next, &p, 0.5f, 2); /* restarts after shutdown */
    assert(!memcmp(a, b, sizeof(a)));
    tr_shutdown();
    printf("PASS: transition frames (first/last exact, 1/4/8 threads identical, restart after shutdown)\n");

    /* tab push: halfway through, the left part of the screen shows the old page moved left and the
     * right part the new page coming in - never a blend of both pages */
    tr_plan(&p, TR_SLIDE_LEFT, prev, next, NULL, W, H);
    int y = H / 2;
    uint32_t solid_old[W * H], solid_new[W * H];
    for (int i = 0; i < W * H; i++)
        solid_old[i] = 0x0000ff, solid_new[i] = 0xff0000, back[i] = 0x000000;
    tr_plan(&p, TR_SLIDE_LEFT, solid_old, solid_new, back, W, H);
    for (int step = 1; step < 10; step++) {
        tr_compose(a, solid_old, solid_new, &p, step / 10.0f, 1);
        int seen_new = 0;
        for (int x = 0; x < W; x++) {
            uint32_t v = a[y * W + x];
            int blue = v & 0xff, red = v >> 16 & 0xff;
            assert(!(blue && red)); /* never both pages in one pixel */
            if (red)
                seen_new = 1;
            else
                assert(!seen_new || !blue); /* old page on the left, new page on the right */
        }
    }
    tr_plan(&p, TR_SLIDE_RIGHT, solid_old, solid_new, back, W, H);
    tr_compose(a, solid_old, solid_new, &p, 0.5f, 1);
    assert((a[y * W] >> 16 & 0xff) && (a[y * W + W - 1] & 0xff)); /* previous tab: new comes from the left */
    printf("PASS: tab push (pages side by side, next tab moves left, previous tab moves right)\n");

    /* hint bar: old hints fade out first, new ones fade in after - never both at once */
    tr_plan(&p, TR_RISE, solid_old, solid_new, back, W, H);
    for (int step = 1; step < 10; step++) {
        tr_compose(a, solid_old, solid_new, &p, step / 10.0f, 1);
        uint32_t v = a[(H - 2) * W + W / 2];
        assert(!((v & 0xff) && (v >> 16 & 0xff)));
    }
    tr_shutdown();
    printf("PASS: hint bar fades through (old and new hints never overlap)\n");

    assert(tr_choose(0, 0, 2, 0) == TR_SLIDE_LEFT);
    assert(tr_choose(3, 0, 1, 0) == TR_SLIDE_RIGHT);
    assert(tr_choose(1, 0, 1, 1) == TR_RISE);
    assert(tr_choose(1, 2, 1, 1) == TR_DROP);
    assert(tr_choose(1, 0, 1, 0) == TR_FADE);
    printf("PASS: transition choice (tabs slide by direction, dialogs rise/drop, else fade)\n");
    return 0;
}
