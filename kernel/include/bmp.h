#ifndef GOS_BMP_H
#define GOS_BMP_H

#include <stdint.h>

/* Minimal hand-rolled BMP decoder (extracted from Milestone 15.3's wallpaper
 * loader so Milestone 24.3's image viewer can reuse it). Accepts only the
 * exact shape the build system bundles - BITMAPINFOHEADER, 24bpp, BI_RGB
 * (uncompressed), positive height (bottom-up rows). Anything else is
 * rejected (0 return, reason logged to serial).
 *
 * On success, *out_pixels is a freshly kmalloc'd array of w*h screen-format
 * uint32_t pixels (top-down row order, one fb_make_color() call already
 * applied per pixel) - the caller owns it and must kfree() it. Returns 1 on
 * success, 0 on failure (nothing allocated). */
int bmp_decode(const uint8_t *buf, uint32_t len, uint32_t **out_pixels, uint64_t *out_w, uint64_t *out_h);

#endif
