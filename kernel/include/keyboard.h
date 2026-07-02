#ifndef GOS_KEYBOARD_H
#define GOS_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);

/* Blocks (in a low-power hlt loop) until a key has been translated to
 * ASCII, then returns it. */
char kb_getchar(void);

/* Returns 1 if a translated character is waiting in the ring buffer. */
int kb_has_char(void);

/* Milestone 21.2: edge-triggered and self-consuming, like a one-slot flag
 * rather than a character - returns 1 exactly once per Alt+Tab press (Alt
 * held, Tab pressed), then 0 until the next press. Kept separate from
 * kb_getchar()'s character stream since Alt+Tab is a window-manager
 * command, not typed text (Tab while Alt is held deliberately does NOT
 * also enqueue a '\t' character). */
int kb_consume_alt_tab(void);

#endif
