#ifndef GOS_VMM_H
#define GOS_VMM_H

#include <stdint.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_HUGE     (1ULL << 7)

/* hhdm_offset is needed so the VMM can zero newly allocated page-table
 * pages (obtained from the PMM as physical addresses) before the VMM's own
 * mapping is what provides virtual access to physical memory. */
void vmm_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_start,
              uint64_t kernel_virt_end);

/* Maps a single 4KiB page. virt/phys must both be 4KiB aligned. */
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmaps a single 4KiB page (clears its PTE) and invalidates that
 * virtual address in the TLB via invlpg, so a later remap of the same
 * virtual address to a different physical page can't leave a stale TLB
 * entry silently pointing at the old physical page. No-op if the page
 * (or any page table level above it) isn't currently mapped. */
void vmm_unmap_page(uint64_t virt);

uint64_t vmm_get_pml4_phys(void);

/* Milestone 20.1: per-process address spaces. */
uint64_t vmm_create_process_pml4(void);
void vmm_map_page_in(uint64_t target_pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

#endif
