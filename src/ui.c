/* Orbit 0.7 renderer: software 2D on a 1280x720 virtual canvas, scaled to the output mode.
 * Everything is drawn here (no image assets): anti-aliased shapes and controller glyphs. */
#include "ui.h"
#include "emoji.h"
#include "image.h"
#include "anim.h"
#include <pthread.h>
#include "qrcodegen.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static const OrbitImage *pic_get(const char *hash);
static const OrbitImage *anim_get(const char *hash);
static void anim_close(void);
static void anim_frame_begin(uint64_t now);
static void blit(const OrbitImage *img, int x, int y, int w, int h, int r, int fade, unsigned alpha);
static void emoji_free_all(void);
static FT_Library library;
static FT_Face face;
static uint32_t *screen;
static int sw, sh, timezone_minutes;
/* 1.2 (04): list screens (Friends, Servers, Messages, Settings) are drawn under the shared top bar
 * like Home/Discover/Games: no own logo or tab-icon row any more */
static int unified_ui = 1;
static uint32_t *legacy_buf;
static size_t legacy_cap;
/* ---------- glyph cache: four-way, bounded to 2 MiB ---------- */
typedef struct {
    uint32_t cp, age;
    int size, advance, left, top, width, height;
    unsigned char *pixels;
} Glyph;
static Glyph glyphs[2048][4];
static size_t glyph_bytes;
static uint32_t glyph_age;
static int font_size;
static void clear_glyphs(void) {
    for (int i = 0; i < 2048; i++)
        for (int j = 0; j < 4; j++)
            free(glyphs[i][j].pixels);
    memset(glyphs, 0, sizeof(glyphs));
    glyph_bytes = 0;
}
static Glyph *glyph(uint32_t cp, int size) {
    if (size < 1)
        size = 1;
    Glyph *set = glyphs[(cp * 131u + (unsigned)size * 977u) & 2047], *slot = &set[0];
    for (int i = 0; i < 4; i++) {
        if (set[i].size == size && set[i].cp == cp) {
            set[i].age = ++glyph_age;
            return &set[i];
        }
        if (!set[i].size || (slot->size && set[i].age < slot->age))
            slot = &set[i];
    }
    if (font_size != size) {
        if (FT_Set_Pixel_Sizes(face, 0, size))
            return NULL;
        font_size = size;
    }
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER))
        return NULL;
    FT_GlyphSlot g = face->glyph;
    size_t bytes = (size_t)g->bitmap.width * g->bitmap.rows;
    if (bytes > 65536)
        return NULL;
    if (glyph_bytes + bytes > 6 * 1024 * 1024) {
        clear_glyphs();
        slot = &set[0];
    }
    glyph_bytes -= (size_t)slot->width * slot->height;
    free(slot->pixels);
    memset(slot, 0, sizeof(*slot));
    if (bytes) {
        slot->pixels = malloc(bytes);
        if (!slot->pixels)
            return NULL;
        for (int y = 0; y < (int)g->bitmap.rows; y++)
            memcpy(slot->pixels + y * g->bitmap.width, g->bitmap.buffer + y * g->bitmap.pitch,
                   g->bitmap.width);
    }
    slot->cp = cp;
    slot->size = size;
    slot->age = ++glyph_age;
    slot->advance = (int)(g->advance.x >> 6);
    slot->left = g->bitmap_left;
    slot->top = g->bitmap_top;
    slot->width = (int)g->bitmap.width;
    slot->height = (int)g->bitmap.rows;
    glyph_bytes += bytes;
    return slot;
}
/* ---------- palette ---------- */
static uint32_t C_BG = 0x0d0f13, C_SIDE = 0x111318, C_SURF = 0x171a20, C_SURF2 = 0x1f232a,
                C_LINE = 0x262a32, C_ACCENT2 = 0xa66bff;
static const uint32_t C_TEXT = 0xf3f5f8, C_MUTED = 0x9aa1ab, C_DIM = 0x636a74,
                      C_OK = 0x39d98a, C_WARN = 0xf4b445, C_BAD = 0xff5d62,
                      G_CROSS = 0x8ab6ff, G_CIRCLE = 0xff6f6f, G_SQUARE = 0xf39ad9,
                      G_TRIANGLE = 0x43dcb7;
static uint32_t C_ACCENT = 0x4f8cff;
static int bg_rev = -1, current_theme, perf_mode;
static void ui_next_close(void);
static char text_rows[64][1024];
void ui_set_timezone(int minutes) {
    timezone_minutes = minutes;
}
int ui_init(const char *path) {
    return FT_Init_FreeType(&library) || FT_New_Face(library, path, 0, &face) ? -1 : 0;
}
static uint32_t *bg_cache;
static size_t bg_size;
void ui_close(void) {
    clear_glyphs();
    emoji_free_all();
    ui_next_close();
    ui_pic_clear();
    anim_close();
    free(bg_cache);
    bg_cache = NULL;
    bg_size = 0;
    free(legacy_buf);
    legacy_buf = NULL;
    legacy_cap = 0;
    font_size = 0;
    if (face)
        FT_Done_Face(face);
    if (library)
        FT_Done_FreeType(library);
    face = NULL;
    library = NULL;
}
/* ---------- primitives (virtual 1280x720 coordinates) ---------- */
static int sx(int x) {
    return x * sw / 1280;
}
static int sy(int y) {
    return y * sh / 720;
}
static float fx(float x) {
    return x * (float)sw / 1280.0f;
}
static float fy(float y) {
    return y * (float)sh / 720.0f;
}
/* a..b by t/255. Two-lane integer form (red and blue share one multiply): 4 multiplies per
 * pixel instead of 6 plus the divisions. t maps to 0..256, exact at 0 and 255. */
static inline uint32_t blend(uint32_t a, uint32_t b, unsigned t) {
    t += t >> 7;
    unsigned u = 256 - t;
    uint32_t rb = (((a & 0xff00ffu) * u + (b & 0xff00ffu) * t + 0x800080u) >> 8) & 0xff00ffu;
    uint32_t g = (((a & 0x00ff00u) * u + (b & 0x00ff00u) * t + 0x008000u) >> 8) & 0x00ff00u;
    return rb | g;
}
static void fill_px(int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > sw)
        x1 = sw;
    if (y1 > sh)
        y1 = sh;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = screen + j * sw;
        for (int i = x0; i < x1; i++)
            row[i] = c;
    }
}
static void box(int x, int y, int w, int h, uint32_t c) {
    fill_px(sx(x), sy(y), sx(x + w), sy(y + h), c);
}
static inline void plot(int x, int y, uint32_t c, float cover) {
    if (cover <= 0.0f || x < 0 || y < 0 || x >= sw || y >= sh)
        return;
    uint32_t *p = &screen[y * sw + x];
    *p = cover >= 1.0f ? c : blend(*p, c, (unsigned)(cover * 255.0f));
}
/* Rounded rectangle with anti-aliased corners. */
static void rrect(int x, int y, int w, int h, int r, uint32_t c) {
    float X0 = fx((float)x), Y0 = fy((float)y), X1 = fx((float)(x + w)), Y1 = fy((float)(y + h));
    float R = fy((float)r);
    if (R * 2 > X1 - X0)
        R = (X1 - X0) / 2;
    if (R * 2 > Y1 - Y0)
        R = (Y1 - Y0) / 2;
    int ri = (int)ceilf(R);
    int ix0 = (int)X0, iy0 = (int)Y0, ix1 = (int)X1, iy1 = (int)Y1;
    fill_px(ix0 + ri, iy0, ix1 - ri, iy1, c);
    fill_px(ix0, iy0 + ri, ix0 + ri, iy1 - ri, c);
    fill_px(ix1 - ri, iy0 + ri, ix1, iy1 - ri, c);
    for (int j = 0; j < ri; j++)
        for (int i = 0; i < ri; i++) {
            float dx = R - (i + 0.5f), dy = R - (j + 0.5f);
            float cover = R - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (cover <= 0)
                continue;
            if (cover > 1)
                cover = 1;
            plot(ix0 + i, iy0 + j, c, cover);
            plot(ix1 - 1 - i, iy0 + j, c, cover);
            plot(ix0 + i, iy1 - 1 - j, c, cover);
            plot(ix1 - 1 - i, iy1 - 1 - j, c, cover);
        }
}
static void disc(float cx, float cy, float r, uint32_t c) {
    float X = fx(cx), Y = fy(cy), R = fy(r);
    for (int j = (int)(Y - R - 1); j <= (int)(Y + R + 1); j++)
        for (int i = (int)(X - R - 1); i <= (int)(X + R + 1); i++) {
            float dx = i + 0.5f - X, dy = j + 0.5f - Y;
            float cover = R - sqrtf(dx * dx + dy * dy) + 0.5f;
            plot(i, j, c, cover > 1 ? 1 : cover);
        }
}
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = (wx * vx + wy * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = wx - t * vx, dy = wy - t * vy;
    return sqrtf(dx * dx + dy * dy);
}
/* ---------- controller glyphs ---------- */
enum { K_CROSS, K_CIRCLE, K_SQUARE, K_TRIANGLE };
static void face_button(float cx, float cy, float d, int kind) {
    disc(cx, cy, d / 2, 0x2a2f37);
    disc(cx, cy, d / 2 - 1.3f, 0x1a1d23);
    float X = fx(cx), Y = fy(cy), s = fy(d) * 0.26f, w = fy(d) * 0.05f + 0.45f;
    uint32_t color = kind == K_CROSS    ? G_CROSS
                     : kind == K_CIRCLE ? G_CIRCLE
                     : kind == K_SQUARE ? G_SQUARE
                                        : G_TRIANGLE;
    int r = (int)(fy(d) / 2) + 1;
    for (int j = (int)Y - r; j <= (int)Y + r; j++)
        for (int i = (int)X - r; i <= (int)X + r; i++) {
            float px = i + 0.5f, py = j + 0.5f, dist;
            if (kind == K_CROSS)
                dist = fminf(seg_dist(px, py, X - s, Y - s, X + s, Y + s),
                             seg_dist(px, py, X - s, Y + s, X + s, Y - s));
            else if (kind == K_CIRCLE)
                dist = fabsf(sqrtf((px - X) * (px - X) + (py - Y) * (py - Y)) - s * 1.02f);
            else if (kind == K_SQUARE) {
                float q = s * 0.86f, ax = fabsf(px - X) - q, ay = fabsf(py - Y) - q;
                float out = sqrtf(fmaxf(ax, 0) * fmaxf(ax, 0) + fmaxf(ay, 0) * fmaxf(ay, 0));
                dist = fabsf(out + fminf(fmaxf(ax, ay), 0));
            } else {
                float t = s * 1.12f, top = Y - t * 0.95f, base = Y + t * 0.62f;
                dist = fminf(fminf(seg_dist(px, py, X, top, X - t, base),
                                   seg_dist(px, py, X - t, base, X + t, base)),
                             seg_dist(px, py, X + t, base, X, top));
            }
            float cover = w - dist + 0.5f;
            if (cover > 0)
                plot(i, j, color, cover > 1 ? 1 : cover);
        }
}
static uint32_t codepoint(const unsigned char **p) {
    uint32_t c = *(*p)++;
    int n = c < 128 ? 0 : c < 224 ? 1 : c < 240 ? 2 : 3;
    if (!n)
        return c;
    c &= (1U << (6 - n)) - 1;
    while (n-- && **p)
        c = (c << 6) | (*(*p)++ & 63);
    return c;
}
/* ---------- emoji inside text: ORBIT's own pictures (emoji.c), cached per pixel size ---------- */
#define EMOJI_CACHE 48
static struct {
    int idx, side;
    uint32_t age, *px;
} emoji_cache[EMOJI_CACHE];
static uint32_t emoji_age;
static int emoji_side(int px) {
    return px * 23 / 20;
}
static const uint32_t *emoji_bitmap(int idx, int side) {
    int slot = 0;
    for (int i = 0; i < EMOJI_CACHE; i++) {
        if (emoji_cache[i].px && emoji_cache[i].idx == idx && emoji_cache[i].side == side) {
            emoji_cache[i].age = ++emoji_age;
            return emoji_cache[i].px;
        }
        if (!emoji_cache[i].px || (emoji_cache[slot].px && emoji_cache[i].age < emoji_cache[slot].age))
            slot = i;
    }
    if (side < 4 || side > 256)
        return NULL;
    free(emoji_cache[slot].px);
    emoji_cache[slot].px = malloc((size_t)side * side * 4);
    if (!emoji_cache[slot].px)
        return NULL;
    emoji_render(idx, side, emoji_cache[slot].px);
    emoji_cache[slot].idx = idx;
    emoji_cache[slot].side = side;
    emoji_cache[slot].age = ++emoji_age;
    return emoji_cache[slot].px;
}
static void emoji_free_all(void) {
    for (int i = 0; i < EMOJI_CACHE; i++)
        free(emoji_cache[i].px);
    memset(emoji_cache, 0, sizeof(emoji_cache));
}
/* advance of one code point in pixels: emoji pictures, zero for modifiers, -1 if not drawable */
static int cp_advance(uint32_t cp, int px) {
    if (emoji_is_modifier(cp))
        return 0;
    if (emoji_find(cp) >= 0)
        return emoji_side(px) + px / 8;
    Glyph *g = glyph(cp, px);
    return g ? g->advance : -1;
}
static void emoji_draw(int idx, int pen, int baseline, int px, int limit) {
    int side = emoji_side(px);
    const uint32_t *bm = emoji_bitmap(idx, side);
    if (!bm)
        return;
    int x0 = pen + px / 16, y0 = baseline - side * 17 / 20;
    for (int j = 0; j < side; j++) {
        int dy = y0 + j;
        if (dy < 0 || dy >= sh)
            continue;
        uint32_t *row = screen + dy * sw;
        for (int i = 0; i < side; i++) {
            int dx = x0 + i;
            uint32_t p = bm[j * side + i];
            unsigned a = p >> 24;
            if (a && dx >= 0 && dx < sw && dx < limit)
                row[dx] = a == 255 ? (p & 0xffffffu) : blend(row[dx], p & 0xffffffu, a);
        }
    }
}
static int text_raw(int x, int y, int size, uint32_t color, const char *str, int maxw) {
    if (!face)
        return x;
    int pen = sx(x), limit = sx(x + maxw), baseline = sy(y + size);
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        uint32_t cp = codepoint(&p);
        if (emoji_is_modifier(cp))
            continue;
        int em = emoji_find(cp);
        if (em >= 0) {
            int adv = cp_advance(cp, sy(size));
            if (pen + adv > limit)
                break;
            emoji_draw(em, pen, baseline, sy(size), limit);
            pen += adv;
            continue;
        }
        Glyph *g = glyph(cp, sy(size));
        if (!g)
            continue;
        if (pen + g->advance > limit)
            break;
        for (int j = 0; j < g->height; j++) {
            int dy = baseline - g->top + j;
            if (dy < 0 || dy >= sh)
                continue;
            const unsigned char *src = g->pixels + j * g->width;
            uint32_t *row = screen + dy * sw;
            for (int i = 0; i < g->width; i++) {
                int dx = pen + g->left + i;
                unsigned alpha = src[i];
                if (alpha && dx >= 0 && dx < sw && dx < limit)
                    row[dx] = alpha == 255 ? color : blend(row[dx], color, alpha);
            }
        }
        pen += g->advance;
    }
    return pen * 1280 / sw;
}
static int measured(const char *str, int size) {
    const unsigned char *p = (const unsigned char *)str;
    int width = 0;
    while (*p) {
        int adv = cp_advance(codepoint(&p), sy(size));
        if (adv > 0)
            width += adv;
    }
    return width;
}
static int text_width(const char *str, int size) {
    return measured(str, size) * 1280 / (sw ? sw : 1280);
}
static int text(int x, int y, int size, uint32_t color, const char *str, int maxw) {
    if (!face || maxw <= 0)
        return x;
    if (measured(str, size) <= sx(maxw))
        return text_raw(x, y, size, color, str, maxw);
    char clipped[1100];
    const unsigned char *p = (const unsigned char *)str;
    int pixels = 0, n = 0, limit = sx(maxw) - measured("...", size);
    while (*p && n < 1090) {
        const unsigned char *start = p;
        int adv = cp_advance(codepoint(&p), sy(size));
        if (adv < 0)
            continue;
        if (pixels + adv > limit)
            break;
        while (start < p)
            clipped[n++] = (char)*start++;
        pixels += adv;
    }
    memcpy(clipped + n, "...", 4);
    return text_raw(x, y, size, color, clipped, maxw);
}
/* Faux bold for headings: one font file, drawn twice with a subpixel-sized offset. */
static int bold(int x, int y, int size, uint32_t color, const char *str, int maxw) {
    text(x, y, size, color, str, maxw);
    if (sw >= 1000)
        text_raw(x + 1, y, size, color, str, maxw - 1);
    return x + text_width(str, size) + 1;
}
static void center(int cx, int y, int size, uint32_t color, const char *str, int heading) {
    int w = text_width(str, size);
    if (heading)
        bold(cx - w / 2, y, size, color, str, w + 4);
    else
        text(cx - w / 2, y, size, color, str, w + 4);
}
static int split_lines(const char *str, int size, int width, char lines[64][1024]) {
    const unsigned char *p = (const unsigned char *)str;
    int row = 0;
    for (; row < 64 && *p; row++) {
        while (*p == ' ')
            p++;
        int n = 0, pixels = 0, last_space = -1;
        const unsigned char *next_word = NULL;
        while (*p && n < 1000) {
            const unsigned char *start = p;
            uint32_t cp = codepoint(&p);
            int adv = cp_advance(cp, sy(size));
            if (adv < 0)
                continue;
            if (pixels + adv > sx(width) && n) {
                if (last_space >= 0) {
                    n = last_space;
                    p = next_word;
                } else
                    p = start;
                break;
            }
            if (cp == ' ') {
                last_space = n;
                next_word = p;
            }
            while (start < p)
                lines[row][n++] = (char)*start++;
            pixels += adv;
        }
        lines[row][n] = 0;
    }
    return row;
}
static int wrapped(int x, int y, int size, uint32_t color, const char *str, int width, int lines,
                   int gap) {
    int count = split_lines(str, size, width, text_rows);
    int i = 0;
    for (; i < count && i < lines; i++) {
        if (i == lines - 1 && count > lines) {
            size_t n = strlen(text_rows[i]);
            if (n + 4 < sizeof(text_rows[i]))
                memcpy(text_rows[i] + n, " ...", 5);
        }
        text(x, y + i * (size + gap), size, color, text_rows[i], width);
    }
    return y + i * (size + gap); /* bottom edge of the text */
}
/* Small labelled key: L1, R1, START, SELECT, R3. Returns right edge. */
static int key_cap(int x, int y, const char *label) {
    int w = text_width(label, 13) + 16;
    rrect(x, y + 2, w, 24, 7, 0x2a2f37);
    rrect(x + 1, y + 3, w - 2, 22, 6, 0x1a1d23);
    text(x + 8, y + 5, 13, C_TEXT, label, w);
    return x + w;
}
static void dpad(int x, int y, int vertical) {
    int c = 0x2a2f37;
    rrect(x + 8, y + 1, 10, 26, 3, c);
    rrect(x - 1, y + 9, 28, 10, 3, c);
    if (vertical) {
        rrect(x + 10, y + 3, 6, 8, 2, C_TEXT);
        rrect(x + 10, y + 17, 6, 8, 2, C_TEXT);
    } else {
        rrect(x + 1, y + 11, 8, 6, 2, C_TEXT);
        rrect(x + 17, y + 11, 8, 6, 2, C_TEXT);
    }
}
/* Hint: glyph followed by its label. kind < 0 = labelled key text. Returns next x. */
static int hint(int x, int y, int kind, const char *key, const char *label) {
    int end;
    if (kind >= 0) {
        face_button((float)x + 14, (float)y + 14, 28, kind);
        end = x + 28;
    } else if (kind == -2) {
        dpad(x, y, 1);
        end = x + 28;
    } else if (kind == -3) {
        dpad(x, y, 0);
        end = x + 28;
    } else
        end = key_cap(x, y, key);
    int after = text(end + 9, y + 5, 17, C_MUTED, label, 300);
    return after + 30;
}
static void footer_bar(void) {
    box(0, 664, 1280, 56, C_BG);
    box(48, 664, 1184, 1, C_LINE);
}
/* ---------- brand mark: planet with orbit ---------- */
static void orbit_mark(float cx, float cy, float r) {
    float X = fx(cx), Y = fy(cy), R = fy(r);
    int ext = (int)(R * 1.7f) + 2;
    for (int j = (int)Y - ext; j <= (int)Y + ext; j++)
        for (int i = (int)X - ext; i <= (int)X + ext; i++) {
            float px = i + 0.5f - X, py = j + 0.5f - Y;
            /* rotated ellipse ring */
            float c = 0.866f, s = -0.5f;
            float ex = (px * c - py * s) / (R * 1.55f), ey = (px * s + py * c) / (R * 0.48f);
            float e = sqrtf(ex * ex + ey * ey);
            float ring = (1.0f - fabsf(e - 1.0f) * R * 0.48f / (R * 0.09f + 0.8f));
            float d = sqrtf(px * px + py * py);
            float planet = R * 0.62f - d + 0.5f;
            int front = (px * s + py * c) > 0;
            if (!front && ring > 0)
                plot(i, j, C_ACCENT, ring > 1 ? 1 : ring);
            if (planet > 0) {
                float shade = 0.75f + 0.25f * (-px - py) / (R * 0.9f);
                uint32_t col = blend(0x2c63d8, 0x8fb5ff, (unsigned)(fminf(fmaxf(shade, 0), 1) * 255));
                plot(i, j, col, planet > 1 ? 1 : planet);
            }
            if (front && ring > 0 && d > R * 0.5f)
                plot(i, j, C_ACCENT, ring > 1 ? 1 : ring);
        }
}
static void chip(int x, int y, uint32_t dot, const char *label) {
    int w = text_width(label, 14) + 34;
    rrect(x, y, w, 28, 14, C_SURF2);
    disc((float)x + 14, (float)y + 14, 4, dot);
    text(x + 25, y + 5, 14, C_MUTED, label, w);
}
static void clock_text(char out[24], uint32_t t) {
    out[0] = 0;
    if (!t)
        return;
    long local = (long)t + timezone_minutes * 60L;
    long today = ((long)time(NULL) + timezone_minutes * 60L) / 86400;
    int minute = (int)(local / 60 % 60), hour = (int)(local / 3600 % 24);
    if (local / 86400 == today)
        snprintf(out, 24, "%02d:%02d", hour, minute);
    else if (local / 86400 == today - 1)
        snprintf(out, 24, "%s %02d:%02d", T(T_YESTERDAY), hour, minute);
    else {
        time_t tt = (time_t)local;
        struct tm tmv;
        gmtime_r(&tt, &tmv);
        snprintf(out, 24, "%02d.%02d. %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1, hour, minute);
    }
}
/* ---------- toast ---------- */
static uint32_t toast_id;
static uint64_t toast_at;
int ui_toast_visible(const UiState *s, uint64_t now) {
    if (s->status_id != toast_id) {
        toast_id = s->status_id;
        toast_at = now;
    }
    return s->status[0] && s->status_id && now - toast_at < 4500;
}
static void toast(const UiState *s, const UiLocal *l) {
    if (!ui_toast_visible(s, l->now))
        return;
    int w = text_width(s->status, 17) + 48;
    if (w > 900)
        w = 900;
    int x = 802 - w / 2, y = 122;
    rrect(x - 1, y - 1, w + 2, 46, 23, C_LINE);
    rrect(x, y, w, 44, 22, 0x252a33);
    text(x + 24, y + 11, 17, C_TEXT, s->status, w - 44);
}
/* ---------- screens ---------- */
static const char *net_label(int net) {
    return T(net == NET_INTERNET ? T_NET_INTERNET
             : net == NET_HOME   ? T_NET_HOME
             : net == NET_OFFLINE ? T_NET_OFFLINE
                                  : T_NET_CHECKING);
}
static uint32_t net_color(int net) {
    return net == NET_INTERNET ? C_OK : net == NET_HOME ? C_WARN : net == NET_OFFLINE ? C_BAD : C_DIM;
}
static void mic_icon(int x, int y, uint32_t c, int muted) {
    rrect(x + 4, y, 8, 13, 4, c);
    rrect(x + 7, y + 15, 2, 4, 1, c);
    rrect(x + 3, y + 19, 10, 2, 1, c);
    if (muted)
        for (int i = 0; i < 18; i++)
            rrect(x + i, y + 1 + i, 2, 2, 1, C_BAD);
}
/* ---------- Orbit 1.0 screens (included by ui.c) ---------- */
static const uint32_t person_colors[12] = {0x4f8cff, 0x39d98a, 0xf4b445, 0xff6f91, 0xa98bff, 0x2fc6d6,
                                           0xff8a4c, 0x7bd35a, 0xe46cff, 0x5ad1ff, 0xffd166, 0xc2c7cf};
static const char *const symbols[12] = {"", "★", "♥", "♪", "☺", "✓", "GG", "XD", "#1", "PS", "OK", "77"};
static const char *const reactions[ORBIT_REACTIONS] = {"❤", "😂", "★", "✓", "☺", "♪"};
const char *ui_reaction(int i) {
    return i >= 0 && i < ORBIT_REACTIONS ? reactions[i] : "";
}
void ui_set_theme(int theme) {
    current_theme = theme;
    perf_mode = theme == THEME_PERF;
    /* PS3-friendly themes: only flat colours change, no extra effects */
    switch (theme) {
    case THEME_BLUE:
        C_BG = 0x0a1020, C_SIDE = 0x0d1428, C_SURF = 0x132040, C_SURF2 = 0x1a2b52, C_LINE = 0x22355f;
        C_ACCENT = 0x3d9bff, C_ACCENT2 = 0x7a6bff;
        break;
    case THEME_PURPLE:
        C_BG = 0x110d18, C_SIDE = 0x161020, C_SURF = 0x1e1630, C_SURF2 = 0x281d40, C_LINE = 0x33264f;
        C_ACCENT = 0xa66bff, C_ACCENT2 = 0x4f8cff;
        break;
    case THEME_MINIMAL:
        C_BG = 0x0b0b0c, C_SIDE = 0x0b0b0c, C_SURF = 0x161618, C_SURF2 = 0x1f1f22, C_LINE = 0x2a2a2e;
        C_ACCENT = 0xe8e8ea, C_ACCENT2 = 0x9a9aa0;
        break;
    case THEME_GLASS:
        C_BG = 0x070b14, C_SIDE = 0x0a0f1b, C_SURF = 0x121a2b, C_SURF2 = 0x1b2640, C_LINE = 0x22304d;
        C_ACCENT = 0x4f8cff, C_ACCENT2 = 0x8b6bff;
        break;
    case THEME_SPACE:
        C_BG = 0x04050a, C_SIDE = 0x070912, C_SURF = 0x0f1220, C_SURF2 = 0x181d33, C_LINE = 0x1f2540;
        C_ACCENT = 0x5b8dff, C_ACCENT2 = 0xb56bff;
        break;
    case THEME_CRYSTAL:
        C_BG = 0x0b1418, C_SIDE = 0x0e1a1f, C_SURF = 0x14242b, C_SURF2 = 0x1c323b, C_LINE = 0x25414b;
        C_ACCENT = 0x55d6ff, C_ACCENT2 = 0x7fa8ff;
        break;
    case THEME_PERF:
        C_BG = 0x0d0f13, C_SIDE = 0x111318, C_SURF = 0x171a20, C_SURF2 = 0x1f232a, C_LINE = 0x262a32;
        C_ACCENT = 0x4f8cff, C_ACCENT2 = 0xa66bff;
        break;
    default:
        C_BG = 0x0d0f13, C_SIDE = 0x111318, C_SURF = 0x171a20, C_SURF2 = 0x1f232a, C_LINE = 0x262a32;
        C_ACCENT = 0x4f8cff, C_ACCENT2 = 0xa66bff;
        break;
    }
    bg_rev = -1;
}
/* avatar: symbol or initial on a coloured disc */
static void person(int x, int y, int d, const char *name, int color, int symbol, const char *pic, const char *anim) {
    const OrbitImage *img = pic && pic[0] ? anim_get(anim) : NULL;
    if (!img)
        img = pic_get(pic);
    if (img) { /* 1.2: profile picture (animated when allowed) */
        blit(img, x, y, d, d, d / 2, 0, 255);
        return;
    }
    uint32_t c = person_colors[(color < 0 ? 0 : color) % 12];
    disc((float)x + d / 2.0f, (float)y + d / 2.0f, d / 2.0f, blend(C_BG, c, 70));
    char initial[8] = "?";
    if (symbol > 0 && symbol < 12)
        snprintf(initial, sizeof(initial), "%s", symbols[symbol]);
    else if (name && name[0]) {
        const unsigned char *p = (const unsigned char *)name, *start = p;
        codepoint(&p);
        size_t n = (size_t)(p - start);
        if (n <= 4) {
            memcpy(initial, start, n);
            initial[n] = 0;
            if (n == 1 && initial[0] >= 'a' && initial[0] <= 'z')
                initial[0] -= 32;
        }
    }
    int size = d * (strlen(initial) > 3 && symbol >= 6 ? 34 : 45) / 100;
    int w = text_width(initial, size);
    text(x + (d - w) / 2, y + (d - size) / 2 - size / 8, size, c, initial, d);
}
static uint32_t status_color(int st) {
    return st == ST_ONLINE ? C_OK : st == ST_AWAY ? C_WARN : st == ST_BUSY ? C_BAD : C_DIM;
}
static const char *status_label(int st) {
    static const int keys[5] = {T_ST_ONLINE, T_ST_AWAY, T_ST_BUSY, T_ST_INVISIBLE, T_ST_OFFLINE};
    return T(keys[st < 0 || st > 4 ? 4 : st]);
}
static void presence(int x, int y, int st) {
    disc((float)x, (float)y, 7, C_SIDE);
    disc((float)x, (float)y, 5, status_color(st));
}
static void badge(int x, int y, int n, uint32_t c) {
    if (n <= 0)
        return;
    char t[8];
    snprintf(t, sizeof(t), n > 99 ? "99+" : "%d", n);
    int w = text_width(t, 12) + 12;
    if (w < 20)
        w = 20;
    rrect(x - w / 2, y - 10, w, 20, 10, c);
    text(x - w / 2 + (w - text_width(t, 12)) / 2, y - 8, 12, 0xffffff, t, w);
}
/* ---------- tab icons ---------- */
static void tab_icon(int tab, int cx, int cy, uint32_t c) {
    switch (tab) {
    case TAB_HOME:
        for (int i = 0; i < 9; i++)
            rrect(cx - i - 1, cy - 9 + i, 2 * i + 2, 2, 1, c);
        rrect(cx - 8, cy, 16, 10, 2, c);
        rrect(cx - 2, cy + 4, 4, 6, 1, C_SIDE);
        break;
    case TAB_FRIENDS:
        disc((float)cx - 5, (float)cy - 4, 4.5f, c);
        rrect(cx - 12, cy + 2, 14, 8, 4, c);
        disc((float)cx + 6, (float)cy - 5, 4, blend(C_SIDE, c, 170));
        rrect(cx + 1, cy + 1, 12, 8, 4, blend(C_SIDE, c, 170));
        break;
    case TAB_MESSAGES:
        rrect(cx - 11, cy - 9, 22, 15, 5, c);
        for (int i = 0; i < 5; i++)
            rrect(cx - 7 + i, cy + 5 + i, 2, 2, 1, c);
        rrect(cx - 6, cy - 4, 12, 2, 1, C_SIDE);
        rrect(cx - 6, cy, 8, 2, 1, C_SIDE);
        break;
    case TAB_DISCOVER: /* compass */
        disc((float)cx, (float)cy, 10, c);
        disc((float)cx, (float)cy, 8, C_SIDE);
        for (int i = 0; i < 7; i++) {
            disc(cx - 3.5f + i * 1.2f, cy + 3.5f - i * 1.2f, 2.2f - i * 0.15f, c);
        }
        disc((float)cx, (float)cy, 1.6f, C_SIDE);
        break;
    case TAB_GAMES: /* game pad */
        rrect(cx - 12, cy - 6, 24, 13, 6, c);
        rrect(cx - 12, cy - 2, 8, 12, 4, c);
        rrect(cx + 4, cy - 2, 8, 12, 4, c);
        rrect(cx - 9, cy - 1, 6, 2, 1, C_SIDE);
        rrect(cx - 7, cy - 3, 2, 6, 1, C_SIDE);
        disc(cx + 6.0f, cy - 1.0f, 1.4f, C_SIDE);
        disc(cx + 9.0f, cy + 1.5f, 1.4f, C_SIDE);
        break;
    case TAB_SERVERS:
        rrect(cx - 10, cy - 10, 9, 9, 3, c);
        rrect(cx + 1, cy - 10, 9, 9, 3, c);
        rrect(cx - 10, cy + 1, 9, 9, 3, c);
        rrect(cx + 1, cy + 1, 9, 9, 3, blend(C_SIDE, c, 150));
        break;
    default:
        disc((float)cx, (float)cy, 10, c);
        for (int k = 0; k < 8; k++) {
            float ang = (float)k * 0.785f;
            disc(cx + 10.5f * cosf(ang), cy + 10.5f * sinf(ang), 3, c);
        }
        disc((float)cx, (float)cy, 4.5f, C_SIDE);
        break;
    }
}
static void speaker_icon(int x, int y, uint32_t c) {
    rrect(x, y + 5, 5, 8, 1, c);
    for (int i = 0; i < 6; i++)
        rrect(x + 4 + i, y + 5 - i, 1, 8 + 2 * i, 0, c);
    for (int i = 0; i < 3; i++)
        rrect(x + 13 + i * 3, y + 6 - i * 2, 2, 6 + i * 4, 1, blend(C_SIDE, c, 200 - i * 50));
}
static void hash_icon(int x, int y, uint32_t c) {
    text(x, y - 2, 18, c, "#", 20);
}
/* ---------- sidebar rows (shared with input handling) ---------- */
static int cmp_ch(const UiGroup *g, const UiChannel *a, const UiChannel *b) {
    int pa = -1, pb = -1;
    for (int i = 0; i < g->ncat; i++) {
        if (g->cat[i].i == a->cat)
            pa = g->cat[i].pos;
        if (g->cat[i].i == b->cat)
            pb = g->cat[i].pos;
    }
    if (pa != pb)
        return pa < pb ? -1 : 1;
    if (a->type != b->type && a->cat < 0)
        return a->type < b->type ? -1 : 1;
    return a->pos < b->pos ? -1 : a->pos > b->pos;
}
static int add_row(UiRow *rows, int n, int cap, int kind, int a, int b, int c) {
    if (n < cap) {
        rows[n].kind = kind;
        rows[n].a = a;
        rows[n].b = b;
        rows[n].c = c;
    }
    return n + 1;
}
int ui_rows(const UiState *s, int tab, UiRow *rows, int cap) {
    int n = 0;
    switch (tab) {
    case TAB_HOME:
        n = add_row(rows, n, cap, ROW_ACTION, 0, 0, 0);
        for (int i = s->note_count - 1; i >= 0; i--)
            n = add_row(rows, n, cap, ROW_NOTE, i, 0, 0);
        break;
    case TAB_FRIENDS:
        n = add_row(rows, n, cap, ROW_ACTION, 1, 0, 0);
        n = add_row(rows, n, cap, ROW_ACTION, 2, 0, 0);
        for (int pass = 0; pass < 3; pass++) {
            /* requests first, then online friends, then everybody else */
            int header = 0;
            for (int i = 0; i < s->friend_count; i++) {
                const UiFriend *f = &s->friends[i];
                int want = pass == 0   ? (f->state == FR_INCOMING || f->state == FR_OUTGOING)
                           : pass == 1 ? (f->state == FR_FRIENDS && f->online)
                                       : (f->state == FR_FRIENDS && !f->online);
                if (!want)
                    continue;
                if (!header)
                    n = add_row(rows, n, cap, ROW_HEADER, pass, 0, 0), header = 1;
                n = add_row(rows, n, cap, ROW_FRIEND, f->i, 0, 0);
            }
        }
        break;
    case TAB_MESSAGES:
        n = add_row(rows, n, cap, ROW_ACTION, 3, 0, 0);
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < s->group_count; i++) {
                const UiGroup *g = &s->groups[i];
                if (g->kind == GK_DM && (pass == 0) == (g->call > 0))
                    n = add_row(rows, n, cap, ROW_DM, g->link, g->id, 0);
            }
        break;
    case TAB_SERVERS:
        for (int i = 0; i < s->group_count; i++) {
            const UiGroup *g = &s->groups[i];
            if (g->kind != GK_SERVER)
                continue;
            n = add_row(rows, n, cap, ROW_SERVER, g->link, g->id, 0);
            if (i != s->selected)
                continue;
            int order[ORBIT_CHANNELS];
            for (int k = 0; k < g->nch; k++)
                order[k] = k;
            for (int x = 1; x < g->nch; x++)
                for (int y = x; y > 0 && cmp_ch(g, &g->ch[order[y]], &g->ch[order[y - 1]]) < 0; y--) {
                    int t = order[y];
                    order[y] = order[y - 1];
                    order[y - 1] = t;
                }
            int cat = -2;
            for (int k = 0; k < g->nch; k++) {
                const UiChannel *h = &g->ch[order[k]];
                if (h->cat != cat && h->cat >= 0)
                    n = add_row(rows, n, cap, ROW_CATEGORY, g->link, g->id, h->cat);
                cat = h->cat;
                n = add_row(rows, n, cap, ROW_CHANNEL, g->link, g->id, h->id);
            }
        }
        n = add_row(rows, n, cap, ROW_ACTION, 4, 0, 0);
        n = add_row(rows, n, cap, ROW_ACTION, 5, 0, 0);
        n = add_row(rows, n, cap, ROW_ACTION, 6, 0, 0);
        break;
    case TAB_DISCOVER:
    case TAB_GAMES:
        break; /* spotlight screens have their own focus model */
    default:
        for (int i = 0; i < 8; i++)
            n = add_row(rows, n, cap, ROW_SETTING, i, 0, 0);
        break;
    }
    return n < cap ? n : cap;
}
static const UiGroup *group_of(const UiState *s, int link, int id) {
    for (int i = 0; i < s->group_count; i++)
        if (s->groups[i].link == link && s->groups[i].id == id)
            return &s->groups[i];
    return NULL;
}
static const UiChannel *channel_of(const UiGroup *g, int ch) {
    for (int i = 0; g && i < g->nch; i++)
        if (g->ch[i].id == ch)
            return &g->ch[i];
    return NULL;
}
static const UiFriend *friend_of(const UiState *s, int i) {
    for (int k = 0; k < s->friend_count; k++)
        if (s->friends[k].i == i)
            return &s->friends[k];
    return NULL;
}
static int row_height(int kind) {
    switch (kind) {
    case ROW_SERVER:
    case ROW_DM:
    case ROW_FRIEND:
    case ROW_NOTE:
        return 52;
    case ROW_CHANNEL:
        return 36;
    case ROW_CATEGORY:
    case ROW_HEADER:
        return 28;
    default:
        return 46;
    }
}
static const char *setting_name(int i) {
    static const int keys[8] = {T_SET_PROFILE, T_SET_LANGUAGE, T_SET_LOOK,  T_SET_NOTIFY,
                                T_SET_AUDIO,   T_SET_NETWORK,  T_SET_PRIVACY, T_SET_ABOUT};
    return T(keys[i & 7]);
}
static const char *action_name(int id) {
    static const int keys[7] = {T_HOME_OVERVIEW, T_ADD_FRIEND, T_MY_FRIEND_CODE, T_NEW_GROUP_DM,
                                T_CREATE_SERVER, T_JOIN_SERVER, T_PUBLIC_SERVERS};
    return T(keys[id < 0 || id > 6 ? 0 : id]);
}
static void server_icon(int x, int y, int d, const UiGroup *g) {
    if (g->pic[0]) { /* 1.2: server icon picture (animated when allowed) */
        const OrbitImage *img = anim_get(g->anim);
        if (!img)
            img = pic_get(g->pic);
        if (img) {
            blit(img, x, y, d, d, d / 3, 0, 255);
            return;
        }
    }
    uint32_t c = person_colors[g->color % 12];
    rrect(x, y, d, d, d / 3, blend(C_BG, c, 80));
    char t[8] = "";
    if (g->icon > 0 && g->icon < 12)
        snprintf(t, sizeof(t), "%s", symbols[g->icon]);
    else {
        const unsigned char *p = (const unsigned char *)g->name, *st = p;
        codepoint(&p);
        size_t n = (size_t)(p - st);
        if (n && n < sizeof(t)) {
            memcpy(t, st, n);
            t[n] = 0;
            if (n == 1 && t[0] >= 'a' && t[0] <= 'z')
                t[0] -= 32;
        }
    }
    int size = d * 42 / 100, w = text_width(t, size);
    bold(x + (d - w) / 2, y + (d - size) / 2 - size / 8, size, c, t, d);
}
static void draw_row(const UiState *s, const UiRow *r, int y, int active, int focus) {
    int h = row_height(r->kind);
    if (active && r->kind != ROW_HEADER && r->kind != ROW_CATEGORY) {
        rrect(40, y, 284, h - 4, 12, focus ? C_SURF2 : blend(C_SIDE, C_SURF2, 140));
        if (focus)
            rrect(40, y + h / 2 - 13, 4, 22, 2, C_ACCENT);
    }
    char sub[200];
    switch (r->kind) {
    case ROW_HEADER: {
        static const int keys[3] = {T_REQUESTS, T_ONLINE_FRIENDS, T_ALL_FRIENDS};
        text(56, y + 8, 13, C_DIM, T(keys[r->a % 3]), 260);
        break;
    }
    case ROW_CATEGORY: {
        const UiGroup *g = group_of(s, r->a, r->b);
        for (int i = 0; g && i < g->ncat; i++)
            if (g->cat[i].i == r->c)
                text(64, y + 8, 13, C_DIM, g->cat[i].name, 240);
        break;
    }
    case ROW_ACTION:
    case ROW_SETTING: {
        const char *label = r->kind == ROW_ACTION ? action_name(r->a) : setting_name(r->a);
        uint32_t c = active ? C_TEXT : 0xc4c9d0;
        rrect(56, y + 10, 22, 22, 7, blend(C_SIDE, C_ACCENT, active ? 120 : 60));
        text(61, y + 9, 17, C_TEXT, r->kind == ROW_ACTION && r->a >= 1 && r->a != 2 && r->a != 6 ? "+" : "»", 20);
        text(90, y + 11, 17, c, label, 220);
        if (r->kind == ROW_ACTION && r->a == 1 && s->requests)
            badge(300, y + 22, s->requests, C_BAD);
        break;
    }
    case ROW_NOTE: {
        const UiNote *n = &s->notes[r->a];
        static const uint32_t nc[N_COUNT] = {0xa66bff, 0x39d98a, 0x4f8cff, 0x9aa1ab,
                                             0xf4b445, 0x39d98a, 0xff6f91, 0xff5d62};
        disc(64, (float)y + 24, 6, nc[n->type % N_COUNT]);
        char t[24];
        clock_text(t, n->time);
        text(80, y + 6, 13, C_DIM, t, 200);
        text(80, y + 25, 15, active ? C_TEXT : 0xc4c9d0, n->text, 230);
        break;
    }
    case ROW_FRIEND: {
        const UiFriend *f = friend_of(s, r->a);
        if (!f)
            break;
        person(52, y + 6, 38, f->name, f->color, f->avatar, f->pic, f->anim);
        presence(84, y + 38, f->state == FR_FRIENDS ? f->status : ST_OFFLINE);
        text(102, y + 6, 17, active ? C_TEXT : 0xd6dae0, f->name, 200);
        if (f->state == FR_INCOMING)
            snprintf(sub, sizeof(sub), "%s", T(T_REQUEST_IN));
        else if (f->state == FR_OUTGOING)
            snprintf(sub, sizeof(sub), "%s", T(T_REQUEST_OUT));
        else if (f->playing) {
            if (f->playing_name[0])
                TF(sub, sizeof(sub), T_PLAYING_NOW, f->playing_name);
            else
                snprintf(sub, sizeof(sub), "%s", T(T_PLAYING_SOME));
        } else
            snprintf(sub, sizeof(sub), "%s", f->custom[0] && f->online ? f->custom : status_label(f->status));
        text(102, y + 28, 13, f->state == FR_INCOMING ? C_ACCENT2 : f->playing ? C_OK : C_DIM, sub, 200);
        break;
    }
    case ROW_DM:
    case ROW_SERVER: {
        const UiGroup *g = group_of(s, r->a, r->b);
        if (!g)
            break;
        if (r->kind == ROW_DM) {
            const UiPerson *p = g->ndm ? &g->dm[0] : NULL;
            if (g->ndm > 1) {
                person(52, y + 4, 30, g->dm[1].name, g->dm[1].color, g->dm[1].avatar, g->dm[1].pic, g->dm[1].anim);
                person(62, y + 14, 30, p->name, p->color, p->avatar, p->pic, p->anim);
            } else
                person(52, y + 6, 38, p ? p->name : g->name, p ? p->color : 0, p ? p->avatar : 0, p ? p->pic : NULL, p ? p->anim : NULL);
            if (g->ndm == 1)
                presence(84, y + 38, p->status);
        } else
            server_icon(52, y + 6, 38, g);
        uint32_t tc = g->state == LINK_ONLINE || g->own ? (active ? C_TEXT : 0xd6dae0) : C_DIM;
        text(102, y + 6, 17, tc, g->name, g->call ? 150 : 190);
        if (r->kind == ROW_DM)
            snprintf(sub, sizeof(sub), "%s", g->ndm > 1 ? g->people : status_label(g->ndm ? g->dm[0].status : 4));
        else if (g->own)
            TF(sub, sizeof(sub), T_SERVER_SUB_OWN, g->online, g->members);
        else
            TF(sub, sizeof(sub), g->state == LINK_ONLINE ? T_SERVER_SUB_ON : T_SERVER_SUB_OFF,
               g->host[0] ? g->host : "?");
        text(102, y + 28, 13, C_DIM, sub, 196);
        if (g->call) {
            char n[16];
            snprintf(n, sizeof(n), "%d", g->call);
            int w = text_width(n, 13) + 36;
            rrect(292 - w, y + 7, w, 22, 11, 0x163a28);
            mic_icon(300 - w, y + 8, C_OK, 0);
            text(318 - w, y + 9, 13, C_OK, n, w);
        }
        if (g->unread && !g->muted_chat)
            disc(306, (float)y + 26, 5.5f, C_ACCENT);
        else if (g->muted_chat)
            text(298, y + 16, 13, C_DIM, "·", 14);
        break;
    }
    case ROW_CHANNEL: {
        const UiGroup *g = group_of(s, r->a, r->b);
        const UiChannel *h = channel_of(g, r->c);
        if (!h)
            break;
        uint32_t c = active ? C_TEXT : 0xaab0b8;
        if (h->type == CH_VOICE)
            speaker_icon(66, y + 9, c);
        else
            hash_icon(68, y + 7, c);
        text(94, y + 7, 16, c, h->name, h->call ? 160 : 210);
        if (h->call) {
            char n[16];
            snprintf(n, sizeof(n), "%d", h->call);
            text(300, y + 8, 14, C_OK, n, 30);
        }
        if (h->view)
            text(286, y + 7, 13, C_DIM, "•", 14);
        break;
    }
    }
}
static int call_card(const UiState *s, int y);
static void sidebar(const UiState *s, const UiLocal *l) {
    box(0, 0, 340, 664, C_SIDE);
    box(340, 0, 1, 664, C_LINE);
    static const int titles[TAB_COUNT] = {T_TAB_HOME, T_TAB_DISCOVER, T_TAB_FRIENDS, T_TAB_GAMES,
                                          T_TAB_SERVERS, T_TAB_MESSAGES, T_TAB_SETTINGS};
    int top;
    if (unified_ui) { /* the top bar above shows logo, tabs and badges */
        top = 22;
        text(56, top + 4, 13, C_DIM, T(titles[l->tab]), 120);
        chip(186, top, net_color(s->net), net_label(s->net));
        top += 36;
    } else {
        orbit_mark(66, 44, 12);
        bold(88, 28, 24, C_TEXT, "Orbit", 120);
        chip(186, 30, net_color(s->net), net_label(s->net));
        /* main navigation: LEFT / RIGHT */
        int badges[TAB_COUNT] = {0, 0, s->requests, 0, s->unread_server, s->unread_dm, 0};
        for (int t = 0; t < TAB_COUNT; t++) {
            int x = 40 + t * 41, active = t == l->tab;
            if (active) {
                rrect(x, 76, 38, 44, 11, C_SURF2);
                rrect(x + 9, 116, 20, 3, 1, C_ACCENT);
            }
            tab_icon(t, x + 19, 98, active ? C_TEXT : C_DIM);
            if (badges[t])
                badge(x + 34, 82, badges[t], t == TAB_FRIENDS ? C_BAD : C_ACCENT);
        }
        top = 132;
        text(56, top, 13, C_DIM, T(titles[l->tab]), 200);
        top += 22;
    }
    if (s->call_state != CALL_OFF)
        top += call_card(s, top) + 8;
    UiRow rows[96];
    int n = ui_rows(s, l->tab, rows, 96);
    int bottom = 594, cursor = l->side < n ? l->side : n - 1;
    /* scroll so the cursor stays visible */
    int start = 0, used = 0;
    for (int i = cursor; i >= 0; i--) {
        used += row_height(rows[i].kind);
        if (top + used > bottom)
            break;
        start = i;
    }
    int y = top;
    for (int i = start; i < n && y + row_height(rows[i].kind) <= bottom; i++) {
        draw_row(s, &rows[i], y, i == cursor, !l->msg_mode);
        y += row_height(rows[i].kind);
    }
    if (!n)
        text(56, top + 6, 14, C_DIM, T(T_NOTHING_HERE), 260);
    box(40, 600, 284, 1, C_LINE);
    person(44, 610, 40, s->username, s->me.color, s->me.avatar, s->me.pic, s->me.anim);
    presence(78, 644, s->me.status == ST_INVISIBLE ? ST_OFFLINE : s->me.status);
    text(96, 610, 17, C_TEXT, s->username, 226);
    char line[120];
    snprintf(line, sizeof(line), "%s  ·  %s",
             s->call_state != CALL_OFF ? T(T_IN_VOICE) : status_label(s->me.status), s->oid);
    text(96, 633, 13, C_DIM, line, 226);
}
/* Discord-style voice panel: who is in the call, who is talking right now. */
static int call_card(const UiState *s, int y) {
    int shown = s->call_count > 4 ? 4 : s->call_count;
    int h = 64 + (shown ? shown * 30 : 24) + (s->ds_note[0] ? 18 : 0) + 6;
    rrect(40, y, 284, h, 14, 0x14241c);
    uint32_t dot = s->call_state == CALL_ONLINE ? C_OK : C_WARN;
    disc(58, (float)y + 19, 4.5f, dot);
    text(70, y + 10, 13, dot, T(T_VOICE_CAPS), 120);
    const char *state = s->call_state == CALL_ONLINE       ? T(T_CALL_CONNECTED)
                        : s->call_state == CALL_CONNECTING ? T(T_CALL_CONNECTING)
                                                           : T(T_CALL_RETRY);
    int w = text_width(state, 13);
    text(308 - w, y + 10, 13, C_MUTED, state, w + 4);
    bold(56, y + 30, 16, C_TEXT, s->call_name[0] ? s->call_name : "", 250);
    int py = y + 58;
    for (int i = 0; i < shown; i++) {
        const UiCallPerson *p = &s->call_people[i];
        if (p->speaking && !p->local_muted)
            disc(69, (float)py + 12, 14, C_OK);
        disc(69, (float)py + 12, p->speaking ? 11.5f : 12, 0x14241c);
        person(57, py, 24, p->name, 0, 0, NULL, NULL);
        char label[80];
        snprintf(label, sizeof(label), "%s%s", p->name, p->self ? T(T_YOU_SUFFIX) : "");
        text(90, py + 3, 14, p->speaking ? C_TEXT : 0xc4c9d0, label, 170);
        if (p->self)
            mic_icon(292, py + 2, s->call_muted ? C_BAD : C_MUTED, s->call_muted);
        else if (p->local_muted || p->server_muted)
            mic_icon(292, py + 2, C_BAD, 1);
        else if (p->volume != 100) {
            char v[12];
            snprintf(v, sizeof(v), "%d%%", p->volume);
            text(270, py + 4, 12, C_MUTED, v, 50);
        }
        py += 30;
    }
    if (!shown)
        text(56, py, 14, C_MUTED, T(T_CALL_ALMOST), 250), py += 24;
    else if (s->call_count > shown) {
        char more[32];
        TN(more, sizeof(more), T_MORE_N, s->call_count - shown, s->call_count - shown);
        text(240, y + 32, 13, C_MUTED, more, 80);
    }
    if (s->ds_note[0])
        text(56, py, 12, C_ACCENT2, s->ds_note, 260);
    return h;
}
/* ---------- content: chat ---------- */
static void reactions_row(int x, int y, const UiMessage *m, int me_bit_user) {
    (void)me_bit_user;
    for (int r = 0; r < ORBIT_REACTIONS; r++) {
        int n = __builtin_popcount(m->react[r]);
        if (!n)
            continue;
        char t[24];
        snprintf(t, sizeof(t), "%s %d", reactions[r], n);
        int w = text_width(t, 15) + 18;
        rrect(x, y, w, 26, 13, C_SURF2);
        text(x + 9, y + 3, 15, C_TEXT, t, w);
        x += w + 6;
    }
}
static int has_reactions(const UiMessage *m) {
    for (int r = 0; r < ORBIT_REACTIONS; r++)
        if (m->react[r])
            return 1;
    return 0;
}
static void chat_view(const UiState *s, const UiLocal *l, const UiGroup *g, const UiChannel *h) {
    char title[140], sub[260];
    if (g->kind == GK_DM) {
        bold(380, 34, 28, C_TEXT, g->name, 600);
        if (g->ndm == 1)
            snprintf(sub, sizeof(sub), "%s%s%s", status_label(g->dm[0].status), g->dm[0].oid[0] ? "  ·  " : "",
                     g->dm[0].oid);
        else
            snprintf(sub, sizeof(sub), "%s", g->people);
    } else {
        snprintf(title, sizeof(title), "%s", h ? h->name : "");
        hash_icon(380, 40, C_DIM);
        int e = bold(404, 34, 28, C_TEXT, title, 420);
        text(e + 14, 44, 16, C_DIM, g->name, 300);
        snprintf(sub, sizeof(sub), "%s", h && h->topic[0] ? h->topic : g->people);
    }
    text(382, 76, 15, C_MUTED, sub, 620);
    const char *state = g->own                       ? T(T_HOSTED_HERE)
                        : g->state == LINK_ONLINE     ? T(T_CONNECTED)
                        : g->state == LINK_CONNECTING ? T(T_CONNECTING)
                                                      : T(T_HOST_OFFLINE_SHORT);
    uint32_t dot = g->own || g->state == LINK_ONLINE ? C_OK : g->state == LINK_CONNECTING ? C_WARN : C_DIM;
    int cw = text_width(state, 14) + 34;
    chip(1232 - cw, 40, dot, state);
    if (g->call && !s->call_here) {
        char line[96];
        TF(line, sizeof(line), T_IN_VOICE_JOIN, g->call);
        int w = text_width(line, 14);
        text(1232 - w, 80, 14, C_OK, line, w + 4);
    }
    box(372, 110, 860, 1, C_LINE);
    int top = 124, bottom = l->reply_id || l->edit_id ? 552 : 584;
    if (!s->message_count) {
        if (s->loading)
            center(802, 320, 18, C_MUTED, T(T_LOADING), 0);
        else if (!g->own && g->state != LINK_ONLINE) {
            center(802, 270, 22, C_TEXT, T(T_HOST_UNREACHABLE), 1);
            char who[220];
            TF(who, sizeof(who), T_HOST_UNREACHABLE_LONG, g->host[0] ? g->host : "?");
            wrapped(552, 310, 16, C_MUTED, who, 500, 4, 8);
            if (s->link_error[0])
                wrapped(552, 410, 14, C_DIM, s->link_error, 500, 3, 6);
        } else {
            center(802, 270, 24, C_TEXT, T(g->kind == GK_DM ? T_DM_START : T_CHAT_START), 1);
            if (g->welcome[0] && g->kind == GK_SERVER)
                wrapped(552, 312, 17, 0xd6dae0, g->welcome, 500, 5, 8);
            else
                center(802, 312, 17, C_MUTED, T(T_CHAT_START_HINT), 0);
        }
    } else {
        int y = bottom, last = s->message_count - 1 - l->history_offset;
        if (last < 0)
            last = 0;
        for (int i = last; i >= 0 && y > top; i--) {
            const UiMessage *m = &s->messages[i];
            int grouped = i > 0 && !strcmp(s->messages[i - 1].name, m->name) &&
                          m->time - s->messages[i - 1].time < 300 && !m->reply &&
                          !(l->msg_mode && (l->msg_sel == i || l->msg_sel == i - 1));
            const char *body = m->blocked ? T(T_BLOCKED_MESSAGE)
                               : (m->flags & MF_DELETED) ? T(T_DELETED_MESSAGE)
                                                         : m->text;
            int count = split_lines(body, 19, 740, text_rows);
            if (count > 14)
                count = 14;
            int extra = (m->reply ? 24 : 0) + (has_reactions(m) ? 32 : 0) + (m->pending ? 20 : 0);
            int h = count * 27 + (grouped ? 6 : 40) + extra;
            int y0 = y - h;
            if (l->msg_mode && l->msg_sel == i)
                rrect(374, y0 - 2, 856, h + 2, 10, blend(C_BG, C_ACCENT, 40));
            if (!grouped) {
                if (y0 + 2 >= top)
                    person(384, y0 + 4, 38, m->name, m->mine ? s->me.color : m->color,
                           m->mine ? s->me.avatar : m->avatar, m->mine ? s->me.pic : m->pic,
                           m->mine ? s->me.anim : m->anim);
                if (y0 + 4 >= top) {
                    int e = bold(438, y0 + 4, 17, m->mine ? 0x9cc0ff : C_TEXT, m->name, 460);
                    char t[48];
                    clock_text(t, m->time);
                    if (m->flags & MF_EDITED) {
                        size_t n = strlen(t);
                        snprintf(t + n, sizeof(t) - n, "  %s", T(T_EDITED));
                    }
                    text(e + 10, y0 + 7, 13, C_DIM, t, 260);
                }
            }
            int ty = y0 + (grouped ? 3 : 32);
            if (m->reply) {
                char q[160];
                snprintf(q, sizeof(q), "» %s: %s", m->rname[0] ? m->rname : "…", m->rtext);
                box(440, ty + 2, 3, 18, C_ACCENT2);
                if (ty >= top)
                    text(450, ty, 14, C_MUTED, q, 720);
                ty += 24;
            }
            uint32_t bc = m->blocked || (m->flags & MF_DELETED) ? C_DIM : m->pending ? C_MUTED : 0xdfe3e8;
            for (int r = 0; r < count; r++)
                if (ty + r * 27 >= top)
                    text(438, ty + r * 27, 19, bc, text_rows[r], 780);
            ty += count * 27;
            if (has_reactions(m) && !m->blocked) {
                if (ty + 4 >= top)
                    reactions_row(438, ty + 4, m, 0);
                ty += 32;
            }
            if (m->pending && ty >= top)
                text(438, ty, 13, C_WARN, T(T_PENDING_DELIVERY), 600);
            y = y0 - 6;
        }
        if (l->history_offset && !l->msg_mode) {
            char more[64];
            TF(more, sizeof(more), T_NEWER_N, l->history_offset);
            int w = text_width(more, 14) + 28;
            rrect(1232 - w, bottom - 46, w, 30, 15, C_ACCENT);
            text(1246 - w, bottom - 41, 14, 0xffffff, more, w);
        }
    }
    /* composer */
    int online = g->own || g->state == LINK_ONLINE;
    if (l->reply_id || l->edit_id) {
        char line[200];
        const UiMessage *m = NULL;
        for (int i = 0; i < s->message_count; i++)
            if (s->messages[i].id == (l->reply_id ? l->reply_id : l->edit_id))
                m = &s->messages[i];
        if (l->reply_id)
            TF(line, sizeof(line), T_REPLYING_TO, m ? m->name : "");
        else
            snprintf(line, sizeof(line), "%s", T(T_EDITING));
        rrect(372, 558, 860, 34, 10, C_SURF);
        box(384, 566, 3, 18, C_ACCENT2);
        text(396, 565, 14, C_MUTED, line, 600);
        text(1100, 565, 13, C_DIM, T(T_CIRCLE_CANCEL), 130);
    }
    int can_send = (g->perms & P_SEND) && !g->smuted;
    rrect(372, 596, 860, 52, 14, C_SURF2);
    face_button(400, 622, 28, K_CROSS);
    text(426, 611, 18, l->draft[0] ? C_TEXT : C_DIM,
         !can_send   ? T(T_CANNOT_SEND)
         : l->draft[0] ? l->draft
         : online      ? T(T_WRITE_MESSAGE)
                       : T(T_WRITE_OFFLINE),
         780);
}
/* ---------- content: voice channel ---------- */
static void voice_view(const UiState *s, const UiGroup *g, const UiChannel *h) {
    speaker_icon(382, 42, C_DIM);
    int e = bold(410, 34, 28, C_TEXT, h->name, 420);
    text(e + 14, 44, 16, C_DIM, g->name, 300);
    text(382, 76, 15, C_MUTED, h->topic[0] ? h->topic : T(T_VOICE_CHANNEL), 620);
    box(372, 110, 860, 1, C_LINE);
    int here = s->call_state != CALL_OFF && s->call_link == g->link && s->call_group == g->id &&
               s->call_channel == h->id;
    int n = 0;
    for (int i = 0; i < g->ncall; i++)
        if (g->caller[i].ch == h->id)
            n++;
    if (!n && !here) {
        center(802, 260, 24, C_TEXT, T(T_VOICE_EMPTY), 1);
        center(802, 304, 17, C_MUTED, T(T_VOICE_JOIN_HINT), 0);
    }
    int col = 0, row = 0;
    for (int i = 0; i < g->ncall; i++) {
        const UiCaller *c = &g->caller[i];
        if (c->ch != h->id)
            continue;
        int x = 400 + col * 208, y = 140 + row * 168;
        int speaking = 0;
        for (int k = 0; here && k < s->call_count; k++)
            if (s->call_people[k].user == c->user)
                speaking = s->call_people[k].speaking && !s->call_people[k].local_muted;
        rrect(x, y, 192, 152, 18, C_SURF);
        if (speaking)
            disc((float)x + 96, (float)y + 58, 40, C_OK);
        disc((float)x + 96, (float)y + 58, speaking ? 36.5f : 38, C_SURF);
        person(x + 62, y + 24, 68, c->name, 0, 0, NULL, NULL);
        int tw = text_width(c->name, 16);
        text(x + 96 - (tw > 170 ? 85 : tw / 2), y + 104, 16, C_TEXT, c->name, 170);
        if (c->muted)
            mic_icon(x + 160, y + 12, C_BAD, 1);
        if (++col == 4)
            col = 0, row++;
    }
    rrect(372, 596, 860, 52, 14, here ? 0x163a28 : C_SURF2);
    text(400, 611, 18, here ? C_OK : C_TEXT, T(here ? T_VOICE_YOU_IN : T_VOICE_PRESS_JOIN), 800);
}
/* ---------- content: server overview ---------- */
static void banner(int x, int y, int w, int h, int style, uint32_t c) {
    uint32_t c2 = C_ACCENT2;
    for (int j = 0; j < h; j += 2)
        for (int i = 0; i < w; i += 2) {
            float u = (float)i / (float)w, v = (float)j / (float)h;
            unsigned t;
            switch (style & 7) {
            case 1:
                t = (unsigned)(255 * (0.5f * u + 0.5f * v));
                break;
            case 2:
                t = ((i + j) / 24) & 1 ? 200 : 90;
                break;
            case 3:
                t = (unsigned)(127 + 120 * sinf(u * 12.0f + v * 3.0f));
                break;
            case 4:
                t = (unsigned)(255 * fabsf(v - 0.5f + 0.3f * sinf(u * 6.28f)));
                break;
            case 5:
                t = (unsigned)(255 * (1 - u));
                break;
            case 6:
                t = ((i * 7 + j * 13) % 97) < 3 ? 255 : 30;
                break;
            case 7:
                t = (unsigned)(255 * v * v);
                break;
            default:
                t = 120;
                break;
            }
            if (t > 255)
                t = 255;
            uint32_t col = blend(blend(C_BG, c, 150), blend(C_BG, c2, 150), t);
            box(x + i, y + j, 2, 2, col);
        }
}
static void server_view(const UiState *s, const UiGroup *g) {
    banner(372, 24, 860, 120, g->banner, person_colors[g->color % 12]);
    server_icon(396, 104, 84, g);
    bold(500, 150, 30, C_TEXT, g->name, 600);
    char line[200];
    TF(line, sizeof(line), T_SERVER_STATS, g->online, g->members);
    text(502, 190, 15, C_MUTED, line, 600);
    const char *state = g->own ? T(T_HOSTED_HERE) : g->state == LINK_ONLINE ? T(T_CONNECTED) : T(T_HOST_OFFLINE_SHORT);
    int cw = text_width(state, 14) + 34;
    chip(1232 - cw, 156, g->own || g->state == LINK_ONLINE ? C_OK : C_DIM, state);
    if (!g->own && g->state != LINK_ONLINE) {
        char who[220];
        TF(who, sizeof(who), T_SERVER_OFFLINE_LONG, g->host[0] ? g->host : "?");
        wrapped(396, 226, 16, C_WARN, who, 820, 2, 6);
    }
    int y = 270;
    if (g->desc[0])
        y = wrapped(396, y, 17, 0xd6dae0, g->desc, 400, 4, 6) + 14;
    if (g->welcome[0]) {
        text(396, y, 13, C_DIM, T(T_WELCOME_MESSAGE), 300);
        wrapped(396, y + 22, 15, C_MUTED, g->welcome, 400, 5, 5);
    }
    /* my roles */
    int rx = 396, ry = 560;
    for (int i = 0; i < g->nrole; i++)
        if (g->myroles & (1 << g->role[i].i)) {
            int w = text_width(g->role[i].name, 13) + 30;
            rrect(rx, ry, w, 26, 13, C_SURF2);
            disc((float)rx + 13, (float)ry + 13, 4.5f, g->role[i].color);
            text(rx + 22, ry + 4, 13, C_TEXT, g->role[i].name, w);
            rx += w + 6;
        }
    /* members */
    rrect(830, 226, 402, 422, 16, C_SURF);
    text(852, 240, 13, C_DIM, T(T_MEMBERS_CAPS), 300);
    if (s->members_group != g->id || s->members_link != g->link || !s->member_count) {
        text(852, 270, 15, C_MUTED, T(T_LOADING), 300);
        return;
    }
    int my = 266;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->member_count && my < 620; i++) {
            const UiMember *m = &s->members[i];
            if ((pass == 0) != (m->status != ST_OFFLINE))
                continue;
            person(852, my, 30, m->name, m->color, m->avatar, m->pic, m->anim);
            presence(878, my + 26, m->status);
            uint32_t rc = C_TEXT;
            int best = -1;
            for (int k = 0; k < g->nrole; k++)
                if ((m->roles & (1 << g->role[k].i)) && (best < 0 || g->role[k].prio > g->role[best].prio))
                    best = k;
            if (best >= 0 && g->role[best].prio > 10)
                rc = g->role[best].color;
            text(894, my + 1, 15, m->status == ST_OFFLINE ? C_DIM : rc, m->name, 200);
            text(894, my + 19, 12, C_DIM, m->custom[0] ? m->custom : (best >= 0 ? g->role[best].name : ""), 200);
            if (m->smuted)
                mic_icon(1200, my + 6, C_BAD, 1);
            my += 40;
        }
}
/* ---------- content: friends, profile ---------- */
static void profile_card(int x, int y, const char *name, const char *oid, int color, int avatar, const char *pic,
                         const char *anim, int status,
                         const char *custom, const char *bio, const char *game, uint32_t created,
                         uint32_t since, const char *mutual) {
    rrect(x, y, 860, 300, 18, C_SURF);
    banner(x, y, 860, 90, 1, person_colors[color % 12]);
    disc((float)x + 80, (float)y + 96, 52, C_SURF);
    person(x + 32, y + 48, 96, name, color, avatar, pic, anim);
    presence(x + 116, y + 132, status);
    bold(x + 148, y + 100, 28, C_TEXT, name, 500);
    char line[200];
    snprintf(line, sizeof(line), "%s  ·  %s", status_label(status), oid[0] ? oid : "—");
    text(x + 150, y + 138, 15, C_MUTED, line, 640);
    int ty = y + 176;
    if (custom && custom[0] && status != ST_OFFLINE)
        text(x + 32, ty, 17, C_TEXT, custom, 790), ty += 30;
    if (bio && bio[0])
        ty = wrapped(x + 32, ty, 16, 0xd6dae0, bio, 790, 2, 6) + 8;
    char facts[300] = "";
    char t1[24] = "", t2[24] = "";
    if (game && game[0]) {
        TF(facts, sizeof(facts), T_FAV_GAME, game);
        strcat(facts, "   ");
    }
    if (created) {
        time_t tt = (time_t)created;
        struct tm tmv;
        gmtime_r(&tt, &tmv);
        snprintf(t1, sizeof(t1), "%02d.%02d.%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
        size_t n = strlen(facts);
        TF(facts + n, (unsigned)(sizeof(facts) - n), T_MEMBER_SINCE, t1);
        strcat(facts, "   ");
    }
    if (since) {
        time_t tt = (time_t)since;
        struct tm tmv;
        gmtime_r(&tt, &tmv);
        snprintf(t2, sizeof(t2), "%02d.%02d.%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
        size_t n = strlen(facts);
        TF(facts + n, (unsigned)(sizeof(facts) - n), T_FRIENDS_SINCE, t2);
    }
    if (facts[0] && ty < y + 270)
        text(x + 32, ty, 14, C_MUTED, facts, 800), ty += 24;
    if (mutual && mutual[0] && ty < y + 270) {
        char m[260];
        TF(m, sizeof(m), T_MUTUAL, mutual);
        text(x + 32, ty, 14, C_MUTED, m, 800);
    }
}
static void friend_view(const UiState *s, const UiFriend *f) {
    profile_card(372, 40, f->name, f->fid, f->color, f->avatar, f->pic, f->anim, f->state == FR_FRIENDS ? f->status : ST_OFFLINE,
                 f->custom, f->bio, f->game, f->created, f->state == FR_FRIENDS ? f->since : 0, f->mutual);
    int y = 370;
    if (f->state == FR_FRIENDS && f->playing) { /* 1.2: verified by the friend's presence plugin */
        char line[160];
        if (f->playing_name[0])
            TF(line, sizeof(line), T_PLAYING_NOW, f->playing_name);
        else
            snprintf(line, sizeof(line), "%s", T(T_PLAYING_SOME));
        disc(386, (float)y + 11, 5, C_OK);
        bold(400, y, 18, C_OK, line, 560);
        char auto_[96];
        snprintf(auto_, sizeof(auto_), "%s%s%s", f->playing_id, f->playing_id[0] ? "  ·  " : "", T(T_PLAYING_AUTO));
        text(400 + text_width(line, 18) + 16, y + 3, 14, C_DIM, auto_, 300);
        y += 40;
    }
    const char *info = f->state == FR_INCOMING ? T(T_FRIEND_INFO_IN)
                       : f->state == FR_OUTGOING ? T(T_FRIEND_INFO_OUT)
                       : f->user < 0             ? T(T_FRIEND_INFO_WAIT)
                                                 : T(T_FRIEND_INFO_OK);
    wrapped(380, y, 16, C_MUTED, info, 840, 3, 8);
    (void)s;
}
static void big_code(int y, const char *title, const char *code, const char *note) {
    rrect(372, y, 860, 150, 18, C_SURF);
    text(400, y + 20, 14, C_DIM, title, 600);
    if (code[0])
        bold(400, y + 52, 44, C_TEXT, code, 800);
    else
        text(400, y + 64, 18, C_MUTED, T(T_CODE_PREPARING), 800);
    if (note)
        text(400, y + 116, 14, C_MUTED, note, 800);
}
static void home_view(const UiState *s, const UiLocal *l) {
    char line[200];
    TF(line, sizeof(line), T_HELLO, s->username);
    bold(380, 34, 30, C_TEXT, line, 800);
    text(382, 78, 16, C_MUTED, T(T_HOME_SUB), 800);
    int online = 0, friends = 0, servers = 0, dms = 0;
    for (int i = 0; i < s->friend_count; i++)
        if (s->friends[i].state == FR_FRIENDS)
            friends++, online += s->friends[i].online;
    for (int i = 0; i < s->group_count; i++)
        s->groups[i].kind == GK_SERVER ? servers++ : dms++;
    struct {
        int key, value, tab;
        uint32_t c;
    } cards[4] = {{T_CARD_FRIENDS_ONLINE, online, TAB_FRIENDS, C_OK},
                  {T_CARD_REQUESTS, s->requests, TAB_FRIENDS, C_ACCENT2},
                  {T_CARD_UNREAD, s->unread_dm + s->unread_server, TAB_MESSAGES, C_ACCENT},
                  {T_CARD_SERVERS, servers, TAB_SERVERS, C_WARN}};
    for (int i = 0; i < 4; i++) {
        int x = 372 + i * 218;
        rrect(x, 118, 206, 112, 16, C_SURF);
        rrect(x + 18, 136, 6, 26, 3, cards[i].c);
        char v[24];
        if (i == 0)
            snprintf(v, sizeof(v), "%d / %d", cards[i].value, friends);
        else
            snprintf(v, sizeof(v), "%d", cards[i].value);
        bold(x + 34, 130, 30, C_TEXT, v, 160);
        text(x + 20, 182, 14, C_MUTED, T(cards[i].key), 180);
    }
    (void)dms;
    rrect(372, 248, 860, 400, 16, C_SURF);
    text(396, 264, 13, C_DIM, T(T_RECENT_ACTIVITY), 400);
    if (!s->note_count)
        text(396, 296, 16, C_MUTED, T(T_NO_ACTIVITY), 800);
    int y = 292;
    int sel = l->tab == TAB_HOME && l->side > 0 ? s->note_count - l->side : -1;
    for (int i = s->note_count - 1; i >= 0 && y < 620; i--) {
        const UiNote *n = &s->notes[i];
        if (i == sel)
            rrect(386, y - 4, 832, 40, 10, C_SURF2);
        char t[24];
        clock_text(t, n->time);
        text(400, y + 6, 13, C_DIM, t, 120);
        text(520, y + 4, 16, C_TEXT, n->text, 690);
        y += 42;
    }
}
static void settings_view(const UiState *s, int which) {
    bold(380, 34, 28, C_TEXT, setting_name(which), 700);
    box(372, 90, 860, 1, C_LINE);
    char v[8][200];
    const char *labels[8];
    int rows = 0;
    static const int theme_keys[THEME_COUNT] = THEME_KEYS;
    switch (which) {
    case 0:
        profile_card(372, 110, s->username, s->oid, s->me.color, s->me.avatar, s->me.pic, s->me.anim,
                     s->me.status == ST_INVISIBLE ? ST_OFFLINE : s->me.status, s->me.custom, s->me.bio,
                     s->me.game, s->me.created, 0, NULL);
        text(380, 430, 15, C_MUTED, T(T_PROFILE_HINT), 840);
        return;
    case 1:
        labels[rows] = T(T_LANGUAGE_NOW);
        snprintf(v[rows++], 200, "%s%s", i18n_lang_name(i18n_lang()), s->set.lang_auto ? T(T_LANG_AUTO_SUFFIX) : "");
        labels[rows] = T(T_LANGUAGE_NOTE_TITLE);
        snprintf(v[rows++], 200, "%s", T(T_LANGUAGE_NOTE));
        break;
    case 2:
        labels[rows] = T(T_THEME);
        snprintf(v[rows++], 200, "%s", T(theme_keys[s->set.theme % THEME_COUNT]));
        break;
    case 3:
        for (int i = 0; i < N_COUNT && rows < 8; i++) {
            static const int nk[N_COUNT] = {T_NT_FRIEND_REQUEST, T_NT_FRIEND_ACCEPTED, T_NT_DM, T_NT_GROUP,
                                            T_NT_INVITE, T_NT_CALL, T_NT_MENTION, T_NT_MODERATION};
            labels[rows] = T(nk[i]);
            snprintf(v[rows++], 200, "%s", T(s->set.notify & (1 << i) ? T_ON : T_OFF));
        }
        break;
    case 4:
        labels[rows] = T(T_AUDIO_NOTE_TITLE);
        snprintf(v[rows++], 200, "%s", T(T_DS_NATIVE));
        labels[rows] = T(T_MIC_TEST);
        if (s->mic_test)
            TF(v[rows++], 200, T_MIC_TEST_LIVE, s->mic_level, s->mic_floor);
        else
            snprintf(v[rows++], 200, "%s", T(T_MIC_TEST_SUB));
        labels[rows] = T(T_MIC_DATA);
        snprintf(v[rows++], 200, "%s", s->mic_info[0] ? s->mic_info : T(T_MIC_DATA_NONE));

        break;
    case 5:
        labels[rows] = T(T_NET_STATE);
        snprintf(v[rows++], 200, "%s", net_label(s->net));
        labels[rows] = T(T_NET_LOCAL);
        snprintf(v[rows++], 200, "%s", s->local_ip);
        labels[rows] = T(T_NET_TUNNEL_LABEL);
        snprintf(v[rows++], 200, "%s%s", s->tunnel, s->tunnel_online ? T(T_CONNECTED_SUFFIX) : "");
        labels[rows] = T(T_NET_RELAY_LABEL);
        snprintf(v[rows++], 200, "%s", s->relay[0] ? s->relay : T(T_NOT_NEEDED));
        labels[rows] = T(T_NET_ID);
        snprintf(v[rows++], 200, "%s", s->server_id);
        break;
    case 6: {
        labels[rows] = T(T_PRIVACY_STATUS);
        snprintf(v[rows++], 200, "%s", status_label(s->me.status));
        int blocked = 0;
        for (int i = 0; i < s->friend_count; i++)
            blocked += s->friends[i].state == FR_BLOCKED;
        labels[rows] = T(T_BLOCKED_PEOPLE);
        snprintf(v[rows++], 200, "%d", blocked);
        labels[rows] = T(T_ENCRYPTION);
        snprintf(v[rows++], 200, "%s", T(T_ENCRYPTION_NOTE));
        break;
    }
    default:
        labels[rows] = "Orbit";
        snprintf(v[rows++], 200, "%s %s  ·  %s %d", T(T_VERSION_WORD), ORBIT_VERSION, T(T_PROTOCOL_WORD), ORBIT_PROTOCOL);
        labels[rows] = T(T_ABOUT_TITLE);
        snprintf(v[rows++], 200, "%s", T(T_ABOUT_TEXT));
        labels[rows] = T(T_LICENSE);
        snprintf(v[rows++], 200, "GPL-3.0  ·  mbedTLS, Monocypher, cJSON, FreeType");
        break;
    }
    int y = 112;
    for (int i = 0; i < rows; i++) {
        text(384, y, 14, C_DIM, labels[i], 300);
        int end = wrapped(384, y + 22, 18, C_TEXT, v[i], 830, 3, 4);
        y = end + 16;
        box(380, y - 8, 844, 1, C_LINE);
    }
    if (which != 7)
        text(384, y + 6, 15, C_MUTED, T(T_SETTINGS_HINT), 840);
    else
        hint(384, y + 6, K_CROSS, NULL, T(T_UPD_CHECK));
}
static void action_view(const UiState *s, int id) {
    bold(380, 34, 28, C_TEXT, action_name(id), 800);
    box(372, 90, 860, 1, C_LINE);
    switch (id) {
    case 1:
        wrapped(384, 120, 18, C_TEXT, T(T_ADD_FRIEND_TEXT), 830, 5, 8);
        break;
    case 2:
        big_code(120, T(T_YOUR_FRIEND_CODE), s->friend_code, NULL);
        {
            char line[120];
            TF(line, sizeof(line), T_YOUR_ORBIT_ID, s->oid);
            text(384, 300, 18, C_TEXT, line, 830);
        }
        wrapped(384, 340, 16, C_MUTED, T(T_FRIEND_CODE_TEXT), 830, 6, 8);
        break;
    case 3:
        wrapped(384, 120, 18, C_TEXT, T(T_GROUP_DM_TEXT), 830, 5, 8);
        break;
    case 4:
        wrapped(384, 120, 18, C_TEXT, T(T_CREATE_SERVER_TEXT), 830, 6, 8);
        break;
    case 5:
        wrapped(384, 120, 18, C_TEXT, T(T_JOIN_SERVER_TEXT), 830, 5, 8);
        break;
    case 6:
        wrapped(384, 120, 18, C_TEXT, T(T_PUBLIC_TEXT), 830, 6, 8);
        break;
    }
}
#include "ui_next.c"
/* ---------- content dispatcher ---------- */
static void content(const UiState *s, const UiLocal *l) {
    UiRow rows[96];
    int n = ui_rows(s, l->tab, rows, 96);
    const UiRow *r = n ? &rows[l->side < n ? l->side : n - 1] : NULL;
    if (!r) {
        if (l->tab == TAB_MESSAGES)
            action_view(s, 3);
        return;
    }
    switch (r->kind) {
    case ROW_NOTE:
        home_view(s, l);
        break;
    case ROW_ACTION:
        if (r->a == 0)
            home_view(s, l);
        else
            action_view(s, r->a);
        break;
    case ROW_SETTING:
        settings_view(s, r->a);
        break;
    case ROW_FRIEND: {
        const UiFriend *f = friend_of(s, r->a);
        if (f)
            friend_view(s, f);
        break;
    }
    case ROW_SERVER: {
        const UiGroup *g = group_of(s, r->a, r->b);
        if (g)
            server_view(s, g);
        break;
    }
    case ROW_DM:
    case ROW_CHANNEL: {
        const UiGroup *g = group_of(s, r->a, r->b);
        const UiChannel *h = channel_of(g, r->kind == ROW_DM ? s->channel : r->c);
        if (!g)
            break;
        if (h && h->type == CH_VOICE)
            voice_view(s, g, h);
        else
            chat_view(s, l, g, h);
        break;
    }
    default:
        break;
    }
}
/* ---------- footer hints, depending on what is selected ---------- */
static void hints_main(const UiState *s, const UiLocal *l) {
    footer_bar();
    int x = 56, y = 678;
    UiRow rows[96];
    int n = ui_rows(s, l->tab, rows, 96);
    const UiRow *r = n ? &rows[l->side < n ? l->side : n - 1] : NULL;
    if (l->msg_mode) {
        x = hint(x, y, -2, NULL, T(T_H_MESSAGE));
        x = hint(x, y, K_CROSS, NULL, T(T_H_ACTIONS));
        x = hint(x, y, K_TRIANGLE, NULL, T(T_H_REACT));
        hint(x, y, K_CIRCLE, NULL, T(T_H_BACK));
        return;
    }
    x = hint(x, y, unified_ui ? -1 : -3, unified_ui ? "L1/R1" : NULL, T(T_H_TABS));
    x = hint(x, y, -2, NULL, T(T_H_SELECT));
    if (r && (r->kind == ROW_DM || r->kind == ROW_CHANNEL)) {
        const UiGroup *g = group_of(s, r->a, r->b);
        const UiChannel *h = channel_of(g, r->kind == ROW_DM ? s->channel : r->c);
        if (h && h->type == CH_VOICE)
            x = hint(x, y, K_CROSS, NULL, T(T_H_JOIN));
        else {
            x = hint(x, y, K_CROSS, NULL, T(T_H_WRITE));
            x = hint(x, y, -1, "L2", T(T_H_MESSAGES));
        }
        x = hint(x, y, -1, "SELECT", s->call_state != CALL_OFF ? T(T_H_HANG_UP) : T(T_H_VOICE));
        if (s->call_state != CALL_OFF)
            x = hint(x, y, -1, "R2", s->call_muted ? T(T_H_UNMUTE) : T(T_H_MUTE));
        if (r->kind == ROW_CHANNEL)
            x = hint(x, y, K_TRIANGLE, NULL, T(T_H_INVITE));
    } else if (r && r->kind == ROW_FRIEND) {
        const UiFriend *f = friend_of(s, r->a);
        if (f && f->state == FR_INCOMING) {
            x = hint(x, y, K_CROSS, NULL, T(T_H_ACCEPT));
            x = hint(x, y, K_SQUARE, NULL, T(T_H_DECLINE));
        } else if (f && f->state == FR_FRIENDS) {
            x = hint(x, y, K_CROSS, NULL, T(T_H_MESSAGE_FRIEND));
            x = hint(x, y, -1, "SELECT", T(T_H_CALL));
            x = hint(x, y, K_TRIANGLE, NULL, T(T_H_TO_SERVER));
        }
    } else if (r && r->kind == ROW_SERVER) {
        x = hint(x, y, K_CROSS, NULL, T(T_H_OPEN));
        x = hint(x, y, K_TRIANGLE, NULL, T(T_H_INVITE));
    } else if (r && r->kind == ROW_NOTE)
        x = hint(x, y, K_CROSS, NULL, T(T_H_GO_THERE));
    else if (r && (r->kind == ROW_ACTION || r->kind == ROW_SETTING) && !(r->kind == ROW_ACTION && r->a == 0))
        x = hint(x, y, K_CROSS, NULL, T(T_H_OK));
    hint(x, y, K_CIRCLE, NULL, T(T_H_MENU));
}
static void empty_welcome(void) {
    orbit_mark(790, 196, 44);
}
static void main_screen(const UiState *s, const UiLocal *l) {
    sidebar(s, l);
    content(s, l);
    (void)empty_welcome;
}
static void dim_background(void) {
    for (int j = 0; j < sh; j++) {
        uint32_t *row = screen + j * sw;
        for (int i = 0; i < sw; i++) {
            uint32_t p = row[i];
            row[i] = (p >> 2 & 0x3f3f3f) + (p >> 3 & 0x1f1f1f);
        }
    }
}
static void modal(int x, int y, int w, int h) {
    rrect(x - 1, y - 1, w + 2, h + 2, 22, C_LINE);
    rrect(x, y, w, h, 21, C_SURF);
}
static void invite_page(const UiState *s) {
    modal(240, 80, 800, 520);
    bold(286, 118, 30, C_TEXT, T(T_INVITE_TITLE), 600);
    text(288, 162, 16, C_MUTED, s->invite_group[0] ? s->invite_group : "", 700);
    rrect(286, 206, 708, 120, 16, C_BG);
    if (s->invite_state == 2) {
        int w = text_width(s->invite_code, 46);
        bold(640 - w / 2, 236, 46, C_TEXT, s->invite_code, 700);
    } else if (s->invite_state == 3)
        center(640, 252, 20, C_BAD, T(T_INVITE_FAILED), 0);
    else
        center(640, 252, 20, C_MUTED, T(T_INVITE_MAKING), 0);
    if (s->invite_state == 2) {
        int internet = strstr(s->invite_scope, T(T_SCOPE_HOME)) == NULL &&
                       strcmp(s->invite_scope, T(T_SCOPE_HOST_HOME));
        disc(298, 357, 6, internet ? C_OK : C_WARN);
        wrapped(314, 346, 16, internet ? C_OK : C_WARN, s->invite_scope, 660, 2, 6);
    }
    int y = 410;
    const int steps[3] = {T_INVITE_STEP1, T_INVITE_STEP2, T_INVITE_STEP3};
    for (int i = 0; i < 3; i++) {
        disc(300, (float)y + i * 46 + 12, 13, C_SURF2);
        char n[4];
        snprintf(n, sizeof(n), "%d", i + 1);
        center(300, y + i * 46 + 3, 15, C_TEXT, n, 1);
        text(326, y + i * 46 + 2, 17, 0xd6dae0, T(steps[i]), 660);
    }
    footer_bar();
    hint(56, 678, K_CIRCLE, NULL, T(T_H_BACK));
}
static void qr_panel(int x, int y, int size, const char *text) {
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
        return;
    int n = qrcodegen_getSize(qr), cell = (size - 32) / n, side = cell * n;
    rrect(x, y, side + 32, side + 32, 16, 0xffffff);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (qrcodegen_getModule(qr, c, r))
                box(x + 16 + c * cell, y + 16 + r * cell, cell, cell, 0x000000);
}
/* 1.2: picture preview next to a list: large and at the sizes it is used at */
static void preview_panel(const UiLocal *l) {
    glass_panel(330, 150, 340, 380, 22);
    UiGroup g;
    memset(&g, 0, sizeof(g));
    snprintf(g.pic, sizeof(g.pic), "%s", l->preview_pic);
    snprintf(g.anim, sizeof(g.anim), "%s", l->preview_anim);
    if (l->preview_square) {
        server_icon(404, 180, 192, &g);
        server_icon(420, 400, 84, &g);
        server_icon(524, 420, 52, &g);
        server_icon(596, 430, 38, &g);
    } else {
        person(404, 180, 192, "", 0, 0, g.pic, g.anim);
        person(420, 400, 84, "", 0, 0, g.pic, g.anim);
        person(524, 420, 52, "", 0, 0, g.pic, g.anim);
        person(596, 430, 38, "", 0, 0, g.pic, g.anim);
    }
}
static void list_page(const UiLocal *l) {
    if (l->qr[0])
        qr_panel(330, 170, 340, l->qr);
    else if (l->preview_pic[0])
        preview_panel(l);
    int count = l->list_count, visible = count > 9 ? 9 : count;
    int h = 150 + visible * 52 + (l->list_note[0] ? 40 : 0);
    if (h > 620)
        h = 620;
    int y0 = (664 - h) / 2;
    modal(700, y0, 540, h);
    bold(736, y0 + 30, 24, C_TEXT, l->list_title, 470);
    int y = y0 + 76;
    if (l->list_note[0]) {
        wrapped(736, y - 6, 14, C_MUTED, l->list_note, 470, 2, 4);
        y += 40;
    }
    int top = l->list_top;
    for (int i = top; i < count && i < top + visible; i++) {
        int active = i == l->list_focus, flags = l->list[i].flags;
        if (flags & LI_HEADER) {
            text(736, y + 18, 13, C_DIM, l->list[i].text, 460);
            y += 52;
            continue;
        }
        if (active) {
            rrect(716, y, 508, 48, 12, C_SURF2);
            rrect(716, y + 13, 4, 22, 2, C_ACCENT);
        }
        int x = 740;
        if (flags & (LI_CHECKED | LI_RADIO)) {
            int on = flags & LI_CHECKED;
            if (flags & LI_RADIO) {
                disc(750, (float)y + 24, 9, on ? C_ACCENT : C_LINE);
                disc(750, (float)y + 24, on ? 4 : 7, on ? 0xffffff : C_SURF);
            } else {
                rrect(741, y + 15, 18, 18, 5, on ? C_ACCENT : C_LINE);
                if (on)
                    text(744, y + 12, 15, 0xffffff, "✓", 16);
            }
            x = 770;
        }
        if (l->list[i].color) {
            disc((float)x + 7, (float)y + 24, 6, l->list[i].color);
            x += 22;
        }
        uint32_t c = flags & LI_DISABLED ? C_DIM : flags & LI_DANGER && active ? C_BAD : C_TEXT;
        text(x, y + (l->list[i].sub[0] ? 4 : 13), 18, c, l->list[i].text, 1210 - x);
        if (l->list[i].sub[0])
            text(x, y + 27, 13, C_DIM, l->list[i].sub, 1210 - x);
        y += 52;
    }
    if (count > visible) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d / %d", l->list_focus + 1, count);
        text(1160, y0 + 36, 13, C_DIM, pos, 70);
    }
    footer_bar();
    int x = hint(56, 678, -2, NULL, T(T_H_SELECT));
    x = hint(x, 678, K_CROSS, NULL, T(T_H_OK));
    hint(x, 678, K_CIRCLE, NULL, T(T_H_BACK));
}
static void leave_page(const UiState *s) {
    const UiGroup *g = s->group_count && s->selected >= 0 ? &s->groups[s->selected] : NULL;
    modal(300, 190, 680, 300);
    int own = g && g->own && g->perms == P_ALL;
    bold(340, 226, 28, C_TEXT, T(own ? T_DELETE_Q : T_LEAVE_Q), 600);
    text(342, 272, 18, C_MUTED, g ? g->name : "", 600);
    wrapped(342, 316, 17, 0xd6dae0, T(own ? T_DELETE_TEXT : T_LEAVE_TEXT), 600, 3, 8);
    footer_bar();
    int x = hint(56, 678, K_CROSS, NULL, T(own ? T_H_DELETE_FOREVER : T_H_LEAVE));
    hint(x, 678, K_CIRCLE, NULL, T(T_H_CANCEL));
}
static void network_page(const UiState *s) {
    modal(200, 60, 880, 560);
    bold(240, 96, 28, C_TEXT, T(T_SET_NETWORK), 600);
    int y = 158;
    rrect(240, y, 800, 128, 16, C_BG);
    disc(270, (float)y + 34, 8, net_color(s->net));
    bold(290, y + 21, 21, C_TEXT,
         T(s->net == NET_INTERNET ? T_NET_BIG_INTERNET
           : s->net == NET_HOME   ? T_NET_BIG_HOME
           : s->net == NET_OFFLINE ? T_NET_BIG_OFFLINE
                                   : T_NET_BIG_CHECKING),
         700);
    wrapped(290, y + 54, 15, C_MUTED, s->net_detail, 730, 3, 5);
    y = 310;
    char relay[160], tunnel[180];
    snprintf(relay, sizeof(relay), "%s%s", s->relay[0] ? s->relay : T(T_NOT_NEEDED),
             !s->relay[0] ? "" : s->relay_online ? T(T_CONNECTED_SUFFIX) : T(T_UNREACHABLE_SUFFIX));
    snprintf(tunnel, sizeof(tunnel), "%s%s", s->tunnel, s->tunnel_online ? T(T_CONNECTED_SUFFIX) : "");
    const char *labels[] = {T(T_NET_LOCAL), T(T_NET_TUNNEL_LABEL), T(T_NET_RELAY_LABEL), T(T_NET_ID)};
    const char *values[] = {s->local_ip, tunnel, relay, s->server_id};
    for (int i = 0; i < 4; i++) {
        text(244, y + i * 44, 15, C_DIM, labels[i], 300);
        text(560, y + i * 44, 17, i == 1 && s->tunnel_online ? C_OK : C_TEXT, values[i], 460);
        box(240, y + i * 44 + 32, 800, 1, C_LINE);
    }
    wrapped(244, 500, 15, C_MUTED, T(T_NET_EXPLAIN), 790, 3, 7);
    footer_bar();
    int x = hint(56, 678, K_SQUARE, NULL, T(T_H_RECONNECT));
    x = hint(x, 678, K_TRIANGLE, NULL, T(T_H_CHANGE_RELAY));
    hint(x, 678, K_CIRCLE, NULL, T(T_H_BACK));
}
static void welcome_page(const UiState *s, const UiLocal *l) {
    orbit_mark(640, 150, 46);
    center(640, 222, 40, C_TEXT, T(T_WELCOME_TITLE), 1);
    center(640, 280, 19, C_MUTED, T(T_WELCOME_LINE1), 0);
    center(640, 306, 19, C_MUTED, T(T_WELCOME_LINE2), 0);
    for (int i = 0; i < 2; i++) {
        int y = 366 + i * 92, active = l->focus == i;
        rrect(390, y, 500, 78, 16, active ? C_SURF2 : C_SURF);
        if (active)
            rrect(390, y + 22, 4, 34, 2, C_ACCENT);
        if (i == 0) {
            person(414, y + 17, 44, s->username, s->me.color, s->me.avatar, s->me.pic, s->me.anim);
            text(472, y + 12, 14, C_DIM, T(T_WELCOME_NAME), 380);
            bold(472, y + 34, 22, C_TEXT, s->username, 360);
        } else {
            bold(472, y + 25, 22, active ? C_TEXT : C_MUTED, T(T_WELCOME_GO), 360);
            face_button(430, (float)y + 39, 34, K_CROSS);
        }
    }
    footer_bar();
    int x = hint(56, 678, -2, NULL, T(T_H_SELECT));
    x = hint(x, 678, K_CROSS, NULL, T(l->focus == 0 ? T_H_CHANGE_NAME : T_H_START));
    hint(x, 678, -1, "START", T(T_H_QUIT));
}
/* ---------- in-app keyboard: German, English, French and Spanish layouts ---------- */
static const char *const kb_rows[KB_COUNT][2][4][11] = {
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "ß"},
      {"q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä"},
      {"y", "x", "c", "v", "b", "n", "m", ",", ".", "-", "?"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "+"},
      {"Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä"},
      {"Y", "X", "C", "V", "B", "N", "M", ";", ":", "_", "'"}}},
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "'"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "@"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "?"}},
     {{"!", "\"", "#", "$", "%", "&", "*", "(", ")", "=", "+"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "_"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "~"},
      {"Z", "X", "C", "V", "B", "N", "M", "<", ">", "|", "^"}}},
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "é"},
      {"a", "z", "e", "r", "t", "y", "u", "i", "o", "p", "è"},
      {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m", "à"},
      {"w", "x", "c", "v", "b", "n", "ç", ",", ".", "'", "?"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "+"},
      {"A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", "ù"},
      {"Q", "S", "D", "F", "G", "H", "J", "K", "L", "M", "ê"},
      {"W", "X", "C", "V", "B", "N", "Ç", ";", ":", "-", "€"}}},
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "¿"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "á"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ñ", "é"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "-", "?"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "¡"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "í"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "Ñ", "ó"},
      {"Z", "X", "C", "V", "B", "N", "M", ";", ":", "ú", "ü"}}},
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "'"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "è"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ò", "à"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "-", "ù"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "?"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "é"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "ç", "ì"},
      {"Z", "X", "C", "V", "B", "N", "M", ";", ":", "_", "€"}}},
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "?"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "á"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ç", "ã"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "-", "é"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "ê"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "í"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "Ç", "õ"},
      {"Z", "X", "C", "V", "B", "N", "M", ";", ":", "ó", "ú"}}},
    /* 1.2 RU: ЙЦУКЕН */
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "ё"},
      {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х"},
      {"ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э"},
      {"я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "ъ", "."}},
     {{"!", "\"", "?", "-", "%", ":", ";", "(", ")", "=", "Ё"},
      {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х"},
      {"Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э"},
      {"Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", "Ъ", ","}}},
    /* 1.2 UK: ЙЦУКЕН with і, ї, є, ґ and the apostrophe */
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "'"},
      {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х"},
      {"ф", "і", "в", "а", "п", "р", "о", "л", "д", "ж", "є"},
      {"я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "ї", "ґ"}},
     {{"!", "\"", "?", "-", "%", ":", ";", "(", ")", ",", "."},
      {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х"},
      {"Ф", "І", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Є"},
      {"Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", "Ї", "Ґ"}}},
    /* 1.2 Nordic (nb, da, sv, fi): å ø æ ä ö */
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "å"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ø", "æ"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "ö", "ä"}},
     {{"!", "\"", "@", "#", "%", "&", "/", "(", ")", "=", "?"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "Å"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "Ø", "Æ"},
      {"Z", "X", "C", "V", "B", "N", "M", ";", ":", "Ö", "Ä"}}}
};
int ui_kb_shift(int mode, int row, int shift) {
    if (mode == 1) /* codes: digits on the top row, capital letters below, whatever shift says */
        return row != 0;
    return shift;
}
int ui_kb_cols(int row) {
    return row < 4 ? 11 : 4;
}
const char *ui_kb_key(int layout, int row, int col, int shift) {
    if (row < 0 || row > 3 || col < 0 || col > 10)
        return NULL;
    if (layout < 0 || layout >= KB_COUNT)
        layout = KB_EN;
    return kb_rows[layout][shift ? 1 : 0][row][col];
}
int ui_kb_special(int row, int col) {
    static const int specials[4] = {KB_SHIFT, KB_SPACE, KB_BACK, KB_DONE};
    return row == 4 && col >= 0 && col < 4 ? specials[col] : 0;
}
static void kb_key(int x, int y, int w, const char *label, int active, int on) {
    rrect(x, y, w, 48, 10, active ? C_ACCENT : on ? 0x2b3b5c : C_SURF2);
    int tw = text_width(label, 20);
    text(x + (w - tw) / 2, y + 11, 20, active ? 0xffffff : C_TEXT, label, w);
}
static void keyboard_overlay(const UiLocal *l) {
    rrect(119, 179, 1042, 482, 22, C_LINE);
    rrect(120, 180, 1040, 480, 22, C_SURF);
    text(150, 198, 14, C_DIM, l->kb_title, 800);
    char count[24];
    snprintf(count, sizeof(count), "%d / %d", (int)strlen(l->kb_text), l->kb_max);
    int cw = text_width(count, 13);
    text(1130 - cw, 199, 13, C_DIM, count, cw + 4);
    rrect(150, 224, 980, 60, 12, C_BG);
    /* 1.2: word suggestions (UP from the top row, or L2 for the first) */
    for (int i = 0, sxp = 150; i < l->kb_sugg_n && i < 3; i++) {
        int w = text_width(l->kb_sugg[i], 18) + 40, on = l->kb_row == -1 && l->kb_col == i;
        rrect(sxp, 296, w, 44, 22, on ? C_ACCENT : C_SURF2);
        if (i == 0 && !on)
            ring_rect(sxp, 296, w, 44, 22, 1, C_ACCENT, 140);
        text(sxp + 20, 306, 18, on ? 0xffffff : C_TEXT, l->kb_sugg[i], w);
        sxp += w + 12;
    }
    /* show the end of the text so the cursor is always visible */
    const char *t = l->kb_text;
    while (*t && text_width(t, 22) > 930) {
        t++;
        while ((*t & 0xc0) == 0x80)
            t++;
    }
    int end = l->kb_text[0] ? text(170, 239, 22, C_TEXT, t, 940) : 170;
    if (!l->kb_text[0])
        text(170, 239, 22, C_DIM, l->kb_mode == 1 ? "ABCDE-FGHJK-LMNPQ-RSTUV" : T(T_TYPE_HERE), 900);
    rrect(end + 2, 236, 3, 36, 1, C_ACCENT);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 11; c++)
            kb_key(150 + c * 90, 352 + r * 56, 82,
                   l->kb_emoji ? emoji_utf8(r * 11 + c)
                               : ui_kb_key(l->kb_layout, r, c, ui_kb_shift(l->kb_mode, r, l->kb_shift)),
                   l->kb_row == r && l->kb_col == c, 0);
    int y = 352 + 4 * 56;
    const int xs[4] = {150, 330, 780, 960}, ws[4] = {172, 442, 172, 172};
    const char *labels[4] = {T(T_KB_SHIFT), T(T_KB_SPACE), T(T_KB_DELETE),
                             T(l->kb_mode == 2 ? T_KB_SEND : T_KB_DONE)};
    for (int i = 0; i < 4; i++)
        kb_key(xs[i], y, ws[i], labels[i], l->kb_row == 4 && l->kb_col == i,
               (i == 0 && l->kb_shift) || i == 3);
    footer_bar();
    int x = hint(56, 678, K_CROSS, NULL, T(T_H_KEY));
    x = hint(x, 678, K_SQUARE, NULL, T(T_KB_DELETE));
    x = hint(x, 678, K_TRIANGLE, NULL, T(T_KB_SPACE));
    x = hint(x, 678, -1, "L1", T(T_H_CAPS));
    x = hint(x, 678, -1, "R1", T(T_H_LAYOUT));
    if (l->kb_mode != 1)
        x = hint(x, 678, -1, "R2", T(l->kb_emoji ? T_H_LETTERS : T_H_EMOJI));
    if (l->kb_sugg_n)
        x = hint(x, 678, -1, "L2", T(T_H_SUGGEST));
    x = hint(x, 678, -1, "START", T(l->kb_mode == 2 ? T_KB_SEND : T_KB_DONE));
    hint(x, 678, K_CIRCLE, NULL, T(T_H_CANCEL));
}
/* Overlays (menu, keyboard, dialogs) reuse a cached, dimmed copy of the screen below:
 * moving the cursor redraws only the overlay instead of the whole screen. */
static int bg_w, bg_h, bg_off, bg_base, bg_side, bg_tab, bg_mode;
static unsigned bg_home;
static unsigned bg_draft;
static unsigned hash_text(const char *t) {
    unsigned h = 2166136261u;
    for (; *t; t++)
        h = (h ^ (unsigned char)*t) * 16777619u;
    return h;
}
/* 1.2 (04): the list screens' page (virtual 1280 x 664, without the footer) is drawn into its own
 * buffer at a smaller size and placed under the shared top bar. Text is rasterised at that size
 * (no image scaling), so it stays sharp. */
#define LEGACY_TOP 76
static void unified_screen(const UiState *s, const UiLocal *l) {
    uint32_t *full = screen;
    int fw = sw, fh = sh;
    space_background();
    top_bar(s, l);
    /* scale k = (664 - top) / 664 for both axes */
    int bw = fw * (664 - LEGACY_TOP) / 664, bh = fh * (664 - LEGACY_TOP) / 664;
    size_t need = (size_t)bw * (size_t)bh;
    if (legacy_cap < need) {
        free(legacy_buf);
        legacy_buf = malloc(need * 4);
        legacy_cap = legacy_buf ? need : 0;
    }
    if (!legacy_buf) {
        main_screen(s, l);
        return;
    }
    screen = legacy_buf;
    sw = bw;
    sh = bh;
    fill_px(0, 0, sw, sh, C_BG);
    main_screen(s, l);
    int rows = bh * 664 / 720, x0 = (fw - bw) / 2, y0 = LEGACY_TOP * fh / 720;
    screen = full;
    sw = fw;
    sh = fh;
    for (int j = 0; j < rows && y0 + j < fh; j++)
        memcpy(screen + (size_t)(y0 + j) * fw + x0, legacy_buf + (size_t)j * bw, (size_t)bw * 4);
    /* hairline frame so the page reads as one glass panel */
    int fx0 = x0 * 1280 / fw, fy0 = LEGACY_TOP, fw0 = bw * 1280 / fw, fh0 = rows * 720 / fh;
    box(fx0, fy0, fw0, 1, C_LINE);
    box(fx0, fy0 + fh0, fw0, 1, C_LINE);
    box(fx0, fy0, 1, fh0, C_LINE);
    box(fx0 + fw0 - 1, fy0, 1, fh0, C_LINE);
}
static void base_screen(const UiState *s, const UiLocal *l) {
    fill_px(0, 0, sw, sh, C_BG);
    if (l->page == PAGE_WELCOME)
        welcome_page(s, l);
    else if (TAB_SPOTLIGHT(l->tab))
        spotlight_screen(s, l);
    else {
        if (unified_ui)
            unified_screen(s, l);
        else
            main_screen(s, l);
        if (l->page == PAGE_CHAT)
            hints_main(s, l);
    }
}
void ui_render(uint32_t *pixels, int width, int height, const UiState *s, const UiLocal *l) {
    pic_gen_now = s->pic_gen;
    pic_loads_frame = 0;
    anim_frame_begin(l->now);
    screen = pixels;
    sw = width;
    sh = height;
    int overlay = l->kb_active || (l->page != PAGE_CHAT && l->page != PAGE_WELCOME);
    if (!overlay) {
        base_screen(s, l);
        toast(s, l);
        return;
    }
    size_t need = (size_t)sw * (size_t)sh;
    int base = l->page == PAGE_WELCOME ? 1 : l->page == PAGE_CHAT ? 2 : 3;
    unsigned draft = hash_text(l->draft);
    if (bg_size < need) {
        free(bg_cache);
        bg_cache = malloc(need * sizeof(uint32_t));
        bg_size = bg_cache ? need : 0;
        bg_rev = -1;
    }
    if (!bg_cache || bg_rev != s->revision || bg_w != sw || bg_h != sh || bg_off != l->history_offset ||
        bg_base != base || bg_draft != draft || base == 1 || bg_side != l->side || bg_tab != l->tab ||
        bg_mode != l->msg_mode * 1000 + l->msg_sel || bg_home != home_key(l)) {
        UiLocal *under = malloc(sizeof(*under));
        if (under) {
            *under = *l;
            under->kb_active = 0;
            if (base == 3)
                under->page = PAGE_CHAT;
            base_screen(s, under);
            free(under);
        }
        dim_background();
        if (bg_cache) {
            memcpy(bg_cache, screen, need * sizeof(uint32_t));
            bg_rev = s->revision;
            bg_w = sw;
            bg_h = sh;
            bg_off = l->history_offset;
            bg_base = base;
            bg_draft = draft;
            bg_side = l->side;
            bg_tab = l->tab;
            bg_mode = l->msg_mode * 1000 + l->msg_sel;
            bg_home = home_key(l);
        }
    } else
        memcpy(screen, bg_cache, need * sizeof(uint32_t));
    if (l->kb_active)
        keyboard_overlay(l);
    else if (l->page == PAGE_INVITE)
        invite_page(s);
    else if (l->page == PAGE_LIST || l->page == PAGE_MENU)
        list_page(l);
    else if (l->page == PAGE_LEAVE)
        leave_page(s);
    else if (l->page == PAGE_NETWORK)
        network_page(s);
    toast(s, l);
}

/* ---------- animated intro (about 3 seconds, skippable) ---------- */
static float clamp01(float v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static float ease_out(float t) {
    t = clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
static float ease_back(float t) {
    t = clamp01(t);
    const float c = 1.6f;
    return 1 + (c + 1) * (t - 1) * (t - 1) * (t - 1) + c * (t - 1) * (t - 1);
}
static inline void plot_a(int x, int y, uint32_t c, float cover, float alpha) {
    plot(x, y, c, (cover > 1 ? 1 : cover) * alpha);
}
/* Planet with a tilted ring that draws itself in, a glow and an orbiting moon. */
static void orbit_mark_anim(float cx, float cy, float r, float scale, float reveal, float moon,
                            float alpha) {
    if (scale <= 0.01f || alpha <= 0)
        return;
    float X = fx(cx), Y = fy(cy), R = fy(r);
    float P = R * 0.62f * scale; /* planet radius */
    const float c = 0.866f, s = -0.5f, A = R * 1.55f, B = R * 0.48f, two_pi = 6.2831853f;
    int ext = (int)(R * 1.9f) + 3;
    int y0 = (int)Y - ext, y1 = (int)Y + ext, x0 = (int)X - ext, x1 = (int)X + ext;
    if (y0 < 0)
        y0 = 0;
    if (x0 < 0)
        x0 = 0;
    if (y1 >= sh)
        y1 = sh - 1;
    if (x1 >= sw)
        x1 = sw - 1;
    float glow_r = R * 1.9f * scale;
    for (int j = y0; j <= y1; j++)
        for (int i = x0; i <= x1; i++) {
            float px = i + 0.5f - X, py = j + 0.5f - Y;
            float d = sqrtf(px * px + py * py);
            /* soft glow */
            if (d < glow_r) {
                float g = 1 - d / glow_r;
                plot_a(i, j, 0x2a5fd0, g * g * 0.55f, alpha);
            }
            float lx = px * c - py * s, ly = px * s + py * c;
            float ex = lx / A, ey = ly / B, e = sqrtf(ex * ex + ey * ey);
            float ring = 1.0f - fabsf(e - 1.0f) * B / (R * 0.09f + 0.8f);
            int front = ly > 0;
            if (ring > 0 && reveal < 1) {
                float ang = atan2f(ey, ex) + 1.2f; /* start drawing at the upper left */
                if (ang < 0)
                    ang += two_pi;
                if (ang > two_pi)
                    ang -= two_pi;
                if (ang > reveal * two_pi)
                    ring = 0;
            }
            if (!front && ring > 0)
                plot_a(i, j, C_ACCENT, ring, alpha);
            float planet = P - d + 0.5f;
            if (planet > 0) {
                float shade = 0.72f + 0.28f * (-px - py) / (P * 1.4f + 1);
                uint32_t col = blend(0x2350c4, 0x9cc0ff, (unsigned)(clamp01(shade) * 255));
                plot_a(i, j, col, planet, alpha);
            }
            if (front && ring > 0 && d > P * 0.8f)
                plot_a(i, j, C_ACCENT, ring, alpha);
        }
    /* moon on the ring */
    if (moon >= 0 && reveal > 0.98f) {
        float th = moon * two_pi;
        float lx = cosf(th) * A, ly = sinf(th) * B;
        float mx = lx * c + ly * s, my = -lx * s + ly * c;
        int behind = ly < 0 && sqrtf(mx * mx + my * my) < P;
        if (!behind) {
            float mr = R * 0.11f + 1.5f, MX = X + mx, MY = Y + my;
            for (int j = (int)(MY - mr - 2); j <= (int)(MY + mr + 2); j++)
                for (int i = (int)(MX - mr - 2); i <= (int)(MX + mr + 2); i++) {
                    float dx = i + 0.5f - MX, dy = j + 0.5f - MY, dd = sqrtf(dx * dx + dy * dy);
                    plot_a(i, j, 0xe8f0ff, mr - dd + 0.5f, alpha);
                }
        }
    }
}
static uint32_t *intro_bg;
static int intro_w, intro_h;
static void intro_background(void) {
    if (intro_bg && intro_w == sw && intro_h == sh) {
        memcpy(screen, intro_bg, (size_t)sw * sh * 4);
        return;
    }
    /* deep space: vertical gradient with a soft vignette, computed once */
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            float u = (i + 0.5f) / sw - 0.5f, v = (j + 0.5f) / sh - 0.45f;
            float glow = clamp01(1 - sqrtf(u * u * 1.2f + v * v * 2.2f) * 1.6f);
            /* ordered dithering hides 8-bit banding in the dark gradient */
            static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9},
                                              {15, 7, 13, 5}};
            float g = glow * glow * 255 + (bayer[j & 3][i & 3] / 16.0f - 0.5f) * 2.5f;
            uint32_t base = blend(0x07080b, 0x0f1626, (unsigned)(g < 0 ? 0 : g > 255 ? 255 : g));
            screen[j * sw + i] = base;
        }
    free(intro_bg);
    intro_bg = malloc((size_t)sw * sh * 4);
    if (intro_bg) {
        memcpy(intro_bg, screen, (size_t)sw * sh * 4);
        intro_w = sw;
        intro_h = sh;
    }
}
static void stars(float t, float alpha) {
    unsigned seed = 1234567;
    for (int k = 0; k < 140; k++) {
        seed = seed * 1103515245u + 12345u;
        float x = (seed >> 8) % 1280;
        seed = seed * 1103515245u + 12345u;
        float y = (seed >> 8) % 640;
        seed = seed * 1103515245u + 12345u;
        float tw = 0.45f + 0.55f * (0.5f + 0.5f * sinf(t * (1.5f + (seed % 7) * 0.4f) + k));
        float size = (seed >> 4) % 9 == 0 ? 1.3f : 0.75f;
        float drift = t * (4 + k % 5); /* slow parallax drift */
        float sxp = fx(x - drift), syp = fy(y);
        uint32_t col = k % 9 == 0 ? 0xbcd2ff : 0xffffff;
        for (int j = (int)(syp - 2); j <= (int)(syp + 2); j++)
            for (int i = (int)(sxp - 2); i <= (int)(sxp + 2); i++) {
                float dx = i + 0.5f - sxp, dy = j + 0.5f - syp;
                float cover = size - sqrtf(dx * dx + dy * dy) + 0.5f;
                if (cover > 0)
                    plot_a(i, j, col, cover, alpha * tw * 0.8f);
            }
    }
}
static void text_alpha(int x, int y, int size, uint32_t color, const char *str, int maxw,
                       float alpha, int heading) {
    if (alpha <= 0.01f)
        return;
    uint32_t c = blend(0x0b0d12, color, (unsigned)(clamp01(alpha) * 255));
    if (heading)
        bold(x, y, size, c, str, maxw);
    else
        text(x, y, size, c, str, maxw);
}
/* ORBIT NEXT intro (~4 s, any button skips to the hand-over):
 * 0.0 darkness, blue particles drift in and gather on the orbit
 * 0.5 the planet emerges, 1.0 the ring draws itself with a bright head
 * 1.9 the ORBIT wordmark appears, a light sweep travels across the logo
 * 2.5 "YOUR CONSOLE. RECONNECTED."
 * 3.2 the logo flies to its place in the top bar while the scene clears for Home.
 * Networking already runs in the background during all of this. */
#define INTRO_LENGTH 4.0f
#define INTRO_HANDOVER 3.2f
static int intro_reduced;
void ui_set_reduced_motion(int on) {
    intro_reduced = on;
}
float ui_intro_skip_time(void) {
    return INTRO_HANDOVER;
}
static void particles(float t, float alpha) {
    if (alpha <= 0.01f)
        return;
    unsigned seed = 0x5eed1234u;
    const float c = 0.866f, s = -0.5f, A = 74 * 1.55f, B = 74 * 0.48f;
    for (int k = 0; k < 120; k++) {
        seed = seed * 1103515245u + 12345u;
        float ang = (seed >> 8) % 6283 / 1000.0f;
        seed = seed * 1103515245u + 12345u;
        float dist = 260 + (seed >> 8) % 420;
        seed = seed * 1103515245u + 12345u;
        float delay = (seed >> 8) % 500 / 1000.0f;
        float target = (float)k / 120 * 6.2831853f;
        /* start: scattered far away; end: a point on the tilted orbit */
        float sx0 = 640 + cosf(ang) * dist, sy0 = 300 + sinf(ang) * dist * 0.6f;
        float lx = cosf(target) * A, ly = sinf(target) * B;
        float ex = 640 + lx * c + ly * s, ey = 300 - lx * s + ly * c;
        float p = ease_out((t - delay * 0.6f) / 1.1f);
        float px = sx0 + (ex - sx0) * p, py = sy0 + (ey - sy0) * p;
        float life = clamp01(t / 0.4f) * (1 - clamp01((t - 1.25f - delay * 0.4f) / 0.4f));
        if (life <= 0)
            continue;
        float X = fx(px), Y = fy(py), r = fy(1.0f + (k % 5 == 0)) + 0.6f;
        uint32_t col = k % 7 ? 0x6fa6ff : 0xc9dcff;
        for (int j = (int)(Y - r - 2); j <= (int)(Y + r + 2); j++)
            for (int i = (int)(X - r - 2); i <= (int)(X + r + 2); i++) {
                float dx = i + 0.5f - X, dy = j + 0.5f - Y, d = sqrtf(dx * dx + dy * dy);
                float cover = r - d + 0.8f;
                if (cover > 0)
                    plot_a(i, j, col, cover, alpha * life);
                else if (d < r * 3)
                    plot_a(i, j, col, 0.25f * (1 - d / (r * 3)), alpha * life);
            }
    }
}
/* diagonal light band over bright pixels only (logo and wordmark), background stays dark */
static void light_sweep(int x0, int y0, int x1, int y1, float pos) {
    int X0 = sx(x0), Y0 = sy(y0), X1 = sx(x1), Y1 = sy(y1);
    if (X0 < 0)
        X0 = 0;
    if (Y0 < 0)
        Y0 = 0;
    if (X1 > sw)
        X1 = sw;
    if (Y1 > sh)
        Y1 = sh;
    float band = fx(70), center = fx((float)x0) + (fx((float)x1) - fx((float)x0) + band * 2) * pos - band;
    for (int j = Y0; j < Y1; j++) {
        uint32_t *row = screen + j * sw;
        float shift = (j - Y0) * 0.45f;
        for (int i = X0; i < X1; i++) {
            float d = fabsf(i - (center - shift)) / band;
            if (d >= 1)
                continue;
            uint32_t p = row[i];
            unsigned lum = (p >> 16 & 255) > (p & 255) ? (p >> 16 & 255) : (p & 255);
            if (lum < 70)
                continue;
            float k = (1 - d) * (1 - d) * 0.75f * (lum / 255.0f);
            row[i] = blend(p, 0xffffff, (unsigned)(k * 255));
        }
    }
}
int ui_intro(uint32_t *pixels, int width, int height, float t, const char *version) {
    screen = pixels;
    sw = width;
    sh = height;
    intro_background();
    if (intro_reduced || perf_mode) { /* reduced motion / performance: a calm 1.2 s fade, no particles */
        float a = clamp01(t / 0.4f) * (1 - clamp01((t - 0.9f) / 0.3f));
        orbit_mark_anim(640, 280, 74, 1, 1, -1, a);
        int ww = text_width("ORBIT", 70);
        text_alpha(640 - ww / 2, 400, 70, 0xf3f5f8, "ORBIT", ww + 6, a, 1);
        return t >= 1.2f;
    }
    float hand = ease_out((t - INTRO_HANDOVER) / (INTRO_LENGTH - INTRO_HANDOVER));
    if (hand > 0 && intro_bg) { /* the stage dissolves into the Home background */
        space_background();
        unsigned k = (unsigned)((1 - hand) * 255);
        size_t n = (size_t)sw * sh;
        for (size_t i = 0; i < n; i++)
            screen[i] = blend(screen[i], intro_bg[i], k);
    }
    float scene = 1 - clamp01((t - INTRO_HANDOVER) / 0.35f); /* text and stars leave first */
    stars(t, clamp01((t - 0.2f) / 1.2f) * 0.8f * (1 - hand));
    particles(t, 1 - hand);
    /* planet + ring: centre moves from the stage to the top bar logo position (70, 50) */
    float grow = ease_out((t - 0.5f) / 0.8f);
    float reveal = ease_out((t - 1.0f) / 0.9f);
    float moon = t > 1.9f ? fmodf(0.62f + (t - 1.9f) * 0.30f, 1.0f) : -1;
    float cx = 640 + (70 - 640) * hand, cy = 280 + (50 - 280) * hand, r = 74 + (13 - 74) * hand;
    orbit_mark_anim(cx, cy, r, grow, reveal, hand > 0.5f ? -1 : moon, 1);
    /* bright head of the ring while it forms */
    if (reveal > 0.02f && reveal < 0.99f) {
        const float c = 0.866f, s = -0.5f, A = r * 1.55f, B = r * 0.48f;
        float th = reveal * 6.2831853f - 1.2f;
        float lx = cosf(th) * A, ly = sinf(th) * B;
        float hx = cx + lx * c + ly * s, hy = cy - lx * s + ly * c;
        disc(hx, hy, 5, 0xe6f0ff);
        for (int k = 1; k < 4; k++)
            disc(hx, hy, 5.0f + k * 4, blend(0x07080b, C_ACCENT, (unsigned)(60 / k)));
        disc(hx, hy, 3, 0xffffff);
    }
    /* wordmark */
    float wa = ease_out((t - 1.9f) / 0.5f) * scene;
    int ww = text_width("ORBIT", 76);
    int wy = 392 + (int)((1 - wa) * 18);
    text_alpha(640 - ww / 2, wy, 76, 0xf3f5f8, "ORBIT", ww + 6, wa, 1);
    /* light sweep across logo and wordmark */
    if (t > 2.05f && t < 2.75f && scene > 0)
        light_sweep(640 - 260, 170, 640 + 260, 490, (t - 2.05f) / 0.7f);
    /* tagline, spaced capitals */
    float ta = ease_out((t - 2.5f) / 0.55f) * scene;
    const char *tag = T(T_INTRO_RECONNECTED);
    int tw = text_width(tag, 22);
    text_alpha(640 - tw / 2, 500, 22, 0x9cc0ff, tag, tw + 4, ta, 0);
    float va = clamp01((t - 2.8f) / 0.4f) * scene;
    char v[32];
    snprintf(v, sizeof(v), "%s %s", T(T_VERSION_WORD), version);
    int vw = text_width(v, 13);
    text_alpha(640 - vw / 2, 640, 13, 0x636a74, v, vw + 4, va, 0);
    return t >= INTRO_LENGTH;
}
/* GitHub social preview (render at 1280x720, the top/bottom 40 px are cropped to 1280x640). */
void ui_social(uint32_t *pixels, int width, int height, const char *version) {
    screen = pixels;
    sw = width;
    sh = height;
    intro_background();
    stars(1.7f, 1.0f);
    orbit_mark_anim(300, 340, 118, 1.0f, 1.0f, 0.70f, 1.0f);
    bold(560, 158, 120, C_TEXT, "ORBIT", 700);
    text(566, 300, 32, 0xc9d3e6, "Text & voice chat for PS3", 680);
    text(566, 344, 22, C_MUTED, "Groups · voice chat · one invite code · TLS-encrypted", 680);
    const char *chips[] = {"No PSN", "No account", "No router setup", "PS4 coming"};
    const uint32_t dots[] = {C_OK, C_OK, C_OK, C_WARN};
    int x = 566;
    for (int i = 0; i < 4; i++) {
        int w = text_width(chips[i], 18) + 44;
        rrect(x, 412, w, 40, 20, 0x1a2233);
        disc((float)x + 20, 432, 5, dots[i]);
        text(x + 32, 420, 18, 0xdfe6f2, chips[i], w);
        x += w + 12;
    }
    char v[48];
    snprintf(v, sizeof(v), "v%s  ·  homebrew for CFW/HEN", version);
    text(566, 490, 16, 0x636a74, v, 600);
    int bx = 566;
    for (int k = 0; k < 4; k++) /* the four face buttons as a small signature */
        face_button((float)bx + 18 + k * 44, 560, 32, k);
}
void ui_intro_done(void) {
    free(intro_bg);
    intro_bg = NULL;
    intro_w = intro_h = 0;
}
void ui_icon(uint32_t *pixels, int width, int height) {
    screen = pixels;
    sw = width;
    sh = height;
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            float t = (float)(i + j) / (float)(sw + sh);
            screen[j * sw + i] = blend(0x0d0f13, 0x16223a, (unsigned)(t * 255));
        }
    orbit_mark(360, 360, 150);
    bold(640, 250, 170, C_TEXT, "orbit", 640);
    text(650, 440, 50, C_MUTED, "Chat für PS3", 600);
}
void ui_audio_probe(uint32_t *pixels, int width, int height, const char *status,
                    const char rows[][100], int count, int player, int remaining, int level) {
    screen = pixels;
    sw = width;
    sh = height;
    fill_px(0, 0, sw, sh, C_BG);
    orbit_mark(80, 60, 18);
    bold(110, 44, 24, C_TEXT, "Orbit Audiotest", 600);
    rrect(64, 120, 730, 440, 18, C_SURF);
    if (!count)
        wrapped(90, 150, 20, C_TEXT,
                "Wähle dein Headset in den PS3-Einstellungen. Der Test liest 12 Sekunden lang "
                "echte Mikrofon-Daten.",
                670, 5, 8);
    for (int i = 0; i < count && i < 12; i++)
        text(90, 146 + i * 32, 17, C_TEXT, rows[i], 676);
    rrect(820, 120, 396, 440, 18, C_SURF);
    char value[80];
    snprintf(value, sizeof(value), "Gerät %d", player);
    bold(846, 150, 22, C_TEXT, value, 340);
    if (level >= 0) {
        if (level > 100)
            level = 100;
        text(846, 220, 14, C_DIM, "PEGEL", 338);
        rrect(846, 246, 340, 14, 7, C_SURF2);
        if (level > 1)
            rrect(846, 246, 340 * level / 100, 14, 7, C_ACCENT);
    }
    snprintf(value, sizeof(value), remaining ? "Noch %d s" : "Bereit", remaining);
    text(846, 300, 20, C_ACCENT, value, 338);
    wrapped(64, 584, 18, C_MUTED, status, 1150, 2, 6);
    footer_bar();
    int x = hint(56, 678, K_CROSS, NULL, "Testen");
    x = hint(x, 678, -3, NULL, "Gerät");
    x = hint(x, 678, K_CIRCLE, NULL, "Stoppen");
    hint(x, 678, -1, "START", "Beenden");
}
static void perf_line(int y, const char *label, const char *value, uint32_t c) {
    text(940, y, 13, C_MUTED, label, 130);
    text(1076, y, 13, c, value, 150);
}
void ui_performance(const UiPerf *p) {
    char v[64];
    int fps_low = p->frame_low1_us > 0 ? 1000000 / p->frame_low1_us : 0;
    rrect_a(928, 360, 312, 300, 14, 0x000000, 200);
    int y = 372;
    snprintf(v, sizeof(v), "%d", p->fps);
    perf_line(y, "FPS", v, p->fps >= 58 ? C_OK : p->fps >= 45 ? C_WARN : C_BAD);
    y += 22;
    snprintf(v, sizeof(v), "%d.%d ms", p->frame_avg_us / 1000, p->frame_avg_us / 100 % 10);
    perf_line(y, "Frame avg", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%d FPS (%d.%d ms)", fps_low, p->frame_low1_us / 1000, p->frame_low1_us / 100 % 10);
    perf_line(y, "1% low", v, fps_low >= 55 ? C_OK : fps_low >= 30 ? C_WARN : C_BAD);
    y += 22;
    snprintf(v, sizeof(v), "%d.%d / %d.%d ms", p->render_us / 1000, p->render_us / 100 % 10,
             p->render_avg_us / 1000, p->render_avg_us / 100 % 10);
    perf_line(y, "UI last/avg", v, p->render_us <= 16667 ? C_OK : C_WARN);
    y += 22;
    snprintf(v, sizeof(v), "%d.%d ms", p->render_worst_us / 1000, p->render_worst_us / 100 % 10);
    perf_line(y, "UI worst", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%d.%d ms", p->present_us / 1000, p->present_us / 100 % 10);
    perf_line(y, "Present", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%d/s", p->renders);
    perf_line(y, "Redraws", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%d pad  %d data  %d anim", p->by_input, p->by_data, p->by_anim);
    perf_line(y, "Reason", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%dx%d", p->draw_w, p->draw_h);
    perf_line(y, "Drawn at", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%dx%d", p->out_w, p->out_h);
    perf_line(y, "Output", v, C_TEXT);
    y += 22;
    snprintf(v, sizeof(v), "%d KB", p->cache_kb);
    perf_line(y, "Image cache", v, C_TEXT);
    y += 22;
    perf_line(y, "GPU", "scan-out + flip", C_DIM);
}
int ui_cache_kb(void) {
    return (int)(scaled_pixels * 4u / 1024u) + (space_bg ? space_w * space_h * 4 / 1024 : 0);
}
static inline uint32_t avg2(uint32_t a, uint32_t b) {
    return ((a & 0xfefefeu) >> 1) + ((b & 0xfefefeu) >> 1);
}
static void expand_row_3_2(const uint32_t *s, int w, uint32_t *d) {
    for (int i = 0; i + 1 < w; i += 2) {
        uint32_t a = s[i], b = s[i + 1];
        d[0] = a;
        d[1] = avg2(a, b);
        d[2] = b;
        d += 3;
    }
}
void ui_upscale_3_2(const uint32_t *src, int w, int h, uint32_t *dst, int dst_pitch) {
    static uint32_t ra[3840], rb[3840], rm[3840];
    int dw = w * 3 / 2;
    if (dw > 3840)
        return;
    for (int j = 0; j + 1 < h; j += 2) {
        expand_row_3_2(src + j * w, w, ra);
        expand_row_3_2(src + (j + 1) * w, w, rb);
        for (int i = 0; i < dw; i++)
            rm[i] = avg2(ra[i], rb[i]);
        uint32_t *d = dst + (size_t)(j * 3 / 2) * dst_pitch;
        memcpy(d, ra, (size_t)dw * 4);
        memcpy(d + dst_pitch, rm, (size_t)dw * 4);
        memcpy(d + 2 * dst_pitch, rb, (size_t)dw * 4);
    }
}
