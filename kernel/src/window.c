#include <window.h>
#include <mouse.h>
#include <fb.h>
#include <serial.h>

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
                   uint32_t titlebar_color, uint32_t body_color) {
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
    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        windows[idx].buttons[i].in_use = 0;
    }

    z_order[window_count] = idx;
    window_count++;

    return idx;
}

int window_add_button(int win_index, int64_t x, int64_t y, uint64_t w, uint64_t h,
                       uint32_t color, button_callback_t on_click) {
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

static int point_in_rect(int64_t px, int64_t py, int64_t rx, int64_t ry, uint64_t rw, uint64_t rh) {
    return px >= rx && py >= ry && px < rx + (int64_t)rw && py < ry + (int64_t)rh;
}

/* Finds the frontmost window whose title bar or body contains the point. */
static int window_at_point(int64_t px, int64_t py) {
    for (int i = window_count - 1; i >= 0; i--) {
        int idx = z_order[i];
        struct window *win = &windows[idx];
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
            if (point_in_rect(mx, my, win->x, win->y, win->w, WINDOW_TITLEBAR_HEIGHT)) {
                dragging_window = hit;
                drag_offset_x = mx - win->x;
                drag_offset_y = my - win->y;
            } else {
                /* Click landed in the body - check buttons. */
                int64_t local_x = mx - win->x;
                int64_t local_y = my - (win->y + WINDOW_TITLEBAR_HEIGHT);
                for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
                    struct button *b = &win->buttons[i];
                    if (b->in_use && point_in_rect(local_x, local_y, b->x, b->y, b->w, b->h)) {
                        if (b->on_click) {
                            b->on_click();
                        }
                    }
                }
            }
        }
    }

    if (left_released_edge) {
        dragging_window = -1;
    }

    if (dragging_window != -1 && (buttons & MOUSE_LEFT_BUTTON)) {
        windows[dragging_window].x = mx - drag_offset_x;
        windows[dragging_window].y = my - drag_offset_y;
    }

    prev_buttons = buttons;
}

static void draw_window(struct window *win) {
    fb_draw_rect(win->x, win->y, win->w, WINDOW_TITLEBAR_HEIGHT, win->titlebar_color);
    fb_draw_rect(win->x, win->y + WINDOW_TITLEBAR_HEIGHT, win->w, win->h, win->body_color);
    fb_draw_rect_outline(win->x, win->y, win->w, win->h + WINDOW_TITLEBAR_HEIGHT, fb_make_color(0, 0, 0), 2);

    for (int i = 0; i < MAX_WIDGETS_PER_WINDOW; i++) {
        struct button *b = &win->buttons[i];
        if (!b->in_use) {
            continue;
        }
        int64_t bx = win->x + b->x;
        int64_t by = win->y + WINDOW_TITLEBAR_HEIGHT + b->y;
        fb_draw_rect(bx, by, b->w, b->h, b->color);
        fb_draw_rect_outline(bx, by, b->w, b->h, fb_make_color(0, 0, 0), 1);
    }
}

void window_composite(void) {
    for (int i = 0; i < window_count; i++) {
        draw_window(&windows[z_order[i]]);
    }
    mouse_draw_cursor();
}
