/* ORBIT NEXT screens: Glass primitives, top bar, Spotlight Home, Discover, Games.
 * Included by ui.c (shares its primitives and palette). Software rendering only: every
 * full-screen effect is computed once and cached; panels are cheap alpha blends. */
#include "avatar.h"
#include "image.h"
static unsigned hash_text(const char *t);
static void scaled_forget(const uint32_t *src);
static void scaled_clear(void);
static float cl01(float v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* ---------- glass primitives ---------- */
static void fill_a(int x0, int y0, int x1, int y1, uint32_t c, unsigned a) {
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > sw)
        x1 = sw;
    if (y1 > sh)
        y1 = sh;
    if (a >= 255) {
        fill_px(x0, y0, x1, y1, c);
        return;
    }
    if (!a || x1 <= x0)
        return;
    unsigned t = a + (a >> 7), u = 256 - t;
    uint32_t crb = (c & 0xff00ffu) * t + 0x800080u, cg = (c & 0x00ff00u) * t + 0x008000u;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = screen + j * sw;
        for (int i = x0; i < x1; i++) {
            uint32_t d = row[i];
            row[i] = ((((d & 0xff00ffu) * u + crb) >> 8) & 0xff00ffu) | ((((d & 0x00ff00u) * u + cg) >> 8) & 0x00ff00u);
        }
    }
}
/* rows [ya, yb) of a panel whose rounded top corners (radius R, pixels) start at row ytop.
 * Edges are not anti-aliased: only used for faint light bands inside a panel. */
static void band_rows(float X0, float X1, float ytop, float R, int ya, int yb, uint32_t c, unsigned a) {
    for (int j = ya; j < yb; j++) {
        float dy = ytop + R - (j + 0.5f), inset = 0;
        if (dy > 0)
            inset = R - sqrtf(fmaxf(0.0f, R * R - dy * dy));
        fill_a((int)(X0 + inset + 0.5f), j, (int)(X1 - inset + 0.5f), j + 1, c, a);
    }
}
/* rounded rectangle blended with alpha a (0..255); mask: 1 = round top corners, 2 = round bottom */
static void rrect_ca(int x, int y, int w, int h, int r, uint32_t c, unsigned a, int mask) {
    if (w <= 0 || h <= 0 || !a)
        return;
    float X0 = fx((float)x), Y0 = fy((float)y), X1 = fx((float)(x + w)), Y1 = fy((float)(y + h));
    float R = fy((float)r);
    if (R * 2 > X1 - X0)
        R = (X1 - X0) / 2;
    if (R * 2 > Y1 - Y0)
        R = (Y1 - Y0) / 2;
    int ri = (int)ceilf(R), ix0 = (int)X0, iy0 = (int)Y0, ix1 = (int)X1, iy1 = (int)Y1;
    int top = mask & 1 ? ri : 0, bottom = mask & 2 ? ri : 0;
    fill_a(ix0 + ri, iy0, ix1 - ri, iy1, c, a);
    fill_a(ix0, iy0 + top, ix0 + ri, iy1 - bottom, c, a);
    fill_a(ix1 - ri, iy0 + top, ix1, iy1 - bottom, c, a);
    float fa = a / 255.0f;
    for (int j = 0; j < ri; j++)
        for (int i = 0; i < ri; i++) {
            float dx = R - (i + 0.5f), dy = R - (j + 0.5f);
            float cover = R - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (cover <= 0)
                continue;
            cover = (cover > 1 ? 1 : cover) * fa;
            if (mask & 1) {
                plot(ix0 + i, iy0 + j, c, cover);
                plot(ix1 - 1 - i, iy0 + j, c, cover);
            }
            if (mask & 2) {
                plot(ix0 + i, iy1 - 1 - j, c, cover);
                plot(ix1 - 1 - i, iy1 - 1 - j, c, cover);
            }
        }
}
static void rrect_a(int x, int y, int w, int h, int r, uint32_t c, unsigned a) {
    rrect_ca(x, y, w, h, r, c, a, 3);
}
/* outline of a rounded rectangle, thickness t (virtual px) */
static void ring_rect(int x, int y, int w, int h, int r, int t, uint32_t c, unsigned a) {
    float X0 = fx((float)x), Y0 = fy((float)y), X1 = fx((float)(x + w)), Y1 = fy((float)(y + h));
    float R = fy((float)r), T = fy((float)t);
    if (T < 1)
        T = 1;
    float fa = a / 255.0f;
    int ext = 2;
    /* straight edges */
    fill_a((int)(X0 + R), (int)Y0, (int)(X1 - R), (int)(Y0 + T), c, a);
    fill_a((int)(X0 + R), (int)(Y1 - T), (int)(X1 - R), (int)Y1, c, a);
    fill_a((int)X0, (int)(Y0 + R), (int)(X0 + T), (int)(Y1 - R), c, a);
    fill_a((int)(X1 - T), (int)(Y0 + R), (int)X1, (int)(Y1 - R), c, a);
    /* anti-aliased corner arcs */
    int ri = (int)ceilf(R) + ext;
    float cxs[2] = {X0 + R, X1 - R}, cys[2] = {Y0 + R, Y1 - R};
    for (int k = 0; k < 4; k++) {
        float cx = cxs[k & 1], cy = cys[k >> 1];
        int sx0 = k & 1 ? (int)cx : (int)cx - ri, sy0 = k >> 1 ? (int)cy : (int)cy - ri;
        for (int j = sy0; j < sy0 + ri; j++)
            for (int i = sx0; i < sx0 + ri; i++) {
                float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
                if ((k & 1) ? dx < 0 : dx > 0)
                    continue;
                if ((k >> 1) ? dy < 0 : dy > 0)
                    continue;
                float d = sqrtf(dx * dx + dy * dy);
                float cover = fminf(R - d + 0.5f, d - (R - T) + 0.5f);
                if (cover > 0)
                    plot(i, j, c, (cover > 1 ? 1 : cover) * fa);
            }
    }
}
/* frosted panel: translucent body, slightly brighter top, hairline edge highlight */
static void glass_panel(int x, int y, int w, int h, int r) {
    if (perf_mode) { /* Performance theme: opaque panel, no blending */
        rrect(x, y, w, h, r, C_SURF);
        return;
    }
    rrect_a(x, y, w, h, r, C_SURF, 205);
    /* soft top light: stacked bands with tiny alpha read as a smooth gradient */
    static const int bands[5] = {0, 8, 18, 30, 46}; /* alpha 12, 9, 6, 3 from the top */
    float X0 = fx((float)(x + 1)), X1 = fx((float)(x + w - 1)), Yt = fy((float)(y + 1)), R = fy((float)r);
    for (int k = 0; k < 4; k++) {
        int ya = (int)fy((float)(y + 1 + h * bands[k] / 100)), yb = (int)fy((float)(y + 1 + h * bands[k + 1] / 100));
        band_rows(X0, X1, Yt, R, ya < 0 ? 0 : ya, yb > sh ? sh : yb, 0xffffff, (unsigned)(12 - 3 * k));
    }
    ring_rect(x, y, w, h, r, 1, 0xffffff, 24);
}
/* controller focus: crisp accent ring plus a soft outer glow (2 cheap rings) */
static void focus_ring(int x, int y, int w, int h, int r) {
    ring_rect(x - 6, y - 6, w + 12, h + 12, r + 6, 3, C_ACCENT, 38);
    ring_rect(x - 3, y - 3, w + 6, h + 6, r + 3, 2, C_ACCENT, 255);
}
/* pill button; returns its width */
static int pill(int x, int y, const char *label, int focused, int primary) {
    int w = text_width(label, 17) + 44, h = 44;
    if (primary)
        rrect(x, y, w, h, h / 2, focused ? blend(C_ACCENT, 0xffffff, 40) : C_ACCENT);
    else
        rrect_a(x, y, w, h, h / 2, focused ? C_SURF2 : 0xffffff, focused ? 255 : 18);
    if (!primary)
        ring_rect(x, y, w, h, h / 2, 1, 0xffffff, focused ? 60 : 30);
    if (focused)
        ring_rect(x - 4, y - 4, w + 8, h + 8, h / 2 + 4, 2, primary ? 0xffffff : C_ACCENT, 230);
    text(x + 22, y + 11, 17, primary ? 0xffffff : C_TEXT, label, w);
    return w;
}

/* ---------- atmospheric background (computed once per size/theme) ---------- */
static uint32_t *space_bg;
static int space_w, space_h, space_theme = -1;
static void space_background(void) {
    if (space_bg && space_w == sw && space_h == sh && space_theme == current_theme) {
        memcpy(screen, space_bg, (size_t)sw * sh * 4);
        return;
    }
    if (perf_mode) {
        fill_px(0, 0, sw, sh, C_BG);
        return;
    }
    static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    uint32_t deep = blend(C_BG, 0x000000, 70);
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            float u = (i + 0.5f) / sw, v = (j + 0.5f) / sh;
            float dither = (bayer[j & 3][i & 3] / 16.0f - 0.5f) * 2.0f;
            /* blue glow upper left, violet glow upper right, darker towards the bottom */
            float b1 = cl01(1 - sqrtf((u - 0.18f) * (u - 0.18f) * 1.4f + (v - 0.05f) * (v - 0.05f) * 2.6f) * 1.5f);
            float b2 = cl01(1 - sqrtf((u - 0.92f) * (u - 0.92f) * 1.6f + (v - 0.0f) * (v - 0.0f) * 2.2f) * 1.7f);
            uint32_t p = blend(C_BG, deep, (unsigned)(v * 160));
            p = blend(p, C_ACCENT, (unsigned)fminf(255, fmaxf(0, b1 * b1 * 46 + dither)));
            p = blend(p, C_ACCENT2, (unsigned)fminf(255, fmaxf(0, b2 * b2 * 34 + dither)));
            screen[j * sw + i] = p;
        }
    /* faint orbit arcs in the background: part of the ORBIT identity, very low contrast */
    for (int k = 0; k < 2; k++) {
        float cx = fx(1060), cy = fy(-260), R = fy(620 + k * 140);
        for (int j = 0; j < sh; j++)
            for (int i = sw / 3; i < sw; i++) {
                float dx = i + 0.5f - cx, dy = (j + 0.5f - cy) * 1.25f, d = sqrtf(dx * dx + dy * dy);
                float cover = 1.0f - fabsf(d - R) / 1.2f;
                if (cover > 0)
                    plot(i, j, C_ACCENT, cover * 0.07f);
            }
    }
    free(space_bg);
    space_bg = malloc((size_t)sw * sh * 4);
    if (space_bg) {
        memcpy(space_bg, screen, (size_t)sw * sh * 4);
        space_w = sw;
        space_h = sh;
        space_theme = current_theme;
    }
}

/* ---------- game data and lazily decoded icons ---------- */
static const OrbitGame *ui_games;
static int ui_game_count, ui_games_scanning, ui_lazy_pending;
void ui_set_games(const OrbitGame *games, int count, int scanning) {
    ui_games = games;
    ui_game_count = games && count > 0 ? count : 0;
    ui_games_scanning = scanning;
}
int ui_games_count(void) {
    return ui_game_count;
}
int ui_pending(void) {
    return ui_lazy_pending;
}
#define ICON_SLOTS 24
static struct {
    char path[160];
    OrbitImage img; /* 0xAARRGGBB, at most 320x176 */
    uint32_t avg, age;
    int state;      /* 0 empty, 1 ok, -1 failed */
} icons[ICON_SLOTS];
static uint32_t icon_age;
static int icon_loads_this_frame;
static int icon_get(const char *path, OrbitImage **img, uint32_t *avg) {
    *img = NULL;
    if (!path || !path[0])
        return -1;
    int free_slot = -1;
    for (int i = 0; i < ICON_SLOTS; i++) {
        if (icons[i].state && !strcmp(icons[i].path, path)) {
            icons[i].age = ++icon_age;
            if (icons[i].state < 0)
                return -1;
            *img = &icons[i].img;
            if (avg)
                *avg = icons[i].avg;
            return 0;
        }
        if (free_slot < 0 || (icons[free_slot].state && (!icons[i].state || icons[i].age < icons[free_slot].age)))
            free_slot = i;
    }
    if (icon_loads_this_frame >= 1) { /* decode at most one icon per frame: no input lag */
        ui_lazy_pending = 1;
        return 1;
    }
    icon_loads_this_frame++;
    scaled_forget(icons[free_slot].img.px);
    image_free(&icons[free_slot].img);
    memset(&icons[free_slot], 0, sizeof(icons[free_slot]));
    snprintf(icons[free_slot].path, sizeof(icons[free_slot].path), "%s", path);
    icons[free_slot].age = ++icon_age;
    OrbitImage raw;
    /* ICON0.PNG is 320x176; anything larger than 1024 is refused before decoding */
    if (image_load_png(path, &raw, 1024, 1024) != IMG_OK) {
        icons[free_slot].state = -1;
        return -1;
    }
    if (raw.w > 320 || raw.h > 176) {
        OrbitImage small;
        int rc = image_scale(&raw, &small, 320, 176);
        image_free(&raw);
        if (rc != IMG_OK) {
            icons[free_slot].state = -1;
            return -1;
        }
        raw = small;
    }
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < raw.w * raw.h; i += 7) {
        uint32_t p = raw.px[i];
        if ((p >> 24) < 128)
            continue;
        r += p >> 16 & 255, g += p >> 8 & 255, b += p & 255, n++;
    }
    icons[free_slot].avg = n ? (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n) : C_ACCENT;
    icons[free_slot].img = raw;
    icons[free_slot].state = 1;
    *img = &icons[free_slot].img;
    if (avg)
        *avg = icons[free_slot].avg;
    return 0;
}
static void ui_next_close(void) {
    free(space_bg);
    space_bg = NULL;
    space_w = space_h = 0;
    scaled_clear();
    for (int i = 0; i < ICON_SLOTS; i++)
        image_free(&icons[i].img);
    memset(icons, 0, sizeof(icons));
}
/* Scaled-image cache. Bilinear scaling, rounded corners and the fade are computed once per
 * (image, pixel size) into a private ARGB buffer (alpha = coverage); every later frame only
 * blends that buffer. Least recently used entries are dropped above the pixel budget. */
#define SCALED_SLOTS 32
#define SCALED_BUDGET (3u * 1024u * 1024u) /* pixels in all slots together (12 MB) */
static struct {
    const uint32_t *src;
    int W, H, r, fade;
    uint32_t *px, age;
} scaled[SCALED_SLOTS];
static uint32_t scaled_age, scaled_pixels;
static int scaled_misses;
static void scaled_drop(int i) {
    if (scaled[i].px)
        scaled_pixels -= (uint32_t)(scaled[i].W * scaled[i].H);
    free(scaled[i].px);
    memset(&scaled[i], 0, sizeof(scaled[i]));
}
static void scaled_forget(const uint32_t *src) { /* call before the source pixels are freed */
    for (int i = 0; i < SCALED_SLOTS; i++)
        if (scaled[i].px && scaled[i].src == src)
            scaled_drop(i);
}
static void scaled_clear(void) {
    for (int i = 0; i < SCALED_SLOTS; i++)
        scaled_drop(i);
    scaled_pixels = 0;
}
static void blit_draw(const OrbitImage *img, int X0, int Y0, int W, int H, int r, int fade, unsigned alpha,
                      uint32_t *out);
static void blit(const OrbitImage *img, int x, int y, int w, int h, int r, int fade, unsigned alpha) {
    int X0 = sx(x), Y0 = sy(y), W = sx(x + w) - X0, H = sy(y + h) - Y0;
    if (!img || !img->px || W <= 0 || H <= 0)
        return;
    int hit = -1;
    for (int i = 0; i < SCALED_SLOTS && hit < 0; i++)
        if (scaled[i].px && scaled[i].src == img->px && scaled[i].W == W && scaled[i].H == H && scaled[i].r == r &&
            scaled[i].fade == fade)
            hit = i;
    if (hit < 0) {
        uint32_t need = (uint32_t)W * (uint32_t)H;
        if (need > SCALED_BUDGET / 2) {
            blit_draw(img, X0, Y0, W, H, r, fade, alpha, NULL);
            return;
        }
        scaled_misses++;
        int slot = -1;
        for (;;) {
            int old = -1;
            slot = -1;
            for (int i = 0; i < SCALED_SLOTS; i++) {
                if (!scaled[i].px && slot < 0)
                    slot = i;
                if (scaled[i].px && (old < 0 || scaled[i].age < scaled[old].age))
                    old = i;
            }
            if (slot >= 0 && scaled_pixels + need <= SCALED_BUDGET)
                break;
            if (old < 0)
                break;
            scaled_drop(old);
        }
        uint32_t *px = slot >= 0 ? calloc(need, 4) : NULL;
        if (!px) {
            blit_draw(img, X0, Y0, W, H, r, fade, alpha, NULL);
            return;
        }
        blit_draw(img, 0, 0, W, H, r, fade, 255, px);
        scaled[slot].src = img->px;
        scaled[slot].W = W;
        scaled[slot].H = H;
        scaled[slot].r = r;
        scaled[slot].fade = fade;
        scaled[slot].px = px;
        scaled_pixels += need;
        hit = slot;
    }
    scaled[hit].age = ++scaled_age;
    const uint32_t *src = scaled[hit].px;
    int i0 = X0 < 0 ? -X0 : 0, i1 = X0 + W > sw ? sw - X0 : W;
    for (int j = 0; j < H; j++) {
        int py = Y0 + j;
        if (py < 0 || py >= sh)
            continue;
        uint32_t *row = screen + py * sw + X0;
        const uint32_t *s = src + j * W;
        for (int i = i0; i < i1; i++) {
            uint32_t p = s[i];
            unsigned a = p >> 24;
            if (alpha < 255)
                a = a * alpha / 255;
            if (a >= 255)
                row[i] = p & 0xffffffu;
            else if (a)
                row[i] = blend(row[i], p & 0xffffffu, a);
        }
    }
}
/* bilinear scaling with rounded corners r and a left fade (0 = none). out == NULL: blends straight
 * onto the screen at pixel position X0/Y0; otherwise writes W x H ARGB pixels (alpha = coverage). */
static void blit_draw(const OrbitImage *img, int X0, int Y0, int W, int H, int r, int fade, unsigned alpha,
                      uint32_t *out) {
    float R = fy((float)r), F = fx((float)fade);
    for (int j = 0; j < H; j++) {
        int py = Y0 + j;
        if (!out && (py < 0 || py >= sh))
            continue;
        float sy_ = ((j + 0.5f) * img->h / H) - 0.5f;
        int y0 = (int)floorf(sy_), y1 = y0 + 1;
        float ty = sy_ - y0;
        if (y0 < 0)
            y0 = 0;
        if (y1 >= img->h)
            y1 = img->h - 1;
        if (y0 >= img->h)
            y0 = img->h - 1;
        for (int i = 0; i < W; i++) {
            int px = X0 + i;
            if (!out && (px < 0 || px >= sw))
                continue;
            float cover = alpha / 255.0f;
            /* rounded corners */
            float cx = i < R ? R - i - 0.5f : i > W - R ? i + 0.5f - (W - R) : 0;
            float cy = j < R ? R - j - 0.5f : j > H - R ? j + 0.5f - (H - R) : 0;
            if (cx > 0 && cy > 0) {
                float d = sqrtf(cx * cx + cy * cy);
                if (d > R + 0.5f)
                    continue;
                if (d > R - 0.5f)
                    cover *= R + 0.5f - d;
            }
            if (F > 0 && i < F)
                cover *= (i + 0.5f) / F;
            float sx_ = ((i + 0.5f) * img->w / W) - 0.5f;
            int x0 = (int)floorf(sx_), x1 = x0 + 1;
            float tx = sx_ - x0;
            if (x0 < 0)
                x0 = 0;
            if (x1 >= img->w)
                x1 = img->w - 1;
            if (x0 >= img->w)
                x0 = img->w - 1;
            uint32_t a = img->px[y0 * img->w + x0], b = img->px[y0 * img->w + x1],
                     c = img->px[y1 * img->w + x0], d = img->px[y1 * img->w + x1];
            float wa = (1 - tx) * (1 - ty), wb = tx * (1 - ty), wc = (1 - tx) * ty, wd = tx * ty;
            float al = ((a >> 24) * wa + (b >> 24) * wb + (c >> 24) * wc + (d >> 24) * wd) / 255.0f;
            unsigned rr = (unsigned)((a >> 16 & 255) * wa + (b >> 16 & 255) * wb + (c >> 16 & 255) * wc + (d >> 16 & 255) * wd);
            unsigned gg = (unsigned)((a >> 8 & 255) * wa + (b >> 8 & 255) * wb + (c >> 8 & 255) * wc + (d >> 8 & 255) * wd);
            unsigned bb = (unsigned)((a & 255) * wa + (b & 255) * wb + (c & 255) * wc + (d & 255) * wd);
            if (out) {
                float k = cover * al;
                unsigned ak = k <= 0 ? 0 : k >= 1 ? 255 : (unsigned)(k * 255.0f + 0.5f);
                out[j * W + i] = (uint32_t)ak << 24 | rr << 16 | gg << 8 | bb;
            } else
                plot(px, py, rr << 16 | gg << 8 | bb, cover * al);
        }
    }
}
/* generated fallback when a title has no readable ICON0.PNG: initials on a tinted tile */
static void game_fallback(const OrbitGame *g, int x, int y, int w, int h, int r) {
    unsigned hsh = hash_text(g->id);
    uint32_t c = person_colors[hsh % 12];
    rrect(x, y, w, h, r, blend(C_SURF, c, 70));
    rrect_a(x, y, w, h / 2, r, 0xffffff, 10);
    char ini[8] = "";
    int n = 0;
    const char *p = g->title;
    while (*p && n < 2) {
        while (*p == ' ')
            p++;
        if (*p && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) {
            ini[n++] = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
        }
        while (*p && *p != ' ')
            p++;
    }
    ini[n] = 0;
    int size = h * 38 / 100, tw = text_width(ini, size);
    bold(x + (w - tw) / 2, y + (h - size) / 2 - size / 8, size, blend(C_TEXT, c, 60), ini, w);
}
static void game_art(const OrbitGame *g, int x, int y, int w, int h, int r, int fade, uint32_t *avg) {
    OrbitImage *img = NULL;
    int rc = icon_get(g->icon, &img, avg);
    if (!rc && img)
        blit(img, x, y, w, h, r, fade, 255);
    else if (rc > 0) /* still loading: calm placeholder of the same size */
        rrect_a(x, y, w, h, r, 0xffffff, 12);
    else
        game_fallback(g, x, y, w, h, r);
}

/* ---------- real-data helpers ---------- */
static int ci_contains(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (n < 4)
        return 0;
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        for (; i < n && h[i]; i++) {
            char a = h[i], b = needle[i];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (a != b)
                break;
        }
        if (i == n)
            return 1;
    }
    return 0;
}
static int same_game(const char *a, const char *b) {
    return a[0] && b[0] && (ci_contains(a, b) || ci_contains(b, a));
}
/* Friends who typed this game into their profile. This is self-declared profile text, NOT
 * verified activity, and is labelled that way ("lists it in their profile"). Real "playing
 * now" needs automatic presence from the background plugin (see docs/next/PRESENCE.md). */
static int friends_listing(const UiState *s, const char *title) {
    int n = 0;
    for (int i = 0; i < s->friend_count; i++) {
        const UiFriend *f = &s->friends[i];
        if (f->state == FR_FRIENDS && same_game(f->game, title))
            n++;
    }
    return n;
}
/* 1.2: friends whose presence plugin reports this game right now (verified, not profile text) */
static int friends_playing(const UiState *s, const OrbitGame *g) {
    int n = 0;
    for (int i = 0; i < s->friend_count; i++) {
        const UiFriend *f = &s->friends[i];
        if (f->state == FR_FRIENDS && f->playing &&
            ((f->playing_id[0] && !strcmp(f->playing_id, g->id)) || same_game(f->playing_name, g->title)))
            n++;
    }
    return n;
}
/* known servers (joined + listed public ones) whose name or description mentions the game */
static int communities_for(const UiState *s, const char *title) {
    int n = 0;
    for (int i = 0; i < s->group_count; i++)
        if (s->groups[i].kind == GK_SERVER && (same_game(s->groups[i].name, title) || ci_contains(s->groups[i].desc, title)))
            n++;
    for (int i = 0; i < s->browse_count; i++)
        if (same_game(s->browse[i].name, title) || ci_contains(s->browse[i].desc, title))
            n++;
    return n;
}
int ui_home_game(const UiLocal *l) {
    if (!ui_game_count)
        return -1;
    return l->home_game >= 0 && l->home_game < ui_game_count ? l->home_game : 0;
}
int ui_home_buttons(const UiState *s, const UiLocal *l) {
    (void)s;
    return ui_home_game(l) >= 0 ? 3 : 2;
}
static uint32_t group_last(const UiGroup *g) {
    uint32_t t = 0;
    for (int k = 0; k < g->nch; k++)
        if (g->ch[k].last > t)
            t = g->ch[k].last;
    return t;
}
int ui_home_messages(const UiState *s, int out[3]) {
    int n = 0;
    for (int i = 0; i < s->group_count; i++) {
        if (s->groups[i].kind != GK_DM)
            continue;
        uint32_t t = group_last(&s->groups[i]);
        int k = n < 3 ? n++ : 3;
        while (k > 0 && group_last(&s->groups[out[k - 1]]) < t) {
            if (k < 3)
                out[k] = out[k - 1];
            k--;
        }
        if (k < 3)
            out[k] = i;
    }
    return n;
}
static unsigned home_key(const UiLocal *l) {
    return (unsigned)l->home_zone | (unsigned)(l->home_item & 255) << 4 | (unsigned)((l->home_game + 1) & 255) << 12 |
           (unsigned)(l->grid_focus & 255) << 20;
}

/* ---------- top bar ---------- */
static void top_bar(const UiState *s, const UiLocal *l) {
    orbit_mark(70, 50, 13);
    bold(96, 32, 26, C_TEXT, "ORBIT", 140);
    static const int names[5] = {T_NAV_HOME, T_NAV_DISCOVER, T_NAV_FRIENDS, T_NAV_GAMES, T_NAV_SERVERS};
    static const int tabs[5] = {TAB_HOME, TAB_DISCOVER, TAB_FRIENDS, TAB_GAMES, TAB_SERVERS};
    /* right aligned: [L1] tabs [R1] | messages settings bell */
    int widths[5], total = 0;
    for (int i = 0; i < 5; i++)
        total += (widths[i] = text_width(T(names[i]), 18) + 30);
    int r1w = text_width("R1", 13) + 16, l1w = text_width("L1", 13) + 16;
    int x = 1232 - 3 * 46 - 24 - r1w - 8 - total - 8 - l1w;
    x = key_cap(x, 34, "L1") + 8;
    for (int i = 0; i < 5; i++) {
        int active = l->tab == tabs[i];
        if (active) {
            rrect_a(x, 30, widths[i], 38, 19, 0xffffff, 22);
            rrect(x + widths[i] / 2 - 12, 66, 24, 3, 1, C_ACCENT);
        }
        text(x + 15, 38, 18, active ? C_TEXT : C_MUTED, T(names[i]), widths[i]);
        if (tabs[i] == TAB_FRIENDS && s->requests)
            badge(x + widths[i] - 6, 30, s->requests, C_BAD);
        if (tabs[i] == TAB_SERVERS && s->unread_server)
            disc((float)x + widths[i] - 6, 34, 4, C_ACCENT);
        x += widths[i];
    }
    key_cap(x + 8, 34, "R1");
    box(1232 - 3 * 46 - 8, 36, 1, 30, C_LINE);
    int ix = 1232 - 3 * 46 + 22;
    int extra[3] = {TAB_MESSAGES, TAB_SETTINGS, -1};
    for (int i = 0; i < 3; i++, ix += 46) {
        int active = extra[i] >= 0 && l->tab == extra[i];
        if (active)
            rrect_a(ix - 20, 30, 40, 38, 14, 0xffffff, 22);
        if (extra[i] >= 0)
            tab_icon(extra[i], ix, 49, active ? C_TEXT : C_MUTED);
        else { /* notification bell */
            uint32_t c = C_MUTED;
            disc((float)ix, 45, 8, c);
            rrect(ix - 8, 45, 16, 9, 2, c);
            rrect(ix - 11, 52, 22, 3, 1, c);
            disc((float)ix, 58, 2.5f, c);
            disc((float)ix, 45, 5.5f, blend(C_BG, C_SURF, 120));
            rrect(ix - 5, 45, 10, 6, 1, blend(C_BG, C_SURF, 120));
        }
        if (extra[i] == TAB_MESSAGES && s->unread_dm)
            badge(ix + 14, 34, s->unread_dm, C_ACCENT);
        if (extra[i] < 0 && s->requests)
            badge(ix + 14, 34, s->requests, C_BAD);
    }
}

/* ---------- Spotlight Home ---------- */
#define HERO_X 48
#define HERO_Y 100
#define HERO_W 800
#define HERO_H 372
#define COL_X 872
#define COL_W 360
#define STRIP_Y 500
#define TILE_W 168
#define TILE_H 92
#define TILE_GAP 18
static void hero(const UiState *s, const UiLocal *l) {
    int focused = l->home_zone == HZ_HERO;
    int gi = ui_home_game(l);
    glass_panel(HERO_X, HERO_Y, HERO_W, HERO_H, 24);
    char line[200];
    if (gi < 0) {
        /* empty state: new user without games */
        orbit_mark(HERO_X + 640, HERO_Y + 150, 54);
        text(HERO_X + 36, HERO_Y + 40, 15, C_ACCENT, T(T_WELCOME_NEW), 400);
        bold(HERO_X + 36, HERO_Y + 70, 36, C_TEXT, T(T_HERO_EMPTY_TITLE), 520);
        wrapped(HERO_X + 36, HERO_Y + 128, 18, C_MUTED, T(T_HERO_EMPTY_SUB), 460, 3, 8);
        int bx = HERO_X + 36, by = HERO_Y + HERO_H - 80;
        bx += pill(bx, by, T(T_BTN_JOIN), focused && l->home_item == 0, 1) + 14;
        pill(bx, by, T(T_BTN_ALL_SERVERS), focused && l->home_item == 1, 0);
        if (ui_games_scanning)
            text(HERO_X + HERO_W - 260, HERO_Y + HERO_H - 66, 14, C_DIM, T(T_GAMES_SCANNING), 230);
    } else {
        const OrbitGame *g = &ui_games[gi];
        uint32_t avg = C_ACCENT;
        /* art first (right side), then the text column over the faded edge */
        int ax = HERO_X + 368, ay = HERO_Y + 28, aw = 404, ah = 222;
        game_art(g, ax, ay, aw, ah, 16, 0, &avg);
        rrect_a(HERO_X, HERO_Y + HERO_H - 120, HERO_W, 120, 24, blend(avg, C_BG, 200), 60);
        const char *label = g->source == GAME_SRC_DISC ? T(T_HERO_DISC) : g->played ? T(T_HERO_PLAYED) : T(T_HERO_RECENT);
        rrect_a(HERO_X + 36, HERO_Y + 36, text_width(label, 13) + 24, 26, 13, C_ACCENT, 60);
        text(HERO_X + 48, HERO_Y + 40, 13, blend(C_ACCENT, 0xffffff, 150), label, 300);
        int ty = wrapped(HERO_X + 36, HERO_Y + 78, 32, C_TEXT, g->title, 316, 3, 6);
        char tid[24];
        snprintf(tid, sizeof(tid), "%s", g->id);
        text(HERO_X + 36, ty + 6, 14, C_DIM, tid, 300);
        int fp = friends_listing(s, g->title), live = friends_playing(s, g);
        if (live == 1)
            snprintf(line, sizeof(line), "%s", T(T_HERO_PLAYING_ONE));
        else if (live > 1)
            TN(line, sizeof(line), T_HERO_PLAYING_N, live, live);
        else if (fp == 1)
            snprintf(line, sizeof(line), "%s", T(T_HERO_FRIEND_NOW));
        else if (fp > 1)
            TN(line, sizeof(line), T_HERO_FRIENDS_NOW, fp, fp);
        else
            snprintf(line, sizeof(line), "%s", T(T_HERO_NO_FRIENDS));
        /* green only for verified presence; profile text stays blue (not live) */
        disc((float)HERO_X + 42, (float)ty + 42, 4, live ? C_OK : fp ? C_ACCENT : C_DIM);
        text(HERO_X + 54, ty + 32, 16, live || fp ? C_TEXT : C_MUTED, line, 300);
        int nc = communities_for(s, g->title);
        char comm[64];
        if (!nc) /* never show "0 communities": offer the directory instead */
            snprintf(comm, sizeof(comm), "%s", T(T_BTN_ALL_SERVERS));
        else if (nc == 1)
            snprintf(comm, sizeof(comm), "%s", T(T_BTN_COMMUNITY_ONE));
        else
            TN(comm, sizeof(comm), T_BTN_COMMUNITIES, nc, nc);
        int bx = HERO_X + 36, by = HERO_Y + HERO_H - 76;
        bx += pill(bx, by, T(T_BTN_FIND_PLAYERS), focused && l->home_item == 0, 1) + 14;
        bx += pill(bx, by, T(T_BTN_INVITE_FRIENDS), focused && l->home_item == 1, 0) + 14;
        pill(bx, by, comm, focused && l->home_item == 2, 0);
    }
    if (focused)
        ring_rect(HERO_X - 4, HERO_Y - 4, HERO_W + 8, HERO_H + 8, 28, 2, C_ACCENT, 140);
}
static void online_panel(const UiState *s, const UiLocal *l) {
    int y = HERO_Y, h = 168, focused = l->home_zone == HZ_ONLINE;
    glass_panel(COL_X, y, COL_W, h, 20);
    int online = 0, friends = 0;
    for (int i = 0; i < s->friend_count; i++)
        if (s->friends[i].state == FR_FRIENDS)
            friends++, online += s->friends[i].online;
    bold(COL_X + 24, y + 20, 20, C_TEXT, T(T_PANEL_ONLINE), 200);
    char n[24];
    snprintf(n, sizeof(n), "%d / %d", online, friends);
    text(COL_X + COL_W - 24 - text_width(n, 15), y + 24, 15, C_MUTED, n, 100);
    if (!friends) {
        rrect_a(COL_X + 24, y + 70, COL_W - 48, 68, 14, C_ACCENT, focused ? 70 : 34);
        text(COL_X + 44, y + 82, 22, C_TEXT, "+", 20);
        wrapped(COL_X + 72, y + 80, 15, C_TEXT, T(T_ADD_FRIEND_HINT), COL_W - 110, 2, 4);
    } else if (!online)
        wrapped(COL_X + 24, y + 76, 16, C_MUTED, T(T_ONLINE_NONE), COL_W - 48, 2, 6);
    else {
        int shown = 0, x = COL_X + 24;
        for (int i = 0; i < s->friend_count && shown < 5; i++) {
            const UiFriend *f = &s->friends[i];
            if (f->state != FR_FRIENDS || !f->online)
                continue;
            person(x, y + 64, 48, f->name, f->color, f->avatar, f->pic, f->anim);
            presence(x + 41, y + 106, f->status);
            text(x - 2, y + 120, 12, C_MUTED, f->name, 56);
            x += 60;
            shown++;
        }
        if (online > shown) {
            char more[16];
            snprintf(more, sizeof(more), "+%d", online - shown);
            disc((float)x + 24, (float)y + 88, 24, blend(C_SURF, C_ACCENT, 60));
            int w = text_width(more, 16);
            text(x + 24 - w / 2, y + 78, 16, C_TEXT, more, 48);
        }
    }
    if (focused)
        focus_ring(COL_X, y, COL_W, h, 20);
}
static void messages_panel(const UiState *s, const UiLocal *l) {
    int y = HERO_Y + 186, h = HERO_H - 186, focused = l->home_zone == HZ_MESSAGES;
    glass_panel(COL_X, y, COL_W, h, 20);
    bold(COL_X + 24, y + 18, 20, C_TEXT, T(T_PANEL_MESSAGES), 200);
    if (s->unread_dm)
        badge(COL_X + COL_W - 36, y + 30, s->unread_dm, C_ACCENT);
    int idx[3], n = ui_home_messages(s, idx);
    if (!n)
        text(COL_X + 24, y + 64, 15, C_MUTED, T(T_MSG_NONE), COL_W - 48);
    for (int k = 0; k < n; k++) {
        const UiGroup *g = &s->groups[idx[k]];
        int ry = y + 56 + k * 42, sel = focused && l->home_item == k;
        if (sel)
            rrect_a(COL_X + 12, ry - 4, COL_W - 24, 40, 12, 0xffffff, 26);
        const UiPerson *p = g->ndm ? &g->dm[0] : NULL;
        person(COL_X + 20, ry, 32, p ? p->name : g->name, p ? p->color : 0, p ? p->avatar : 0, p ? p->pic : NULL, p ? p->anim : NULL);
        text(COL_X + 62, ry - 1, 15, C_TEXT, g->ndm > 1 ? g->people : (p ? p->name : g->name), 200);
        const char *last = "";
        uint32_t t = 0;
        for (int c = 0; c < g->nch; c++)
            if (g->ch[c].last >= t && g->ch[c].ltext[0])
                t = g->ch[c].last, last = g->ch[c].ltext;
        text(COL_X + 62, ry + 17, 12, C_DIM, last, COL_W - 110);
        if (g->unread && !g->muted_chat)
            disc((float)COL_X + COL_W - 30, (float)ry + 16, 5, C_ACCENT);
    }
    if (focused && !n)
        focus_ring(COL_X, y, COL_W, h, 20);
    else if (focused)
        ring_rect(COL_X - 4, y - 4, COL_W + 8, h + 8, 24, 2, C_ACCENT, 140);
}
static void game_strip(const UiState *s, const UiLocal *l) {
    (void)s;
    bold(56, STRIP_Y, 19, C_TEXT, T(T_YOUR_GAMES), 300);
    if (ui_game_count) {
        char n[24];
        TN(n, sizeof(n), T_GAMES_COUNT, ui_game_count, ui_game_count);
        text(60 + text_width(T(T_YOUR_GAMES), 19) + 14, STRIP_Y + 3, 14, C_DIM, n, 200);
    }
    int y = STRIP_Y + 34;
    if (!ui_game_count) {
        glass_panel(48, y, 1184, TILE_H + 26, 18);
        text(80, y + 44, 16, C_MUTED, ui_games_scanning ? T(T_GAMES_SCANNING) : T(T_GAMES_NONE), 1100);
        return;
    }
    int per = 6, top = l->home_strip_top;
    if (top < 0 || top >= ui_game_count)
        top = 0;
    int hero_game = ui_home_game(l);
    for (int k = 0; k < per && top + k < ui_game_count; k++) {
        int gi = top + k, x = 56 + k * (TILE_W + TILE_GAP);
        const OrbitGame *g = &ui_games[gi];
        int sel = l->home_zone == HZ_STRIP && l->home_item == gi;
        game_art(g, x, y, TILE_W, TILE_H, 12, 0, NULL);
        if (gi == hero_game)
            rrect(x + TILE_W / 2 - 14, y + TILE_H + 6, 28, 3, 1, C_ACCENT);
        if (sel)
            focus_ring(x, y, TILE_W, TILE_H, 12);
        text(x, y + TILE_H + 14, 13, sel ? C_TEXT : C_MUTED, g->title, TILE_W);
    }
    if (top + per < ui_game_count) {
        char more[16];
        snprintf(more, sizeof(more), "+%d", ui_game_count - top - per);
        text(1232 - 60, y + 34, 18, C_DIM, more, 60);
    }
    if (top > 0)
        text(30, y + 34, 18, C_DIM, "‹", 20);
}
static void home_next(const UiState *s, const UiLocal *l) {
    hero(s, l);
    online_panel(s, l);
    messages_panel(s, l);
    game_strip(s, l);
}

/* ---------- Discover ----------
 * The list comes from the persistent directory: live communities first, then ones whose
 * host PS3 is off ("Host offline", greyed, join disabled). Without any reachable PS3 the
 * saved list is shown with a "saved list" note and CROSS retries; "none found" is only
 * shown after a real answer. */
static int state_badge(int x, int y, const char *label, uint32_t c) {
    int w = text_width(label, 13) + 24;
    rrect_a(x, y, w, 26, 13, c, 46);
    ring_rect(x, y, w, 26, 13, 1, c, 120);
    text(x + 12, y + 5, 13, blend(c, 0xffffff, 140), label, w);
    return w;
}
static void discover_status(const UiState *s, int y) {
    const char *t = NULL;
    uint32_t c = C_MUTED;
    if (s->browse_state == BROWSE_ASKING)
        t = T(T_DISC_UPDATING);
    else if (s->browse_state == BROWSE_NO_CONNECTION) {
        t = T(T_DISC_SAVED_LIST);
        c = C_WARN;
    }
    if (t)
        text(58, y, 14, c, t, 1160);
}
static void discover_next(const UiState *s, const UiLocal *l) {
    bold(56, 96, 30, C_TEXT, T(T_DISCOVER_TITLE), 900);
    discover_status(s, 138);
    int n = s->browse_count;
    int narrowed = s->dir_query[0] || s->dir_filter != DIR_F_ALL;
    if (narrowed || s->dir_sort != DIR_S_POPULAR) { /* 1.2: active search / filter, right of the title */
        static const int fk[DIR_F_COUNT] = {T_DISC_F_ALL, T_DISC_F_LIVE, T_DISC_F_JOINED, T_DISC_F_NEW};
        char what[96], line[160];
        if (s->dir_query[0])
            snprintf(what, sizeof(what), "\"%s\"  ·  %s", s->dir_query, T(fk[s->dir_filter % DIR_F_COUNT]));
        else
            snprintf(what, sizeof(what), "%s", T(fk[s->dir_filter % DIR_F_COUNT]));
        TF(line, sizeof(line), T_DISC_SHOWN, n, s->dir_total, what);
        text(640, 108, 14, C_ACCENT, line, 590);
    }
    if (!n && narrowed && s->dir_total) {
        glass_panel(48, 172, 1184, 120, 20);
        text(80, 222, 18, C_MUTED, T(T_DISC_NO_MATCH), 1100);
        return;
    }
    if (!n) {
        glass_panel(48, 172, 1184, 160, 20);
        const char *t = s->browse_state == BROWSE_ASKING          ? T(T_DISCOVER_LOADING)
                        : s->browse_state == BROWSE_NO_CONNECTION ? T(T_DISC_NO_CONNECTION)
                                                                  : T(T_DISCOVER_NONE);
        text(80, 226, 18, C_MUTED, t, 1100);
        if (s->browse_state == BROWSE_NO_CONNECTION)
            pill(80, 262, T(T_DISC_RETRY), 1, 1);
        return;
    }
    uint32_t now = (uint32_t)time(NULL);
    int cols = 3, cw = 376, ch = 200, top = l->grid_top;
    for (int k = 0; k < 6 && top + k < n; k++) {
        int i = top + k, x = 48 + (k % cols) * (cw + 28), y = 172 + (k / cols) * (ch + 28);
        const UiBrowse *b = &s->browse[i];
        uint32_t c = b->live ? person_colors[b->color % 12] : C_DIM;
        glass_panel(x, y, cw, ch, 20);
        /* banner: server accent colour as a soft strip (grey while the host is offline) */
        rrect_ca(x, y, cw, 70, 20, c, b->live ? 70 : 40, 1);
        UiGroup tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.color = b->live ? b->color : 11;
        tmp.icon = b->icon;
        snprintf(tmp.pic, sizeof(tmp.pic), "%s", b->pic);
        snprintf(tmp.anim, sizeof(tmp.anim), "%s", b->anim);
        snprintf(tmp.name, sizeof(tmp.name), "%s", b->name);
        server_icon(x + 20, y + 44, 52, &tmp);
        /* state badges, top right of the banner */
        int bx = x + 86, by = y + 14;
        if (b->live) {
            char on[48];
            if (b->online >= 0) {
                TF(on, sizeof(on), T_DISC_ONLINE_N, b->online);
                bx += state_badge(bx, by, on, C_OK) + 8;
            } else
                bx += state_badge(bx, by, T(T_ST_ONLINE), C_OK) + 8;
        } else {
            char off[96];
            snprintf(off, sizeof(off), "%s", T(T_HOST_OFFLINE_SHORT));
            if (b->seen && now > b->seen && now - b->seen >= 86400u) {
                char ago[48];
                TN(ago, sizeof(ago), T_DISC_SEEN_DAYS, (int)((now - b->seen) / 86400u), (int)((now - b->seen) / 86400u));
                size_t m = strlen(off);
                snprintf(off + m, sizeof(off) - m, "  ·  %s", ago);
            }
            bx += state_badge(bx, by, off, C_BAD) + 8;
        }
        if (b->member)
            state_badge(bx, by, T(T_DISC_JOINED), C_ACCENT);
        bold(x + 86, y + 78, 19, b->live ? C_TEXT : C_MUTED, b->name, cw - 100);
        char sub[160];
        TF(sub, sizeof(sub), T_BROWSE_SUB, b->members, b->host);
        text(x + 20, y + 112, 13, C_MUTED, sub, cw - 40);
        wrapped(x + 20, y + 136, 14, C_DIM, b->desc, cw - 40, 2, 4);
        if (l->grid_focus == i)
            focus_ring(x, y, cw, ch, 20);
    }
    /* page indicator when there are more than 6 communities */
    if (n > 6) {
        char pg[32];
        snprintf(pg, sizeof(pg), "%d / %d", top / 6 + 1, (n + 5) / 6);
        text(1160, 640, 14, C_DIM, pg, 80);
    }
}

/* ---------- Games (phase 4 adds covers, search, favourites) ---------- */
static void games_next(const UiState *s, const UiLocal *l) {
    (void)s;
    bold(56, 96, 30, C_TEXT, T(T_GAMES_TITLE), 900);
    text(58, 138, 14, C_DIM, T(T_GAMES_NOTE), 1100);
    if (!ui_game_count) {
        glass_panel(48, 180, 1184, 140, 20);
        text(80, 236, 18, C_MUTED, ui_games_scanning ? T(T_GAMES_SCANNING) : T(T_GAMES_NONE), 1100);
        return;
    }
    int cols = 5, tw = 216, th = 119, top = l->grid_top;
    for (int k = 0; k < 15 && top + k < ui_game_count; k++) {
        int i = top + k, x = 56 + (k % cols) * (tw + 20), y = 180 + (k / cols) * (th + 42);
        const OrbitGame *g = &ui_games[i];
        game_art(g, x, y, tw, th, 12, 0, NULL);
        if (l->grid_focus == i)
            focus_ring(x, y, tw, th, 12);
        text(x, y + th + 8, 14, l->grid_focus == i ? C_TEXT : C_MUTED, g->title, tw);
    }
}

static void spotlight_hints(const UiState *s, const UiLocal *l) {
    footer_bar();
    int x = 56, y = 678;
    x = hint(x, y, -1, "L1/R1", T(T_H_TABS));
    x = hint(x, y, -2, NULL, T(T_H_MOVE));
    x = hint(x, y, K_CROSS, NULL, T(T_H_SELECT));
    if (l->tab == TAB_HOME)
        x = hint(x, y, K_TRIANGLE, NULL, T(T_H_ADD_FRIEND));
    else if (l->tab == TAB_DISCOVER) {
        x = hint(x, y, K_SQUARE, NULL, T(T_H_SEARCH));
        if (s->browse_count)
            x = hint(x, y, K_TRIANGLE, NULL, T(T_REPORT));
    }
    hint(x, y, K_CIRCLE, NULL, T(T_H_MENU));
}
static void spotlight_screen(const UiState *s, const UiLocal *l) {
    icon_loads_this_frame = 0;
    ui_lazy_pending = 0;
    space_background();
    top_bar(s, l);
    if (l->tab == TAB_HOME)
        home_next(s, l);
    else if (l->tab == TAB_DISCOVER)
        discover_next(s, l);
    else
        games_next(s, l);
    if (l->page == PAGE_CHAT)
        spotlight_hints(s, l);
}

static int pic_gen_now, pic_loads_frame;
static char pic_dir[400];
/* ---------- 1.2: animated pictures (anim.h) ----------
 * Animations are decoded by a small loader thread (never on the interface thread), kept in an LRU
 * cache bounded by bytes, and only advance while they are drawn: an off-screen picture costs
 * nothing and asks for no redraw. At most ANIM_ACTIVE pictures move in one frame; the rest show
 * the frame they have. Under heavy load (slow frames), with "Reduce motion" or with animated
 * pictures switched off, the first frame (the still picture) is shown instead. */
#define ANIM_SLOTS 8
#define ANIM_CACHE_BYTES (8u << 20)
#define ANIM_ACTIVE 6
#define ANIM_HEAVY_US 30000 /* average frame drawing time that pauses animations */
static struct {
    char hash[33];
    int state, gen, frame; /* 1 loaded, 2 loading, -1 missing (retried when pictures arrive) */
    uint32_t age;
    OrbitAnim a;
    OrbitImage img; /* current frame, 96 x 96 */
} anims[ANIM_SLOTS];
static uint32_t anim_age;
static int anim_enabled = 1, anim_heavy, anim_active_frame;
static uint64_t anim_now, anim_due; /* due: earliest frame change of the pictures drawn last frame */
void (*ui_anim_wanted)(const char *hash);
static pthread_mutex_t anim_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t anim_cv = PTHREAD_COND_INITIALIZER;
static pthread_t anim_thread;
static int anim_thread_on, anim_quit, anim_req_slot = -1, anim_res_slot = -1, anim_res_ok;
static char anim_req_hash[33];
static OrbitAnim anim_res;
static void *anim_loader(void *arg) {
    (void)arg;
    pthread_mutex_lock(&anim_mx);
    while (!anim_quit) {
        if (anim_req_slot < 0 || anim_res_slot >= 0) {
            pthread_cond_wait(&anim_cv, &anim_mx);
            continue;
        }
        int slot = anim_req_slot;
        char hash[33];
        snprintf(hash, sizeof(hash), "%s", anim_req_hash);
        pthread_mutex_unlock(&anim_mx);
        OrbitAnim a;
        uint8_t *blob;
        size_t n;
        int ok = !anim_load(pic_dir, hash, &blob, &n);
        if (ok) {
            ok = !anim_decode(blob, n, hash, &a);
            free(blob);
        }
        pthread_mutex_lock(&anim_mx);
        anim_req_slot = -1;
        anim_res_slot = slot;
        anim_res_ok = ok;
        if (ok)
            anim_res = a;
    }
    pthread_mutex_unlock(&anim_mx);
    return NULL;
}
static void anim_slot_free(int i) {
    scaled_forget(anims[i].img.px);
    image_free(&anims[i].img);
    anim_free(&anims[i].a);
    memset(&anims[i], 0, sizeof(anims[i]));
}
static size_t anim_cache_bytes(void) {
    size_t b = 0;
    for (int i = 0; i < ANIM_SLOTS; i++)
        if (anims[i].state == 1)
            b += anim_bytes(&anims[i].a) + ANIM_FRAME_PX * 4;
    return b;
}
/* take a finished load (once per frame) */
static void anim_collect(void) {
    pthread_mutex_lock(&anim_mx);
    int slot = anim_res_slot, ok = anim_res_ok;
    OrbitAnim a = anim_res;
    anim_res_slot = -1;
    pthread_cond_signal(&anim_cv);
    pthread_mutex_unlock(&anim_mx);
    if (slot < 0)
        return;
    if (anims[slot].state != 2) { /* the slot was given away meanwhile */
        if (ok)
            anim_free(&a);
        return;
    }
    if (!ok) {
        anims[slot].state = -1;
        anims[slot].gen = pic_gen_now;
        if (ui_anim_wanted)
            ui_anim_wanted(anims[slot].hash);
        return;
    }
    anims[slot].img.px = malloc(ANIM_FRAME_PX * 4);
    if (!anims[slot].img.px) {
        anim_free(&a);
        anims[slot].state = -1;
        return;
    }
    anims[slot].img.w = anims[slot].img.h = ANIM_SIDE;
    anims[slot].a = a;
    anims[slot].state = 1;
    anims[slot].frame = -1;
    /* stay inside the memory budget: drop the least recently drawn ones */
    while (anim_cache_bytes() > ANIM_CACHE_BYTES) {
        int old = -1;
        for (int i = 0; i < ANIM_SLOTS; i++)
            if (i != slot && anims[i].state == 1 && (old < 0 || anims[i].age < anims[old].age))
                old = i;
        if (old < 0)
            break;
        anim_slot_free(old);
    }
}
void ui_anim_settings(int enabled) {
    anim_enabled = enabled;
}
void ui_anim_load(int render_avg_us) {
    anim_heavy = render_avg_us > ANIM_HEAVY_US;
}
/* 1 when an animated picture that was on screen needs its next frame */
int ui_anim_due(uint64_t now) {
    return anim_due && now >= anim_due;
}
/* the current frame of an animation, or NULL (then the still picture is used) */
static const OrbitImage *anim_get(const char *hash) {
    if (!hash || !hash[0] || !pic_dir[0] || !anim_enabled)
        return NULL;
    if (!anim_thread_on) {
        anim_quit = 0;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 128 * 1024);
        anim_thread_on = !pthread_create(&anim_thread, &attr, anim_loader, NULL);
        pthread_attr_destroy(&attr);
        if (!anim_thread_on)
            return NULL;
    }
    int slot = -1;
    for (int i = 0; i < ANIM_SLOTS; i++) {
        if (anims[i].state && !strcmp(anims[i].hash, hash)) {
            slot = i;
            break;
        }
    }
    if (slot >= 0 && anims[slot].state == -1 && anims[slot].gen != pic_gen_now)
        anims[slot].state = 0; /* pictures arrived since: look again */
    if (slot < 0 || !anims[slot].state) {
        pthread_mutex_lock(&anim_mx);
        int busy = anim_req_slot >= 0 || anim_res_slot >= 0;
        pthread_mutex_unlock(&anim_mx);
        if (busy) {
            ui_lazy_pending = 1;
            return NULL;
        }
        if (slot < 0) { /* a free slot or the least recently drawn one */
            for (int i = 0; i < ANIM_SLOTS; i++)
                if (anims[i].state != 2 && (slot < 0 || !anims[i].state || (anims[slot].state && anims[i].age < anims[slot].age)))
                    slot = i;
            if (slot < 0)
                return NULL;
            anim_slot_free(slot);
        }
        snprintf(anims[slot].hash, sizeof(anims[slot].hash), "%s", hash);
        anims[slot].state = 2;
        anims[slot].age = ++anim_age;
        pthread_mutex_lock(&anim_mx);
        anim_req_slot = slot;
        snprintf(anim_req_hash, sizeof(anim_req_hash), "%s", hash);
        pthread_cond_signal(&anim_cv);
        pthread_mutex_unlock(&anim_mx);
        ui_lazy_pending = 1;
        return NULL;
    }
    if (anims[slot].state != 1)
        return NULL;
    anims[slot].age = ++anim_age;
    int f = anims[slot].frame < 0 ? 0 : anims[slot].frame;
    if (!anim_heavy && anim_active_frame < ANIM_ACTIVE) {
        anim_active_frame++;
        f = anim_frame_at(&anims[slot].a, (uint32_t)anim_now);
        /* when does this frame end? */
        uint32_t t = (uint32_t)(anim_now % (uint64_t)anims[slot].a.total_ms), end = 0;
        for (int k = 0; k <= f; k++)
            end += anims[slot].a.delay[k];
        uint64_t due = anim_now + (end > t ? end - t : 1);
        if (!anim_due || due < anim_due)
            anim_due = due;
    }
    if (f != anims[slot].frame) {
        scaled_forget(anims[slot].img.px);
        anim_render(&anims[slot].a, f, anims[slot].img.px);
        anims[slot].frame = f;
    }
    return &anims[slot].img;
}
static void anim_frame_begin(uint64_t now) {
    anim_now = now;
    anim_due = 0;
    anim_active_frame = 0;
    anim_collect();
}
static void anim_close(void) {
    if (anim_thread_on) {
        pthread_mutex_lock(&anim_mx);
        anim_quit = 1;
        pthread_cond_signal(&anim_cv);
        pthread_mutex_unlock(&anim_mx);
        pthread_join(anim_thread, NULL);
        anim_thread_on = 0;
        if (anim_res_slot >= 0 && anim_res_ok)
            anim_free(&anim_res);
        anim_res_slot = anim_req_slot = -1;
    }
    for (int i = 0; i < ANIM_SLOTS; i++)
        anim_slot_free(i);
}

/* ---------- 1.2: profile pictures (avatar.h) ---------- */
#define PIC_SLOTS 48
static struct {
    char hash[33];
    int state, gen; /* 1 = loaded, -1 = not on disk yet (retried when a picture arrives) */
    uint32_t age;
    OrbitImage img;
} pics[PIC_SLOTS];
static uint32_t pic_age;
void (*ui_pic_wanted)(const char *hash);
void ui_set_pic_dir(const char *dir) {
    snprintf(pic_dir, sizeof(pic_dir), "%s", dir ? dir : "");
}
void ui_pic_clear(void) {
    for (int i = 0; i < PIC_SLOTS; i++) {
        scaled_forget(pics[i].img.px);
        image_free(&pics[i].img);
        memset(&pics[i], 0, sizeof(pics[i]));
    }
}
static const OrbitImage *pic_get(const char *hash) {
    if (!hash || !hash[0] || !pic_dir[0])
        return NULL;
    int slot = -1;
    for (int i = 0; i < PIC_SLOTS; i++) {
        if (pics[i].state && !strcmp(pics[i].hash, hash)) {
            pics[i].age = ++pic_age;
            if (pics[i].state > 0)
                return &pics[i].img;
            if (pics[i].gen == pic_gen_now)
                return NULL;
            slot = i; /* a picture arrived since: look again */
            break;
        }
        if (slot < 0 || (pics[slot].state && (!pics[i].state || pics[i].age < pics[slot].age)))
            slot = i;
    }
    if (pic_loads_frame >= 2) { /* at most two decodes per frame */
        ui_lazy_pending = 1;
        return NULL;
    }
    pic_loads_frame++;
    scaled_forget(pics[slot].img.px);
    image_free(&pics[slot].img);
    memset(&pics[slot], 0, sizeof(pics[slot]));
    snprintf(pics[slot].hash, sizeof(pics[slot].hash), "%s", hash);
    pics[slot].age = ++pic_age;
    uint8_t *blob = malloc(AVATAR_BLOB_MAX);
    size_t n = 0;
    if (blob && !avatar_load(pic_dir, hash, blob, &n) && !avatar_decode(blob, n, hash, &pics[slot].img))
        pics[slot].state = 1;
    else {
        pics[slot].state = -1;
        pics[slot].gen = pic_gen_now;
        if (ui_pic_wanted)
            ui_pic_wanted(hash);
    }
    free(blob);
    return pics[slot].state > 0 ? &pics[slot].img : NULL;
}
