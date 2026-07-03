#ifndef GOS_WALLPAPER_H
#define GOS_WALLPAPER_H

/* Desktop wallpaper layer, drawn by the compositor before any windows.
 * Milestone 15.2/15.3 built a single bundled-BMP-or-gradient choice; the
 * desktop right-click menu (kernel/src/desktop.c) extends this to a small
 * fixed list of options - a gradient plus every bundled BMP - selected by
 * index and persisted via settings.c. */

/* Index 0 is always the built-in gradient (no file, can't fail to load).
 * Indices 1+ correspond 1:1 with a bundled BMP file on the FAT32 root. */
#define WALLPAPER_OPTION_COUNT 5

/* Call once at boot, after heap + FAT32 are up. Loads the last-selected
 * option (index 1, the original bundled wallpaper, if nothing was
 * persisted yet) - falls back to the gradient if that file is missing or
 * malformed, same as Milestone 15.3's original behavior. */
void wallpaper_init(void);

/* Returns 1 if a BMP wallpaper is currently loaded (any non-gradient
 * option), 0 if the gradient is in use (either selected directly, or as a
 * fallback because the selected BMP failed to load). */
int wallpaper_image_loaded(void);

/* Draws the wallpaper across the whole screen (the selected BMP if loaded,
 * gradient otherwise). Must be the first thing drawn each frame. */
void wallpaper_render(void);

/* Selects wallpaper option `idx` (0 = gradient, 1..WALLPAPER_OPTION_COUNT-1
 * = a bundled BMP) and loads it immediately, freeing any previously-loaded
 * image. Out-of-range indices and load failures are logged and leave the
 * current selection unchanged (never crashes, never silently corrupts
 * state). Does not itself persist the choice - callers save via
 * settings_save() after a successful user-driven change. */
void wallpaper_select(int idx);

/* The currently active selection (see wallpaper_select()) and its short
 * display label (e.g. "Gradient", "Default", "Custom", "Mac", "Windows"),
 * for the desktop context menu to render and for settings.c to persist. */
int wallpaper_current_selection(void);
const char *wallpaper_option_label(int idx);

#endif
