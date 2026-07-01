#include <panic.h>
#include <fb.h>
#include <font.h>
#include <serial.h>

static void hex_to_str(uint64_t val, char *out) {
    const char *digits = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        uint64_t shift = (15 - i) * 4;
        out[2 + i] = digits[(val >> shift) & 0xF];
    }
    out[18] = '\0';
}

static void concat(char *dst, int *pos, const char *src) {
    while (*src) {
        dst[(*pos)++] = *src++;
    }
}

void panic_screen(const char *exception_name, uint64_t vector, uint64_t error_code,
                   uint64_t rip, int has_cr2, uint64_t cr2) {
    serial_write_string("\n!!! KERNEL PANIC - drawing panic screen !!!\n");

    if (!fb_is_ready()) {
        serial_write_string("(framebuffer not initialized yet - panic screen skipped, serial log above is the only record)\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    fb_panic_reset_to_real();

    uint32_t red = fb_make_color(180, 20, 20);
    uint32_t white = fb_make_color(255, 255, 255);
    fb_clear(red);

    char hexbuf[19];
    char line[96];
    int pos;
    int64_t y = 40;
    int64_t x = 40;

    fb_draw_string(x, y, "*** KERNEL PANIC ***", white, red);
    y += FONT_HEIGHT * 2;

    pos = 0;
    concat(line, &pos, "Exception: ");
    concat(line, &pos, exception_name);
    line[pos] = '\0';
    fb_draw_string(x, y, line, white, red);
    y += FONT_HEIGHT + 4;

    hex_to_str(vector, hexbuf);
    pos = 0;
    concat(line, &pos, "Vector: ");
    concat(line, &pos, hexbuf);
    line[pos] = '\0';
    fb_draw_string(x, y, line, white, red);
    y += FONT_HEIGHT + 4;

    hex_to_str(error_code, hexbuf);
    pos = 0;
    concat(line, &pos, "Error code: ");
    concat(line, &pos, hexbuf);
    line[pos] = '\0';
    fb_draw_string(x, y, line, white, red);
    y += FONT_HEIGHT + 4;

    hex_to_str(rip, hexbuf);
    pos = 0;
    concat(line, &pos, "RIP: ");
    concat(line, &pos, hexbuf);
    line[pos] = '\0';
    fb_draw_string(x, y, line, white, red);
    y += FONT_HEIGHT + 4;

    if (has_cr2) {
        hex_to_str(cr2, hexbuf);
        pos = 0;
        concat(line, &pos, "CR2 (faulting address): ");
        concat(line, &pos, hexbuf);
        line[pos] = '\0';
        fb_draw_string(x, y, line, white, red);
        y += FONT_HEIGHT + 4;
    }

    y += FONT_HEIGHT;
    fb_draw_string(x, y, "System halted. See serial log for full details.", white, red);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
