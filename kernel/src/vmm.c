#include <vmm.h>
#include <pmm.h>
#include <serial.h>

#define ENTRIES_PER_TABLE 512
#define PAGE_SIZE_4K  0x1000ULL
#define PAGE_SIZE_2M  0x200000ULL
#define ADDR_MASK     0x000FFFFFFFFFF000ULL

static uint64_t hhdm_off;
static uint64_t *pml4_virt;
static uint64_t pml4_phys;

extern void vmm_load_cr3(uint64_t phys);

static inline uint64_t *phys_to_virt(uint64_t phys) {
    return (uint64_t *)(phys + hhdm_off);
}

static uint64_t *table_get_or_create(uint64_t *table, uint64_t index, uint64_t flags) {
    if (!(table[index] & PAGE_PRESENT)) {
        uint64_t new_table_phys = pmm_alloc_page();
        uint64_t *new_table_virt = phys_to_virt(new_table_phys);
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            new_table_virt[i] = 0;
        }
        table[index] = new_table_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    return phys_to_virt(table[index] & ADDR_MASK);
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = table_get_or_create(pml4_virt, pml4_i, flags);
    uint64_t *pd = table_get_or_create(pdpt, pdpt_i, flags);
    uint64_t *pt = table_get_or_create(pd, pd_i, flags);

    pt[pt_i] = (phys & ADDR_MASK) | flags | PAGE_PRESENT;
}

/* Walks the page tables for `virt` without creating any missing levels
 * (unlike table_get_or_create). Returns 0 if any level above the PT
 * isn't present - the page can't be mapped in that case, so there's
 * nothing to unmap. */
static uint64_t *table_get_existing(uint64_t *table, uint64_t index) {
    if (!(table[index] & PAGE_PRESENT)) {
        return 0;
    }
    return phys_to_virt(table[index] & ADDR_MASK);
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = table_get_existing(pml4_virt, pml4_i);
    if (!pdpt) {
        return;
    }
    uint64_t *pd = table_get_existing(pdpt, pdpt_i);
    if (!pd) {
        return;
    }
    if (pd[pd_i] & PAGE_HUGE) {
        /* A 2MiB huge page covers this address - not something
         * vmm_unmap_page (a 4KiB-granularity API) can partially unmap.
         * Nothing in the codebase currently maps a virtual address this
         * function would be called on via a huge page, so this is a
         * documented limitation rather than a silent wrong unmap. */
        return;
    }
    uint64_t *pt = table_get_existing(pd, pd_i);
    if (!pt) {
        return;
    }

    pt[pt_i] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

/* Maps a 2MiB-aligned region using PD-level huge pages (PS bit set),
 * avoiding one full level of 4KiB page-table allocations. Used only for
 * the bulk identity/HHDM mapping of physical RAM, where per-page
 * permission granularity is not needed. */
static void vmm_map_2mb(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;

    uint64_t *pdpt = table_get_or_create(pml4_virt, pml4_i, flags);
    uint64_t *pd = table_get_or_create(pdpt, pdpt_i, flags);

    pd[pd_i] = (phys & ADDR_MASK) | flags | PAGE_PRESENT | PAGE_HUGE;
}

void vmm_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_start,
              uint64_t kernel_virt_end) {
    hhdm_off = hhdm_offset;

    pml4_phys = pmm_alloc_page();
    pml4_virt = phys_to_virt(pml4_phys);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        pml4_virt[i] = 0;
    }

    /* Identity-map and HHDM-map the first 4GiB of physical address space
     * using 2MiB huge pages. 4GiB covers all RAM on this system (207MiB
     * usable) plus the framebuffer (physical base 0x80000000) and low MMIO.
     * This is a deliberate v1 minimum-viable scope: a machine with more
     * physical RAM or high MMIO BARs above 4GiB would need those regions
     * mapped separately (a Phase 3+ enhancement, not required here). */
    const uint64_t map_limit = 4ULL * 1024 * 1024 * 1024;
    for (uint64_t addr = 0; addr < map_limit; addr += PAGE_SIZE_2M) {
        vmm_map_2mb(addr, addr, PAGE_WRITABLE);
        vmm_map_2mb(hhdm_off + addr, addr, PAGE_WRITABLE);
    }

    serial_write_string("VMM: identity + HHDM mapped first 4GiB (2MiB pages)\n");

    /* Map the kernel's own higher-half virtual range to its physical load
     * location at 4KiB granularity. v1 minimum-viable scope: mapped
     * uniformly read-write-execute rather than separating .text (RX) from
     * .rodata (RO) from .data/.bss (RW) - fine-grained W^X permissions are
     * a hardening improvement for a later phase, not required for the
     * kernel to function correctly now. */
    uint64_t kernel_size = kernel_virt_end - kernel_virt_start;
    for (uint64_t off = 0; off < kernel_size; off += PAGE_SIZE_4K) {
        vmm_map_page(kernel_virt_start + off, kernel_phys_base + off, PAGE_WRITABLE);
    }

    serial_write_string("VMM: kernel higher-half mapped (");
    serial_write_uint(kernel_size / 1024);
    serial_write_string(" KiB)\n");

    vmm_load_cr3(pml4_phys);

    serial_write_string("VMM: new CR3 loaded, kernel still running\n");
}

uint64_t vmm_get_pml4_phys(void) {
    return pml4_phys;
}
