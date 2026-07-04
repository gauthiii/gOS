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

/* Milestone 25.1 (audit2 Critical #1): returns 1 if every byte in
 * [vaddr, vaddr+len) is mapped PRESENT and PAGE_USER at every page-table
 * level within `pml4_phys`, 0 otherwise (including on vaddr+len overflow).
 * A zero-length range is always considered valid. Syscalls that read/write
 * caller-supplied pointers must check this before dereferencing them -
 * without it, a process can pass any address (including unmapped or
 * kernel-only memory) and the kernel would fault trying to service the
 * syscall, which (per idt.c's unconditional panic-on-page-fault handling)
 * halts the entire machine, not just that process. */
int vmm_range_mapped_user(uint64_t pml4_phys, uint64_t vaddr, uint64_t len);

/* Milestone 25.2 (audit2 Critical #2): frees every physical page reachable
 * from PML4 slot `proc_pml4_index` - every PT/PD/PDPT table page plus every
 * leaf page they point at - then frees the PML4 page itself. Slot 0 (the
 * kernel's shared identity/HHDM/higher-half mappings, present in every
 * process's PML4 via vmm_create_process_pml4()'s shallow copy) is never
 * touched by this function; only the caller-specified slot is walked, so
 * passing the process-private slot index (see PROC_LOAD_BASE's own slot 1
 * in process.h) can never free shared kernel page tables. */
void vmm_destroy_process_pml4(uint64_t pml4_phys, uint64_t proc_pml4_index);

#endif
