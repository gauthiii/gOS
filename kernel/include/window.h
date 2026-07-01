#ifndef GOS_WINDOW_H
#define GOS_WINDOW_H

#include <stdint.h>

#define WINDOW_TITLEBAR_HEIGHT 24
#define MAX_WINDOWS 8
#define MAX_WIDGETS_PER_WINDOW 4

typedef void (*button_callback_t)(void);

struct button {
    int64_t x, y; /* relative to window body */
    uint64_t w, h;
    uint32_t color;
    button_callback_t on_click;
    int in_use;
};

struct window {
    int64_t x, y;
    uint64_t w, h;
    uint32_t body_color;
    uint32_t titlebar_color;
    int in_use;
    struct button buttons[MAX_WIDGETS_PER_WINDOW];
};

void window_system_init(void);

/* Returns a window index (>= 0) or -1 if the window table is full. */
int window_create(int64_t x, int64_t y, uint64_t w, uint64_t h,
                   uint32_t titlebar_color, uint32_t body_color);

int window_add_button(int win_index, int64_t x, int64_t y, uint64_t w, uint64_t h,
                       uint32_t color, button_callback_t on_click);

/* Feeds the current mouse state into the window system: handles
 * click-to-focus, title-bar dragging, and button click dispatch. Must be
 * called once per frame, before window_composite(). */
void window_system_update(void);

/* Draws all windows back-to-front (respecting z-order), followed by the
 * mouse cursor on top. Call once per frame after window_system_update(). */
void window_composite(void);

#endif
