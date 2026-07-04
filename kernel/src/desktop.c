#include <desktop.h>
#include <fb.h>
#include <font.h>
#include <window.h>
#include <mouse.h>
#include <keyboard.h>
#include <fm.h>
#include <serial.h>
#include <taskbar.h>
#include <wallpaper.h>
#include <settings.h>
#include <terminal.h>
#include <calculator.h>

#define ICON_X 20
#define ICON_Y 20
#define ICON_SIZE 64
#define ICON_GAP 40

/* Milestone 24.1/24.2: Terminal and Calculator sit to the right of the
 * Files icon, same size, spaced by ICON_GAP - following the same
 * single-fixed-slot pattern as the existing Files icon rather than
 * generalizing into an icon array for just three total icons. */
#define ICON2_X (ICON_X + ICON_SIZE + ICON_GAP)
#define ICON2_Y ICON_Y
#define ICON3_X (ICON2_X + ICON_SIZE + ICON_GAP)
#define ICON3_Y ICON_Y

/* Desktop right-click context menu: one row per wallpaper option (see
 * wallpaper.h - Gradient plus every bundled BMP), giving the existing F2
 * shortcut (kb_consume_toggle_wallpaper(), Milestone 22.3, which just
 * cycles to the next option) a discoverable, direct-select on-screen home
 * instead of being a hidden keyboard-only control. */
#define MENU_ITEM_W 160
#define MENU_ITEM_H 24

static int fm_win_index = -1;
static uint8_t prev_buttons = 0;
static int menu_visible = 0;
static int64_t menu_x, menu_y;

static int fm_is_open(void) {
    return fm_win_index != -1 && window_get(fm_win_index) != 0;
}

static void select_wallpaper(int idx) {
    wallpaper_select(idx);
    settings_save();
    serial_write_string("Desktop: wallpaper selection now ");
    serial_write_uint((uint64_t)wallpaper_current_selection());
    serial_write_string(" (");
    serial_write_string(wallpaper_option_label(wallpaper_current_selection()));
    serial_write_string(")\n");
}

void desktop_render(void) {
    /* Milestone 15.2/15.3: real wallpaper layer (BMP image if bundled on
     * the disk image, vertical-gradient fallback otherwise), replacing
     * v1's solid fill + horizontal bands. */
    wallpaper_render();

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

    fb_draw_rect(ICON2_X, ICON2_Y, ICON_SIZE, ICON_SIZE, fb_make_color(90, 90, 160));
    fb_draw_rect_outline(ICON2_X, ICON2_Y, ICON_SIZE, ICON_SIZE, fb_make_color(0, 0, 0), 2);
    fb_draw_rect(ICON2_X + 10, ICON2_Y + 10, ICON_SIZE - 20, ICON_SIZE - 20, fb_make_color(20, 20, 30));
    fb_draw_string(ICON2_X + 14, ICON2_Y + 14, ">_", fb_make_color(120, 255, 150), fb_make_color(20, 20, 30));
    fb_draw_string(ICON2_X, ICON2_Y + ICON_SIZE + 4, "Terminal",
                   fb_make_color(255, 255, 255), fb_make_color(25, 70, 100));
    if (terminal_is_open()) {
        fb_draw_rect(ICON2_X + ICON_SIZE / 2 - 2, ICON2_Y + ICON_SIZE + 4 + FONT_HEIGHT + 3, 4, 4,
                     fb_make_color(255, 255, 255));
    }

    fb_draw_rect(ICON3_X, ICON3_Y, ICON_SIZE, ICON_SIZE, fb_make_color(150, 110, 170));
    fb_draw_rect_outline(ICON3_X, ICON3_Y, ICON_SIZE, ICON_SIZE, fb_make_color(0, 0, 0), 2);
    fb_draw_rect(ICON3_X + 10, ICON3_Y + 10, ICON_SIZE - 20, ICON_SIZE - 20, fb_make_color(40, 30, 45));
    fb_draw_string(ICON3_X + 16, ICON3_Y + 22, "42", fb_make_color(120, 255, 150), fb_make_color(40, 30, 45));
    fb_draw_string(ICON3_X, ICON3_Y + ICON_SIZE + 4, "Calc",
                   fb_make_color(255, 255, 255), fb_make_color(25, 70, 100));
    if (calculator_is_open()) {
        fb_draw_rect(ICON3_X + ICON_SIZE / 2 - 2, ICON3_Y + ICON_SIZE + 4 + FONT_HEIGHT + 3, 4, 4,
                     fb_make_color(255, 255, 255));
    }
}

/* Milestone 26.5 (audit2 High #10): moved out of desktop_render() (which
 * runs BEFORE window_composite(), i.e. on the bottom compositing layer)
 * into its own function called AFTER window_composite() - the menu is now
 * a genuine top-layer overlay like the mouse cursor, so it can never be
 * drawn underneath an open window regardless of where it's positioned. */
void desktop_render_menu_overlay(void) {
    if (!menu_visible) {
        return;
    }
    int64_t menu_h = MENU_ITEM_H * WALLPAPER_OPTION_COUNT;
    fb_draw_rect_outline(menu_x, menu_y, MENU_ITEM_W, (uint64_t)menu_h, fb_make_color(0, 0, 0), 1);
    int current = wallpaper_current_selection();
    for (int i = 0; i < WALLPAPER_OPTION_COUNT; i++) {
        int64_t row_y = menu_y + (int64_t)i * MENU_ITEM_H;
        uint32_t bg = (i == current) ? fb_make_color(80, 110, 150) : fb_make_color(60, 60, 70);
        fb_draw_rect(menu_x, row_y, MENU_ITEM_W, MENU_ITEM_H, bg);
        char label[24];
        int p = 0;
        if (i == current) {
            label[p++] = '>';
            label[p++] = ' ';
        }
        const char *opt = wallpaper_option_label(i);
        for (int k = 0; opt[k] && p < (int)sizeof(label) - 1; k++) {
            label[p++] = opt[k];
        }
        label[p] = '\0';
        fb_draw_string_clipped(menu_x + 6, row_y + (MENU_ITEM_H - FONT_HEIGHT) / 2, label,
                                fb_make_color(255, 255, 255), bg,
                                menu_x, row_y, MENU_ITEM_W, MENU_ITEM_H);
    }
}

void desktop_update(void) {
    /* Milestone 22.3/24-patch: F2 cycles to the next wallpaper option and
     * saves immediately - checked every frame regardless of mouse state,
     * independent of the icon-click handling below. Kept as an alternate
     * shortcut alongside the new right-click menu, not replaced by it. */
    if (kb_consume_toggle_wallpaper()) {
        serial_write_string("Desktop: F2 pressed - ");
        select_wallpaper((wallpaper_current_selection() + 1) % WALLPAPER_OPTION_COUNT);
    }

    /* Milestone 22.3: notice a drag/resize of the File Manager window and
     * persist it - settings_record_fm_geometry() itself no-ops (no FAT32
     * write) if nothing actually changed since the last check, so this
     * runs every frame cheaply rather than needing its own change-event
     * hook into window.c. */
    if (fm_is_open()) {
        struct window *fm_win = window_get(fm_win_index);
        if (fm_win) {
            settings_record_fm_geometry(fm_win->x, fm_win->y, fm_win->w, fm_win->h);
        }
    }

    int64_t mx = mouse_x();
    int64_t my = mouse_y();
    uint8_t buttons = mouse_buttons();
    int left_pressed_edge = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
    int right_pressed_edge = (buttons & MOUSE_RIGHT_BUTTON) && !(prev_buttons & MOUSE_RIGHT_BUTTON);
    prev_buttons = buttons;

    if (right_pressed_edge) {
        if (!window_point_hits_any(mx, my)) {
            /* Right-clicking empty desktop (icons included - the menu
             * applies regardless of what's under the cursor) opens the
             * menu at the click point; right-clicking a window is ignored
             * rather than repositioning/reopening the menu underneath it.
             * Clamped so all WALLPAPER_OPTION_COUNT rows stay fully
             * on-screen even when right-clicking near the bottom/right
             * edge - a right-click low on a 1280x800+ screen previously
             * opened a menu whose last rows were drawn past the visible
             * area (and, worse, past the taskbar, entirely unclickable). */
            int64_t menu_h = MENU_ITEM_H * WALLPAPER_OPTION_COUNT;
            int64_t max_x = (int64_t)fb_width() - MENU_ITEM_W;
            int64_t max_y = (int64_t)fb_height() - TASKBAR_HEIGHT - menu_h;
            menu_visible = 1;
            menu_x = mx > max_x ? max_x : mx;
            menu_y = my > max_y ? max_y : my;
            if (menu_x < 0) menu_x = 0;
            if (menu_y < 0) menu_y = 0;
        }
        return;
    }

    if (left_pressed_edge && menu_visible) {
        int64_t menu_h = MENU_ITEM_H * WALLPAPER_OPTION_COUNT;
        if (mx >= menu_x && mx < menu_x + MENU_ITEM_W && my >= menu_y && my < menu_y + menu_h) {
            int row = (int)((my - menu_y) / MENU_ITEM_H);
            serial_write_string("Desktop: context menu - ");
            select_wallpaper(row);
        }
        /* Any left click while the menu is open dismisses it, whether it
         * hit an item or landed elsewhere - a single click either acts on
         * the menu or closes it, never both an item action and, say, an
         * icon launch underneath in the same click. */
        menu_visible = 0;
        return;
    }

    if (!left_pressed_edge) {
        return;
    }

    if (mx >= ICON_X && mx < ICON_X + ICON_SIZE && my >= ICON_Y && my < ICON_Y + ICON_SIZE) {
        if (window_point_hits_any(mx, my)) {
            return; /* a window is on top of the icon - the click was for it, not the desktop */
        }
        if (fm_is_open()) {
            serial_write_string("Desktop: Files icon clicked, File Manager already open - focusing\n");
            window_focus(fm_win_index);
        } else {
            serial_write_string("Desktop: Files icon clicked - launching File Manager\n");
            /* Milestone 22.3: use the persisted geometry (falls back to the
             * same defaults as before if no GOS.CFG was found/loaded). */
            fm_win_index = fm_create_window(settings_fm_x(), settings_fm_y(), settings_fm_w(), settings_fm_h());
            if (fm_win_index == -1) {
                /* Finding #17: fm_create_window() already guards its own
                 * internal window_create() call, but this caller never
                 * checked the result - hitting MAX_WINDOWS=8 here produced
                 * no dialog, no window, and no error, the app just appeared
                 * to do nothing. */
                serial_write_string("Desktop: fm_create_window() failed (MAX_WINDOWS exhausted)\n");
                taskbar_flash_message("Could not open File Manager - too many windows open");
            }
        }
        return;
    }

    if (mx >= ICON2_X && mx < ICON2_X + ICON_SIZE && my >= ICON2_Y && my < ICON2_Y + ICON_SIZE) {
        if (window_point_hits_any(mx, my)) {
            return;
        }
        serial_write_string("Desktop: Terminal icon clicked\n");
        terminal_open();
        return;
    }

    if (mx >= ICON3_X && mx < ICON3_X + ICON_SIZE && my >= ICON3_Y && my < ICON3_Y + ICON_SIZE) {
        if (window_point_hits_any(mx, my)) {
            return;
        }
        serial_write_string("Desktop: Calculator icon clicked\n");
        calculator_open();
        return;
    }
}
