#ifndef GOS_TASKBAR_H
#define GOS_TASKBAR_H

#define TASKBAR_HEIGHT 28

/* Handles clicks on the taskbar (focusing whichever open window's entry
 * was clicked). Call once per frame, before window_system_update(). */
void taskbar_update(void);

/* Draws a bar across the bottom of the screen listing every open window's
 * title as a clickable entry. Call once per frame, after
 * window_composite() so the bar draws on top of any window dragged down
 * that far. */
void taskbar_render(void);

#endif
