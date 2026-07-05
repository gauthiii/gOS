#include <imageviewer.h>
#include <window.h>
#include <fat32.h>
#include <bmp.h>
#include <fb.h>
#include <heap.h>
#include <serial.h>
#include <taskbar.h>

struct imageviewer_state {
    uint32_t *pixels;
    uint64_t img_w, img_h;
};

static void imageviewer_render(struct window *win) {
    struct imageviewer_state *st = (struct imageviewer_state *)win->user_data;
    if (!st || !st->pixels) {
        return;
    }
    int64_t body_x = win->x;
    int64_t body_y = win->y + WINDOW_TITLEBAR_HEIGHT;
    /* Native resolution, no scaling (matches wallpaper.c's own decoder,
     * which has no scaling logic either) - clip to the window body if the
     * image is bigger than the window (window is sized to the image at
     * open time, clamped to the screen, so this only clips when the image
     * itself is bigger than the whole screen). */
    for (uint64_t y = 0; y < st->img_h && y < win->h; y++) {
        const uint32_t *row = st->pixels + y * st->img_w;
        for (uint64_t x = 0; x < st->img_w && x < win->w; x++) {
            fb_put_pixel((uint64_t)body_x + x, (uint64_t)body_y + y, row[x]);
        }
    }
}

/* Frees the decoded pixel buffer this window owns - without this, every
 * opened image viewer would leak its kmalloc'd buffer on close (window.c's
 * own window_close() only clears the user_data pointer, per Phase 24's
 * research into window.c - it never knew how to free what it points at). */
static void imageviewer_on_close(struct window *win) {
    struct imageviewer_state *st = (struct imageviewer_state *)win->user_data;
    if (st) {
        if (st->pixels) {
            kfree(st->pixels);
        }
        kfree(st);
    }
}

int imageviewer_open(const char *path) {
    struct fat_dirent ent;
    if (!fat_resolve_path(path, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("ImageViewer: \"");
        serial_write_string(path);
        serial_write_string("\" not found\n");
        taskbar_flash_message("Could not open image - file not found");
        return -1;
    }
    /* #27: an explicit pre-check against remaining heap, rather than just
     * relying on kmalloc() to fail gracefully. The decoded pixel buffer
     * bmp_decode() allocates internally is roughly 4x the raw file size
     * (uint32_t per pixel vs. ~3 bytes/pixel on-disk for 24bpp), so budget
     * for the file buffer PLUS that decode buffer up front - a near-full
     * heap could otherwise let the file buffer kmalloc succeed and then
     * fail deep inside bmp_decode() with a much less specific message. */
    uint64_t estimated_total = ent.size + ent.size * 4;
    if (estimated_total > heap_free_bytes()) {
        serial_write_string("ImageViewer: \"");
        serial_write_string(path);
        serial_write_string("\" too large for available heap\n");
        taskbar_flash_message("Could not open image - image too large");
        return -1;
    }
    uint8_t *buf = (uint8_t *)kmalloc(ent.size);
    if (!buf) {
        serial_write_string("ImageViewer: kmalloc for file buffer failed\n");
        taskbar_flash_message("Could not open image - out of memory");
        return -1;
    }
    int64_t got = fat_read_file(path, buf, ent.size);
    uint32_t *pixels = 0;
    uint64_t img_w = 0, img_h = 0;
    int ok = (got == (int64_t)ent.size) && bmp_decode(buf, ent.size, &pixels, &img_w, &img_h);
    kfree(buf);
    if (!ok) {
        serial_write_string("ImageViewer: failed to decode \"");
        serial_write_string(path);
        serial_write_string("\" as a 24bpp uncompressed BMP\n");
        taskbar_flash_message("Could not open image - unsupported/corrupt BMP");
        return -1;
    }

    /* Size the window to the image, clamped to the screen so a huge BMP
     * doesn't create an off-screen window - clipped in imageviewer_render()
     * if it's still bigger than the clamped window body. */
    uint64_t max_w = fb_width() > 40 ? fb_width() - 40 : fb_width();
    uint64_t max_h = fb_height() > 100 ? fb_height() - 100 : fb_height();
    uint64_t win_w = img_w < max_w ? img_w : max_w;
    uint64_t win_h = img_h < max_h ? img_h : max_h;
    if (win_w < 80) win_w = 80;   /* leave room for the titlebar's close/minimize/maximize buttons */
    if (win_h < 20) win_h = 20;

    int win = window_create(160, 80, win_w, win_h, fb_make_color(120, 120, 170),
                             fb_make_color(0, 0, 0), path);
    if (win == -1) {
        serial_write_string("ImageViewer: window_create() failed (MAX_WINDOWS exhausted)\n");
        taskbar_flash_message("Could not open image - too many windows open");
        kfree(pixels);
        return -1;
    }

    struct imageviewer_state *st = (struct imageviewer_state *)kmalloc(sizeof(struct imageviewer_state));
    if (!st) {
        serial_write_string("ImageViewer: kmalloc for state failed\n");
        window_close(win);
        kfree(pixels);
        return -1;
    }
    st->pixels = pixels;
    st->img_w = img_w;
    st->img_h = img_h;

    window_set_user_data(win, st);
    window_set_render_callback(win, imageviewer_render);
    window_set_close_callback(win, imageviewer_on_close);

    serial_write_string("ImageViewer: opened \"");
    serial_write_string(path);
    serial_write_string("\" (");
    serial_write_uint(img_w);
    serial_write_string("x");
    serial_write_uint(img_h);
    serial_write_string(", window ");
    serial_write_uint(win_w);
    serial_write_string("x");
    serial_write_uint(win_h);
    serial_write_string(")\n");
    return win;
}
