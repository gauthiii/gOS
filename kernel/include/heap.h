#ifndef GOS_HEAP_H
#define GOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
uint64_t heap_corruption_count(void);

/* Stress-tests kmalloc/kfree with a mix of small/large blocks, verifies
 * write/read integrity, and deliberately corrupts one buffer's footer to
 * confirm the canary/guard actually detects overruns (not just that it
 * exists). Logs PASS/FAIL to serial. Returns 1 on success. */
int heap_self_test(void);

#endif
