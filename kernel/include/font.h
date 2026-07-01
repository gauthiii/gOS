#ifndef GOS_FONT_H
#define GOS_FONT_H

#include <stdint.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 8

void fb_draw_char(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg);

/* bg is only used if draw_bg is non-zero; pass 0 to draw text with a
 * transparent background (leaving whatever was already drawn showing
 * through between glyphs' background pixels). */
void fb_draw_char_ex(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg, int draw_bg);
void fb_draw_string(uint64_t x, uint64_t y, const char *str, uint32_t fg, uint32_t bg);

/* Like fb_draw_string, but every glyph pixel outside [clip_x, clip_x+clip_w)
 * x [clip_y, clip_y+clip_h) is skipped - used to keep window titles/labels
 * from drawing outside their window's bounds. */
void fb_draw_string_clipped(uint64_t x, uint64_t y, const char *str, uint32_t fg, uint32_t bg,
                             int64_t clip_x, int64_t clip_y, uint64_t clip_w, uint64_t clip_h);

#endif
