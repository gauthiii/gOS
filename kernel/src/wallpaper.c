#include <wallpaper.h>
#include <fb.h>
#include <fat32.h>
#include <heap.h>
#include <serial.h>
#include <stdint.h>

#define WALLPAPER_PATH "WALLPAPR.BMP"

/* Converted, screen-format pixels (fb_make_color applied once at load
 * time), img_w * img_h entries, top-down row order. NULL until a BMP loads
 * successfully. */
static uint32_t *image_pixels;
static uint64_t img_w, img_h;

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_s32(const uint8_t *p) {
    return (int32_t)read_u32(p);
}

/* Minimal hand-rolled BMP loader (Milestone 15.3): accepts only the exact
 * shape the build system bundles - BITMAPINFOHEADER, 24bpp, BI_RGB
 * (uncompressed), positive height (bottom-up rows). Anything else is
 * rejected with a logged reason and the gradient fallback stays active. */
static int load_bmp(const uint8_t *buf, uint32_t len) {
    if (len < 54) {
        serial_write_string("Wallpaper: file too small for BMP headers\n");
        return 0;
    }
    if (buf[0] != 'B' || buf[1] != 'M') {
        serial_write_string("Wallpaper: missing 'BM' magic\n");
        return 0;
    }
    uint32_t pixel_offset = read_u32(buf + 10);
    uint32_t header_size = read_u32(buf + 14);
    int32_t w = read_s32(buf + 18);
    int32_t h = read_s32(buf + 22);
    uint16_t planes = read_u16(buf + 26);
    uint16_t bpp = read_u16(buf + 28);
    uint32_t compression = read_u32(buf + 30);

    if (header_size < 40 || planes != 1 || bpp != 24 || compression != 0 || w <= 0 || h <= 0) {
        serial_write_string("Wallpaper: unsupported BMP variant (need 24bpp uncompressed, bottom-up)\n");
        return 0;
    }

    uint64_t row_stride = (((uint64_t)w * 3) + 3) & ~3ULL; /* rows pad to 4 bytes */
    if ((uint64_t)pixel_offset + row_stride * (uint64_t)h > len) {
        serial_write_string("Wallpaper: BMP pixel data truncated\n");
        return 0;
    }

    uint32_t *pixels = (uint32_t *)kmalloc((uint64_t)w * (uint64_t)h * sizeof(uint32_t));
    if (!pixels) {
        serial_write_string("Wallpaper: kmalloc for pixel buffer failed\n");
        return 0;
    }

    for (int32_t y = 0; y < h; y++) {
        /* BMP rows are stored bottom-up; flip to top-down while converting. */
        const uint8_t *row = buf + pixel_offset + row_stride * (uint64_t)(h - 1 - y);
        uint32_t *out = pixels + (uint64_t)y * (uint64_t)w;
        for (int32_t x = 0; x < w; x++) {
            const uint8_t *px = row + (uint64_t)x * 3; /* BGR order on disk */
            out[x] = fb_make_color(px[2], px[1], px[0]);
        }
    }

    image_pixels = pixels;
    img_w = (uint64_t)w;
    img_h = (uint64_t)h;
    return 1;
}

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
    if (got != (int64_t)ent.size || !load_bmp(buf, ent.size)) {
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
