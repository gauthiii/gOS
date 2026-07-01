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

#endif
