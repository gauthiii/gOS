#ifndef GOS_PANIC_H
#define GOS_PANIC_H

#include <stdint.h>

/* Draws a red full-screen panic display (if the framebuffer is ready yet -
 * a no-op otherwise, since an exception can in principle happen before
 * fb_init()) and halts forever. Called from the CPU exception handler
 * (idt.c) for any unhandled exception vector - this never returns. */
void panic_screen(const char *exception_name, uint64_t vector, uint64_t error_code,
                   uint64_t rip, int has_cr2, uint64_t cr2);

#endif
