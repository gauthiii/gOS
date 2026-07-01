#include <pmm.h>
#include <serial.h>

#define PAGE_SIZE 4096ULL

static uint8_t *bitmap;
static uint64_t bitmap_size_bytes;
static uint64_t highest_page_index;
static uint64_t hhdm_off;

static uint64_t total_pages = 0;
static uint64_t used_pages = 0;

/* Scanning restarts from this bit index on every allocation. It is reset to
 * 0 whenever a page below it is freed, so freed pages are always reused
 * before the search advances further - required for the Milestone 3.1
 * "allocate, free, allocate again, confirm reuse" smoke test to hold. */
static uint64_t search_hint = 0;

static inline void bitmap_set(uint64_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit) {
    bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static inline int bitmap_test(uint64_t bit) {
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

static void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_off);
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm_off = hhdm_offset;

    /* Size the bitmap against USABLE regions only. Some hosts (observed with
     * QEMU/OVMF) report a RESERVED entry describing a 64-bit PCI MMIO window
     * up near the 1TiB mark; including that in the bitmap span would waste
     * tens of MiB tracking address space we will never allocate from. */
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t end = e->base + e->length;
        if (end > highest_addr) {
            highest_addr = end;
        }
    }

    highest_page_index = highest_addr / PAGE_SIZE;
    bitmap_size_bytes = (highest_page_index + 7) / 8;

    /* Find a USABLE region large enough to hold the bitmap itself. */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmap_size_bytes) {
            bitmap_phys = e->base;
            break;
        }
    }

    serial_write_string("PMM: highest physical address: ");
    serial_write_hex64(highest_addr);
    serial_write_string("\nPMM: bitmap size: ");
    serial_write_uint(bitmap_size_bytes);
    serial_write_string(" bytes, placed at physical ");
    serial_write_hex64(bitmap_phys);
    serial_write_string("\n");

    bitmap = (uint8_t *)phys_to_virt(bitmap_phys);

    /* Mark everything used by default; only USABLE regions get cleared below.
     * This means kernel image, framebuffer, ACPI, and bootloader-reclaimable
     * regions are all conservatively treated as reserved/used for Milestone
     * 3.1's minimum-viable scope (reclaiming bootloader regions is a later
     * optimization, not required here). */
    for (uint64_t i = 0; i < bitmap_size_bytes; i++) {
        bitmap[i] = 0xFF;
    }

    total_pages = highest_page_index;
    used_pages = total_pages;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t start_page = e->base / PAGE_SIZE;
        uint64_t page_count = e->length / PAGE_SIZE;
        for (uint64_t p = 0; p < page_count; p++) {
            bitmap_clear(start_page + p);
            used_pages--;
        }
    }

    /* Physical page 0 is deliberately never handed out: pmm_alloc_page()
     * uses 0 as its "out of memory" sentinel return value, so address 0
     * must never be a value a caller could receive from a successful
     * allocation (it would be indistinguishable from failure). */
    if (!bitmap_test(0)) {
        bitmap_set(0);
        used_pages++;
    }

    /* Re-reserve the pages the bitmap itself occupies (they fall inside a
     * USABLE region we just cleared above). */
    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;
    uint64_t bitmap_page_count = (bitmap_size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = 0; p < bitmap_page_count; p++) {
        if (!bitmap_test(bitmap_start_page + p)) {
            bitmap_set(bitmap_start_page + p);
            used_pages++;
        }
    }

    serial_write_string("PMM: total pages: ");
    serial_write_uint(total_pages);
    serial_write_string(", used: ");
    serial_write_uint(used_pages);
    serial_write_string(", free: ");
    serial_write_uint(total_pages - used_pages);
    serial_write_string("\n");
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t bit = search_hint; bit < highest_page_index; bit++) {
        if (!bitmap_test(bit)) {
            bitmap_set(bit);
            used_pages++;
            search_hint = bit + 1;
            return bit * PAGE_SIZE;
        }
    }
    /* Wrap around once in case earlier freed pages were skipped. */
    for (uint64_t bit = 0; bit < search_hint; bit++) {
        if (!bitmap_test(bit)) {
            bitmap_set(bit);
            used_pages++;
            search_hint = bit + 1;
            return bit * PAGE_SIZE;
        }
    }
    return 0; /* out of memory */
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t bit = phys_addr / PAGE_SIZE;
    if (bit >= highest_page_index) {
        return;
    }
    if (bitmap_test(bit)) {
        bitmap_clear(bit);
        used_pages--;
        if (bit < search_hint) {
            search_hint = bit;
        }
    }
}

uint64_t pmm_total_pages(void) { return total_pages; }
uint64_t pmm_used_pages(void) { return used_pages; }
uint64_t pmm_free_pages(void) { return total_pages - used_pages; }

#define SELF_TEST_PAGES 100

int pmm_self_test(void) {
    uint64_t addrs[SELF_TEST_PAGES];
    uint64_t free_before = pmm_free_pages();

    serial_write_string("PMM self-test: allocating ");
    serial_write_uint(SELF_TEST_PAGES);
    serial_write_string(" pages...\n");

    for (int i = 0; i < SELF_TEST_PAGES; i++) {
        addrs[i] = pmm_alloc_page();
        if (addrs[i] == 0) {
            serial_write_string("PMM self-test: FAIL (allocation returned 0 / OOM)\n");
            return 0;
        }
        for (int j = 0; j < i; j++) {
            if (addrs[j] == addrs[i]) {
                serial_write_string("PMM self-test: FAIL (duplicate address returned)\n");
                return 0;
            }
        }
    }

    uint64_t free_after_alloc = pmm_free_pages();
    if (free_after_alloc != free_before - SELF_TEST_PAGES) {
        serial_write_string("PMM self-test: FAIL (free page count did not drop by expected amount)\n");
        return 0;
    }

    serial_write_string("PMM self-test: freeing all ");
    serial_write_uint(SELF_TEST_PAGES);
    serial_write_string(" pages...\n");
    for (int i = 0; i < SELF_TEST_PAGES; i++) {
        pmm_free_page(addrs[i]);
    }

    uint64_t free_after_free = pmm_free_pages();
    if (free_after_free != free_before) {
        serial_write_string("PMM self-test: FAIL (free page count did not return to baseline)\n");
        return 0;
    }

    serial_write_string("PMM self-test: re-allocating ");
    serial_write_uint(SELF_TEST_PAGES);
    serial_write_string(" pages to confirm reuse...\n");
    int reused = 0;
    for (int i = 0; i < SELF_TEST_PAGES; i++) {
        uint64_t addr = pmm_alloc_page();
        if (addr == 0) {
            serial_write_string("PMM self-test: FAIL (re-allocation returned 0 / OOM)\n");
            return 0;
        }
        for (int j = 0; j < SELF_TEST_PAGES; j++) {
            if (addrs[j] == addr) {
                reused++;
                break;
            }
        }
        pmm_free_page(addr);
    }

    if (reused == 0) {
        serial_write_string("PMM self-test: FAIL (none of the re-allocated addresses matched previously freed pages)\n");
        return 0;
    }

    serial_write_string("PMM self-test: PASS (");
    serial_write_uint(reused);
    serial_write_string("/");
    serial_write_uint(SELF_TEST_PAGES);
    serial_write_string(" re-allocations reused a freed page)\n");
    return 1;
}
