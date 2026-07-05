#ifndef GOS_HEAP_H
#define GOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
uint64_t heap_corruption_count(void);

/* Sums the payload size of every free block in address order (walking the
 * same singly-linked list kmalloc/kfree maintain). Used by regression tests
 * (Milestone 16.1) to prove a repeated allocate/free cycle returns to its
 * original baseline instead of leaking. */
uint64_t heap_free_bytes(void);

/* Number of times heap_grow() has actually mapped new pages since boot.
 * Used by Milestone 27.2's regression test to prove backward coalescing
 * reduces heap growth for a workload that strands free gaps between
 * larger freed regions. */
uint64_t heap_grow_count(void);

/* Stress-tests kmalloc/kfree with a mix of small/large blocks, verifies
 * write/read integrity, and deliberately corrupts one buffer's footer to
 * confirm the canary/guard actually detects overruns (not just that it
 * exists). Logs PASS/FAIL to serial. Returns 1 on success. */
int heap_self_test(void);

#endif
