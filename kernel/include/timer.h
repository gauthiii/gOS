#ifndef GOS_TIMER_H
#define GOS_TIMER_H

#include <stdint.h>

#define PIT_FREQUENCY_HZ 100

void timer_init(void);
uint64_t timer_get_ticks(void);

/* Busy-waits (in a low-power hlt loop, interrupts stay enabled) until at
 * least `ms` milliseconds have elapsed, measured via the PIT tick counter. */
void sleep_ms(uint32_t ms);

/* Sleeps for a known duration with before/after serial markers, so the
 * elapsed wall-clock time can be independently verified from the host
 * (e.g. via `time` around the QEMU invocation) rather than only checking
 * the tick counter against itself. */
void timer_self_test(void);

#endif
