#ifndef ORBIT_IMAGE_H
#define ORBIT_IMAGE_H
/* Controlled image decoding for untrusted files (ICON0.PNG, later covers and USB images).
 * Own strict PNG reader on top of zlib: every chunk CRC-checked, fixed output buffer
 * (decompression bombs cannot grow it), hard limits on file size, dimensions and pixels. */
#include <stddef.h>
#include <stdint.h>
#define IMG_MAX_FILE (4u * 1024u * 1024u)
#define IMG_MAX_SIDE 2048
#define IMG_MAX_PIXELS (2048u * 2048u)
enum { IMG_OK = 0, IMG_E_ARG = -1, IMG_E_FORMAT = -2, IMG_E_LIMIT = -3, IMG_E_MEMORY = -4,
       IMG_E_DATA = -5, IMG_E_UNSUPPORTED = -6, IMG_E_IO = -7 };
typedef struct {
    int w, h;
    uint32_t *px; /* 0xAARRGGBB, row-major, w*h */
} OrbitImage;
/* max_w/max_h: caller limits (<= IMG_MAX_SIDE). Returns IMG_OK or a negative IMG_E_* code. */
int image_png_decode(const uint8_t *data, size_t len, OrbitImage *out, int max_w, int max_h);
int image_load_png(const char *path, OrbitImage *out, int max_w, int max_h);
/* area-average down/up-scale into a new image of w x h (aspect not preserved) */
int image_scale(const OrbitImage *src, OrbitImage *dst, int w, int h);
/* scale to cover w x h (crop the overflow, keep aspect) */
int image_cover(const OrbitImage *src, OrbitImage *dst, int w, int h);
/* anchor: 0 centre, 1 top/left, 2 bottom/right */
int image_cover_at(const OrbitImage *src, OrbitImage *dst, int w, int h, int anchor);
void image_free(OrbitImage *img);
/* 1.2: JPEG (src/jpeg.c) and format sniffing for pictures the user brings (USB, phone).
 * Big pictures come back at 1/8 size (see jpeg.c); max_pixels <= IMG_MAX_PIXELS (0 = default). */
#define IMG_MAX_UPLOAD (16u * 1024u * 1024u)
int image_jpeg_decode(const uint8_t *data, size_t len, OrbitImage *out, int max_pixels);
int image_gif_first(const uint8_t *data, size_t len, OrbitImage *out);
int image_decode_any(const uint8_t *data, size_t len, OrbitImage *out);
int image_load_any(const char *path, OrbitImage *out);
#endif
