#include <fm.h>
#include <fat32.h>
#include <fb.h>
#include <font.h>
#include <serial.h>
#include <timer.h>
#include <editor.h>
#include <imageviewer.h>
#include <taskbar.h>

#define FM_TOOLBAR_HEIGHT 30
#define FM_PATH_HEIGHT    14
#define FM_LIST_TOP       (FM_TOOLBAR_HEIGHT + FM_PATH_HEIGHT)
#define FM_ROW_HEIGHT     16
#define FM_MAX_DEPTH      16
#define FM_DOUBLE_CLICK_TICKS 40 /* ~400ms at the Phase 4 100Hz timer rate */

static uint32_t fm_dir_stack[FM_MAX_DEPTH];
static char fm_name_stack[FM_MAX_DEPTH][FAT32_NAME_MAX];
static int fm_depth = 0;

static uint32_t fm_current_cluster;
static struct fat_dirent fm_raw_entries[FAT32_MAX_DIRENTS];
static int fm_raw_count = 0;

/* Filtered view: excludes "." and ".." so they can't be clicked/navigated
 * into (Up is a dedicated toolbar button instead). */
static int fm_visible_index[FAT32_MAX_DIRENTS];
static int fm_visible_count = 0;
static int fm_selected = -1; /* index into fm_visible_index, or -1 */

/* Finding #24 (documentation-only, per audit's own assessment that this
 * is latent, not an active bug): double-click identity here is tracked
 * by ROW INDEX, not filename. This is only safe because fm_refresh()
 * (below) unconditionally resets fm_last_click_row to -1 on every
 * mutation (create/delete/rename/navigate) before the listing could
 * possibly change under a user's fingers. If a future code path ever
 * mutates fm_raw_entries/fm_visible_index WITHOUT going through
 * fm_refresh() first, a stale row index could silently identify the
 * wrong file for a double-click. Switching to filename-based tracking
 * would remove this invariant entirely, but isn't required while every
 * mutation path continues to call fm_refresh(). */
static int fm_last_click_row = -1;
static uint64_t fm_last_click_tick = 0;

static char fm_path_display[256] = "/";
static int fm_win_index = -1;

/* --- Modal dialog (New Folder / New File / Rename / Delete confirm) ---
 * A single reusable dialog window: a prompt label (custom-rendered), a
 * text box (Phase 7.3's widget, ignored in DELETE_CONFIRM mode), and
 * Confirm/Cancel buttons. Only one can be open at a time - adequate for
 * a single-window file manager in v1. */
enum fm_dialog_mode {
    FM_DIALOG_NONE,
    FM_DIALOG_NEW_FOLDER,
    FM_DIALOG_NEW_FILE,
    FM_DIALOG_RENAME,
    FM_DIALOG_DELETE_CONFIRM,
};

static int fm_dialog_win = -1;
static enum fm_dialog_mode fm_dialog_mode = FM_DIALOG_NONE;
static char fm_dialog_prompt[64] = "";
static char fm_dialog_target_name[FAT32_NAME_MAX] = ""; /* entry being renamed/deleted */
static int fm_dialog_target_is_dir = 0;

static int is_dot_entry(const char *name) {
    return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static void fm_rebuild_path_display(void) {
    int pos = 0;
    fm_path_display[pos++] = '/';
    for (int i = 0; i < fm_depth; i++) {
        const char *name = fm_name_stack[i];
        for (int j = 0; name[j] && pos < (int)sizeof(fm_path_display) - 2; j++) {
            fm_path_display[pos++] = name[j];
        }
        if (i != fm_depth - 1 && pos < (int)sizeof(fm_path_display) - 2) {
            fm_path_display[pos++] = '/';
        }
    }
    fm_path_display[pos] = '\0';
}

/* Builds a FAT32 path (no leading slash, e.g. "LEVEL1/LEVEL2/NAME.TXT")
 * for `name` inside the currently displayed directory. */
static void fm_full_path(const char *name, char *out, int out_size) {
    int pos = 0;
    if (fm_depth > 0) {
        /* fm_path_display is "/A/B" - copy everything after the leading
         * slash, then a separating slash before `name`. */
        for (int i = 1; fm_path_display[i] && pos < out_size - 2; i++) {
            out[pos++] = fm_path_display[i];
        }
        out[pos++] = '/';
    }
    for (int i = 0; name[i] && pos < out_size - 1; i++) {
        out[pos++] = name[i];
    }
    out[pos] = '\0';
}

static void fm_refresh(void) {
    fm_raw_count = fat_list_dir(fm_current_cluster, fm_raw_entries, FAT32_MAX_DIRENTS);
    fm_visible_count = 0;
    for (int i = 0; i < fm_raw_count; i++) {
        if (is_dot_entry(fm_raw_entries[i].name)) {
            continue;
        }
        fm_visible_index[fm_visible_count++] = i;
    }
    fm_selected = -1;
    fm_last_click_row = -1;
    fm_rebuild_path_display();

    serial_write_string("FM: listed \"");
    serial_write_string(fm_path_display);
    serial_write_string("\" (");
    serial_write_uint((uint64_t)fm_visible_count);
    serial_write_string(" entries)\n");
}

static void fm_navigate_into(int raw_index) {
    struct fat_dirent *e = &fm_raw_entries[raw_index];
    if (!(e->attr & FAT32_ATTR_DIRECTORY)) {
        return;
    }
    if (fm_depth >= FM_MAX_DEPTH) {
        serial_write_string("FM: max navigation depth reached, ignoring click\n");
        return;
    }
    fm_dir_stack[fm_depth] = fm_current_cluster;
    int i = 0;
    for (; i < FAT32_NAME_MAX - 1 && e->name[i]; i++) {
        fm_name_stack[fm_depth][i] = e->name[i];
    }
    fm_name_stack[fm_depth][i] = '\0';
    fm_depth++;
    fm_current_cluster = e->first_cluster;
    fm_refresh();
}

static void fm_navigate_up(void) {
    if (fm_depth == 0) {
        serial_write_string("FM: already at root, Up ignored\n");
        return;
    }
    fm_depth--;
    fm_current_cluster = fm_dir_stack[fm_depth];
    fm_refresh();
}

static void fm_on_up_click(void) {
    serial_write_string("FM: [Up] clicked\n");
    fm_navigate_up();
}

/* --- Dialog plumbing --- */

static void fm_dialog_render(struct window *win) {
    int64_t body_x = win->x;
    int64_t body_y = win->y + WINDOW_TITLEBAR_HEIGHT;
    /* The generic text-box widget (window.c) always draws its one line of
     * typed text at body_y+4 - drawing the prompt label *below* that line
     * instead of above it avoids the two overlapping at the same row. */
    fb_draw_string_clipped(body_x + 6, body_y + 4 + FONT_HEIGHT + 6, fm_dialog_prompt,
                            fb_make_color(220, 220, 220), win->body_color,
                            body_x, body_y, win->w, win->h);
}

static void fm_close_dialog(void) {
    if (fm_dialog_win != -1) {
        window_close(fm_dialog_win);
    }
    fm_dialog_win = -1;
    fm_dialog_mode = FM_DIALOG_NONE;
}

static void fm_dialog_cancel_click(void) {
    serial_write_string("FM: dialog cancelled\n");
    fm_close_dialog();
}

static void fm_dialog_confirm_click(void) {
    struct window *dw = window_get(fm_dialog_win);
    if (!dw) {
        fm_close_dialog();
        return;
    }

    char path[300];
    switch (fm_dialog_mode) {
        case FM_DIALOG_NEW_FOLDER:
            if (dw->textbox_length == 0) {
                serial_write_string("FM: New Folder cancelled (empty name)\n");
                break;
            }
            fm_full_path(dw->textbox_buffer, path, sizeof(path));
            serial_write_string("FM: fat_create_dir(\"");
            serial_write_string(path);
            serial_write_string("\") = ");
            serial_write_string(fat_create_dir(path) ? "1 (OK)" : "0 (FAILED)");
            serial_write_string("\n");
            fm_refresh();
            break;

        case FM_DIALOG_NEW_FILE:
            if (dw->textbox_length == 0) {
                serial_write_string("FM: New File cancelled (empty name)\n");
                break;
            }
            fm_full_path(dw->textbox_buffer, path, sizeof(path));
            serial_write_string("FM: fat_create_file(\"");
            serial_write_string(path);
            serial_write_string("\") = ");
            serial_write_string(fat_create_file(path) ? "1 (OK)" : "0 (FAILED)");
            serial_write_string("\n");
            fm_refresh();
            break;

        case FM_DIALOG_RENAME: {
            if (dw->textbox_length == 0) {
                serial_write_string("FM: Rename cancelled (empty name)\n");
                break;
            }
            char old_path[300];
            fm_full_path(fm_dialog_target_name, old_path, sizeof(old_path));
            serial_write_string("FM: fat_rename(\"");
            serial_write_string(old_path);
            serial_write_string("\", \"");
            serial_write_string(dw->textbox_buffer);
            serial_write_string("\") = ");
            serial_write_string(fat_rename(old_path, dw->textbox_buffer) ? "1 (OK)" : "0 (FAILED)");
            serial_write_string("\n");
            fm_refresh();
            break;
        }

        case FM_DIALOG_DELETE_CONFIRM: {
            char del_path[300];
            fm_full_path(fm_dialog_target_name, del_path, sizeof(del_path));
            int ok = fm_dialog_target_is_dir ? fat_delete_dir(del_path) : fat_delete_file(del_path);
            serial_write_string("FM: ");
            serial_write_string(fm_dialog_target_is_dir ? "fat_delete_dir(\"" : "fat_delete_file(\"");
            serial_write_string(del_path);
            serial_write_string("\") = ");
            serial_write_string(ok ? "1 (OK)" : "0 (FAILED)");
            serial_write_string("\n");
            fm_refresh();
            break;
        }

        case FM_DIALOG_NONE:
        default:
            break;
    }

    fm_close_dialog();
}

static void fm_open_dialog(enum fm_dialog_mode mode, const char *prompt, const char *prefill) {
    if (fm_dialog_win != -1) {
        /* Finding #23: this guard is correct (a second dialog request
         * while one's already open must be dropped, not stacked), but
         * previously gave no feedback at all - clicking a second toolbar
         * button did nothing visible, with no way to tell "ignored" from
         * "broken." */
        serial_write_string("FM: fm_open_dialog() ignored - a dialog is already open\n");
        taskbar_flash_message("A dialog is already open - close it first");
        return; /* one dialog at a time */
    }
    struct window *fm_win = window_get(fm_win_index);
    int64_t dx = fm_win ? fm_win->x + 40 : 200;
    int64_t dy = fm_win ? fm_win->y + 60 : 200;

    fm_dialog_win = window_create(dx, dy, 300, 110, fb_make_color(150, 130, 60),
                                   fb_make_color(45, 40, 25), "Prompt");
    if (fm_dialog_win == -1) {
        /* Finding #17: same silent-no-op class as the File Manager and
         * editor launch sites - hitting MAX_WINDOWS=8 while opening a
         * dialog produced no visible feedback at all. */
        serial_write_string("FM: fm_open_dialog window_create() failed (MAX_WINDOWS exhausted)\n");
        taskbar_flash_message("Could not open dialog - too many windows open");
        return;
    }
    window_enable_textbox(fm_dialog_win);
    window_add_button(fm_dialog_win, 10, 60, 100, 26, fb_make_color(100, 170, 100), "OK", fm_dialog_confirm_click);
    window_add_button(fm_dialog_win, 130, 60, 100, 26, fb_make_color(190, 90, 90), "Cancel", fm_dialog_cancel_click);
    window_set_render_callback(fm_dialog_win, fm_dialog_render);

    fm_dialog_mode = mode;
    int i = 0;
    for (; i < (int)sizeof(fm_dialog_prompt) - 1 && prompt[i]; i++) {
        fm_dialog_prompt[i] = prompt[i];
    }
    fm_dialog_prompt[i] = '\0';

    struct window *dw = window_get(fm_dialog_win);
    if (dw && prefill) {
        int j = 0;
        for (; j < TEXTBOX_BUFFER_SIZE - 1 && prefill[j]; j++) {
            dw->textbox_buffer[j] = prefill[j];
        }
        dw->textbox_buffer[j] = '\0';
        dw->textbox_length = j;
    }

    window_focus(fm_dialog_win);
}

static void fm_on_new_folder_click(void) {
    serial_write_string("FM: [New Folder] clicked\n");
    fm_open_dialog(FM_DIALOG_NEW_FOLDER, "New folder name:", "");
}

static void fm_on_new_file_click(void) {
    serial_write_string("FM: [New File] clicked\n");
    fm_open_dialog(FM_DIALOG_NEW_FILE, "New file name:", "");
}

static void fm_on_delete_click(void) {
    serial_write_string("FM: [Delete] clicked\n");
    if (fm_selected == -1) {
        serial_write_string("FM: Delete ignored (no item selected)\n");
        return;
    }
    struct fat_dirent *e = &fm_raw_entries[fm_visible_index[fm_selected]];
    int i = 0;
    for (; i < FAT32_NAME_MAX - 1 && e->name[i]; i++) {
        fm_dialog_target_name[i] = e->name[i];
    }
    fm_dialog_target_name[i] = '\0';
    fm_dialog_target_is_dir = (e->attr & FAT32_ATTR_DIRECTORY) != 0;

    char prompt[64] = "Delete \"";
    int p = 8;
    for (int k = 0; e->name[k] && p < 60; k++, p++) {
        prompt[p] = e->name[k];
    }
    prompt[p++] = '"';
    prompt[p++] = '?';
    prompt[p] = '\0';
    fm_open_dialog(FM_DIALOG_DELETE_CONFIRM, prompt, "");
}

static void fm_on_rename_click(void) {
    serial_write_string("FM: [Rename] clicked\n");
    if (fm_selected == -1) {
        serial_write_string("FM: Rename ignored (no item selected)\n");
        return;
    }
    struct fat_dirent *e = &fm_raw_entries[fm_visible_index[fm_selected]];
    int i = 0;
    for (; i < FAT32_NAME_MAX - 1 && e->name[i]; i++) {
        fm_dialog_target_name[i] = e->name[i];
    }
    fm_dialog_target_name[i] = '\0';
    fm_open_dialog(FM_DIALOG_RENAME, "Rename to:", fm_dialog_target_name);
}

/* Milestone 24.3: double-clicking a .BMP file opens the Image Viewer
 * instead of the text editor. Case-insensitive since FAT 8.3 names are
 * stored uppercase but a long (VFAT) name could be typed in any case. */
static int fm_has_bmp_suffix(const char *name) {
    int len = 0;
    while (name[len]) len++;
    if (len < 4) {
        return 0;
    }
    const char *ext = name + len - 4;
    char c0 = ext[0] >= 'a' && ext[0] <= 'z' ? (char)(ext[0] - 32) : ext[0];
    char c1 = ext[1] >= 'a' && ext[1] <= 'z' ? (char)(ext[1] - 32) : ext[1];
    char c2 = ext[2] >= 'a' && ext[2] <= 'z' ? (char)(ext[2] - 32) : ext[2];
    char c3 = ext[3] >= 'a' && ext[3] <= 'z' ? (char)(ext[3] - 32) : ext[3];
    return c0 == '.' && c1 == 'B' && c2 == 'M' && c3 == 'P';
}

static void fm_click(struct window *win, int64_t local_x, int64_t local_y) {
    (void)win;
    if (local_y < FM_LIST_TOP) {
        return; /* click landed in the path-label strip, not a row */
    }
    int row = (int)((local_y - FM_LIST_TOP) / FM_ROW_HEIGHT);
    if (row < 0 || row >= fm_visible_count) {
        return;
    }
    (void)local_x;
    int raw_index = fm_visible_index[row];
    struct fat_dirent *e = &fm_raw_entries[raw_index];

    if (e->attr & FAT32_ATTR_DIRECTORY) {
        serial_write_string("FM: click-to-open folder \"");
        serial_write_string(e->name);
        serial_write_string("\"\n");
        fm_navigate_into(raw_index);
        return;
    }

    uint64_t now = timer_get_ticks();
    int is_double_click = (row == fm_last_click_row) &&
                           (now - fm_last_click_tick) < FM_DOUBLE_CLICK_TICKS;

    if (is_double_click) {
        /* Finding #18: only re-arm the double-click timer on a SINGLE
         * click, not on the double-click event itself. The double-click
         * state is invalidated here (row set to -1, an impossible row)
         * instead of updated to "now" - otherwise a third rapid click
         * would still be within the window of the second click's
         * timestamp and re-trigger editor_open(), silently reloading
         * from disk and discarding any unsaved edits typed in between. */
        fm_last_click_row = -1;
        char path[300];
        fm_full_path(e->name, path, sizeof(path));
        serial_write_string("FM: double-click-to-open file \"");
        serial_write_string(path);
        serial_write_string("\"\n");
        if (fm_has_bmp_suffix(e->name)) {
            imageviewer_open(path);
        } else {
            editor_open(path);
        }
    } else {
        fm_last_click_row = row;
        fm_last_click_tick = now;
        fm_selected = row;
        serial_write_string("FM: selected file \"");
        serial_write_string(e->name);
        serial_write_string("\"\n");
    }
}

static void fm_render(struct window *win) {
    int64_t body_x = win->x;
    int64_t body_y = win->y + WINDOW_TITLEBAR_HEIGHT;

    fb_draw_rect(body_x, body_y + FM_TOOLBAR_HEIGHT, win->w, 1, fb_make_color(0, 0, 0));

    fb_draw_string_clipped(body_x + 4, body_y + FM_TOOLBAR_HEIGHT + 3, fm_path_display,
                            fb_make_color(220, 220, 220), win->body_color,
                            body_x, body_y, win->w, win->h);

    for (int row = 0; row < fm_visible_count; row++) {
        int64_t row_y = body_y + FM_LIST_TOP + (int64_t)row * FM_ROW_HEIGHT;
        if (row_y + FM_ROW_HEIGHT > body_y + (int64_t)win->h) {
            break; /* no scrolling in v1 - stop drawing past the window's bottom edge */
        }
        struct fat_dirent *e = &fm_raw_entries[fm_visible_index[row]];
        int is_dir = (e->attr & FAT32_ATTR_DIRECTORY) != 0;

        if (row == fm_selected) {
            fb_draw_rect(body_x + 2, row_y, win->w - 4, FM_ROW_HEIGHT, fb_make_color(60, 90, 140));
        }

        /* Icon placeholder: a colored square distinguishes folders (amber)
         * from files (gray) - no real icon graphics for v1, per the plan. */
        uint32_t icon_color = is_dir ? fb_make_color(230, 180, 60) : fb_make_color(160, 160, 160);
        fb_draw_rect(body_x + 4, row_y + 3, 10, 10, icon_color);

        fb_draw_string_clipped(body_x + 20, row_y + 4, e->name,
                                fb_make_color(255, 255, 255),
                                row == fm_selected ? fb_make_color(60, 90, 140) : win->body_color,
                                body_x, body_y, win->w, win->h);
    }
}

int fm_create_window(int64_t x, int64_t y, uint64_t w, uint64_t h) {
    int win = window_create(x, y, w, h, fb_make_color(90, 90, 90), fb_make_color(35, 35, 35), "File Manager");
    if (win < 0) {
        return -1;
    }
    fm_win_index = win;

    int64_t bx = 4;
    const int64_t by = 4, bh = 22, gap = 4;
    window_add_button(win, bx, by, 50, bh, fb_make_color(120, 120, 200), "Up", fm_on_up_click);
    bx += 50 + gap;
    window_add_button(win, bx, by, 90, bh, fb_make_color(100, 170, 100), "New Folder", fm_on_new_folder_click);
    bx += 90 + gap;
    window_add_button(win, bx, by, 80, bh, fb_make_color(100, 170, 100), "New File", fm_on_new_file_click);
    bx += 80 + gap;
    window_add_button(win, bx, by, 70, bh, fb_make_color(190, 90, 90), "Delete", fm_on_delete_click);
    bx += 70 + gap;
    window_add_button(win, bx, by, 80, bh, fb_make_color(170, 150, 90), "Rename", fm_on_rename_click);

    window_set_render_callback(win, fm_render);
    window_set_click_callback(win, fm_click);

    fm_depth = 0;
    fm_current_cluster = fat32_root_cluster();
    fm_refresh();

    return win;
}
