#ifndef GOS_KEYBOARD_H
#define GOS_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);

/* Blocks (in a low-power hlt loop) until a key has been translated to
 * ASCII, then returns it. */
char kb_getchar(void);

/* Returns 1 if a translated character is waiting in the ring buffer. */
int kb_has_char(void);

#endif
