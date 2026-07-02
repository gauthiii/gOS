#include <limine.h>
#include <stdint.h>
#include <serial.h>
#include <gdt.h>
#include <idt.h>
#include <pic.h>
#include <timer.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <keyboard.h>
#include <fb.h>
#include <mouse.h>
#include <window.h>
#include <font.h>
#include <ata.h>
#include <fat32.h>
#include <fm.h>
#include <desktop.h>
#include <taskbar.h>
#include <wallpaper.h>
#include <usermode.h>
#include <syscall.h>
#include <process.h>

extern uint8_t __kernel_virt_start[];
extern uint8_t __kernel_virt_end[];

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static uint64_t test_button_click_count = 0;

static void on_test_button_click(void) {
    test_button_click_count++;
    serial_write_string("Button clicked! (click count=");
    serial_write_uint(test_button_click_count);
    serial_write_string(")\n");
}

#if defined(GOS_TEST_STALE_WINDOW_DISPATCH)
/* Phase 13, Milestone 13.6 repro: two deliberately-overlapping buttons in
 * the same window - button 0 (dispatched first) closes the window from
 * inside its own on_click(), button 1 (same rect, dispatched second)
 * just increments a counter. Before the fix, the dispatch loop kept
 * iterating win->buttons[] after button 0's callback closed the window,
 * still firing button 1's callback against the now-stale (but not yet
 * cleared) slot. */
static int stale_test_win = -1;
static uint64_t stale_test_second_fired = 0;

static void on_stale_test_close(void) {
    serial_write_string("TEST: stale-dispatch button 0 fired (closing window)\n");
    window_close(stale_test_win);
}

static void on_stale_test_second(void) {
    stale_test_second_fired++;
    serial_write_string("TEST: stale-dispatch button 1 fired (BUG if this appears - window should already be closed)\n");
}
#endif

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

#if defined(GOS_TEST_STACK_OVERFLOW)
__attribute__((noinline))
static void stack_overflow_recurse(uint64_t depth) {
    volatile uint8_t junk[256];
    for (int i = 0; i < 256; i++) {
        junk[i] = (uint8_t)(depth + i); /* prevent the array from being optimized away */
    }
    stack_overflow_recurse(depth + 1);
}
#endif

/* Milestone 11.1 stress test: the plan calls for "rapidly create/delete/
 * rename files and open/close windows for several minutes without a
 * crash." A literal several-minute run isn't practical for an automated
 * headless boot-time self-test, so this runs a large, fixed number of
 * cycles instead (fast - seconds, not minutes) as the automated
 * equivalent; see version1/phase11.md for the live -display cocoa command used to
 * additionally soak-test this interactively for real minutes. Must run
 * after fat32_init() and window_system_init(), and with no other windows
 * open yet (so window slots are free to cycle through). */
static void stress_test(void) {
    serial_write_string("Stress test: running file + window create/delete/rename cycles...\n");

    int file_cycles_ok = 0;
    int file_ok = 1;
    for (int i = 0; i < 150; i++) {
        if (!fat_create_file("STRESS.TXT")) { file_ok = 0; break; }
#if defined(GOS_TEST_STRESS_LEAK)
        /* Finding #16 repro: force a failure right after create on
         * iteration 5, simulating a real mid-cycle failure (e.g. a
         * write error) - STRESS.TXT now exists on disk from the create
         * step, with rename/delete never reached, exactly the partial
         * state that used to leak permanently. */
        if (i == 5) {
            serial_write_string("TEST: forcing a mid-cycle failure at iteration 5 (STRESS.TXT created, not written/renamed/deleted)\n");
            file_ok = 0;
            break;
        }
#endif
        if (!fat_write_file("STRESS.TXT", (const uint8_t *)"x", 1)) { file_ok = 0; break; }
        if (!fat_rename("STRESS.TXT", "STRESSR.TXT")) { file_ok = 0; break; }
        if (!fat_delete_file("STRESSR.TXT")) { file_ok = 0; break; }
        file_cycles_ok++;
    }
    if (!file_ok) {
        /* Finding #16: the create->write->rename->delete cycle breaks on
         * the first failed step with no cleanup, leaving whichever of
         * STRESS.TXT/STRESSR.TXT the cycle got up to permanently on
         * disk. Whichever name the loop broke at, the OTHER one is
         * guaranteed not to exist yet (rename/delete haven't happened),
         * so trying to delete both and ignoring which one (if either)
         * actually existed is a safe, complete cleanup regardless of
         * which specific step failed. */
        fat_delete_file("STRESS.TXT");
        fat_delete_file("STRESSR.TXT");
        serial_write_string("Stress test: cleaned up STRESS.TXT/STRESSR.TXT after partial failure\n");
    }

    int win_cycles_ok = 0;
    int win_ok = 1;
    for (int i = 0; i < 300; i++) {
        int w = window_create(0, 0, 50, 50, fb_make_color(1, 1, 1), fb_make_color(1, 1, 1), "S");
        if (w < 0) { win_ok = 0; break; }
        window_close(w);
        win_cycles_ok++;
    }

    serial_write_string("Stress test: ");
    serial_write_string((file_ok && win_ok) ? "PASS (" : "FAIL (");
    serial_write_uint((uint64_t)file_cycles_ok);
    serial_write_string(" file cycles, ");
    serial_write_uint((uint64_t)win_cycles_ok);
    serial_write_string(" window cycles, no crash)\n");
}

static const char *memmap_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return "USABLE";
        case LIMINE_MEMMAP_RESERVED: return "RESERVED";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "ACPI_RECLAIMABLE";
        case LIMINE_MEMMAP_ACPI_NVS: return "ACPI_NVS";
        case LIMINE_MEMMAP_BAD_MEMORY: return "BAD_MEMORY";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "BOOTLOADER_RECLAIMABLE";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES: return "KERNEL_AND_MODULES";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "FRAMEBUFFER";
        default: return "UNKNOWN";
    }
}

void _start(void) {
    serial_init();
    serial_write_string("\n=== gOS booting... ===\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_write_string("PANIC: Limine base revision not supported\n");
        hcf();
    }
    serial_write_string("Limine base revision: supported\n");

    gdt_init();
    {
        uint16_t cs, ds, ss, tr;
        __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
        __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
        __asm__ volatile ("mov %%ss, %0" : "=r"(ss));
        __asm__ volatile ("str %0" : "=r"(tr));
        serial_write_string("GDT loaded. CS=");
        serial_write_hex64(cs);
        serial_write_string(" DS=");
        serial_write_hex64(ds);
        serial_write_string(" SS=");
        serial_write_hex64(ss);
        serial_write_string(" TR=");
        serial_write_hex64(tr);
        serial_write_string("\n");
    }

    idt_init();
    serial_write_string("IDT loaded (256 entries, vectors 0-31 exceptions, 32-47 reserved for IRQs)\n");

#if defined(GOS_TEST_DIVIDE_BY_ZERO)
    serial_write_string("TEST: deliberately triggering divide-by-zero...\n");
    {
        volatile int a = 10, b = 0;
        volatile int c = a / b;
        (void)c;
    }
#elif defined(GOS_TEST_PAGE_FAULT)
    serial_write_string("TEST: deliberately triggering page fault (write to unmapped address)...\n");
    {
        volatile uint64_t *bad_ptr = (volatile uint64_t *)0xdeadbeef000ULL;
        *bad_ptr = 0x1234;
    }
#endif

    if (memmap_request.response == 0) {
        serial_write_string("PANIC: no memmap response from Limine\n");
        hcf();
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    serial_write_string("Memory map entries: ");
    serial_write_uint(memmap->entry_count);
    serial_write_string("\n");

    uint64_t usable_bytes = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        serial_write_string("  [");
        serial_write_uint(i);
        serial_write_string("] base=");
        serial_write_hex64(e->base);
        serial_write_string(" length=");
        serial_write_hex64(e->length);
        serial_write_string(" type=");
        serial_write_string(memmap_type_name(e->type));
        serial_write_string("\n");
        if (e->type == LIMINE_MEMMAP_USABLE) {
            usable_bytes += e->length;
        }
    }
    serial_write_string("Total usable memory: ");
    serial_write_uint(usable_bytes / 1024 / 1024);
    serial_write_string(" MiB\n");

    if (framebuffer_request.response == 0 ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_write_string("PANIC: no framebuffer response from Limine\n");
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    serial_write_string("Framebuffer address: ");
    serial_write_hex64((uint64_t)fb->address);
    serial_write_string("\nFramebuffer width: ");
    serial_write_uint(fb->width);
    serial_write_string("\nFramebuffer height: ");
    serial_write_uint(fb->height);
    serial_write_string("\nFramebuffer pitch: ");
    serial_write_uint(fb->pitch);
    serial_write_string("\nFramebuffer bpp: ");
    serial_write_uint(fb->bpp);
    serial_write_string("\n");

    pic_remap();
    serial_write_string("PIC remapped: IRQ0-7 -> vectors 32-39, IRQ8-15 -> vectors 40-47\n");

#if defined(GOS_TEST_SPURIOUS_IRQ_CHECK)
    /* Finding #14 repro: neither IRQ7 (LPT) nor IRQ15 (secondary ATA
     * channel) has a driver registered in this kernel, so neither can
     * genuinely be "in service" right now - calling pic_send_eoi()
     * for them synthetically exercises exactly the spurious-IRQ path
     * (ISR bit clear) live, without needing real spurious hardware
     * timing. Confirms the ISR-register read actually executes and the
     * spurious branch is taken (no full EOI sent) instead of assuming
     * it from code review alone. */
    serial_write_string("TEST: synthetic pic_send_eoi(7) and pic_send_eoi(15) (spurious IRQ repro)...\n");
    uint32_t isr_reads_before = pic_debug_isr_read_count();
    pic_send_eoi(7);
    pic_send_eoi(15);
    uint32_t isr_reads_after = pic_debug_isr_read_count();
    serial_write_string("TEST: ISR-register reads before=");
    serial_write_uint((uint64_t)isr_reads_before);
    serial_write_string(" after=");
    serial_write_uint((uint64_t)isr_reads_after);
    serial_write_string(isr_reads_after == isr_reads_before + 2 ? " (both EOI calls read ISR - OK)\n" : " (BUG - ISR read did not execute for both calls)\n");
#endif

    timer_init();
    __asm__ volatile ("sti");
    serial_write_string("Interrupts enabled (sti). Waiting for timer ticks...\n");

    if (hhdm_request.response == 0) {
        serial_write_string("PANIC: no HHDM response from Limine\n");
        hcf();
    }
    pmm_init(memmap, hhdm_request.response->offset);
    pmm_self_test();

    if (kernel_address_request.response == 0) {
        serial_write_string("PANIC: no kernel address response from Limine\n");
        hcf();
    }
    vmm_init(hhdm_request.response->offset,
              kernel_address_request.response->physical_base,
              (uint64_t)__kernel_virt_start,
              (uint64_t)__kernel_virt_end);

#if defined(GOS_TEST_VMM_UNMAP)
    /* Phase 13, Milestone 13.5: map a scratch virtual address to physical
     * page A, prime the TLB by reading through it, unmap, remap the SAME
     * virtual address to a different physical page B, then read again
     * through the virtual address WITHOUT any intervening write (a write
     * would go through whatever mapping the TLB currently resolves to,
     * confounding the result either way). Page A/B are seeded directly
     * via their identity-mapped physical addresses (vmm_init identity-maps
     * the first 4GiB) - an always-fresh path independent of scratch_virt's
     * TLB state - so the final read's value alone reveals whether the
     * remap took effect or a stale TLB entry is still resolving to A. */
    {
        const uint64_t scratch_virt = 0xffffffff70001000ULL;
        uint64_t phys_a = pmm_alloc_page();
        uint64_t phys_b = pmm_alloc_page();
        serial_write_string("TEST: vmm_unmap_page - phys_a=0x");
        serial_write_hex64(phys_a);
        serial_write_string(" phys_b=0x");
        serial_write_hex64(phys_b);
        serial_write_string("\n");

        *(volatile uint64_t *)phys_a = 0xAAAAAAAAAAAAAAAAULL; /* seed A via identity map */
        *(volatile uint64_t *)phys_b = 0xBBBBBBBBBBBBBBBBULL; /* seed B via identity map */

        vmm_map_page(scratch_virt, phys_a, PAGE_WRITABLE);
        volatile uint64_t *p = (volatile uint64_t *)scratch_virt;
        uint64_t primed = *p; /* warm the TLB entry for scratch_virt -> phys_a */
        serial_write_string("TEST: scratch_virt mapped to page A, primed read = 0x");
        serial_write_hex64(primed);
        serial_write_string("\n");

        vmm_unmap_page(scratch_virt);
        vmm_map_page(scratch_virt, phys_b, PAGE_WRITABLE);

        uint64_t readback = *p; /* no intervening write - pure resolve test */
        serial_write_string("TEST: after unmap+remap to page B (no write in between), read = 0x");
        serial_write_hex64(readback);
        serial_write_string(readback == 0xBBBBBBBBBBBBBBBBULL ? " (correct - reflects page B)\n" : " (STALE - still shows page A!)\n");

        vmm_unmap_page(scratch_virt);
    }
#endif

    heap_init();
    heap_self_test();

#if defined(GOS_DIAGNOSTIC_BOOT)
    /* Milestone 18.1: the full timer self-test deliberately sleeps 2000ms
     * to measure real elapsed ticks - a genuine correctness check, but not
     * something a normal boot needs to pay for every single time. Gated
     * behind the same diagnostic flag as the other regression demos below;
     * the default path just confirms the PIT is ticking at all (no sleep). */
    timer_self_test();
#else
    serial_write_string("Timer: PIT running (tick=");
    serial_write_uint(timer_get_ticks());
    serial_write_string(") - full 2000ms self-test skipped (define GOS_DIAGNOSTIC_BOOT to run it)\n");
#endif

#if defined(GOS_TEST_USERMODE)
    /* Milestone 19.1/19.2: run the tiny hand-assembled ring3 test blob -
     * proves the GDT user segments + TSS.rsp0 wiring and the int 0x80
     * syscall gate both work, before the ELF loader (Milestone 19.3,
     * tested separately below once FAT32 is up) adds any more moving
     * parts. Placed here (after heap/PMM/VMM are all live, before
     * keyboard/ATA/FAT32) since it only needs the VMM's page-mapping API
     * and the syscall gate, both already initialized by this point. */
    serial_write_string("TEST: running Milestone 19.1/19.2 ring3 test blob...\n");
    int ring3_ok = usermode_run_ring3_test();
    serial_write_string("TEST: usermode_run_ring3_test() = ");
    serial_write_string(ring3_ok ? "1 (OK)" : "0 (FAILED)");
    serial_write_string("\n");
    serial_write_string("TEST: syscall_last_caller_cs() = 0x");
    serial_write_hex64(syscall_last_caller_cs());
    serial_write_string(syscall_last_caller_cs() & 3 ? " (RPL=3 - genuinely called from ring 3)\n"
                                                       : " (RPL != 3 - BUG, not actually ring 3)\n");
#endif

    keyboard_init();
    serial_write_string("Keyboard driver initialized (IRQ1 unmasked)\n");

#if defined(GOS_TEST_KEYBOARD)
    serial_write_string("TEST: reading 5 characters from keyboard (waiting for keypresses)...\n");
    for (int i = 0; i < 5; i++) {
        char c = kb_getchar();
        serial_write_string("Got char: '");
        serial_write_char(c);
        serial_write_string("' (");
        serial_write_hex64((uint64_t)(uint8_t)c);
        serial_write_string(")\n");
    }
    serial_write_string("TEST: keyboard test complete\n");
#endif

    /* Milestone 8.1: bring up the ATA PIO driver and read sector 0 of the
     * attached disk, logging its boot-signature bytes and a hex dump of
     * the first 16 bytes to serial for verification. */
    ata_init();
    {
        static uint8_t sector0[ATA_SECTOR_SIZE];
        if (!ata_read_sector(0, sector0)) {
            serial_write_string("ATA: ERROR - failed to read sector 0 (non-fatal, boot continues)\n");
#if defined(GOS_TEST_ATA_PROBE)
            extern uint32_t ata_debug_busy_wait_reads;
            serial_write_string("TEST: ata_wait_not_busy() status-port reads for this failed call = ");
            serial_write_uint((uint64_t)ata_debug_busy_wait_reads);
            serial_write_string(ata_debug_busy_wait_reads == 0 ? " (fast-fail - probe correctly skipped the busy-wait)\n" : " (BUG - busy-wait ran despite no drive detected)\n");
#endif
        } else {
            serial_write_string("ATA: sector 0 read OK. First 16 bytes: ");
            for (int i = 0; i < 16; i++) {
                serial_write_hex64(sector0[i]);
                serial_write_string(" ");
            }
            serial_write_string("\nATA: boot signature (bytes 510-511): ");
            serial_write_hex64(sector0[510]);
            serial_write_string(" ");
            serial_write_hex64(sector0[511]);
            serial_write_string(sector0[510] == 0x55 && sector0[511] == 0xAA ? " (valid 0x55AA)\n" : " (INVALID)\n");
        }
    }

    /* Milestone 8.2: parse the FAT32 BPB, list the root directory and a
     * nested subdirectory, and read a known host-created file back to
     * verify byte-for-byte correctness. */
    if (fat32_init()) {
        struct fat_dirent entries[FAT32_MAX_DIRENTS];
        int count = fat_list_dir(fat32_root_cluster(), entries, FAT32_MAX_DIRENTS);
        serial_write_string("FAT32: root directory (");
        serial_write_uint((uint64_t)count);
        serial_write_string(" entries):\n");
        for (int i = 0; i < count; i++) {
            serial_write_string("  ");
            serial_write_string(entries[i].name);
            serial_write_string((entries[i].attr & FAT32_ATTR_DIRECTORY) ? " <DIR>" : "");
            if (!(entries[i].attr & FAT32_ATTR_DIRECTORY)) {
                serial_write_string(" size=");
                serial_write_uint(entries[i].size);
            }
            serial_write_string("\n");
        }

#if defined(GOS_TEST_ZEROLEN_WRITE)
        /* Phase 12, Milestone 12.1 repro: ZEROLEN.TXT is a zero-byte file
         * created externally via mtools (mcopy of an empty host file),
         * which leaves first_cluster == 0 on disk - a legitimate on-disk
         * state fat_write_file must handle without underflowing
         * existing_count. Before the fix, this call corrupted an
         * arbitrary FAT sector (wild write) instead of returning cleanly. */
        serial_write_string("TEST: fat_write_file(ZEROLEN.TXT) - first_cluster==0 repro...\n");
        int zerolen_ok = fat_write_file("ZEROLEN.TXT", (const uint8_t *)"hi", 2);
        serial_write_string("TEST: fat_write_file(ZEROLEN.TXT) = ");
        serial_write_string(zerolen_ok ? "1 (OK)" : "0 (FAILED)");
        serial_write_string("\n");
        {
            uint8_t verify_buf[8];
            int64_t vn = fat_read_file("ZEROLEN.TXT", verify_buf, sizeof(verify_buf) - 1);
            if (vn >= 0) {
                verify_buf[vn] = '\0';
                serial_write_string("TEST: read back ZEROLEN.TXT (");
                serial_write_uint((uint64_t)vn);
                serial_write_string(" bytes): \"");
                serial_write_string((const char *)verify_buf);
                serial_write_string("\"\n");
            } else {
                serial_write_string("TEST: read back ZEROLEN.TXT FAILED\n");
            }
        }
#endif

        static uint8_t file_buf[512];
        int64_t n = fat_read_file("HOSTFILE.TXT", file_buf, sizeof(file_buf) - 1);
        if (n < 0) {
            serial_write_string("FAT32: ERROR - failed to read HOSTFILE.TXT (non-fatal, boot continues)\n");
        } else {
            file_buf[n] = '\0';
            serial_write_string("FAT32: read HOSTFILE.TXT (");
            serial_write_uint((uint64_t)n);
            serial_write_string(" bytes): \"");
            serial_write_string((const char *)file_buf);
            serial_write_string("\"\n");
        }

        n = fat_read_file("TESTDIR/NESTED.TXT", file_buf, sizeof(file_buf) - 1);
        if (n < 0) {
            serial_write_string("FAT32: ERROR - failed to read TESTDIR/NESTED.TXT (non-fatal, boot continues)\n");
        } else {
            file_buf[n] = '\0';
            serial_write_string("FAT32: read TESTDIR/NESTED.TXT (");
            serial_write_uint((uint64_t)n);
            serial_write_string(" bytes): \"");
            serial_write_string((const char *)file_buf);
            serial_write_string("\"\n");
        }

#if defined(GOS_TEST_CYCLIC_CHAIN)
        /* Phase 12, Milestone 12.4 repro: TESTDIR's single directory
         * cluster has been corrupted (via a host-side scratch-image edit)
         * two ways - (1) filled with 16 valid-looking, non-deleted,
         * non-zero dirents so fat_list_dir never finds a 0x00
         * end-of-directory marker within that cluster, and (2) FAT[TESTDIR's
         * cluster] points back to itself instead of end-of-chain. This
         * forces fat_list_dir to call fat_get_next_cluster() repeatedly
         * with no way to naturally terminate. Before the fix this is a
         * genuine infinite loop; after the fix it bails out after
         * total_clusters steps and logs a cycle-detected message instead
         * of hanging. */
        serial_write_string("TEST: listing TESTDIR (cyclic FAT chain repro, non-terminated cluster)...\n");
        {
            struct fat_dirent testdir_entry;
            if (fat_resolve_path("TESTDIR", &testdir_entry)) {
                static struct fat_dirent children[FAT32_MAX_DIRENTS];
                int c = fat_list_dir(testdir_entry.first_cluster, children, FAT32_MAX_DIRENTS);
                serial_write_string("TEST: fat_list_dir(TESTDIR) returned (");
                serial_write_uint((uint64_t)c);
                serial_write_string(" entries) - did not hang\n");
            } else {
                serial_write_string("TEST: could not resolve TESTDIR\n");
            }
        }
        serial_write_string("TEST: cyclic chain repro complete, kernel still responsive\n");
#endif

        /* Milestone 8.3: write support. PERSIST.TXT is deliberately never
         * deleted - on the first boot against a fresh disk image,
         * fat_create_file succeeds and the content is written; on any
         * later boot against the SAME (unreformatted) disk image,
         * fat_create_file correctly FAILS (already exists) while the
         * read immediately after still returns the exact content written
         * by a PRIOR boot - that is the persistence-across-reboot proof,
         * verified without needing any special two-phase test harness. */
        int created = fat_create_file("PERSIST.TXT");
        serial_write_string("FAT32: fat_create_file(PERSIST.TXT) = ");
        serial_write_string(created ? "1 (created new)" : "0 (already exists)");
        serial_write_string("\n");
        if (created) {
            const char *msg = "This file persists across reboots!";
            int wrote = fat_write_file("PERSIST.TXT", (const uint8_t *)msg, 35);
            serial_write_string("FAT32: fat_write_file(PERSIST.TXT) = ");
            serial_write_string(wrote ? "1 (OK)" : "0 (FAILED)");
            serial_write_string("\n");
        }
        n = fat_read_file("PERSIST.TXT", file_buf, sizeof(file_buf) - 1);
        if (n < 0) {
            serial_write_string("FAT32: ERROR - failed to read PERSIST.TXT (non-fatal, boot continues)\n");
        } else {
            file_buf[n] = '\0';
            serial_write_string("FAT32: read PERSIST.TXT (");
            serial_write_uint((uint64_t)n);
            serial_write_string(" bytes): \"");
            serial_write_string((const char *)file_buf);
            serial_write_string("\"\n");
        }

        /* TEMP.TXT and TEMPDIR are created and deleted every boot, proving
         * create/write/delete for both files and directories work as a
         * self-contained, repeatable cycle (unlike PERSIST.TXT above,
         * these must NOT accumulate leftover state run after run). */
        if (fat_create_file("TEMP.TXT")) {
            const char *msg2 = "temporary";
            fat_write_file("TEMP.TXT", (const uint8_t *)msg2, 9);
            n = fat_read_file("TEMP.TXT", file_buf, sizeof(file_buf) - 1);
            file_buf[n < 0 ? 0 : n] = '\0';
            serial_write_string("FAT32: TEMP.TXT created+written+read: \"");
            serial_write_string((const char *)file_buf);
            serial_write_string("\"\n");
        }
        int deleted_file = fat_delete_file("TEMP.TXT");
        serial_write_string("FAT32: fat_delete_file(TEMP.TXT) = ");
        serial_write_string(deleted_file ? "1 (OK)" : "0 (FAILED)");
        serial_write_string("\n");
        struct fat_dirent check;
        int still_there = fat_resolve_path("TEMP.TXT", &check);
        serial_write_string("FAT32: TEMP.TXT still resolvable after delete = ");
        serial_write_string(still_there ? "1 (BUG)" : "0 (correctly gone)");
        serial_write_string("\n");

        if (fat_create_dir("TEMPDIR")) {
            serial_write_string("FAT32: fat_create_dir(TEMPDIR) = 1 (OK)\n");
            if (fat_create_file("TEMPDIR/INNER.TXT")) {
                const char *msg3 = "nested write test";
                fat_write_file("TEMPDIR/INNER.TXT", (const uint8_t *)msg3, 18);
                n = fat_read_file("TEMPDIR/INNER.TXT", file_buf, sizeof(file_buf) - 1);
                file_buf[n < 0 ? 0 : n] = '\0';
                serial_write_string("FAT32: TEMPDIR/INNER.TXT created+written+read: \"");
                serial_write_string((const char *)file_buf);
                serial_write_string("\"\n");
            }
            int del_inner = fat_delete_file("TEMPDIR/INNER.TXT");
            int del_dir = fat_delete_dir("TEMPDIR");
            serial_write_string("FAT32: cleanup - delete INNER.TXT=");
            serial_write_string(del_inner ? "1" : "0");
            serial_write_string(" delete TEMPDIR=");
            serial_write_string(del_dir ? "1" : "0");
            serial_write_string("\n");
        }

#if defined(GOS_TEST_CREATE_ENTRY_ROLLBACK)
        /* Phase 13, Milestone 13.4: force a real directory-growth cycle
         * (16 entries fit in one 512-byte cluster; create enough files
         * in TESTDIR to exceed that, so find_free_slot must allocate and
         * link a new cluster for real) and confirm it completes cleanly
         * with no leaked/dangling clusters afterward - a regression
         * check that the create_entry/find_free_slot rollback edits
         * didn't break the normal (non-failure) growth path. The
         * specific write/link/read-failure rollback branches themselves
         * require a genuine ATA I/O fault to exercise live, which (as
         * with Milestone 13.1) isn't practical to inject on QEMU's
         * emulated IDE disk - verified via code review instead. */
        serial_write_string("TEST: forcing TESTDIR directory growth...\n");
        char grow_path[32];
        int grow_ok = 1;
        for (int i = 0; i < 20; i++) {
            grow_path[0] = 'T'; grow_path[1] = 'E'; grow_path[2] = 'S'; grow_path[3] = 'T';
            grow_path[4] = 'D'; grow_path[5] = 'I'; grow_path[6] = 'R'; grow_path[7] = '/';
            grow_path[8] = 'G';
            grow_path[9] = (char)('0' + (i / 10));
            grow_path[10] = (char)('0' + (i % 10));
            grow_path[11] = '.'; grow_path[12] = 'T'; grow_path[13] = 'X'; grow_path[14] = 'T';
            grow_path[15] = '\0';
            if (!fat_create_file(grow_path)) {
                grow_ok = 0;
                serial_write_string("TEST: fat_create_file failed at i=");
                serial_write_uint((uint64_t)i);
                serial_write_string("\n");
                break;
            }
        }
        serial_write_string("TEST: directory growth create loop = ");
        serial_write_string(grow_ok ? "1 (all 20 created OK)" : "0 (FAILED)");
        serial_write_string("\n");
        /* Clean up so the disk image is left in its original state. */
        int grow_cleanup_ok = 1;
        for (int i = 0; i < 20; i++) {
            grow_path[0] = 'T'; grow_path[1] = 'E'; grow_path[2] = 'S'; grow_path[3] = 'T';
            grow_path[4] = 'D'; grow_path[5] = 'I'; grow_path[6] = 'R'; grow_path[7] = '/';
            grow_path[8] = 'G';
            grow_path[9] = (char)('0' + (i / 10));
            grow_path[10] = (char)('0' + (i % 10));
            grow_path[11] = '.'; grow_path[12] = 'T'; grow_path[13] = 'X'; grow_path[14] = 'T';
            grow_path[15] = '\0';
            if (!fat_delete_file(grow_path)) {
                grow_cleanup_ok = 0;
            }
        }
        serial_write_string("TEST: directory growth cleanup = ");
        serial_write_string(grow_cleanup_ok ? "1 (all 20 deleted OK)" : "0 (FAILED)");
        serial_write_string("\n");
#endif
#if defined(GOS_TEST_USERMODE)
        /* Milestone 19.3: load and run the real, separately-built ELF64
         * binary bundled on the disk image (tools/userland/hello.asm,
         * linked to a fixed address distinct from the 19.1 blob's) -
         * proves the loader itself, not just the ring3/syscall mechanics
         * the 19.1/19.2 test above already covers. */
        serial_write_string("TEST: running Milestone 19.3 ELF loader test (HELLO.ELF)...\n");
        int elf_ok = usermode_run_elf("HELLO.ELF");
        serial_write_string("TEST: usermode_run_elf(\"HELLO.ELF\") = ");
        serial_write_string(elf_ok ? "1 (OK)" : "0 (FAILED)");
        serial_write_string("\n");
#endif
#if defined(GOS_TEST_MULTITASKING)
        /* Milestone 20.1: spawn two independent processes (their own
         * private page tables, own kernel stacks) and let the timer-driven
         * scheduler run them to completion - each writes its own marker
         * character with a spin delay in between, so a genuinely
         * interleaved serial log (not two runs back-to-back) is direct
         * proof of real preemption. */
        process_init();
        serial_write_string("TEST: Milestone 20.1 - spawning SPIN1.ELF and SPIN2.ELF...\n");
        int spin1 = process_spawn("SPIN1.ELF");
        int spin2 = process_spawn("SPIN2.ELF");
        serial_write_string("TEST: spin1=");
        serial_write_uint((uint64_t)(spin1 < 0 ? 0xFFFFFFFF : (uint64_t)spin1));
        serial_write_string(" spin2=");
        serial_write_uint((uint64_t)(spin2 < 0 ? 0xFFFFFFFF : (uint64_t)spin2));
        serial_write_string("\n");
        scheduler_run_until_done();
        serial_write_string("TEST: Milestone 20.1 complete\n");

        /* Milestone 20.2: parent spawns a child, polls SYS_WAITPID until
         * the child's exit code (7) comes back - proves the process
         * lifecycle syscalls (spawn/exit/waitpid) round-trip correctly. */
        serial_write_string("TEST: Milestone 20.2 - spawning PARENT.ELF (spawns CHILD.ELF itself)...\n");
        int parent_pid = process_spawn("PARENT.ELF");
        serial_write_string("TEST: parent_pid=");
        serial_write_uint((uint64_t)(parent_pid < 0 ? 0xFFFFFFFF : (uint64_t)parent_pid));
        serial_write_string("\n");
        scheduler_run_until_done();
        serial_write_string("TEST: Milestone 20.2 complete\n");

        /* Milestone 20.3: 5 concurrent processes under load - confirm none
         * starve (every marker appears the full ITERS times) and, right
         * after, that the desktop's own main loop is still fully alive and
         * interactive (proven the same way Phase 19 proved it: a real
         * simulated click reaching the File Manager after this demo). */
        serial_write_string("TEST: Milestone 20.3 - spawning 5 concurrent processes...\n");
        process_spawn("SPIN1.ELF");
        process_spawn("SPIN2.ELF");
        process_spawn("SPIN3.ELF");
        process_spawn("SPIN4.ELF");
        process_spawn("SPIN5.ELF");
        scheduler_run_until_done();
        serial_write_string("TEST: Milestone 20.3 complete\n");
#endif
    } else {
        serial_write_string("FAT32: PANIC - not a valid FAT32 filesystem\n");
        hcf(); /* BPB globals were never populated - any further FAT32 call
                * (including stress_test() below) would operate on garbage. */
    }

    fb_init(fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
            fb->red_mask_shift, fb->green_mask_shift, fb->blue_mask_shift);
    fb_clear(fb_make_color(0, 64, 128)); /* dark blue, to prove real pixel writes over the raw framebuffer */

#if defined(GOS_TEST_STACK_OVERFLOW)
    /* Phase 12, Milestone 12.5 repro: unbounded recursion that writes to
     * a stack-local array on every call, forcing genuine stack growth
     * (not optimized to a loop - built at -O0, and the volatile write
     * plus recursive call prevent tail-call/inlining), same as a real
     * stack-overflow bug would. vmm_init() identity-maps the entire
     * first 4GiB, so there's no nearby unmapped guard page below the
     * normal boot stack for recursion to hit in a practical amount of
     * time - to keep the repro fast and deterministic, RSP is first
     * pointed near the bottom of the address space (0x1000) so a
     * handful of recursive calls underflow past address 0 into
     * genuinely unmapped/non-canonical memory. The first fault there
     * exhausts that same bad RSP, so pushing the fault handler's own
     * frame immediately faults again -> double fault (vector 8).
     * Confirms IST1 routes vector 8 to a separate, valid stack instead
     * of reusing the exhausted one. Placed after fb_init() (unlike the
     * other GOS_TEST_* triggers) because panic_screen(), called from the
     * exception handler, needs the framebuffer ready to actually draw. */
    serial_write_string("TEST: deliberately overflowing the stack to trigger a double fault...\n");
    __asm__ volatile (
        "mov $0x1000, %%rsp\n"
        "xor %%rdi, %%rdi\n"
        "call stack_overflow_recurse\n"
        :
        :
        : "memory"
    );
#endif

    /* Milestone 5.2 test pattern: nested rectangles + diagonal lines,
     * exercising fb_draw_rect, fb_draw_rect_outline, and fb_draw_line
     * (including all four line-slope octants) in one visually-checkable
     * frame. */
    fb_draw_rect(100, 100, 300, 200, fb_make_color(200, 30, 30));
    fb_draw_rect_outline(100, 100, 300, 200, fb_make_color(255, 255, 255), 4);
    fb_draw_rect_outline(50, 50, 400, 300, fb_make_color(30, 200, 30), 2);
    fb_draw_line(500, 100, 900, 100, fb_make_color(255, 255, 0));   /* horizontal */
    fb_draw_line(500, 100, 500, 400, fb_make_color(255, 255, 0));   /* vertical */
    fb_draw_line(500, 100, 900, 400, fb_make_color(255, 0, 255));   /* shallow diagonal */
    fb_draw_line(500, 400, 900, 100, fb_make_color(0, 255, 255));   /* steep diagonal, opposite direction */
    serial_write_string("FB: test pattern drawn (nested rects + 4 lines)\n");

    /* Milestone 5.3: switch all drawing to a heap-backed back buffer and
     * animate a bouncing rectangle across ~40 frames, flipping each frame.
     * This is the practical headless equivalent of "watch for tearing on
     * screen": each fb_flip() must present one complete, uncorrupted frame
     * (background + rectangle at its new position), verifiable via
     * screendumps taken at different points during the animation. */
    fb_backbuffer_init();
#if defined(GOS_TEST_FB_BACKBUFFER_REENTRY)
    /* Finding #22 repro: call fb_backbuffer_init() a second time and
     * confirm via PMM's free-page count that no additional multi-MB
     * allocation happened (the guard should return early instead of
     * silently overwriting back_buffer with a second kmalloc()). */
    {
        uint64_t free_before = pmm_free_pages();
        serial_write_string("TEST: calling fb_backbuffer_init() a second time...\n");
        fb_backbuffer_init();
        uint64_t free_after = pmm_free_pages();
        serial_write_string("TEST: PMM free pages before=");
        serial_write_uint(free_before);
        serial_write_string(" after=");
        serial_write_uint(free_after);
        serial_write_string(free_before == free_after ? " (unchanged - no double allocation)\n" : " (BUG - second call allocated more memory)\n");
    }
#endif
#if defined(GOS_DIAGNOSTIC_BOOT)
    /* Milestone 18.1: this ~2-second bouncing-rectangle animation (Milestone
     * 5.3's original tearing/double-buffer proof) is a one-time regression
     * demo, not something a normal boot needs to redraw every time - gated
     * behind GOS_DIAGNOSTIC_BOOT along with the other slow demos below. */
    int64_t box_x = 0;
    int64_t box_dx = 20;
    const int64_t box_w = 80, box_h = 80;
    const int64_t area_w = 700;
    for (int frame = 0; frame < 40; frame++) {
        fb_clear(fb_make_color(20, 20, 20));
        fb_draw_rect_outline(0, 0, area_w, 200, fb_make_color(120, 120, 120), 2);
        fb_draw_rect(box_x, 60, box_w, box_h, fb_make_color(255, 140, 0));
        fb_flip();

        box_x += box_dx;
        if (box_x <= 0 || box_x + box_w >= area_w) {
            box_dx = -box_dx;
        }
        sleep_ms(50);
    }
    serial_write_string("FB: bouncing-rectangle animation complete (40 frames flipped)\n");

    /* Milestone 7.1: render "Hello, gOS!" at a fixed position using the
     * new bitmap font renderer, and hold that frame for ~2 seconds so it
     * can be captured with a screendump before the mouse/window demos
     * take over the screen. */
    fb_clear(fb_make_color(10, 10, 10));
    fb_draw_string(40, 40, "Hello, gOS!", fb_make_color(255, 255, 255), fb_make_color(10, 10, 10));
    fb_flip();
    serial_write_string("FB: \"Hello, gOS!\" rendered via bitmap font\n");
    sleep_ms(2000);
#endif

    mouse_init();

#if defined(GOS_DIAGNOSTIC_BOOT)
    /* Milestone 6.1 live test: redraw the cursor at its current tracked
     * position for ~5 seconds (100 frames @ 50ms), logging whenever it
     * moves. Real movement is injected externally via the QEMU monitor's
     * `mouse_move`/`mouse_button` commands during this window - see
     * version1/phase6.md for the exact test procedure. Milestone 18.1:
     * gated behind GOS_DIAGNOSTIC_BOOT - the main desktop loop already
     * redraws the cursor every frame, so this demo's only purpose is the
     * standalone regression proof, not anything a normal boot needs. */
    {
        int64_t last_x = -1, last_y = -1;
        uint8_t last_buttons = 0xFF;
        for (int frame = 0; frame < 100; frame++) {
            fb_clear(fb_make_color(20, 20, 20));
            mouse_draw_cursor();
            fb_flip();

            if (mouse_x() != last_x || mouse_y() != last_y || mouse_buttons() != last_buttons) {
                last_x = mouse_x();
                last_y = mouse_y();
                last_buttons = mouse_buttons();
                serial_write_string("Mouse: x=");
                serial_write_uint((uint64_t)last_x);
                serial_write_string(" y=");
                serial_write_uint((uint64_t)last_y);
                serial_write_string(" buttons=");
                serial_write_hex64(last_buttons);
                serial_write_string("\n");
            }
            sleep_ms(50);
        }
    }
    serial_write_string("Mouse test window complete\n");
#endif

    /* Milestones 6.2/6.3/6.4: two overlapping windows (proving z-order and
     * click-to-focus), one draggable by its title bar, one with a button
     * (proving hit-testing/dispatch). Three windows are created (matching
     * the plan's explicit Milestone 6.3 test requirement) so click-to-focus
     * exercises reordering a window out of the middle/back of the z-order,
     * not just a trivial two-element swap. Driven interactively for ~10
     * seconds via QEMU monitor mouse_move/mouse_button commands - see
     * version1/phase6.md for the exact test procedure. */
    window_system_init();
#if defined(GOS_DIAGNOSTIC_BOOT)
    /* Milestone 18.1: this is by far the single biggest contributor to
     * pre-Phase-18 boot time - 150 file create/write/rename/delete cycles
     * plus 300 window create/close cycles, each involving real ATA PIO
     * I/O. Genuinely valuable as a regression/soak test (see Milestone
     * 11.1), but not something a normal interactive boot should pay for
     * every single time. */
    stress_test();
#else
    (void)stress_test; /* silence "defined but not used" - still referenced under GOS_DIAGNOSTIC_BOOT */
#endif
    int win_a = window_create(150, 150, 300, 200, fb_make_color(70, 70, 200), fb_make_color(30, 30, 60), "Window A");
    int win_b = window_create(400, 250, 280, 180, fb_make_color(200, 70, 70), fb_make_color(60, 30, 30), "Window B");
    int win_c = window_create(280, 350, 260, 160, fb_make_color(70, 200, 120), fb_make_color(30, 60, 40), "Text Editor");
    window_add_button(win_a, 20, 20, 120, 30, fb_make_color(80, 200, 80), "Click Me", on_test_button_click);
    window_enable_textbox(win_c);

#if defined(GOS_TEST_STALE_WINDOW_DISPATCH)
    stale_test_win = window_create(700, 500, 220, 120, fb_make_color(200, 200, 70), fb_make_color(60, 60, 20), "Stale Test");
    /* Both buttons cover the exact same rect - button 0 (dispatched
     * first) closes the window; button 1 (dispatched second) must NOT
     * fire once button 0 has closed it. */
    window_add_button(stale_test_win, 10, 10, 180, 80, fb_make_color(200, 80, 80), "Close", on_stale_test_close);
    window_add_button(stale_test_win, 10, 10, 180, 80, fb_make_color(80, 80, 200), "Second", on_stale_test_second);
#endif
    serial_write_string("Window system initialized: window_a=");
    serial_write_uint((uint64_t)win_a);
    serial_write_string(" window_b=");
    serial_write_uint((uint64_t)win_b);
    serial_write_string(" window_c=");
    serial_write_uint((uint64_t)win_c);
    serial_write_string(" (1 button on window_a)\n");

    /* Milestone 11.2: the File Manager no longer auto-opens at boot - it's
     * launched on demand by clicking the "Files" desktop icon
     * (desktop_update()/desktop_render(), kernel/src/desktop.c). Windows
     * A/B/C above are still created and configured here (they're Phase
     * 6/7 regression demos proving window_create/window_add_button/
     * window_enable_textbox all still work end-to-end on every boot) but
     * are closed again immediately below, before the compositor's main
     * loop ever draws a frame - so they never actually appear on screen,
     * and (Finding #12) don't permanently consume 3 of MAX_WINDOWS=8
     * slots before the user has opened anything real. A taskbar across
     * the bottom of the screen (kernel/src/taskbar.c) lists whatever
     * windows are currently open and lets you click an entry to bring it
     * to front. */
    window_close(win_a);
    window_close(win_b);
    window_close(win_c);
    serial_write_string("Demo windows (Window A/B, Text Editor) auto-closed - ");
    serial_write_uint((uint64_t)window_count_open());
    serial_write_string(" window(s) open, ");
    serial_write_uint((uint64_t)(MAX_WINDOWS - window_count_open()));
    serial_write_string(" slot(s) free\n");
    serial_write_string("Desktop ready - click the \"Files\" icon to launch the File Manager\n");

    /* Milestone 15.3: try to load the bundled BMP wallpaper off the FAT32
     * disk (heap + FAT32 are both up by now); falls back to the built-in
     * gradient (Milestone 15.2) if missing or malformed. */
    wallpaper_init();

#if defined(GOS_TEST_WINDOW_TEARDOWN_LEAK)
    /* Milestone 16.1 repro: open and close a window with a textbox 20 times
     * in a loop, checking heap_free_bytes() before the loop and after every
     * close. window.c's textbox_buffer is a fixed array embedded in struct
     * window (not kmalloc'd), so window_close()'s Phase-13.6 field-clearing
     * is already sufficient teardown - this test exists to prove that with
     * a real before/after heap measurement rather than just code review. */
    {
        uint64_t baseline = heap_free_bytes();
        serial_write_string("TEST: heap_free_bytes() baseline = ");
        serial_write_uint(baseline);
        serial_write_string("\n");
        int leak_detected = 0;
        for (int iter = 0; iter < 20; iter++) {
            int w = window_create(500, 500, 300, 150, fb_make_color(70, 130, 180),
                                   fb_make_color(30, 40, 60), "Teardown Test");
            window_enable_textbox(w);
            struct window *win = window_get(w);
            const char *fill = "unsaved teardown-test content";
            int i = 0;
            for (; fill[i]; i++) {
                win->textbox_buffer[i] = fill[i];
            }
            win->textbox_buffer[i] = '\0';
            win->textbox_length = i;
            window_close(w);
            uint64_t after = heap_free_bytes();
            if (after != baseline) {
                leak_detected = 1;
                serial_write_string("TEST: iteration ");
                serial_write_uint((uint64_t)iter);
                serial_write_string(" heap_free_bytes()=");
                serial_write_uint(after);
                serial_write_string(" (differs from baseline)\n");
            }
        }
        uint64_t final = heap_free_bytes();
        serial_write_string("TEST: after 20 open/close cycles, heap_free_bytes() = ");
        serial_write_uint(final);
        serial_write_string(leak_detected ? " (LEAK - differs from baseline at some point)\n"
                                           : " (matches baseline every iteration - no leak)\n");
    }
#endif
#if defined(GOS_TEST_MAXIMIZE_ROUNDTRIP)
    /* Milestone 17.1 repro: create a window at an arbitrary non-origin
     * geometry, maximize it, then restore it, logging x/y/w/h at each of
     * the three points so a host script can diff "before" against "after
     * restore" and confirm an exact round-trip - not just "restore ran
     * without crashing." */
    {
        int w = window_create(137, 211, 333, 141, fb_make_color(180, 130, 70),
                               fb_make_color(60, 40, 30), "Maximize Test");
        struct window *win = window_get(w);
        serial_write_string("TEST: before maximize x=");
        serial_write_uint((uint64_t)win->x);
        serial_write_string(" y=");
        serial_write_uint((uint64_t)win->y);
        serial_write_string(" w=");
        serial_write_uint(win->w);
        serial_write_string(" h=");
        serial_write_uint(win->h);
        serial_write_string("\n");

        window_maximize_toggle(w);
        serial_write_string("TEST: after maximize x=");
        serial_write_uint((uint64_t)win->x);
        serial_write_string(" y=");
        serial_write_uint((uint64_t)win->y);
        serial_write_string(" w=");
        serial_write_uint(win->w);
        serial_write_string(" h=");
        serial_write_uint(win->h);
        serial_write_string(" maximized=");
        serial_write_uint((uint64_t)window_is_maximized(w));
        serial_write_string("\n");

        window_maximize_toggle(w);
        serial_write_string("TEST: after restore x=");
        serial_write_uint((uint64_t)win->x);
        serial_write_string(" y=");
        serial_write_uint((uint64_t)win->y);
        serial_write_string(" w=");
        serial_write_uint(win->w);
        serial_write_string(" h=");
        serial_write_uint(win->h);
        serial_write_string(" maximized=");
        serial_write_uint((uint64_t)window_is_maximized(w));
        serial_write_string(win->x == 137 && win->y == 211 && win->w == 333 && win->h == 141
                                 ? " (exact round-trip - OK)\n"
                                 : " (MISMATCH - geometry did not round-trip)\n");
        window_close(w);
    }
#endif
#if defined(GOS_TEST_WINDOW_CREATE_FEEDBACK)
    /* Finding #17 repro: fill every remaining window slot, then attempt
     * to open the File Manager (which internally calls window_create())
     * to trigger the exhausted-slots failure path live and confirm the
     * new serial log + on-screen flash both fire instead of the app
     * silently doing nothing. */
    {
        int filler_wins[MAX_WINDOWS];
        int filler_count = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            int w = window_create(600, 600, 40, 40, fb_make_color(80, 80, 80), fb_make_color(20, 20, 20), "Filler");
            if (w < 0) {
                break;
            }
            filler_wins[filler_count++] = w;
        }
        serial_write_string("TEST: filled ");
        serial_write_uint((uint64_t)filler_count);
        serial_write_string(" window slot(s) (");
        serial_write_uint((uint64_t)window_count_open());
        serial_write_string("/");
        serial_write_uint((uint64_t)MAX_WINDOWS);
        serial_write_string(" open)\n");
        serial_write_string("TEST: attempting to open File Manager with no slots free...\n");
        int fm_attempt = fm_create_window(120, 60, 420, 260);
        serial_write_string("TEST: fm_create_window() returned ");
        serial_write_uint((uint64_t)(fm_attempt < 0 ? 0xFFFFFFFF : (uint64_t)fm_attempt));
        serial_write_string(fm_attempt == -1 ? " (-1, correctly failed)\n" : " (unexpected)\n");
        if (fm_attempt == -1) {
            serial_write_string("TEST: fm_create_window() failed (MAX_WINDOWS exhausted) - matches desktop.c's real failure path\n");
            taskbar_flash_message("Could not open File Manager - too many windows open");
        }
        (void)filler_wins;
    }
#endif
#if defined(GOS_TEST_PANIC_SCREEN)
    /* Milestone 11.1 visual test: deliberately trigger a divide-by-zero
     * after the desktop/window system is fully up, so the panic screen's
     * red full-screen display can be screendumped over a realistic
     * graphical scene instead of a blank early-boot frame. */
    for (int frame = 0; frame < 20; frame++) {
        fb_clear(fb_make_color(15, 15, 15));
        window_system_update();
        taskbar_update();
        desktop_update();
        desktop_render();
        window_composite();
        taskbar_render();
        mouse_draw_cursor();
        fb_flip();
        sleep_ms(50);
    }
    serial_write_string("TEST: deliberately triggering divide-by-zero to show the panic screen...\n");
    {
        volatile int a = 10, b = 0;
        volatile int c = a / b;
        (void)c;
    }
#endif

    /* Printed here, before the desktop loop below, rather than after it:
     * every prior phase's headless regression-check scripts grep serial
     * output for this exact line to confirm boot reached the desktop
     * without crashing. It used to be printed after a bounded (frame < N)
     * test loop that existed only so headless screendump scripts had a
     * finite window to run monitor commands in - but that same bound also
     * meant the "real" interactive OS (booted via -display cocoa for
     * actual use, not headless testing) would silently hlt forever once
     * the loop ran out, which looked exactly like a hang after ~25
     * seconds of otherwise-normal use. The desktop loop below now runs
     * forever, as a real OS's event loop should; this line still prints
     * at the same point in boot (right before the desktop becomes
     * interactive) so existing regression-check commands keep working. */
    serial_write_string("=== gOS boot checks complete ===\n");

    for (;;) {
        window_system_update();
        taskbar_update();
        desktop_update();
        desktop_render();
        window_composite();
        taskbar_render();
        /* Milestone 15.1: cursor is the compositor's top layer - drawn
         * after every window AND the taskbar, so it's never occluded. */
        mouse_draw_cursor();
        fb_flip();
        sleep_ms(50);
    }
}
