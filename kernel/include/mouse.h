#ifndef GOS_MOUSE_H
#define GOS_MOUSE_H

#include <stdint.h>

#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04

void mouse_init(void);

int64_t mouse_x(void);
int64_t mouse_y(void);
uint8_t mouse_buttons(void); /* bitmask, see MOUSE_*_BUTTON */

/* Draws a small filled cursor sprite at the current mouse position onto
 * whatever the framebuffer's current draw target is (real buffer or back
 * buffer, per fb.c's redirection). */
void mouse_draw_cursor(void);

#endif
