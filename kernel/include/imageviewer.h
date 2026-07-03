#ifndef GOS_IMAGEVIEWER_H
#define GOS_IMAGEVIEWER_H

/* Opens a new window showing the decoded contents of the BMP file at
 * `path` (a fat_resolve_path()-style path) - Milestone 24.3. Reuses
 * Milestone 15.3's BMP decoder (kernel/src/bmp.c, extracted from
 * wallpaper.c). Each call opens a new window instance (unlike the
 * Terminal/Calculator singletons) since each one displays a different
 * image; its decoded pixel buffer is freed automatically when the window
 * is closed, via window_set_close_callback(). */
/* Returns the new window's index (>= 0), or -1 on failure (file not found,
 * decode failure, or MAX_WINDOWS exhausted) - the return value lets
 * Phase 24's open/close regression test close exactly the window it just
 * opened. */
int imageviewer_open(const char *path);

#endif
