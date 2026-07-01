#include <taskbar.h>
#include <window.h>
#include <fb.h>
#include <font.h>
#include <mouse.h>

#define ENTRY_WIDTH 120
#define ENTRY_GAP 4

static uint8_t prev_buttons = 0;

static int64_t entry_x(int slot) {
    return ENTRY_GAP + slot * (ENTRY_WIDTH + ENTRY_GAP);
}

void taskbar_update(void) {
    int64_t mx = mouse_x();
    int64_t my = mouse_y();
    uint8_t buttons = mouse_buttons();
    int left_pressed_edge = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
    prev_buttons = buttons;

    if (!left_pressed_edge) {
        return;
    }
    int64_t bar_y = (int64_t)fb_height() - TASKBAR_HEIGHT;
    if (my < bar_y) {
        return; /* click was above the taskbar - not ours */
    }

    int count = window_count_open();
    for (int slot = 0; slot < count; slot++) {
        int64_t ex = entry_x(slot);
        if (mx >= ex && mx < ex + ENTRY_WIDTH) {
            int win_index = window_at_zorder(slot);
            if (win_index != -1) {
                window_focus(win_index);
            }
            return;
        }
    }
}

void taskbar_render(void) {
    int64_t bar_y = (int64_t)fb_height() - TASKBAR_HEIGHT;
    fb_draw_rect(0, bar_y, fb_width(), TASKBAR_HEIGHT, fb_make_color(40, 40, 45));
    fb_draw_rect(0, bar_y, fb_width(), 2, fb_make_color(0, 0, 0));

    int count = window_count_open();
    for (int slot = 0; slot < count; slot++) {
        int win_index = window_at_zorder(slot);
        struct window *win = window_get(win_index);
        if (!win) {
            continue;
        }
        int64_t ex = entry_x(slot);
        int is_frontmost = (slot == count - 1);
        fb_draw_rect(ex, bar_y + 3, ENTRY_WIDTH, TASKBAR_HEIGHT - 6,
                     is_frontmost ? fb_make_color(80, 80, 130) : fb_make_color(60, 60, 65));
        fb_draw_rect_outline(ex, bar_y + 3, ENTRY_WIDTH, TASKBAR_HEIGHT - 6, fb_make_color(0, 0, 0), 1);
        fb_draw_string_clipped(ex + 4, bar_y + (TASKBAR_HEIGHT - FONT_HEIGHT) / 2, win->title,
                                fb_make_color(255, 255, 255),
                                is_frontmost ? fb_make_color(80, 80, 130) : fb_make_color(60, 60, 65),
                                ex, bar_y, ENTRY_WIDTH, TASKBAR_HEIGHT);
    }
}
