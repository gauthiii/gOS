#include <desktop.h>
#include <fb.h>
#include <font.h>
#include <window.h>
#include <mouse.h>
#include <fm.h>
#include <serial.h>

#define ICON_X 20
#define ICON_Y 20
#define ICON_SIZE 64

static int fm_win_index = -1;
static uint8_t prev_buttons = 0;

static int fm_is_open(void) {
    return fm_win_index != -1 && window_get(fm_win_index) != 0;
}

void desktop_render(void) {
    /* Simple wallpaper: a solid background plus a few darker horizontal
     * bands, so the desktop reads as "a background" rather than a blank
     * fill - no image decoding needed for v1. */
    fb_clear(fb_make_color(25, 70, 100));
    for (uint64_t y = 0; y < fb_height(); y += 40) {
        fb_draw_rect(0, y, fb_width(), 4, fb_make_color(20, 58, 84));
    }

    fb_draw_rect(ICON_X, ICON_Y, ICON_SIZE, ICON_SIZE, fb_make_color(230, 200, 120));
    fb_draw_rect_outline(ICON_X, ICON_Y, ICON_SIZE, ICON_SIZE, fb_make_color(0, 0, 0), 2);
    fb_draw_rect(ICON_X + 12, ICON_Y + 14, ICON_SIZE - 24, 8, fb_make_color(120, 90, 30));
    fb_draw_rect(ICON_X + 12, ICON_Y + 26, ICON_SIZE - 24, ICON_SIZE - 40, fb_make_color(255, 235, 180));
    fb_draw_string(ICON_X, ICON_Y + ICON_SIZE + 4, "Files",
                   fb_make_color(255, 255, 255), fb_make_color(25, 70, 100));

    if (fm_is_open()) {
        /* Small dot under the label, matching the "app is running" dock
         * convention - a nice-to-have, not load-bearing for the milestone. */
        fb_draw_rect(ICON_X + ICON_SIZE / 2 - 2, ICON_Y + ICON_SIZE + 4 + FONT_HEIGHT + 3, 4, 4,
                     fb_make_color(255, 255, 255));
    }
}

void desktop_update(void) {
    int64_t mx = mouse_x();
    int64_t my = mouse_y();
    uint8_t buttons = mouse_buttons();
    int left_pressed_edge = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
    prev_buttons = buttons;

    if (!left_pressed_edge) {
        return;
    }
    if (mx < ICON_X || mx >= ICON_X + ICON_SIZE || my < ICON_Y || my >= ICON_Y + ICON_SIZE) {
        return;
    }
    if (window_point_hits_any(mx, my)) {
        return; /* a window is on top of the icon - the click was for it, not the desktop */
    }

    if (fm_is_open()) {
        serial_write_string("Desktop: Files icon clicked, File Manager already open - focusing\n");
        window_focus(fm_win_index);
    } else {
        serial_write_string("Desktop: Files icon clicked - launching File Manager\n");
        fm_win_index = fm_create_window(120, 60, 420, 260);
    }
}
