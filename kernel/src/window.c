#include <window.h>
#include <mouse.h>
#include <keyboard.h>
#include <timer.h>
#include <fb.h>
#include <font.h>
#include <serial.h>
#include <taskbar.h>

static struct window windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS]; /* z_order[0] = backmost, z_order[count-1] = frontmost */
static int window_count = 0;

static int dragging_window = -1; /* index into `windows`, or -1 */
static int64_t drag_offset_x, drag_offset_y;
static uint8_t prev_buttons = 0;

void window_system_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].in_use = 0;
    }
    window_count = 0;
    dragging_window = -1;
    prev_buttons = 0;
}

int window_create(int64_t x, int64_t y, uint64_t w, uint64_t h,
                   uint32_t titlebar_color, uint32_t body_color, const char *title) {
    if (window_count >= MAX_WINDOWS) {
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].in_use) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        return -1;
    }

    windows[idx].x = x;
    windows[idx].y = y;
    windows[idx].w = w;
    windows[idx].h = h;
    windows[idx].titlebar_color = titlebar_color;
    windows[idx].body_color = body_color;
    windows[idx].in_use = 1;
    windows[idx].minimized = 0;
    windows[idx].maximized = 0;
    int i2 = 0;
    for (; i2 < WINDOW_TITLE_MAX - 1 && title && title[i2]; i2++) {
        windows[idx].title[i2] = title[i2];
    }
    windows[idx].title[i2] = '\0';
    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        windows[idx].buttons[i].in_use = 0;
    }
    windows[idx].has_textbox = 0;
    windows[idx].textbox_length = 0;
    windows[idx].textbox_buffer[0] = '\0';
    windows[idx].custom_render = 0;
    windows[idx].custom_click = 0;
    windows[idx].custom_key = 0;
    windows[idx].user_data = 0;

    z_order[window_count] = idx;
    window_count++;

    return idx;
}

void window_enable_textbox(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].has_textbox = 1;
}

void window_set_render_callback(int win_index, window_render_callback_t cb) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].custom_render = cb;
}

void window_set_click_callback(int win_index, window_click_callback_t cb) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].custom_click = cb;
}

void window_set_user_data(int win_index, void *data) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].user_data = data;
}

void *window_get_user_data(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return 0;
    }
    return windows[win_index].user_data;
}

void window_set_key_callback(int win_index, window_key_callback_t cb) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].custom_key = cb;
}

struct window *window_get(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return 0;
    }
    return &windows[win_index];
}

void window_set_title(int win_index, const char *title) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    int i = 0;
    for (; i < WINDOW_TITLE_MAX - 1 && title && title[i]; i++) {
        windows[win_index].title[i] = title[i];
    }
    windows[win_index].title[i] = '\0';
}

int window_add_button(int win_index, int64_t x, int64_t y, uint64_t w, uint64_t h,
                       uint32_t color, const char *label, button_callback_t on_click) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return -1;
    }
    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        if (!windows[win_index].buttons[i].in_use) {
            windows[win_index].buttons[i].x = x;
            windows[win_index].buttons[i].y = y;
            windows[win_index].buttons[i].w = w;
            windows[win_index].buttons[i].h = h;
            windows[win_index].buttons[i].color = color;
            windows[win_index].buttons[i].on_click = on_click;
            int j = 0;
            for (; j < BUTTON_LABEL_MAX - 1 && label && label[j]; j++) {
                windows[win_index].buttons[i].label[j] = label[j];
            }
            windows[win_index].buttons[i].label[j] = '\0';
            windows[win_index].buttons[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void raise_to_front(int win_idx) {
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (z_order[i] == win_idx) {
            pos = i;
            break;
        }
    }
    if (pos == -1 || pos == window_count - 1) {
        return;
    }
    for (int i = pos; i < window_count - 1; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[window_count - 1] = win_idx;
}

void window_focus(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    raise_to_front(win_index);
}

void window_close(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (z_order[i] == win_index) {
            pos = i;
            break;
        }
    }
    if (pos != -1) {
        for (int i = pos; i < window_count - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        window_count--;
    }
    windows[win_index].in_use = 0;
    windows[win_index].minimized = 0;
    windows[win_index].maximized = 0;
    /* Clear everything else too, not just in_use - a closed slot must be
     * fully inert. Without this, a slot reused by window_create() later
     * would start from a still-configured previous window's buttons/
     * callbacks/textbox content until each field happened to get
     * overwritten, and (more immediately) the button-dispatch loop below
     * could still invoke a just-closed window's stale callbacks if it
     * keeps iterating that window's other button rects after one button's
     * on_click() closes the window from inside itself. */
    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        windows[win_index].buttons[i].in_use = 0;
        windows[win_index].buttons[i].on_click = 0;
    }
    windows[win_index].has_textbox = 0;
    windows[win_index].textbox_length = 0;
    windows[win_index].textbox_buffer[0] = '\0';
    windows[win_index].custom_render = 0;
    windows[win_index].custom_click = 0;
    windows[win_index].custom_key = 0;
    windows[win_index].user_data = 0;
    if (dragging_window == win_index) {
        dragging_window = -1;
    }
}

static int point_in_rect(int64_t px, int64_t py, int64_t rx, int64_t ry, uint64_t rw, uint64_t rh) {
    return px >= rx && py >= ry && px < rx + (int64_t)rw && py < ry + (int64_t)rh;
}

void window_minimize(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].minimized = 1;
    if (dragging_window == win_index) {
        dragging_window = -1;
    }
}

void window_restore(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    windows[win_index].minimized = 0;
}

int window_is_minimized(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return 0;
    }
    return windows[win_index].minimized;
}

void window_maximize_toggle(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return;
    }
    struct window *win = &windows[win_index];
    if (win->maximized) {
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->w = win->restore_w;
        win->h = win->restore_h;
        win->maximized = 0;
    } else {
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_w = win->w;
        win->restore_h = win->h;
        win->x = 0;
        win->y = 0;
        win->w = fb_width();
        /* h is the BODY height (titlebar is drawn above it) - subtract both
         * the titlebar and the taskbar strip so the maximized window fills
         * everything else exactly, with no bleed under the taskbar. */
        uint64_t reserved = WINDOW_TITLEBAR_HEIGHT + TASKBAR_HEIGHT;
        win->h = (fb_height() > reserved) ? (fb_height() - reserved) : 0;
        win->maximized = 1;
    }
    if (dragging_window == win_index) {
        dragging_window = -1;
    }
}

int window_is_maximized(int win_index) {
    if (win_index < 0 || win_index >= MAX_WINDOWS || !windows[win_index].in_use) {
        return 0;
    }
    return windows[win_index].maximized;
}

int window_point_hits_any(int64_t px, int64_t py) {
    for (int i = 0; i < window_count; i++) {
        struct window *win = &windows[z_order[i]];
        if (win->minimized) {
            continue; /* hidden - can't be under the click */
        }
        if (point_in_rect(px, py, win->x, win->y, win->w, win->h + WINDOW_TITLEBAR_HEIGHT)) {
            return 1;
        }
    }
    return 0;
}

int window_count_open(void) {
    return window_count;
}

int window_at_zorder(int pos) {
    if (pos < 0 || pos >= window_count) {
        return -1;
    }
    return z_order[pos];
}

/* Finds the frontmost window whose title bar or body contains the point.
 * Minimized windows are skipped - they're not drawn, so they can't be
 * clicked. */
static int window_at_point(int64_t px, int64_t py) {
    for (int i = window_count - 1; i >= 0; i--) {
        int idx = z_order[i];
        struct window *win = &windows[idx];
        if (win->minimized) {
            continue;
        }
        if (point_in_rect(px, py, win->x, win->y, win->w, win->h + WINDOW_TITLEBAR_HEIGHT)) {
            return idx;
        }
    }
    return -1;
}

void window_system_update(void) {
    int64_t mx = mouse_x();
    int64_t my = mouse_y();
    uint8_t buttons = mouse_buttons();
    int left_pressed_edge = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
    int left_released_edge = !(buttons & MOUSE_LEFT_BUTTON) && (prev_buttons & MOUSE_LEFT_BUTTON);

    if (left_pressed_edge) {
        int hit = window_at_point(mx, my);
        if (hit != -1) {
            raise_to_front(hit);
            struct window *win = &windows[hit];
            int64_t close_x = win->x + (int64_t)win->w - WINDOW_CLOSE_BUTTON_SIZE - WINDOW_CLOSE_BUTTON_MARGIN;
            int64_t close_y = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
            int64_t min_x = close_x - WINDOW_MINIMIZE_BUTTON_GAP - WINDOW_MINIMIZE_BUTTON_SIZE;
            int64_t min_y = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
            int64_t max_btn_x = min_x - WINDOW_MAXIMIZE_BUTTON_GAP - WINDOW_MAXIMIZE_BUTTON_SIZE;
            int64_t max_btn_y = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
            if (point_in_rect(mx, my, close_x, close_y, WINDOW_CLOSE_BUTTON_SIZE, WINDOW_CLOSE_BUTTON_SIZE)) {
                window_close(hit);
            } else if (point_in_rect(mx, my, min_x, min_y, WINDOW_MINIMIZE_BUTTON_SIZE, WINDOW_MINIMIZE_BUTTON_SIZE)) {
                window_minimize(hit);
            } else if (point_in_rect(mx, my, max_btn_x, max_btn_y, WINDOW_MAXIMIZE_BUTTON_SIZE, WINDOW_MAXIMIZE_BUTTON_SIZE)) {
                window_maximize_toggle(hit);
            } else if (point_in_rect(mx, my, win->x, win->y, win->w, WINDOW_TITLEBAR_HEIGHT)) {
                /* Milestone 17.1: dragging a maximized window doesn't make
                 * sense (it fills the screen) - require restoring first via
                 * the maximize button, matching the button-only toggle
                 * design the user chose over double-click-to-restore. */
                if (!win->maximized) {
                    dragging_window = hit;
                    drag_offset_x = mx - win->x;
                    drag_offset_y = my - win->y;
                }
            } else {
                /* Click landed in the body - check buttons first. */
                int64_t local_x = mx - win->x;
                int64_t local_y = my - (win->y + WINDOW_TITLEBAR_HEIGHT);
                int hit_button = 0;
                for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
                    if (!win->in_use) {
                        /* A previous button's on_click() this same pass
                         * closed this window (window_close() clears
                         * in_use and every other field). `win`/`b` below
                         * would now point into a cleared slot - stop
                         * iterating instead of dispatching a stale
                         * callback (or none, since window_close() zeroed
                         * them, but a slot reused by a NEW window_create()
                         * before this loop finishes could otherwise fire
                         * that new window's unrelated callback). */
                        break;
                    }
                    struct button *b = &win->buttons[i];
                    if (b->in_use && point_in_rect(local_x, local_y, b->x, b->y, b->w, b->h)) {
                        hit_button = 1;
                        if (b->on_click) {
                            b->on_click();
                        }
                    }
                }
                if (win->in_use && !hit_button && win->custom_click) {
                    win->custom_click(win, local_x, local_y);
                }
            }
        }
    }

    if (left_released_edge) {
        dragging_window = -1;
    }

    if (dragging_window != -1 && (buttons & MOUSE_LEFT_BUTTON)) {
        int64_t new_x = mx - drag_offset_x;
        int64_t new_y = my - drag_offset_y;
        /* Clamp to the screen edges instead of letting negative x/y reach
         * fb_draw_rect's uint64_t params, where they'd wrap to a huge
         * value and make the draw loop's start already past its end -
         * the window would silently fail to render at all rather than
         * clipping at the edge. Also cap the upper bound so a window
         * can't be dragged so far right/down that its titlebar (the only
         * drag handle) becomes fully unreachable. */
        if (new_x < 0) {
            new_x = 0;
        }
        if (new_y < 0) {
            new_y = 0;
        }
        int64_t max_x = (int64_t)fb_width() - WINDOW_TITLEBAR_HEIGHT;
        int64_t max_y = (int64_t)fb_height() - WINDOW_TITLEBAR_HEIGHT;
        if (new_x > max_x) {
            new_x = max_x;
        }
        if (new_y > max_y) {
            new_y = max_y;
        }
        windows[dragging_window].x = new_x;
        windows[dragging_window].y = new_y;
    }

    prev_buttons = buttons;

    /* Route keyboard input to the focused window - defined as whichever
     * window is currently frontmost in the z-order (the same window
     * click-to-focus just raised, if a click happened this frame). Only
     * the focused window's text box (if it has one) receives characters;
     * unfocused windows' text boxes are untouched even if they have one. */
    if (window_count > 0) {
        struct window *focused = &windows[z_order[window_count - 1]];
        if (focused->has_textbox && !focused->minimized) {
            while (kb_has_char()) {
                char c = kb_getchar();
                if (focused->custom_key && focused->custom_key(focused, c)) {
                    continue; /* consumed by the window's own key handler (e.g. Ctrl+S) */
                }
                if (c == '\b') {
                    if (focused->textbox_length > 0) {
                        focused->textbox_length--;
                        focused->textbox_buffer[focused->textbox_length] = '\0';
                    }
                } else if (focused->textbox_length < TEXTBOX_BUFFER_SIZE - 1) {
                    focused->textbox_buffer[focused->textbox_length++] = c;
                    focused->textbox_buffer[focused->textbox_length] = '\0';
                }
            }
        }
    }
}

static void draw_window(struct window *win) {
    fb_draw_rect(win->x, win->y, win->w, WINDOW_TITLEBAR_HEIGHT, win->titlebar_color);
    fb_draw_rect(win->x, win->y + WINDOW_TITLEBAR_HEIGHT, win->w, win->h, win->body_color);
    fb_draw_rect_outline(win->x, win->y, win->w, win->h + WINDOW_TITLEBAR_HEIGHT, fb_make_color(0, 0, 0), 2);

    /* Title text, clipped to the title bar rect so a long title can never
     * spill out into the body or past the window's right edge, leaving
     * room on the right for the close/minimize/maximize buttons drawn
     * below. */
    int64_t reserved_right = WINDOW_CLOSE_BUTTON_SIZE + WINDOW_CLOSE_BUTTON_MARGIN
                            + WINDOW_MINIMIZE_BUTTON_SIZE + WINDOW_MINIMIZE_BUTTON_GAP
                            + WINDOW_MAXIMIZE_BUTTON_SIZE + WINDOW_MAXIMIZE_BUTTON_GAP + WINDOW_CLOSE_BUTTON_MARGIN;
    fb_draw_string_clipped(win->x + 6, win->y + (WINDOW_TITLEBAR_HEIGHT - FONT_HEIGHT) / 2,
                            win->title, fb_make_color(255, 255, 255), win->titlebar_color,
                            win->x, win->y, win->w - (uint64_t)reserved_right,
                            WINDOW_TITLEBAR_HEIGHT);

    /* Milestone 11.2: a small red "X" close button at the top-right of
     * every title bar, hit-tested in window_system_update(). */
    {
        int64_t cx = win->x + (int64_t)win->w - WINDOW_CLOSE_BUTTON_SIZE - WINDOW_CLOSE_BUTTON_MARGIN;
        int64_t cy = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
        fb_draw_rect(cx, cy, WINDOW_CLOSE_BUTTON_SIZE, WINDOW_CLOSE_BUTTON_SIZE, fb_make_color(200, 60, 60));
        fb_draw_rect_outline(cx, cy, WINDOW_CLOSE_BUTTON_SIZE, WINDOW_CLOSE_BUTTON_SIZE, fb_make_color(0, 0, 0), 1);
        fb_draw_line(cx + 3, cy + 3, cx + WINDOW_CLOSE_BUTTON_SIZE - 4, cy + WINDOW_CLOSE_BUTTON_SIZE - 4, fb_make_color(255, 255, 255));
        fb_draw_line(cx + WINDOW_CLOSE_BUTTON_SIZE - 4, cy + 3, cx + 3, cy + WINDOW_CLOSE_BUTTON_SIZE - 4, fb_make_color(255, 255, 255));
    }

    /* Milestone 16.2: a small amber "_" minimize button immediately to the
     * left of the close button, hit-tested in window_system_update(). */
    {
        int64_t close_x = win->x + (int64_t)win->w - WINDOW_CLOSE_BUTTON_SIZE - WINDOW_CLOSE_BUTTON_MARGIN;
        int64_t mx = close_x - WINDOW_MINIMIZE_BUTTON_GAP - WINDOW_MINIMIZE_BUTTON_SIZE;
        int64_t my = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
        fb_draw_rect(mx, my, WINDOW_MINIMIZE_BUTTON_SIZE, WINDOW_MINIMIZE_BUTTON_SIZE, fb_make_color(210, 160, 60));
        fb_draw_rect_outline(mx, my, WINDOW_MINIMIZE_BUTTON_SIZE, WINDOW_MINIMIZE_BUTTON_SIZE, fb_make_color(0, 0, 0), 1);
        fb_draw_line(mx + 3, my + WINDOW_MINIMIZE_BUTTON_SIZE - 5, mx + WINDOW_MINIMIZE_BUTTON_SIZE - 4,
                     my + WINDOW_MINIMIZE_BUTTON_SIZE - 5, fb_make_color(0, 0, 0));
    }

    /* Milestone 17.1: a small blue-green square maximize/restore toggle
     * button immediately to the left of the minimize button, hit-tested in
     * window_system_update(). Drawn as a hollow square (maximize) or a
     * smaller offset square (restore) so the two states are visually
     * distinguishable, matching the close/minimize buttons' convention of
     * a simple glyph rather than text. */
    {
        int64_t close_x = win->x + (int64_t)win->w - WINDOW_CLOSE_BUTTON_SIZE - WINDOW_CLOSE_BUTTON_MARGIN;
        int64_t min_x = close_x - WINDOW_MINIMIZE_BUTTON_GAP - WINDOW_MINIMIZE_BUTTON_SIZE;
        int64_t mx = min_x - WINDOW_MAXIMIZE_BUTTON_GAP - WINDOW_MAXIMIZE_BUTTON_SIZE;
        int64_t my = win->y + WINDOW_CLOSE_BUTTON_MARGIN;
        fb_draw_rect(mx, my, WINDOW_MAXIMIZE_BUTTON_SIZE, WINDOW_MAXIMIZE_BUTTON_SIZE, fb_make_color(90, 170, 170));
        fb_draw_rect_outline(mx, my, WINDOW_MAXIMIZE_BUTTON_SIZE, WINDOW_MAXIMIZE_BUTTON_SIZE, fb_make_color(0, 0, 0), 1);
        if (win->maximized) {
            /* Restore glyph: two overlapping offset squares. */
            fb_draw_rect_outline(mx + 2, my + 4, 8, 8, fb_make_color(0, 0, 0), 1);
            fb_draw_rect_outline(mx + 5, my + 2, 8, 8, fb_make_color(0, 0, 0), 1);
        } else {
            /* Maximize glyph: one square. */
            fb_draw_rect_outline(mx + 3, my + 3, WINDOW_MAXIMIZE_BUTTON_SIZE - 6, WINDOW_MAXIMIZE_BUTTON_SIZE - 6,
                                 fb_make_color(0, 0, 0), 1);
        }
    }

    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        struct button *b = &win->buttons[i];
        if (!b->in_use) {
            continue;
        }
        int64_t bx = win->x + b->x;
        int64_t by = win->y + WINDOW_TITLEBAR_HEIGHT + b->y;
        fb_draw_rect(bx, by, b->w, b->h, b->color);
        fb_draw_rect_outline(bx, by, b->w, b->h, fb_make_color(0, 0, 0), 1);
        if (b->label[0]) {
            fb_draw_string_clipped(bx + 4, by + ((int64_t)b->h - FONT_HEIGHT) / 2, b->label,
                                    fb_make_color(0, 0, 0), b->color,
                                    bx, by, b->w, b->h);
        }
    }

    if (win->has_textbox) {
        int64_t body_x = win->x;
        int64_t body_y = win->y + WINDOW_TITLEBAR_HEIGHT;

        /* Blinking cursor: appended as a literal '_' character to a local
         * copy of the buffer when visible, rather than computed as a
         * separate drawn rectangle at a tracked (x,y) - this reuses
         * fb_draw_string_clipped's own line-wrapping logic for free,
         * instead of duplicating it to find the cursor's position. */
        char display[TEXTBOX_BUFFER_SIZE + 2];
        int i = 0;
        for (; i < win->textbox_length; i++) {
            display[i] = win->textbox_buffer[i];
        }
        int this_index = (int)(win - windows);
        int is_focused = (window_count > 0) && (z_order[window_count - 1] == this_index);
        int blink_on = (timer_get_ticks() / 50) % 2 == 0;
        if (is_focused && blink_on) {
            display[i++] = '_';
        }
        display[i] = '\0';

        fb_draw_string_clipped(body_x + 4, body_y + 4, display,
                                fb_make_color(255, 255, 255), win->body_color,
                                body_x, body_y, win->w, win->h);
    }

    if (win->custom_render) {
        win->custom_render(win);
    }
}

void window_composite(void) {
    for (int i = 0; i < window_count; i++) {
        struct window *win = &windows[z_order[i]];
        if (win->minimized) {
            continue; /* Milestone 16.2: state preserved, just not drawn */
        }
        draw_window(win);
    }
    /* Milestone 15.1: the cursor is no longer drawn here - it must sit in
     * the compositor's true top layer (above the taskbar too), so the main
     * loop calls mouse_draw_cursor() last, after taskbar_render(). */
}
