#ifndef GOS_PMM_H
#define GOS_PMM_H

#include <stdint.h>
#include <limine.h>

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/* Returns the physical address of a newly allocated 4KiB page, or 0 if OOM. */
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);

uint64_t pmm_total_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_free_pages(void);

/* Allocates then frees a batch of pages, verifying reuse and no
 * double-allocation. Logs PASS/FAIL to serial. Returns 1 on success. */
int pmm_self_test(void);

#endif
