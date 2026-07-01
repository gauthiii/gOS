#ifndef GOS_FB_H
#define GOS_FB_H

#include <stdint.h>

void fb_init(void *address, uint64_t width, uint64_t height, uint64_t pitch, uint16_t bpp,
             uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift);

/* Returns 1 once fb_init() has run, 0 before that - used by the panic
 * screen (Milestone 11.1) to avoid touching framebuffer state that may
 * not exist yet if an exception happens very early in boot. */
int fb_is_ready(void);

uint64_t fb_width(void);
uint64_t fb_height(void);

/* Packs 8-bit R/G/B components into this framebuffer's actual pixel format,
 * using the channel shifts Limine reported - so colors are correct
 * regardless of whether the underlying mode is RGBX, BGRX, etc. */
uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b);

void fb_put_pixel(uint64_t x, uint64_t y, uint32_t color);
void fb_clear(uint32_t color);

void fb_draw_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void fb_draw_rect_outline(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color, uint64_t thickness);
void fb_draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t color);

/* Double buffering (Milestone 5.3). All draw calls above target the back
 * buffer once this is initialized; fb_flip() copies it to the real
 * framebuffer. Before fb_backbuffer_init() is called, draw calls go
 * directly to the real framebuffer (Milestone 5.1/5.2 behavior). */
void fb_backbuffer_init(void);
void fb_flip(void);

/* Points all subsequent drawing directly at the real framebuffer, bypassing
 * the back buffer entirely, and stays that way permanently. Used only by
 * the panic screen (Milestone 11.1): a panic must appear immediately and
 * reliably without depending on a future fb_flip() ever being called
 * again (the CPU is about to halt for good). */
void fb_panic_reset_to_real(void);

#endif
