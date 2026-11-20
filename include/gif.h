/* ORBIT NEXT: strict GIF reader for untrusted files (profile pictures, server icons).
 *
 * - Checks the signature and walks the whole block structure; truncated or malformed files,
 *   frames outside the canvas, bad LZW codes and short image data are refused.
 * - Hard limits: file size, canvas side and area, source frame count and the total number of
 *   decoded pixels (CPU budget), so neither huge canvases nor thousands of tiny frames get through.
 * - LZW output is written into a buffer of exactly the frame size: a decompression bomb cannot
 *   make it grow. Extensions (comments, application data, XMP) are skipped and never kept.
 * - Disposal 0-3 and transparency are handled on a fixed canvas (0xAARRGGBB).
 * The caller receives every composited frame through a callback and keeps only what it needs. */
#ifndef ORBIT_GIF_H
#define ORBIT_GIF_H
#include <stddef.h>
#include <stdint.h>

#define GIF_MAX_FILE (5u * 1024u * 1024u)
#define GIF_MAX_SIDE 1024
#define GIF_MAX_AREA (1024u * 1024u)
#define GIF_MAX_SRC_FRAMES 1000
#define GIF_MAX_WORK (48u * 1024u * 1024u) /* sum of decoded frame pixels */

/* frame: composited canvas (w x h, 0xAARRGGBB), shown for delay_ms (browsers' rule: < 20 ms
 * counts as 100 ms). Return non-zero to stop decoding early (counts as success). */
typedef int (*gif_frame_fn)(void *ctx, const uint32_t *canvas, int w, int h, int index, int delay_ms);

/* Returns the number of frames delivered (>= 1) or a negative IMG_E_* code (image.h). */
int gif_decode(const uint8_t *data, size_t len, gif_frame_fn fn, void *ctx);
/* 1 if the data starts like a GIF */
int gif_sniff(const uint8_t *data, size_t len);
#endif
