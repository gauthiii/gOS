#ifndef GOS_WINDOW_H
#define GOS_WINDOW_H

#include <stdint.h>

#define WINDOW_TITLEBAR_HEIGHT 24
#define MAX_WINDOWS 8
#define MAX_WIDGETS_PER_WINDOW 8
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

struct window;

/* Custom body content, drawn after the body/buttons but before the window
 * border - used by Phase 9's file manager to render a directory listing
 * without window.c needing to know anything about FAT32. */
typedef void (*window_render_callback_t)(struct window *win);

/* Called for any body click that didn't land on a button, with coordinates
 * relative to the window body (same space as button x/y). Used by the file
 * manager for row selection / click-to-open. */
typedef void (*window_click_callback_t)(struct window *win, int64_t local_x, int64_t local_y);

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

    window_render_callback_t custom_render;
    window_click_callback_t custom_click;
    void *user_data;
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

/* Registers custom body rendering / click handling for a window (Phase 9's
 * file manager). Either may be left unset (NULL) if not needed. */
void window_set_render_callback(int win_index, window_render_callback_t cb);
void window_set_click_callback(int win_index, window_click_callback_t cb);
void window_set_user_data(int win_index, void *data);
void *window_get_user_data(int win_index);

/* Feeds the current mouse state into the window system: handles
 * click-to-focus, title-bar dragging, and button click dispatch. Must be
 * called once per frame, before window_composite(). */
void window_system_update(void);

/* Draws all windows back-to-front (respecting z-order), followed by the
 * mouse cursor on top. Call once per frame after window_system_update(). */
void window_composite(void);

#endif
