#ifndef GOS_GDT_H
#define GOS_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

void gdt_init(void);

/* Milestone 20.1: the scheduler swaps this on every context switch, so
 * each process's ring3->ring0 transitions (syscalls, timer preemption)
 * land on that process's own private kernel stack, not a stack another
 * process might still be using. */
void gdt_set_tss_rsp0(uint64_t rsp0);

#endif
