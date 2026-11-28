/* ORBIT screen transitions, see transition.h. Integer blending, no allocation per frame. */
#include "transition.h"
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

int tr_duration_ms(int kind) {
    switch (kind) {
    case TR_SLIDE_LEFT:
    case TR_SLIDE_RIGHT:
        return 420;
    case TR_RISE:
        return 240;
    case TR_DROP:
        return 200;
    case TR_FADE:
        return 200;
    default:
        return 0;
    }
}
float tr_spring(float t) {
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    /* x(t) = 1 - e^(-z w t) (cos(wd t) + z w / wd sin(wd t)), z = 0.78, w = 9 (time normalised):
     * about 2 % overshoot, settled to < 0.2 % at t = 1 */
    const float z = 0.78f, w = 9.0f, wd = w * 0.6257795f; /* sqrt(1 - z^2) */
    float e = expf(-z * w * t);
    float x = 1.0f - e * (cosf(wd * t) + (z * w / wd) * sinf(wd * t));
    return x;
}
float tr_ease_out(float t) {
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    float u = 1 - t;
    return 1 - u * u * u;
}
int tr_choose(int old_tab, int old_level, int new_tab, int new_level) {
    if (new_level > old_level)
        return TR_RISE;
    if (new_level < old_level)
        return TR_DROP;
    if (new_tab != old_tab)
        return new_tab > old_tab ? TR_SLIDE_LEFT : TR_SLIDE_RIGHT;
    return TR_FADE;
}
/* a = 0..256: weight of n */
static inline uint32_t mix(uint32_t o, uint32_t n, uint32_t a) {
    uint32_t b = 256 - a;
    uint32_t rb = (((o & 0xff00ffu) * b + (n & 0xff00ffu) * a) >> 8) & 0xff00ffu;
    uint32_t g = (((o & 0x00ff00u) * b + (n & 0x00ff00u) * a) >> 8) & 0x00ff00u;
    return 0xff000000u | rb | g;
}
static inline uint32_t weight(float v) {
    return v <= 0 ? 0 : v >= 1 ? 256 : (uint32_t)(v * 256.0f);
}

void tr_plan(TrPlan *p, int kind, const uint32_t *prev, const uint32_t *next, const uint32_t *backdrop, int w,
             int h) {
    (void)prev;
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    p->w = w;
    p->h = h;
    p->backdrop = backdrop;
    p->bg = next[(size_t)(h - 1) * w + w / 2] | 0xff000000u;
    /* same layout as ui.c: top bar above 76 / 720, hint bar below 664 / 720 */
    p->head = 76 * h / 720;
    p->foot = 664 * h / 720;
}

/* ---------- composition ---------- */
static inline uint32_t backdrop_px(const TrPlan *p, size_t i) {
    return p->backdrop ? p->backdrop[i] : p->bg;
}
/* smooth deceleration for the push (quartic ease-out: fast start, soft landing, no bounce) */
static float ease_push(float t) {
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    float u = 1 - t;
    return 1 - u * u * u * u;
}
static void foot_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                      int y1);
/* tab change, "push": the old page slides out to one side while the new page slides in from the
 * other, side by side with a small gap, so there is never one screen on top of another.
 * Next tab: everything moves left; previous tab: everything moves right.
 * The space background and the top bar stay where they are (the top bar's tab pill cross-fades);
 * when the backdrop is known only the page's own pixels move over it. The hint bar fades through. */
static void slide_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                       int y1) {
    int w = p->w, dir = p->kind == TR_SLIDE_LEFT ? 1 : -1;
    int gap = w / 24;                                     /* space between the two pages */
    int travel = (int)lroundf(ease_push(t) * (w + gap)); /* how far both pages have moved */
    /* screen x shows old x + travel*dir, and new x + travel*dir - (w + gap)*dir */
    int off_old = travel * dir, off_new = (travel - w - gap) * dir;
    /* a touch of depth: the leaving page dims a little, the arriving one brightens */
    uint32_t a_old = 256 - weight(ease_push(t) * 0.35f), a_new = 256 - weight((1 - ease_push(t)) * 0.35f);
    uint32_t a_head = weight(tr_ease_out(t));
    const uint32_t *bd = p->backdrop;
    for (int y = y0; y < y1; y++) {
        size_t r = (size_t)y * w;
        uint32_t *d = dst + r;
        if (y < p->head) {
            for (int x = 0; x < w; x++)
                d[x] = mix(prev[r + x], next[r + x], a_head);
        } else if (y >= p->foot) {
            foot_rows(dst, prev, next, p, t, y, y + 1);
        } else {
            for (int x = 0; x < w; x++) {
                uint32_t back = backdrop_px(p, r + x), c = back;
                int so = x + off_old, sn = x + off_new;
                if (so >= 0 && so < w) {
                    uint32_t v = prev[r + so];
                    if (!bd || ((v ^ bd[r + so]) & 0xffffffu))
                        c = mix(back, v, a_old);
                } else if (sn >= 0 && sn < w) {
                    uint32_t v = next[r + sn];
                    if (!bd || ((v ^ bd[r + sn]) & 0xffffffu))
                        c = mix(back, v, a_new);
                }
                d[x] = c;
            }
        }
    }
}
/* hint bar: fade through the backdrop (old hints out, then new hints in), so two rows of button
 * hints are never readable on top of each other */
static void foot_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                      int y1) {
    int w = p->w, out = t < 0.5f;
    float u = out ? t / 0.5f : (t - 0.5f) / 0.5f;
    uint32_t a = out ? 256 - weight(u) : weight(u);
    const uint32_t *src = out ? prev : next;
    for (int y = y0; y < y1; y++) {
        size_t r = (size_t)y * w;
        for (int x = 0; x < w; x++)
            dst[r + x] = mix(backdrop_px(p, r + x), src[r + x], a);
    }
}
/* dialogs and everything else: an exact cross-fade in place. ORBIT's dialogs are frosted glass over
 * the dimmed page, so moving or scaling them would shift the page text seen through the glass and
 * show it twice; a fade keeps every pixel where it belongs. Opening eases out, closing is quicker. */
static void fade_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                      int y1) {
    int w = p->w;
    uint32_t a = weight(tr_ease_out(t));
    int y_foot = y1 < p->foot ? y1 : p->foot;
    for (int y = y0; y < y_foot; y++) {
        size_t r = (size_t)y * w;
        for (int x = 0; x < w; x++)
            dst[r + x] = mix(prev[r + x], next[r + x], a);
    }
    if (y1 > p->foot)
        foot_rows(dst, prev, next, p, t, y0 > p->foot ? y0 : p->foot, y1);
}
void tr_compose_rows(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int y0,
                     int y1) {
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    if (y0 < 0)
        y0 = 0;
    if (y1 > p->h)
        y1 = p->h;
    int w = p->w;
    if (t >= 1) { /* exactly the new screen */
        for (int y = y0; y < y1; y++)
            memcpy(dst + (size_t)y * w, next + (size_t)y * w, (size_t)w * 4);
        return;
    }
    if (p->kind == TR_SLIDE_LEFT || p->kind == TR_SLIDE_RIGHT) {
        slide_rows(dst, prev, next, p, t, y0, y1);
        return;
    }
    fade_rows(dst, prev, next, p, t, y0, y1);
}

/* ---------- worker threads ---------- */
#define TR_MAX_THREADS 8
static struct {
    pthread_mutex_t m;
    pthread_cond_t go, done;
    pthread_t th[TR_MAX_THREADS];
    int started, quit, gen, pending;
    /* current job */
    uint32_t *dst;
    const uint32_t *prev, *next;
    const TrPlan *plan;
    int parts;
    float t;
} pool = {.m = PTHREAD_MUTEX_INITIALIZER, .go = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER};

static void *worker(void *arg) {
    int id = (int)(intptr_t)arg, seen = 0;
    pthread_mutex_lock(&pool.m);
    for (;;) {
        while (!pool.quit && pool.gen == seen)
            pthread_cond_wait(&pool.go, &pool.m);
        if (pool.quit)
            break;
        seen = pool.gen;
        if (id >= pool.parts - 1) /* this job needs fewer helpers */
            continue;
        int part = id + 1, parts = pool.parts, h = pool.plan->h;
        pthread_mutex_unlock(&pool.m);
        tr_compose_rows(pool.dst, pool.prev, pool.next, pool.plan, pool.t, h * part / parts, h * (part + 1) / parts);
        pthread_mutex_lock(&pool.m);
        if (--pool.pending == 0)
            pthread_cond_signal(&pool.done);
    }
    pthread_mutex_unlock(&pool.m);
    return NULL;
}
void tr_compose(uint32_t *dst, const uint32_t *prev, const uint32_t *next, const TrPlan *p, float t, int threads) {
    if (threads > TR_MAX_THREADS)
        threads = TR_MAX_THREADS;
    int h = p->h;
    pthread_mutex_lock(&pool.m);
    while (pool.started < threads - 1) {
        if (pthread_create(&pool.th[pool.started], NULL, worker, (void *)(intptr_t)pool.started))
            break;
        pool.started++;
    }
    int parts = 1 + (pool.started < threads - 1 ? pool.started : threads - 1);
    if (parts <= 1) {
        pthread_mutex_unlock(&pool.m);
        tr_compose_rows(dst, prev, next, p, t, 0, h);
        return;
    }
    pool.dst = dst;
    pool.prev = prev;
    pool.next = next;
    pool.plan = p;
    pool.t = t;
    pool.parts = parts;
    pool.pending = parts - 1;
    pool.gen++;
    pthread_cond_broadcast(&pool.go);
    pthread_mutex_unlock(&pool.m);
    tr_compose_rows(dst, prev, next, p, t, 0, h / parts); /* part 0 here */
    pthread_mutex_lock(&pool.m);
    while (pool.pending > 0)
        pthread_cond_wait(&pool.done, &pool.m);
    pthread_mutex_unlock(&pool.m);
}
void tr_shutdown(void) {
    pthread_mutex_lock(&pool.m);
    pool.quit = 1;
    pthread_cond_broadcast(&pool.go);
    int n = pool.started;
    pthread_mutex_unlock(&pool.m);
    for (int i = 0; i < n; i++)
        pthread_join(pool.th[i], NULL);
    pthread_mutex_lock(&pool.m);
    pool.started = 0;
    pool.quit = 0;
    pthread_mutex_unlock(&pool.m);
}
