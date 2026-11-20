/* ORBIT emoji: 44 original, procedurally drawn pictures for common Unicode emoji.
 * No third-party emoji artwork is used. Messages carry the standard UTF-8 code points, so
 * other clients see normal emoji; ORBIT draws its own pictures for them. */
#ifndef ORBIT_EMOJI_H
#define ORBIT_EMOJI_H
#include <stdint.h>
#define EMOJI_COUNT 44
/* index of the picture for a code point, -1 if ORBIT has none */
int emoji_find(uint32_t cp);
uint32_t emoji_codepoint(int index);
/* the UTF-8 text of an emoji (static storage) */
const char *emoji_utf8(int index);
const char *emoji_name(int index);
/* renders size x size pixels, 0xAARRGGBB with straight alpha */
void emoji_render(int index, int size, uint32_t *argb);
/* code points that only modify the previous emoji (variation selector, zero-width joiner) */
int emoji_is_modifier(uint32_t cp);
#endif
