#ifndef GOS_TIMER_H
#define GOS_TIMER_H

#include <stdint.h>

void timer_init(void);
uint64_t timer_get_ticks(void);

#endif
