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
    } else if ((flags & PAGE_USER) && !(table[index] & PAGE_USER)) {
        /* Milestone 19.1 bug fix: the U/S bit is enforced at EVERY page
         * table level, not just the leaf PTE - a user-accessible leaf
         * mapping is silently blocked if any PML4/PDPT/PD entry above it
         * lacks PAGE_USER. An intermediate entry created earlier by a
         * kernel-only mapping that happens to share the same 512GB/1GB/
         * 2MB region (e.g. vmm_init()'s own identity map, which covers
         * the low 4GiB - including where Phase 19's first user-mode
         * mapping landed - with PAGE_WRITABLE only, no PAGE_USER) would
         * otherwise never get PAGE_USER retroactively, since the
         * `!(table[index] & PAGE_PRESENT)` branch above only runs once,
         * at first creation. OR it in here instead. This does NOT expose
         * the kernel's own sibling mappings under the same intermediate
         * table to user access - their own PT-level leaf entries still
         * lack PAGE_USER independently; only the specific new leaf
         * mapping this call is for gains access, once its own PTE is
         * written with PAGE_USER too. */
        table[index] |= PAGE_USER;
    }
    return phys_to_virt(table[index] & ADDR_MASK);
}

static void map_page_in_table(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = table_get_or_create(pml4, pml4_i, flags);
    uint64_t *pd = table_get_or_create(pdpt, pdpt_i, flags);
    uint64_t *pt = table_get_or_create(pd, pd_i, flags);

    pt[pt_i] = (phys & ADDR_MASK) | flags | PAGE_PRESENT;
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    map_page_in_table(pml4_virt, virt, phys, flags);
}

/* Milestone 20.1: maps a page into an arbitrary (non-global) PML4, for
 * per-process address spaces created by vmm_create_process_pml4(). */
void vmm_map_page_in(uint64_t target_pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    map_page_in_table(phys_to_virt(target_pml4_phys), virt, phys, flags);
}

/* Milestone 20.1: allocates a fresh PML4 and shallow-copies the kernel's
 * own top-level entries into it (same physical PDPT pointers, not deep
 * copies) - every process's address space resolves the kernel's identity
 * map, HHDM, and higher-half mappings identically, since that's shared,
 * trusted code/data every process needs (syscall entry, the scheduler
 * itself, etc). Process-private mappings (an ELF's segments, its stack)
 * must go in a PML4 slot NOT shared with the kernel - kernel/include/
 * process.h's PROC_LOAD_BASE/PROC_STACK_BASE constants deliberately live
 * under PML4 index 1, which vmm_init() never touches, so a fresh PDPT
 * chain gets created there per-process on first use instead of aliasing
 * another process's (or the kernel's) tables. */
uint64_t vmm_create_process_pml4(void) {
    uint64_t new_phys = pmm_alloc_page();
    uint64_t *new_virt = phys_to_virt(new_phys);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        new_virt[i] = pml4_virt[i];
    }
    return new_phys;
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

/* Read-only walk (creates nothing) checking PRESENT+USER at every level
 * down to the leaf PTE for a single 4KiB-aligned page. */
static int page_mapped_user(uint64_t target_pml4_phys, uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = phys_to_virt(target_pml4_phys);
    if (!(pml4[pml4_i] & PAGE_PRESENT) || !(pml4[pml4_i] & PAGE_USER)) {
        return 0;
    }
    uint64_t *pdpt = phys_to_virt(pml4[pml4_i] & ADDR_MASK);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT) || !(pdpt[pdpt_i] & PAGE_USER)) {
        return 0;
    }
    if (pdpt[pdpt_i] & PAGE_HUGE) {
        return 0; /* 1GiB huge page - never used for process memory in this codebase */
    }
    uint64_t *pd = phys_to_virt(pdpt[pdpt_i] & ADDR_MASK);
    if (!(pd[pd_i] & PAGE_PRESENT) || !(pd[pd_i] & PAGE_USER)) {
        return 0;
    }
    if (pd[pd_i] & PAGE_HUGE) {
        return 0; /* 2MiB huge page - never used for process memory in this codebase */
    }
    uint64_t *pt = phys_to_virt(pd[pd_i] & ADDR_MASK);
    return (pt[pt_i] & PAGE_PRESENT) && (pt[pt_i] & PAGE_USER);
}

int vmm_range_mapped_user(uint64_t pml4_phys, uint64_t vaddr, uint64_t len) {
    if (len == 0) {
        return 1;
    }
    uint64_t end = vaddr + len;
    if (end < vaddr) {
        return 0; /* vaddr+len overflowed - reject rather than silently wrap */
    }
    uint64_t page = vaddr & ~(PAGE_SIZE_4K - 1);
    while (page < end) {
        if (!page_mapped_user(pml4_phys, page)) {
            return 0;
        }
        uint64_t next = page + PAGE_SIZE_4K;
        if (next < page) {
            break; /* defensive: can't happen since `end` already passed the overflow check */
        }
        page = next;
    }
    return 1;
}

void vmm_destroy_process_pml4(uint64_t pml4_phys, uint64_t proc_pml4_index) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    if (pml4[proc_pml4_index] & PAGE_PRESENT) {
        uint64_t pdpt_phys = pml4[proc_pml4_index] & ADDR_MASK;
        uint64_t *pdpt = phys_to_virt(pdpt_phys);
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            if (!(pdpt[i] & PAGE_PRESENT)) {
                continue;
            }
            if (pdpt[i] & PAGE_HUGE) {
                pmm_free_page(pdpt[i] & ADDR_MASK);
                continue;
            }
            uint64_t pd_phys = pdpt[i] & ADDR_MASK;
            uint64_t *pd = phys_to_virt(pd_phys);
            for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
                if (!(pd[j] & PAGE_PRESENT)) {
                    continue;
                }
                if (pd[j] & PAGE_HUGE) {
                    pmm_free_page(pd[j] & ADDR_MASK);
                    continue;
                }
                uint64_t pt_phys = pd[j] & ADDR_MASK;
                uint64_t *pt = phys_to_virt(pt_phys);
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    if (pt[k] & PAGE_PRESENT) {
                        pmm_free_page(pt[k] & ADDR_MASK);
                    }
                }
                pmm_free_page(pt_phys);
            }
            pmm_free_page(pd_phys);
        }
        pmm_free_page(pdpt_phys);
        pml4[proc_pml4_index] = 0;
    }
    pmm_free_page(pml4_phys);
}
