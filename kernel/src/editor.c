#include <editor.h>
#include <window.h>
#include <fat32.h>
#include <fb.h>
#include <serial.h>
#include <taskbar.h>

#define KEY_CTRL_S 0x13

static int editor_win = -1;
static char editor_path[300] = "";

static int editor_on_key(struct window *win, char c) {
    (void)win;
    if (c == (char)KEY_CTRL_S) {
        int ok = fat_write_file(editor_path, (const uint8_t *)win->textbox_buffer,
                                 (uint32_t)win->textbox_length);
        serial_write_string("Editor: Ctrl+S saved \"");
        serial_write_string(editor_path);
        serial_write_string("\" (");
        serial_write_uint((uint64_t)win->textbox_length);
        serial_write_string(" bytes) = ");
        serial_write_string(ok ? "1 (OK)" : "0 (FAILED)");
        serial_write_string("\n");
        return 1; /* consumed - Ctrl+S must not be inserted as a literal character */
    }
    return 0;
}

void editor_open(const char *path) {
    if (editor_win == -1) {
        editor_win = window_create(300, 120, 380, 220, fb_make_color(70, 200, 120),
                                    fb_make_color(30, 60, 40), "Text Editor");
        if (editor_win == -1) {
            /* Finding #17: window_create() failed (MAX_WINDOWS exhausted)
             * - every callee it feeds into internally guards against a
             * -1 index, so this never crashed, but silently did nothing
             * visible. Surface it instead of leaving the user staring at
             * a dead double-click. */
            serial_write_string("Editor: window_create() failed (MAX_WINDOWS exhausted) - could not open \"");
            serial_write_string(path);
            serial_write_string("\"\n");
            taskbar_flash_message("Could not open editor - too many windows open");
            return;
        }
        window_enable_textbox(editor_win);
        window_set_key_callback(editor_win, editor_on_key);
    } else {
        window_focus(editor_win);
    }

    int i = 0;
    for (; i < (int)sizeof(editor_path) - 1 && path[i]; i++) {
        editor_path[i] = path[i];
    }
    editor_path[i] = '\0';
    window_set_title(editor_win, editor_path);

    struct window *w = window_get(editor_win);
    if (!w) {
        return;
    }
    int64_t n = fat_read_file(editor_path, (uint8_t *)w->textbox_buffer, TEXTBOX_BUFFER_SIZE - 1);
    if (n < 0) {
        n = 0;
    }
    w->textbox_length = (int)n;
    w->textbox_buffer[w->textbox_length] = '\0';

    serial_write_string("Editor: opened \"");
    serial_write_string(editor_path);
    serial_write_string("\" (");
    serial_write_uint((uint64_t)n);
    serial_write_string(" bytes)\n");
}
