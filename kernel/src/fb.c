#include <fb.h>
#include <heap.h>
#include <serial.h>

static uint8_t *fb_addr;
static uint64_t fb_w, fb_h, fb_pitch;
static uint16_t fb_bpp;
static uint8_t fb_red_shift, fb_green_shift, fb_blue_shift;

static uint8_t *draw_target; /* points at fb_addr until a back buffer exists */
static uint8_t *back_buffer;
static int fb_ready = 0;

void fb_init(void *address, uint64_t width, uint64_t height, uint64_t pitch, uint16_t bpp,
             uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift) {
    fb_addr = (uint8_t *)address;
    fb_w = width;
    fb_h = height;
    fb_pitch = pitch;
    fb_bpp = bpp;
    fb_red_shift = red_shift;
    fb_green_shift = green_shift;
    fb_blue_shift = blue_shift;
    draw_target = fb_addr;
    fb_ready = 1;

    serial_write_string("FB: initialized ");
    serial_write_uint(fb_w);
    serial_write_string("x");
    serial_write_uint(fb_h);
    serial_write_string(", ");
    serial_write_uint(fb_bpp);
    serial_write_string("bpp, pitch=");
    serial_write_uint(fb_pitch);
    serial_write_string("\n");
}

int fb_is_ready(void) { return fb_ready; }

uint64_t fb_width(void) { return fb_w; }
uint64_t fb_height(void) { return fb_h; }

uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb_red_shift) | ((uint32_t)g << fb_green_shift) | ((uint32_t)b << fb_blue_shift);
}

void fb_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb_w || y >= fb_h) {
        return;
    }
    uint32_t *px = (uint32_t *)(draw_target + y * fb_pitch + x * (fb_bpp / 8));
    *px = color;
}

void fb_clear(uint32_t color) {
    for (uint64_t y = 0; y < fb_h; y++) {
        uint32_t *row = (uint32_t *)(draw_target + y * fb_pitch);
        for (uint64_t x = 0; x < fb_w; x++) {
            row[x] = color;
        }
    }
}

void fb_draw_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    uint64_t x_end = x + w > fb_w ? fb_w : x + w;
    uint64_t y_end = y + h > fb_h ? fb_h : y + h;
    for (uint64_t py = y; py < y_end; py++) {
        for (uint64_t px = x; px < x_end; px++) {
            fb_put_pixel(px, py, color);
        }
    }
}

void fb_draw_rect_outline(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color, uint64_t thickness) {
    fb_draw_rect(x, y, w, thickness, color);                         /* top */
    fb_draw_rect(x, y + h - thickness, w, thickness, color);         /* bottom */
    fb_draw_rect(x, y, thickness, h, color);                         /* left */
    fb_draw_rect(x + w - thickness, y, thickness, h, color);         /* right */
}

static int64_t abs64(int64_t v) {
    return v < 0 ? -v : v;
}

void fb_draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t color) {
    int64_t dx = abs64(x1 - x0);
    int64_t dy = -abs64(y1 - y0);
    int64_t sx = x0 < x1 ? 1 : -1;
    int64_t sy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            fb_put_pixel((uint64_t)x0, (uint64_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int64_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fb_backbuffer_init(void) {
    uint64_t size = fb_pitch * fb_h;
    back_buffer = (uint8_t *)kmalloc(size);
    if (!back_buffer) {
        serial_write_string("PANIC: fb_backbuffer_init failed to allocate back buffer\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }
    for (uint64_t i = 0; i < size; i++) {
        back_buffer[i] = 0;
    }
    draw_target = back_buffer;

    serial_write_string("FB: back buffer allocated (");
    serial_write_uint(size / 1024);
    serial_write_string(" KiB)\n");
}

void fb_flip(void) {
    if (!back_buffer) {
        return;
    }
    uint64_t size = fb_pitch * fb_h;
    uint64_t *src = (uint64_t *)back_buffer;
    uint64_t *dst = (uint64_t *)fb_addr;
    uint64_t words = size / 8;
    for (uint64_t i = 0; i < words; i++) {
        dst[i] = src[i];
    }
}

void fb_panic_reset_to_real(void) {
    draw_target = fb_addr;
}
