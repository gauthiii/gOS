#ifndef GOS_DESKTOP_H
#define GOS_DESKTOP_H

/* Draws the desktop background and the "Files" launcher icon. Call once
 * per frame, after fb_clear() and before window_system_update()/
 * window_composite(). */
void desktop_render(void);

/* Handles clicks on the desktop (currently just the launcher icon).
 * Call once per frame, before window_system_update() so a click that
 * lands on the icon while no window covers it opens the File Manager. */
void desktop_update(void);

/* Milestone 26.5 (audit2 High #10): draws the right-click wallpaper menu,
 * if open. Must be called AFTER window_composite() (and before
 * mouse_draw_cursor()), not as part of desktop_render() - the menu needs
 * to be a true top-layer overlay like the cursor itself, never rendered
 * underneath an open window the way desktop_render()'s background/icon
 * layer legitimately is. */
void desktop_render_menu_overlay(void);

#endif
