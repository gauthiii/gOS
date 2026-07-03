#include <wallpaper.h>
#include <fb.h>
#include <fat32.h>
#include <heap.h>
#include <serial.h>
#include <bmp.h>
#include <stdint.h>

/* Index 0 (gradient) has no file; NULL is only ever dereferenced after
 * checking idx > 0 below.
 *
 * NOTE: the "Custom"/"Mac" labels are intentionally paired with the
 * opposite-sounding file (MAC.BMP under "Custom", CUSTOM.BMP under "Mac")
 * - a patch-v2 fix for a mixup where the two bundled images ended up
 * associated with the wrong menu label. The on-disk file NAMES were kept
 * as originally bundled; only this mapping changed. See phase-patchv2.md. */
static const char *wallpaper_paths[WALLPAPER_OPTION_COUNT] = {
    0, "WALLPAPR.BMP", "MAC.BMP", "CUSTOM.BMP", "WINDOWS.BMP"
};
static const char *wallpaper_labels[WALLPAPER_OPTION_COUNT] = {
    "Gradient", "Default", "Custom", "Mac", "Windows"
};

/* Converted, screen-format pixels (fb_make_color applied once at load
 * time), img_w * img_h entries, top-down row order. NULL until a BMP loads
 * successfully (including whenever the gradient is selected). */
static uint32_t *image_pixels;
static uint64_t img_w, img_h;
static int current_selection = 0;

static int load_option(int idx) {
    const char *path = wallpaper_paths[idx];
    struct fat_dirent ent;
    if (!fat_resolve_path(path, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("Wallpaper: ");
        serial_write_string(path);
        serial_write_string(" not found\n");
        return 0;
    }
    uint8_t *buf = (uint8_t *)kmalloc(ent.size);
    if (!buf) {
        serial_write_string("Wallpaper: kmalloc for file buffer failed\n");
        return 0;
    }
    uint32_t *pixels = 0;
    uint64_t w = 0, h = 0;
    int64_t got = fat_read_file(path, buf, ent.size);
    int ok = (got == (int64_t)ent.size) && bmp_decode(buf, ent.size, &pixels, &w, &h);
    kfree(buf);
    if (!ok) {
        serial_write_string("Wallpaper: failed to decode ");
        serial_write_string(path);
        serial_write_string("\n");
        return 0;
    }

    if (image_pixels) {
        kfree(image_pixels);
    }
    image_pixels = pixels;
    img_w = w;
    img_h = h;
    serial_write_string("Wallpaper: loaded ");
    serial_write_string(path);
    serial_write_string(" (");
    serial_write_uint(img_w);
    serial_write_string("x");
    serial_write_uint(img_h);
    serial_write_string(", 24bpp BMP)\n");
    return 1;
}

void wallpaper_select(int idx) {
    if (idx < 0 || idx >= WALLPAPER_OPTION_COUNT) {
        serial_write_string("Wallpaper: wallpaper_select() ignored - index out of range\n");
        return;
    }
    if (idx == 0) {
        if (image_pixels) {
            kfree(image_pixels);
            image_pixels = 0;
        }
        current_selection = 0;
        serial_write_string("Wallpaper: selected Gradient\n");
        return;
    }
    if (load_option(idx)) {
        current_selection = idx;
    } else {
        serial_write_string("Wallpaper: selection unchanged - using gradient fallback\n");
        if (image_pixels) {
            kfree(image_pixels);
            image_pixels = 0;
        }
        current_selection = 0;
    }
}

void wallpaper_init(void) {
    /* Milestone 15.3's original default: try the bundled wallpaper (option
     * 1), fall back to the gradient (option 0) if it's missing/malformed.
     * settings_load() (Milestone 22.3) may immediately override this with
     * whatever was last persisted. */
    wallpaper_select(1);
}

int wallpaper_image_loaded(void) {
    return image_pixels != 0;
}

int wallpaper_current_selection(void) {
    return current_selection;
}

const char *wallpaper_option_label(int idx) {
    if (idx < 0 || idx >= WALLPAPER_OPTION_COUNT) {
        return "?";
    }
    return wallpaper_labels[idx];
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

    if (!image_pixels) {
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
