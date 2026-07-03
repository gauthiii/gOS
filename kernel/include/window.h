#ifndef GOS_WINDOW_H
#define GOS_WINDOW_H

#include <stdint.h>

#define WINDOW_TITLEBAR_HEIGHT 24
#define MAX_WINDOWS 8
/* Raised from 8 (Phase 24): the Calculator needs 16 buttons (10 digits + 4
 * operators + '=' + 'C'), comfortably under 20 with room to spare. Purely a
 * capacity constant - every consumer (window.c's loops, struct window's
 * buttons[] array) is already sized off this macro. */
#define MAX_WIDGETS_PER_WINDOW 20
#define WINDOW_TITLE_MAX 32
#define TEXTBOX_BUFFER_SIZE 512
#define WINDOW_CLOSE_BUTTON_SIZE 16
#define WINDOW_CLOSE_BUTTON_MARGIN 4
/* Milestone 16.2: minimize button sits immediately to the left of the close
 * button, same size, 4px gap between the two. */
#define WINDOW_MINIMIZE_BUTTON_SIZE 16
#define WINDOW_MINIMIZE_BUTTON_GAP 4
/* Milestone 17.1: maximize button sits immediately to the left of the
 * minimize button, same size/gap pattern. */
#define WINDOW_MAXIMIZE_BUTTON_SIZE 16
#define WINDOW_MAXIMIZE_BUTTON_GAP 4
#define BUTTON_LABEL_MAX 16

/* Milestone 21.1: a window's rightmost/bottommost WINDOW_RESIZE_MARGIN
 * pixels (measured from the outer edge, spanning the full titlebar+body
 * rect) are drag-to-resize handles instead of drag-to-move/click targets -
 * checked after the close/minimize/maximize buttons but before the
 * ordinary titlebar-drag region, so grabbing the actual edge always
 * resizes. WINDOW_MIN_WIDTH/HEIGHT are body-size floors a resize can't
 * shrink past (arbitrary but generous enough that every existing window's
 * content - toolbars, buttons - stays usable). */
#define WINDOW_RESIZE_MARGIN 6
#define WINDOW_MIN_WIDTH 120
#define WINDOW_MIN_HEIGHT 60

typedef void (*button_callback_t)(void);

struct button {
    int64_t x, y; /* relative to window body */
    uint64_t w, h;
    uint32_t color;
    button_callback_t on_click;
    char label[BUTTON_LABEL_MAX];
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

/* Called for every character routed to a focused window with a text box,
 * before the default typing behavior (append/backspace/enter) runs.
 * Return 1 to mean "consumed - do not also apply default typing behavior"
 * (used by Phase 10's text editor to intercept Ctrl+S without it being
 * inserted as a literal character), or 0 to let default handling proceed
 * as normal. */
typedef int (*window_key_callback_t)(struct window *win, char c);

/* Called once, right before a window's slot is torn down by window_close(),
 * so a window whose user_data points at kmalloc'd memory (e.g. Phase 24's
 * Image Viewer, one decoded BMP pixel buffer per window) can free it. NULL
 * if the window owns no heap allocations beyond the struct itself. */
typedef void (*window_close_callback_t)(struct window *win);

struct window {
    int64_t x, y;
    uint64_t w, h;
    uint32_t body_color;
    uint32_t titlebar_color;
    char title[WINDOW_TITLE_MAX];
    int in_use;
    int minimized; /* Milestone 16.2: state/buttons/textbox preserved, just not drawn or hit-tested */
    int maximized; /* Milestone 17.1: x/y/w/h below reflect the maximized geometry; restore_* holds the pre-maximize geometry */
    int64_t restore_x, restore_y;
    uint64_t restore_w, restore_h;
    struct button buttons[MAX_WIDGETS_PER_WINDOW];

    int has_textbox;
    char textbox_buffer[TEXTBOX_BUFFER_SIZE];
    int textbox_length;

    window_render_callback_t custom_render;
    window_click_callback_t custom_click;
    window_key_callback_t custom_key;
    window_close_callback_t on_close;
    void *user_data;
};

void window_system_init(void);

/* Returns a window index (>= 0) or -1 if the window table is full. */
int window_create(int64_t x, int64_t y, uint64_t w, uint64_t h,
                   uint32_t titlebar_color, uint32_t body_color, const char *title);

int window_add_button(int win_index, int64_t x, int64_t y, uint64_t w, uint64_t h,
                       uint32_t color, const char *label, button_callback_t on_click);

/* Opts a window into receiving keyboard input (via kb_getchar()) whenever
 * it is the frontmost (focused) window. Only one text box per window is
 * supported in v1 - it fills the window's entire body. */
void window_enable_textbox(int win_index);

/* Registers custom body rendering / click handling for a window (Phase 9's
 * file manager). Either may be left unset (NULL) if not needed. */
void window_set_render_callback(int win_index, window_render_callback_t cb);
void window_set_click_callback(int win_index, window_click_callback_t cb);
void window_set_key_callback(int win_index, window_key_callback_t cb);
void window_set_user_data(int win_index, void *data);
void *window_get_user_data(int win_index);

/* Registers a destructor invoked once at the start of window_close(), before
 * any fields are cleared - so it can still read user_data to know what to
 * free. NULL (the default) means window_close() does no extra cleanup. */
void window_set_close_callback(int win_index, window_close_callback_t cb);

/* Returns a pointer to the window struct at this index (so callers like
 * Phase 10's text editor can read/write textbox_buffer directly), or NULL
 * if the index is out of range / not in use. */
struct window *window_get(int win_index);

/* Overwrites a window's title (e.g. the text editor retitling itself to
 * the name of whatever file is currently open). */
void window_set_title(int win_index, const char *title);

/* Raises a window to the front of the z-order without requiring a mouse
 * click - used when reusing an already-open window (e.g. the text editor
 * opening a second file while already visible). */
void window_focus(int win_index);

/* Returns 1 if any current window's title bar or body covers this screen
 * point, 0 otherwise - used by the desktop launcher (Milestone 11.2) so a
 * click on a desktop icon that's actually hidden under a window doesn't
 * fire. */
int window_point_hits_any(int64_t px, int64_t py);

/* Returns the number of windows currently open. */
int window_count_open(void);

/* Returns the window index at z-order position `pos` (0 = backmost), or
 * -1 if out of range - used by the taskbar (Milestone 11.2) to enumerate
 * open windows without exposing window.c's internal arrays. */
int window_at_zorder(int pos);

/* Frees a window's slot so it can be reused by a future window_create()
 * call. Does not shift other windows' indices. */
void window_close(int win_index);

/* Milestone 16.2: hides a window from compositing/hit-testing without
 * tearing it down - state, buttons, and textbox contents are untouched, and
 * it stays in the z-order/taskbar list. */
void window_minimize(int win_index);

/* Clears the minimized flag, restoring the window to visible/interactive.
 * Does not itself change z-order - callers that want the restored window
 * focused should also call window_focus(). */
void window_restore(int win_index);

/* Returns 1 if the window is currently minimized, 0 otherwise (including
 * for an invalid/closed index). */
int window_is_minimized(int win_index);

/* Milestone 17.1: toggles a window between its normal geometry and filling
 * the screen (minus the taskbar). The first call saves the current x/y/w/h
 * into restore_* and resizes to fill the screen; the next call reads
 * restore_* back exactly, so geometry round-trips precisely regardless of
 * how many toggles happen. No-op on an invalid/closed index. */
void window_maximize_toggle(int win_index);

/* Returns 1 if the window is currently maximized, 0 otherwise (including
 * for an invalid/closed index). */
int window_is_maximized(int win_index);

/* Milestone 21.2: rotates focus to the next non-minimized window in
 * z-order (a stable ring rotation - see window.c for why a naive
 * repeated raise-to-front doesn't visit every window). No-op with 0 or 1
 * windows open, or if every open window is minimized. */
void window_focus_next(void);

/* Feeds the current mouse state into the window system: handles
 * click-to-focus, title-bar dragging, edge/corner resize, and button
 * click dispatch. Must be called once per frame, before window_composite(). */
void window_system_update(void);

/* Draws all windows back-to-front (respecting z-order), followed by the
 * mouse cursor on top. Call once per frame after window_system_update(). */
void window_composite(void);

#endif
