#include <bmp.h>
#include <fb.h>
#include <heap.h>
#include <serial.h>

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_s32(const uint8_t *p) {
    return (int32_t)read_u32(p);
}

int bmp_decode(const uint8_t *buf, uint32_t len, uint32_t **out_pixels, uint64_t *out_w, uint64_t *out_h) {
    if (len < 54) {
        serial_write_string("BMP: file too small for headers\n");
        return 0;
    }
    if (buf[0] != 'B' || buf[1] != 'M') {
        serial_write_string("BMP: missing 'BM' magic\n");
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
        serial_write_string("BMP: unsupported variant (need 24bpp uncompressed, bottom-up)\n");
        return 0;
    }

    uint64_t row_stride = (((uint64_t)w * 3) + 3) & ~3ULL; /* rows pad to 4 bytes */
    if ((uint64_t)pixel_offset + row_stride * (uint64_t)h > len) {
        serial_write_string("BMP: pixel data truncated\n");
        return 0;
    }

    uint32_t *pixels = (uint32_t *)kmalloc((uint64_t)w * (uint64_t)h * sizeof(uint32_t));
    if (!pixels) {
        serial_write_string("BMP: kmalloc for pixel buffer failed\n");
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

    *out_pixels = pixels;
    *out_w = (uint64_t)w;
    *out_h = (uint64_t)h;
    return 1;
}
