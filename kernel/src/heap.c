#include <heap.h>
#include <vmm.h>
#include <pmm.h>
#include <serial.h>

#define PAGE_SIZE 4096ULL
#define HEAP_VIRT_START 0xffffffff90000000ULL
#define HEAP_MAX_SIZE   (64ULL * 1024 * 1024) /* 64MiB virtual ceiling for v1 */

#define HEADER_MAGIC 0xB10C4EADULL
#define FOOTER_MAGIC 0xFEEDFACEULL

struct block_header {
    uint64_t magic;
    uint64_t size;   /* payload size, not including header/footer */
    uint8_t  is_free;
    struct block_header *next; /* next block in address order, or NULL */
};

/* Footer is a single uint64_t magic value placed immediately after the
 * payload. Combined with the header magic, this lets kfree() and the
 * stress test detect buffer overruns: writing past the end of an
 * allocation corrupts the footer, and writing before the start corrupts
 * the header - both are checked on every kfree() call. */
static inline uint64_t *footer_of(struct block_header *b) {
    return (uint64_t *)((uint8_t *)(b + 1) + b->size);
}

static struct block_header *heap_start;
static uint64_t heap_mapped_end; /* first unmapped virtual address */
static uint64_t corruption_count = 0;

static int heap_grow(uint64_t min_extra_bytes) {
    uint64_t current_size = heap_mapped_end - HEAP_VIRT_START;
    if (current_size + min_extra_bytes > HEAP_MAX_SIZE) {
        return 0;
    }

    uint64_t pages_needed = (min_extra_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            return 0; /* physical OOM */
        }
        vmm_map_page(heap_mapped_end, phys, PAGE_WRITABLE);
        heap_mapped_end += PAGE_SIZE;
    }
    return 1;
}

void heap_init(void) {
    heap_mapped_end = HEAP_VIRT_START;
    if (!heap_grow(PAGE_SIZE * 4)) {
        serial_write_string("PANIC: heap_init failed to map initial pages\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }

    heap_start = (struct block_header *)HEAP_VIRT_START;
    heap_start->magic = HEADER_MAGIC;
    heap_start->size = (heap_mapped_end - HEAP_VIRT_START) - sizeof(struct block_header) - sizeof(uint64_t);
    heap_start->is_free = 1;
    heap_start->next = 0;
    *footer_of(heap_start) = FOOTER_MAGIC;

    serial_write_string("Heap initialized: ");
    serial_write_uint(heap_start->size);
    serial_write_string(" bytes available (");
    serial_write_uint(HEAP_MAX_SIZE / 1024 / 1024);
    serial_write_string(" MiB virtual ceiling)\n");
}

static uint64_t align8(uint64_t n) {
    return (n + 7) & ~7ULL;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return 0;
    }
    uint64_t want = align8(size);

    struct block_header *b = heap_start;
    while (b) {
        if (b->is_free && b->size >= want) {
            uint64_t min_split = sizeof(struct block_header) + sizeof(uint64_t) + 8;
            if (b->size >= want + min_split) {
                struct block_header *new_block =
                    (struct block_header *)((uint8_t *)(b + 1) + want + sizeof(uint64_t));
                new_block->magic = HEADER_MAGIC;
                new_block->size = b->size - want - sizeof(struct block_header) - sizeof(uint64_t);
                new_block->is_free = 1;
                new_block->next = b->next;
                *footer_of(new_block) = FOOTER_MAGIC;

                b->size = want;
                b->next = new_block;
                *footer_of(b) = FOOTER_MAGIC;
            }
            b->is_free = 0;
            return (void *)(b + 1);
        }
        b = b->next;
    }

    /* No free block large enough - try growing the heap by enough for this
     * allocation plus a new block header/footer, then retry once. */
    uint64_t extra = want + sizeof(struct block_header) + sizeof(uint64_t) + PAGE_SIZE;
    if (!heap_grow(extra)) {
        return 0; /* OOM */
    }

    /* Find the last block (highest address). If it's free, extend it to
     * cover the newly mapped space - cheap and avoids fragmentation. If
     * it's in use, extending it would silently grow a live allocation's
     * reported size instead of creating new free space, corrupting
     * whoever owns that block and leaving the new pages permanently
     * unusable (kmalloc could spuriously report OOM despite the space
     * genuinely being free). Append a brand-new free block header there
     * instead. */
    b = heap_start;
    while (b->next) {
        b = b->next;
    }
    uint64_t old_end = (uint64_t)(b + 1) + b->size + sizeof(uint64_t);
    if (b->is_free) {
        b->size = heap_mapped_end - old_end + b->size;
        *footer_of(b) = FOOTER_MAGIC;
    } else {
        struct block_header *new_block = (struct block_header *)old_end;
        new_block->magic = HEADER_MAGIC;
        new_block->size = heap_mapped_end - old_end - sizeof(struct block_header) - sizeof(uint64_t);
        new_block->is_free = 1;
        new_block->next = 0;
        *footer_of(new_block) = FOOTER_MAGIC;
        b->next = new_block;
    }

    return kmalloc(size); /* retry now that a large-enough free block exists */
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    struct block_header *b = (struct block_header *)ptr - 1;

    if (b->magic != HEADER_MAGIC) {
        corruption_count++;
        serial_write_string("HEAP CORRUPTION: header magic mismatch at kfree() - buffer underrun likely\n");
        return;
    }
    if (*footer_of(b) != FOOTER_MAGIC) {
        corruption_count++;
        serial_write_string("HEAP CORRUPTION: footer magic mismatch at kfree() - buffer overrun likely\n");
        return;
    }
    if (b->is_free) {
        /* Both magic checks pass but the block is already free - a
         * double-free. Without this check, the code below would
         * re-mark it free and potentially coalesce with a neighbor
         * that's since been reallocated and is live, silently
         * corrupting whoever now owns that memory. */
        corruption_count++;
        serial_write_string("HEAP CORRUPTION: double free detected at kfree() - block already free\n");
        return;
    }

    b->is_free = 1;

    /* Coalesce forward with the immediately following block if it's also
     * free. Backward coalescing is intentionally omitted for v1 (would
     * need a doubly-linked list); this can fragment slightly more than a
     * full coalescing allocator but is sufficient for the stress test and
     * does not affect correctness. */
    if (b->next && b->next->is_free) {
        struct block_header *n = b->next;
        b->size += sizeof(struct block_header) + sizeof(uint64_t) + n->size;
        b->next = n->next;
        *footer_of(b) = FOOTER_MAGIC;
    }
}

uint64_t heap_corruption_count(void) {
    return corruption_count;
}

#define STRESS_ITERATIONS 300
#define STRESS_MAX_LIVE 32

/* Simple xorshift-style PRNG so the stress test is deterministic across
 * runs (no hardware RNG dependency needed at this phase). */
static uint32_t rng_state = 0x1234abcd;
static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int heap_self_test(void) {
    void *live[STRESS_MAX_LIVE];
    size_t live_size[STRESS_MAX_LIVE];
    for (int i = 0; i < STRESS_MAX_LIVE; i++) {
        live[i] = 0;
    }

    serial_write_string("Heap self-test: ");
    serial_write_uint(STRESS_ITERATIONS);
    serial_write_string(" alloc/free cycles, mixed small/large blocks...\n");

    uint64_t corruption_before = heap_corruption_count();

    for (int iter = 0; iter < STRESS_ITERATIONS; iter++) {
        int slot = rng_next() % STRESS_MAX_LIVE;

        if (live[slot] != 0) {
            /* Verify the block's contents are exactly what we wrote before
             * freeing it - catches corruption from a NEIGHBORING block's
             * overrun, not just the block's own guard. */
            uint8_t *p = (uint8_t *)live[slot];
            uint8_t expected = (uint8_t)(slot & 0xFF);
            for (size_t j = 0; j < live_size[slot]; j++) {
                if (p[j] != expected) {
                    serial_write_string("Heap self-test: FAIL (payload content corrupted before free)\n");
                    return 0;
                }
            }
            kfree(live[slot]);
            live[slot] = 0;
        } else {
            /* Alternate small (8-64 bytes) and large (512-4096 bytes)
             * allocations to exercise both the split path and the
             * heap-growth path. */
            size_t size = (iter % 3 == 0)
                ? (512 + (rng_next() % 3584))
                : (8 + (rng_next() % 56));
            void *p = kmalloc(size);
            if (!p) {
                serial_write_string("Heap self-test: FAIL (kmalloc returned NULL unexpectedly)\n");
                return 0;
            }
            uint8_t pattern = (uint8_t)(slot & 0xFF);
            for (size_t j = 0; j < size; j++) {
                ((uint8_t *)p)[j] = pattern;
            }
            live[slot] = p;
            live_size[slot] = size;
        }
    }

    for (int i = 0; i < STRESS_MAX_LIVE; i++) {
        if (live[i] != 0) {
            kfree(live[i]);
        }
    }

    if (heap_corruption_count() != corruption_before) {
        serial_write_string("Heap self-test: FAIL (unexpected corruption detected during normal use)\n");
        return 0;
    }

    /* Now deliberately corrupt a live allocation's footer guard, to prove
     * the canary detection mechanism actually catches overruns rather than
     * just existing unused. This is expected to log a "HEAP CORRUPTION"
     * message below - that message appearing is the test PASSING, not
     * failing. */
    serial_write_string("Heap self-test: deliberately overrunning a buffer to verify guard detection...\n");
    void *victim = kmalloc(16);
    if (!victim) {
        serial_write_string("Heap self-test: FAIL (could not allocate victim block)\n");
        return 0;
    }
    uint8_t *victim_bytes = (uint8_t *)victim;
    for (int i = 0; i < 24; i++) { /* write 8 bytes past the 16-byte payload, into the footer */
        victim_bytes[i] = 0x41;
    }
    uint64_t corruption_before_deliberate = heap_corruption_count();
    kfree(victim); /* should detect and log corruption, not crash */
    if (heap_corruption_count() != corruption_before_deliberate + 1) {
        serial_write_string("Heap self-test: FAIL (deliberate overrun was NOT detected by guard)\n");
        return 0;
    }

    /* Phase 12, Milestone 12.3: deliberately double-free a live allocation
     * to prove kfree()'s is_free check actually catches it, the same way
     * the overrun test above proves the magic guards work. Both magic
     * checks pass on the second free (the block is otherwise intact) -
     * only the is_free check can catch this. Expected to log a "HEAP
     * CORRUPTION: double free" message below - that message appearing is
     * the test PASSING. */
    serial_write_string("Heap self-test: deliberately double-freeing a block to verify double-free detection...\n");
    void *dbl = kmalloc(16);
    if (!dbl) {
        serial_write_string("Heap self-test: FAIL (could not allocate double-free victim block)\n");
        return 0;
    }
    kfree(dbl); /* first free: legitimate, should succeed silently */
    uint64_t corruption_before_dblfree = heap_corruption_count();
    kfree(dbl); /* second free: double-free, should be caught and logged, not crash/coalesce */
    if (heap_corruption_count() != corruption_before_dblfree + 1) {
        serial_write_string("Heap self-test: FAIL (double free was NOT detected)\n");
        return 0;
    }

    /* Phase 13, Milestone 13.3: prove heap_grow's "extend last block"
     * path doesn't silently grow a LIVE block's reported size. Force the
     * heap's highest-address block to be a large in-use allocation
     * (sentinel), then trigger a second growth cycle with another large
     * allocation - the fixed code must append a new free block instead
     * of mutating sentinel's size or corrupting its data. */
    serial_write_string("Heap self-test: forcing heap_grow to extend past a live block...\n");
    /* Reaching the heap_grow "extend last block" path with the last
     * block actually in-use is hard to force purely through kmalloc()'s
     * public first-fit behavior - a split during growth always leaves a
     * fresh FREE block as the new tail, so the very next growth just
     * extends that (legitimately free) leftover instead. Since this test
     * lives in the same translation unit as the allocator internals,
     * directly mark the real tail block in-use (simulating "something
     * else is holding it live") for a precise, deterministic repro of
     * the exact condition heap_grow's is_free check is meant to guard. */
    struct block_header *tail = heap_start;
    while (tail->next) {
        tail = tail->next;
    }
    int tail_was_free = tail->is_free;
    tail->is_free = 0; /* pretend this block is a live allocation */
    uint8_t *tail_payload = (uint8_t *)(tail + 1);
    uint64_t tail_size_before = tail->size;
    uint64_t stamp_len = tail_size_before < 64 ? tail_size_before : 64;
    for (uint64_t i = 0; i < stamp_len; i++) {
        tail_payload[i] = (uint8_t)(0xC0 + (i & 0x0F));
    }

    uint64_t grow_size = 200000; /* far larger than any existing free block, forces heap_grow */
    uint8_t *grown = (uint8_t *)kmalloc(grow_size);
    if (!grown) {
        tail->is_free = tail_was_free;
        serial_write_string("Heap self-test: FAIL (could not allocate growth-forcing block)\n");
        return 0;
    }
    for (uint64_t i = 0; i < grow_size; i++) {
        grown[i] = (uint8_t)((i + 3) & 0xFF);
    }

    int tail_size_ok = (tail->size == tail_size_before);
    int tail_data_ok = 1;
    for (uint64_t i = 0; i < stamp_len; i++) {
        if (tail_payload[i] != (uint8_t)(0xC0 + (i & 0x0F))) {
            tail_data_ok = 0;
            break;
        }
    }
    int grown_data_ok = 1;
    for (uint64_t i = 0; i < grow_size; i++) {
        if (grown[i] != (uint8_t)((i + 3) & 0xFF)) {
            grown_data_ok = 0;
            break;
        }
    }
    if (!tail_size_ok || !tail_data_ok || !grown_data_ok) {
        serial_write_string("Heap self-test: FAIL (heap_grow corrupted the live tail block - size_ok=");
        serial_write_uint((uint64_t)tail_size_ok);
        serial_write_string(" tail_data_ok=");
        serial_write_uint((uint64_t)tail_data_ok);
        serial_write_string(" grown_data_ok=");
        serial_write_uint((uint64_t)grown_data_ok);
        serial_write_string(")\n");
        tail->is_free = tail_was_free;
        return 0;
    }
    tail->is_free = tail_was_free; /* restore, then release both blocks normally */
    if (!tail_was_free) {
        kfree(tail_payload);
    }
    kfree(grown);

    serial_write_string("Heap self-test: PASS (");
    serial_write_uint(STRESS_ITERATIONS);
    serial_write_string(" cycles clean, guard correctly detected deliberate overrun, double-free, and live-block heap_grow safety)\n");
    return 1;
}
