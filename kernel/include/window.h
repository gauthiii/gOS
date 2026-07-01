#ifndef GOS_WINDOW_H
#define GOS_WINDOW_H

#include <stdint.h>

#define WINDOW_TITLEBAR_HEIGHT 24
#define MAX_WINDOWS 8
#define MAX_WIDGETS_PER_WINDOW 4
#define WINDOW_TITLE_MAX 32
#define TEXTBOX_BUFFER_SIZE 512

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
    char title[WINDOW_TITLE_MAX];
    int in_use;
    struct button buttons[MAX_WIDGETS_PER_WINDOW];

    int has_textbox;
    char textbox_buffer[TEXTBOX_BUFFER_SIZE];
    int textbox_length;
};

void window_system_init(void);

/* Returns a window index (>= 0) or -1 if the window table is full. */
int window_create(int64_t x, int64_t y, uint64_t w, uint64_t h,
                   uint32_t titlebar_color, uint32_t body_color, const char *title);

int window_add_button(int win_index, int64_t x, int64_t y, uint64_t w, uint64_t h,
                       uint32_t color, button_callback_t on_click);

/* Opts a window into receiving keyboard input (via kb_getchar()) whenever
 * it is the frontmost (focused) window. Only one text box per window is
 * supported in v1 - it fills the window's entire body. */
void window_enable_textbox(int win_index);

/* Feeds the current mouse state into the window system: handles
 * click-to-focus, title-bar dragging, and button click dispatch. Must be
 * called once per frame, before window_composite(). */
void window_system_update(void);

/* Draws all windows back-to-front (respecting z-order), followed by the
 * mouse cursor on top. Call once per frame after window_system_update(). */
void window_composite(void);

#endif
