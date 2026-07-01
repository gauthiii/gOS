#include <fm.h>
#include <fat32.h>
#include <fb.h>
#include <font.h>
#include <serial.h>

#define FM_TOOLBAR_HEIGHT 30
#define FM_PATH_HEIGHT    14
#define FM_LIST_TOP       (FM_TOOLBAR_HEIGHT + FM_PATH_HEIGHT)
#define FM_ROW_HEIGHT     16
#define FM_MAX_DEPTH      16

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

static char fm_path_display[256] = "/";

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

static void fm_on_new_folder_click(void) {
    serial_write_string("FM: [New Folder] clicked (stub - wired to real fat_create_dir in Phase 10)\n");
}

static void fm_on_new_file_click(void) {
    serial_write_string("FM: [New File] clicked (stub - wired to real fat_create_file in Phase 10)\n");
}

static void fm_on_delete_click(void) {
    serial_write_string("FM: [Delete] clicked (stub - wired to real fat_delete_file/dir in Phase 10)\n");
}

static void fm_on_rename_click(void) {
    serial_write_string("FM: [Rename] clicked (stub - wired to real rename logic in Phase 10)\n");
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
    } else {
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

    int64_t bx = 4;
    const int64_t by = 4, bh = 22, gap = 4;
    window_add_button(win, bx, by, 50, bh, fb_make_color(120, 120, 200), fm_on_up_click);
    bx += 50 + gap;
    window_add_button(win, bx, by, 90, bh, fb_make_color(100, 170, 100), fm_on_new_folder_click);
    bx += 90 + gap;
    window_add_button(win, bx, by, 80, bh, fb_make_color(100, 170, 100), fm_on_new_file_click);
    bx += 80 + gap;
    window_add_button(win, bx, by, 70, bh, fb_make_color(190, 90, 90), fm_on_delete_click);
    bx += 70 + gap;
    window_add_button(win, bx, by, 80, bh, fb_make_color(170, 150, 90), fm_on_rename_click);

    window_set_render_callback(win, fm_render);
    window_set_click_callback(win, fm_click);

    fm_depth = 0;
    fm_current_cluster = fat32_root_cluster();
    fm_refresh();

    return win;
}
