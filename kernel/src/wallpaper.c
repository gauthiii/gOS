#include <wallpaper.h>
#include <fb.h>
#include <fat32.h>
#include <heap.h>
#include <serial.h>
#include <bmp.h>
#include <stdint.h>

#define WALLPAPER_PATH "WALLPAPR.BMP"

/* Converted, screen-format pixels (fb_make_color applied once at load
 * time), img_w * img_h entries, top-down row order. NULL until a BMP loads
 * successfully. */
static uint32_t *image_pixels;
static uint64_t img_w, img_h;
static int gradient_forced = 0;

void wallpaper_init(void) {
    struct fat_dirent ent;
    if (!fat_resolve_path(WALLPAPER_PATH, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("Wallpaper: " WALLPAPER_PATH " not found - using gradient fallback\n");
        return;
    }
    uint8_t *buf = (uint8_t *)kmalloc(ent.size);
    if (!buf) {
        serial_write_string("Wallpaper: kmalloc for file buffer failed - using gradient fallback\n");
        return;
    }
    int64_t got = fat_read_file(WALLPAPER_PATH, buf, ent.size);
    if (got != (int64_t)ent.size || !bmp_decode(buf, ent.size, &image_pixels, &img_w, &img_h)) {
        serial_write_string("Wallpaper: load failed - using gradient fallback\n");
        kfree(buf);
        return;
    }
    kfree(buf); /* pixels were converted into image_pixels; raw file no longer needed */
    serial_write_string("Wallpaper: loaded " WALLPAPER_PATH " (");
    serial_write_uint(img_w);
    serial_write_string("x");
    serial_write_uint(img_h);
    serial_write_string(", 24bpp BMP)\n");
}

int wallpaper_image_loaded(void) {
    return image_pixels != 0;
}

void wallpaper_set_gradient_forced(int forced) {
    gradient_forced = forced ? 1 : 0;
}

int wallpaper_is_gradient_forced(void) {
    return gradient_forced;
}

/* Milestone 15.2 fallback / base layer: vertical gradient from deep blue
 * (top) to teal (bottom), interpolated per scanline. */
static void render_gradient(void) {
    uint64_t h = fb_height();
    uint64_t w = fb_width();
    for (uint64_t y = 0; y < h; y++) {
        uint8_t r = (uint8_t)(18 + (32 - 18) * y / (h - 1));
        uint8_t g = (uint8_t)(42 + (120 - 42) * y / (h - 1));
        uint8_t b = (uint8_t)(66 + (130 - 66) * y / (h - 1));
        fb_draw_rect(0, y, w, 1, fb_make_color(r, g, b));
    }
}

void wallpaper_render(void) {
    uint64_t sw = fb_width();
    uint64_t sh = fb_height();

    if (!image_pixels || gradient_forced) {
        render_gradient();
        return;
    }

    /* If the image doesn't cover the whole screen, letterbox it over the
     * gradient (centered, clipped at the edges if larger). */
    if (img_w < sw || img_h < sh) {
        render_gradient();
    }
    int64_t off_x = ((int64_t)sw - (int64_t)img_w) / 2;
    int64_t off_y = ((int64_t)sh - (int64_t)img_h) / 2;
    for (uint64_t y = 0; y < img_h; y++) {
        int64_t sy = off_y + (int64_t)y;
        if (sy < 0 || sy >= (int64_t)sh) {
            continue;
        }
        const uint32_t *row = image_pixels + y * img_w;
        for (uint64_t x = 0; x < img_w; x++) {
            int64_t sx = off_x + (int64_t)x;
            if (sx < 0 || sx >= (int64_t)sw) {
                continue;
            }
            fb_put_pixel((uint64_t)sx, (uint64_t)sy, row[x]);
        }
    }
}
