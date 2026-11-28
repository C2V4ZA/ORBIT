/* ORBIT screen transitions (shared by both consoles; enabled where the hardware has the time).
 * Snapshot based: the last frame of the old screen and the first frame of the new screen are
 * composed per output frame, so the UI renderer is not run again during the animation.
 * - tabs: "push" - the old page slides out to one side while the new one slides in from the other
 *   (next tab: to the left, previous tab: to the right), side by side, never on top of each other;
 *   the space background and the top bar stay in place, the hint bar fades through
 * - dialogs / keyboard: an exact cross-fade in place (the dialogs are frosted glass, so moving them
 *   would show the page text behind them twice); the hint bar fades through
 * Curves: damped spring for motion, ease-out for opacity. Rows can be split over threads. */
#ifndef ORBIT_TRANSITION_H
#define ORBIT_TRANSITION_H
#include <stdint.h>
enum {
    TR_NONE,
    TR_SLIDE_LEFT,  /* new screen comes in from the right (next tab) */
    TR_SLIDE_RIGHT, /* new screen comes in from the left (previous tab) */
    TR_RISE,        /* dialog / deeper level opens */
    TR_DROP,        /* dialog / level closes */
    TR_FADE,        /* anything else: short cross-fade */
    TR_COUNT
};
typedef struct {
    int kind, w, h;
    int head, foot;     /* slide: top bar rows [0, head), hint bar rows [foot, h) */
    const uint32_t *backdrop; /* the empty space background (ui_backdrop) or NULL */
    uint32_t bg;        /* background colour when there is no backdrop */
} TrPlan;
/* durations in milliseconds per kind */
int tr_duration_ms(int kind);
/* damped spring 0..1 for t in 0..1 (slight overshoot, exactly 1 at t >= 1) */
float tr_spring(float t);
/* ease-out curve for opacity (no overshoot) */
float tr_ease_out(float t);
/* choose the transition between two screens.
 * tab: TAB_* index, level: 0 = page, 1.. = dialog depth / keyboard */
int tr_choose(int old_tab, int old_level, int new_tab, int new_level);
/* prepare a transition (layout rows, background) */
void tr_plan(TrPlan *p, int kind, const uint32_t *prev, const uint32_t *next, const uint32_t *backdrop, int w,
             int h);
/* compose rows [y0, y1) of the frame at progress t (0..1) into dst. */
void tr_compose_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                     int y1);
/* the whole frame, split over up to `threads` worker threads (1 = in the calling thread) */
void tr_compose(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int threads);
/* stop the worker threads (at exit) */
void tr_shutdown(void);
#endif
