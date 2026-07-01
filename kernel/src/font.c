#include <font.h>
#include <fb.h>

/* Public-domain 8x8 bitmap font (Daniel Hepper / Marcel Sondaar / IBM VGA
 * fonts), covering U+0000-U+007F (basic Latin). See font8x8_basic.h for
 * full license/attribution. */
#include "font8x8_basic.h"

void fb_draw_char_ex(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg, int draw_bg) {
    uint8_t index = (uint8_t)c;
    if (index > 127) {
        index = '?'; /* font8x8_basic only covers 0-127 */
    }
    const char *glyph = font8x8_basic[index];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = (uint8_t)glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int set = (bits >> col) & 1; /* bit 0 = leftmost pixel in this font */
            if (set) {
                fb_put_pixel(x + col, y + row, fg);
            } else if (draw_bg) {
                fb_put_pixel(x + col, y + row, bg);
            }
        }
    }
}

void fb_draw_char(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg) {
    fb_draw_char_ex(x, y, c, fg, bg, 1);
}

void fb_draw_string(uint64_t x, uint64_t y, const char *str, uint32_t fg, uint32_t bg) {
    uint64_t cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += FONT_HEIGHT;
            str++;
            continue;
        }
        fb_draw_char(cx, y, *str, fg, bg);
        cx += FONT_WIDTH;
        str++;
    }
}

static void fb_draw_char_clipped(int64_t x, int64_t y, char c, uint32_t fg, uint32_t bg,
                                  int64_t clip_x, int64_t clip_y, uint64_t clip_w, uint64_t clip_h) {
    uint8_t index = (uint8_t)c;
    if (index > 127) {
        index = '?';
    }
    const char *glyph = font8x8_basic[index];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        int64_t py = y + row;
        if (py < clip_y || py >= clip_y + (int64_t)clip_h) {
            continue;
        }
        uint8_t bits = (uint8_t)glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int64_t px = x + col;
            if (px < clip_x || px >= clip_x + (int64_t)clip_w) {
                continue;
            }
            int set = (bits >> col) & 1;
            fb_put_pixel((uint64_t)px, (uint64_t)py, set ? fg : bg);
        }
    }
}

void fb_draw_string_clipped(uint64_t x, uint64_t y, const char *str, uint32_t fg, uint32_t bg,
                             int64_t clip_x, int64_t clip_y, uint64_t clip_w, uint64_t clip_h) {
    int64_t cx = (int64_t)x;
    int64_t cy = (int64_t)y;
    while (*str) {
        if (*str == '\n') {
            cx = (int64_t)x;
            cy += FONT_HEIGHT;
            str++;
            continue;
        }
        fb_draw_char_clipped(cx, cy, *str, fg, bg, clip_x, clip_y, clip_w, clip_h);
        cx += FONT_WIDTH;
        str++;
    }
}
