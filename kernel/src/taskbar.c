#include <taskbar.h>
#include <window.h>
#include <fb.h>
#include <font.h>
#include <mouse.h>
#include <timer.h>

#define ENTRY_WIDTH 120
#define ENTRY_GAP 4
#define FLASH_DURATION_TICKS (PIT_FREQUENCY_HZ * 2) /* ~2 seconds at 100Hz */
#define FLASH_MESSAGE_MAX 64

static uint8_t prev_buttons = 0;

static char flash_message[FLASH_MESSAGE_MAX];
static uint64_t flash_expiry_tick = 0;

void taskbar_flash_message(const char *msg) {
    int i = 0;
    for (; i < FLASH_MESSAGE_MAX - 1 && msg[i]; i++) {
        flash_message[i] = msg[i];
    }
    flash_message[i] = '\0';
    flash_expiry_tick = timer_get_ticks() + FLASH_DURATION_TICKS;
}

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
                /* Milestone 16.3: a minimized window's entry restores it
                 * (state/buttons/textbox were preserved, just hidden) before
                 * focusing; an already-visible window's entry just focuses
                 * (brings to front) as before. */
                if (window_is_minimized(win_index)) {
                    window_restore(win_index);
                }
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
        /* Milestone 16.3: "frontmost" (highlighted) means the topmost
         * window that's actually visible - a minimized window can't be the
         * highlighted entry even if it happens to be highest in z-order. */
        int is_minimized = window_is_minimized(win_index);
        int is_frontmost = !is_minimized && (slot == count - 1);
        uint32_t bg = is_frontmost ? fb_make_color(80, 80, 130)
                     : is_minimized ? fb_make_color(45, 45, 50)
                     : fb_make_color(60, 60, 65);
        fb_draw_rect(ex, bar_y + 3, ENTRY_WIDTH, TASKBAR_HEIGHT - 6, bg);
        fb_draw_rect_outline(ex, bar_y + 3, ENTRY_WIDTH, TASKBAR_HEIGHT - 6, fb_make_color(0, 0, 0), 1);
        uint32_t fg = is_minimized ? fb_make_color(150, 150, 155) : fb_make_color(255, 255, 255);
        fb_draw_string_clipped(ex + 4, bar_y + (TASKBAR_HEIGHT - FONT_HEIGHT) / 2, win->title,
                                fg, bg, ex, bar_y, ENTRY_WIDTH, TASKBAR_HEIGHT);
    }

    if (flash_message[0] != '\0' && timer_get_ticks() < flash_expiry_tick) {
        int64_t flash_y = bar_y - FONT_HEIGHT - 6;
        int64_t flash_w = fb_width();
        fb_draw_rect(0, flash_y, flash_w, FONT_HEIGHT + 4, fb_make_color(120, 40, 40));
        fb_draw_string(6, flash_y + 2, flash_message, fb_make_color(255, 255, 255), fb_make_color(120, 40, 40));
    }
}
