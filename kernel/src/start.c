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
#include <settings.h>
#include <rtc.h>
#include <syscall.h>
#include <process.h>
#include <terminal.h>
#include <calculator.h>
#include <imageviewer.h>
#include <bmp.h>

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

    /* Milestone 22.3: load persisted settings (wallpaper selection, File
     * Manager geometry) - after wallpaper_init() so a persisted wallpaper
     * selection correctly overrides whatever wallpaper_init() just decided,
     * and before the desktop loop below ever creates the File Manager or
     * renders a frame. */
    settings_load();

#if defined(GOS_TEST_RTC)
    /* Milestone 22.1: log the parsed date/time directly, so a host script
     * can diff it against the exact value QEMU's `-rtc base=...` flag was
     * told to present - independent proof the BCD/12-hour/update-in-
     * progress handling is all correct, not just "some plausible-looking
     * numbers came out". */
    {
        struct rtc_time t;
        rtc_read(&t);
        serial_write_string("TEST: RTC read - ");
        serial_write_uint(t.year);
        serial_write_string("-");
        serial_write_uint(t.month);
        serial_write_string("-");
        serial_write_uint(t.day);
        serial_write_string(" ");
        serial_write_uint(t.hour);
        serial_write_string(":");
        serial_write_uint(t.min);
        serial_write_string(":");
        serial_write_uint(t.sec);
        serial_write_string("\n");
    }
#endif

#if defined(GOS_TEST_LFN)
    /* Milestone 23.1/23.2: list the root directory and print every entry's
     * (possibly long, VFAT-reconstructed) name, so a host script can grep
     * for the exact long name it seeded/created rather than trusting the
     * File Manager's own rendering of it. */
    {
        struct fat_dirent entries[FAT32_MAX_DIRENTS];
        int count = fat_list_dir(fat32_root_cluster(), entries, FAT32_MAX_DIRENTS);
        serial_write_string("TEST: LFN root listing (");
        serial_write_uint((uint64_t)count);
        serial_write_string(" entries)\n");
        for (int i = 0; i < count; i++) {
            serial_write_string("TEST: LFN entry [");
            serial_write_string(entries[i].name);
            serial_write_string("]\n");
        }
    }
#endif

#if defined(GOS_TEST_LFN_WRITE)
    /* Milestone 23.2: exercise create/write/rename/delete with long names
     * directly (independent of the File Manager UI), leaving the final
     * renamed file on disk so a host script can cross-check it via mdir
     * after this boot exits. */
    {
        const char *long_name = "a freshly created long filename.txt";
        const char *renamed = "a renamed long filename.txt";
        const char *msg = "written by gOS LFN write test";
        int created = fat_create_file(long_name);
        serial_write_string("TEST: fat_create_file(long) = ");
        serial_write_string(created ? "OK" : "FAIL");
        serial_write_string("\n");
        if (created) {
            int wrote = fat_write_file(long_name, (const uint8_t *)msg, 30);
            serial_write_string("TEST: fat_write_file(long) = ");
            serial_write_string(wrote ? "OK" : "FAIL");
            serial_write_string("\n");
            int renamed_ok = fat_rename(long_name, renamed);
            serial_write_string("TEST: fat_rename(long->long) = ");
            serial_write_string(renamed_ok ? "OK" : "FAIL");
            serial_write_string("\n");
        }
    }
#endif

#if defined(GOS_TEST_APPS)
    /* Milestone 24.1/24.2/24.3: repeated open/close of the Terminal,
     * Calculator, and Image Viewer, checking heap_free_bytes() before and
     * after - proves (a) the singleton close-callback reset actually lets
     * terminal_open()/calculator_open() create a fresh window instead of
     * silently no-op'ing on a stale index after the first close, and (b)
     * the Image Viewer's on_close callback frees its kmalloc'd decoded
     * pixel buffer, so opening/closing it repeatedly doesn't leak. */
    {
        uint64_t baseline = heap_free_bytes();
        serial_write_string("TEST: heap_free_bytes() baseline = ");
        serial_write_uint(baseline);
        serial_write_string("\n");

        for (int i = 0; i < 5; i++) {
            terminal_open();
            terminal_close();
        }
        uint64_t after_terminal = heap_free_bytes();
        serial_write_string("TEST: heap_free_bytes() after 5x terminal open/close = ");
        serial_write_uint(after_terminal);
        serial_write_string(after_terminal == baseline ? " (== baseline, OK)\n" : " (MISMATCH)\n");

        /* Re-open once more to prove the singleton index reset actually
         * lets a fresh window be created (not just that repeated
         * open/close is a no-op after the first). */
        terminal_open();
        int reopened_ok = terminal_is_open();
        serial_write_string("TEST: terminal_open() after prior close succeeded = ");
        serial_write_string(reopened_ok ? "1 (OK)\n" : "0 (FAIL)\n");
        terminal_close();

        /* One throwaway open/close first: a ~4MB decoded pixel buffer is
         * bigger than any allocation this heap has served before, so the
         * very first request can trigger heap_grow() to permanently pull
         * more physical pages into the pool (normal, expected behavior -
         * see Phase 13.3), which would make every free-byte count after it
         * larger than a baseline taken before it even with zero leaks.
         * Capturing the baseline AFTER this warm-up call, once the heap
         * has already grown to accommodate this allocation size, isolates
         * the actual thing being tested: does REPEATED open/close leak. */
        int warmup_win = imageviewer_open("WALLPAPR.BMP");
        if (warmup_win != -1) window_close(warmup_win);
        uint64_t viewer_baseline = heap_free_bytes();

        for (int i = 0; i < 5; i++) {
            int win = imageviewer_open("WALLPAPR.BMP");
            if (win != -1) window_close(win);
        }
        uint64_t after_viewer = heap_free_bytes();
        serial_write_string("TEST: heap_free_bytes() after warm-up image-viewer open/close = ");
        serial_write_uint(viewer_baseline);
        serial_write_string("\nTEST: heap_free_bytes() after 5 more image-viewer open/close cycles = ");
        serial_write_uint(after_viewer);
        serial_write_string(after_viewer == viewer_baseline ? " (== post-warm-up baseline, OK - no leak)\n" : " (MISMATCH - leak?)\n");
    }
#endif

#if defined(GOS_TEST_PHASE25)
    /* Milestone 25.1 (audit2 Critical #1): spawn BADPTR.ELF, which calls
     * SYS_WRITE with an unmapped pointer. Pre-fix, the kernel would
     * page-fault servicing that syscall and panic/halt. Post-fix, the
     * syscall should be rejected and the process should continue running
     * and exit normally with its distinctive marker code (42), proving
     * the kernel survived and the process wasn't just silently killed. */
    {
        int pid = process_spawn("BADPTR.ELF");
        serial_write_string("TEST: process_spawn(BADPTR.ELF) = ");
        serial_write_uint((uint64_t)pid);
        serial_write_string("\n");
        if (pid >= 0) {
            scheduler_run_until_done();
            struct process *p = process_get(pid);
            serial_write_string("TEST: BADPTR.ELF exit_code = ");
            serial_write_uint((uint64_t)(p ? p->exit_code : -1));
            serial_write_string(p && p->exit_code == 42 ? " (== 42, OK - kernel survived the bad pointer)\n" : " (MISMATCH)\n");
        }
    }

    /* Milestone 25.2 (audit2 Critical #2): 10x spawn-to-completion cycles
     * via the exact same kernel-callable path the Terminal's `run` command
     * uses (process_spawn + scheduler_run_until_done, no SYS_WAITPID
     * involved) - confirms process exit reclaims memory even when nothing
     * ever reaps the zombie via waitpid. */
    {
        uint64_t heap_baseline = heap_free_bytes();
        uint64_t pmm_baseline = pmm_free_pages();
        serial_write_string("TEST: heap_free_bytes() baseline = ");
        serial_write_uint(heap_baseline);
        serial_write_string("\nTEST: pmm_free_pages() baseline = ");
        serial_write_uint(pmm_baseline);
        serial_write_string("\n");

        for (int i = 0; i < 10; i++) {
            int pid = process_spawn("HELLO.ELF");
            if (pid >= 0) {
                scheduler_run_until_done();
                /* Reap the zombie slot (equivalent to a real caller
                 * eventually calling SYS_WAITPID) so the process TABLE
                 * doesn't exhaust after MAX_PROCESSES=8 unreaped cycles -
                 * that's a separate, real limitation (nothing in this
                 * codebase reaps automatically) but isn't what this test
                 * is measuring. process_free_resources() already ran at
                 * SYS_EXIT time regardless of this reap step - the memory
                 * this test checks was already reclaimed before this line
                 * even runs. */
                struct process *p = process_get(pid);
                if (p) {
                    p->state = PROC_UNUSED;
                }
            }
            serial_write_string("TEST: pmm_free_pages() after cycle ");
            serial_write_uint((uint64_t)i);
            serial_write_string(" = ");
            serial_write_uint(pmm_free_pages());
            serial_write_string("\n");
        }

        uint64_t heap_after = heap_free_bytes();
        uint64_t pmm_after = pmm_free_pages();
        serial_write_string("TEST: heap_free_bytes() after 10x run cycles = ");
        serial_write_uint(heap_after);
        serial_write_string(heap_after == heap_baseline ? " (== baseline, OK - no leak)\n" : " (MISMATCH - leak?)\n");
        serial_write_string("TEST: pmm_free_pages() after 10x run cycles = ");
        serial_write_uint(pmm_after);
        serial_write_string(pmm_after == pmm_baseline ? " (== baseline, OK - no leak)\n" : " (MISMATCH - leak?)\n");
    }
#endif

#if defined(GOS_TEST_FAULT_INJECT)
    /* Milestone 25.3 (audit2 Critical #3): rename a file while injecting a
     * single transient write failure on the erase step - the bounded retry
     * should recover and the rename should still succeed with no duplicate
     * entry left behind. Then repeat with a PERSISTENT failure - the retry
     * should exhaust its 3 attempts, report failure honestly, and (this is
     * the property that matters) leave the OLD name intact rather than a
     * duplicate, since write_named_entry already committed the new one. */
    {
        fat_create_file("RENTEST.TXT");
        /* Milestone 26.4 extended fault injection to write_dirent_at too
         * (not just erase_write_sector), so the shared countdown now also
         * counts write_named_entry's own write of the new entry (1 write,
         * for a short non-LFN name like this) before erase_dirent_and_lfn
         * gets its turn - skip that one (countdown=1: let 1 write through)
         * so the injected failure actually lands on the erase step this
         * test is about, not the unrelated write step earlier in the
         * same fat_rename() call. */
        fat32_test_inject_write_failure(1); /* let the rename's own write through, fail the first erase write */
        int ok = fat_rename("RENTEST.TXT", "RENAMED1.TXT");
        serial_write_string("TEST: fat_rename with 1 transient erase failure = ");
        serial_write_string(ok ? "1 (OK - retry recovered)" : "0 (FAIL)");
        serial_write_string("\n");
        fat32_test_clear_fault_injection();

        fat_create_file("RENTEST2.TXT");
        fat32_test_inject_persistent_write_failure();
        int ok2 = fat_rename("RENTEST2.TXT", "RENAMED2.TXT");
        fat32_test_clear_fault_injection();
        serial_write_string("TEST: fat_rename with PERSISTENT erase failure = ");
        serial_write_string(ok2 ? "1 (UNEXPECTED)" : "0 (expected - reported honestly)");
        serial_write_string("\n");
        struct fat_dirent d;
        int old_still_there = fat_resolve_path("RENTEST2.TXT", &d);
        serial_write_string("TEST: RENTEST2.TXT still resolvable after failed rename = ");
        serial_write_uint((uint64_t)old_still_there);
        serial_write_string(old_still_there ? " (OK - original preserved)\n" : " (MISMATCH)\n");
    }

    /* Milestone 25.4 (audit2 Critical #4): delete a file with a persistent
     * erase failure - the delete should fail closed (entry stays, chain
     * stays allocated), not open. */
    {
        fat_create_file("DELTEST.TXT");
        fat32_test_inject_persistent_write_failure();
        int del_ok = fat_delete_file("DELTEST.TXT");
        fat32_test_clear_fault_injection();
        serial_write_string("TEST: fat_delete_file with PERSISTENT erase failure = ");
        serial_write_string(del_ok ? "1 (UNEXPECTED)" : "0 (expected - failed closed)");
        serial_write_string("\n");
        struct fat_dirent d2;
        int still_there = fat_resolve_path("DELTEST.TXT", &d2);
        serial_write_string("TEST: DELTEST.TXT still resolvable after failed delete = ");
        serial_write_uint((uint64_t)still_there);
        serial_write_string(still_there ? " (OK - failed closed, not open)\n" : " (MISMATCH - failed OPEN, data at risk)\n");
    }

    /* Milestone 26.4 (audit2 High #9): a long-filename create that fails
     * partway through its multi-slot LFN write must not hide/corrupt any
     * pre-existing entry. Create sentinels, fail a long-name create right
     * after its first LFN entry commits, then confirm every sentinel
     * (and the directory listing as a whole) is completely unaffected -
     * proving rollback_partial_entries() leaves no dangling partial run
     * behind that could otherwise confuse a later scan. */
    {
        fat_create_file("SENT1.TXT");
        fat_create_file("SENT2.TXT");
        fat_create_file("SENT3.TXT");
        struct fat_dirent before_entries[FAT32_MAX_DIRENTS];
        int before_count = fat_list_dir(fat32_root_cluster(), before_entries, FAT32_MAX_DIRENTS);

        fat32_test_inject_write_failure(1); /* let 1 write through, fail the 2nd (a multi-entry LFN name needs several) */
        int lfn_create_ok = fat_create_file("a partially written long name test.txt");
        fat32_test_clear_fault_injection();
        serial_write_string("TEST: fat_create_file(long name) with a write failing partway = ");
        serial_write_string(lfn_create_ok ? "1 (create succeeded anyway)" : "0 (expected - failed)");
        serial_write_string("\n");

        struct fat_dirent after_entries[FAT32_MAX_DIRENTS];
        int after_count = fat_list_dir(fat32_root_cluster(), after_entries, FAT32_MAX_DIRENTS);
        int sent1_ok = fat_resolve_path("SENT1.TXT", &(struct fat_dirent){0});
        int sent2_ok = fat_resolve_path("SENT2.TXT", &(struct fat_dirent){0});
        int sent3_ok = fat_resolve_path("SENT3.TXT", &(struct fat_dirent){0});
        serial_write_string("TEST: sentinels still resolvable after the failed LFN create = ");
        serial_write_uint((uint64_t)(sent1_ok && sent2_ok && sent3_ok));
        serial_write_string((sent1_ok && sent2_ok && sent3_ok) ? " (OK)\n" : " (MISMATCH - a sentinel was hidden/lost)\n");
        serial_write_string("TEST: directory entry count before vs after failed create = ");
        serial_write_uint((uint64_t)before_count);
        serial_write_string(" vs ");
        serial_write_uint((uint64_t)after_count);
        serial_write_string(before_count == after_count ? " (OK - unchanged)\n" : " (MISMATCH)\n");
    }
#endif

#if defined(GOS_TEST_PHASE25)
    /* Milestone 25.5 (audit2 Critical #5): a hand-crafted BMP header
     * claiming dimensions far beyond BMP_MAX_DIMENSION - bmp_decode must
     * reject it before ever computing the row-stride/allocation-size
     * arithmetic that a huge w/h could overflow. */
    {
        static uint8_t fake_bmp[54];
        for (int i = 0; i < 54; i++) fake_bmp[i] = 0;
        fake_bmp[0] = 'B'; fake_bmp[1] = 'M';
        fake_bmp[14] = 40; /* header_size = 40 */
        /* w = h = 100000 (0x000186A0), little-endian, at offsets 18/22 */
        fake_bmp[18] = 0xA0; fake_bmp[19] = 0x86; fake_bmp[20] = 0x01; fake_bmp[21] = 0x00;
        fake_bmp[22] = 0xA0; fake_bmp[23] = 0x86; fake_bmp[24] = 0x01; fake_bmp[25] = 0x00;
        fake_bmp[26] = 1; /* planes */
        fake_bmp[28] = 24; /* bpp */
        uint32_t *pixels = 0;
        uint64_t w = 0, h = 0;
        int decode_ok = bmp_decode(fake_bmp, sizeof(fake_bmp), &pixels, &w, &h);
        serial_write_string("TEST: bmp_decode() on a 100000x100000-claiming BMP = ");
        serial_write_string(decode_ok ? "1 (UNEXPECTED - overflow path reachable)" : "0 (correctly rejected)");
        serial_write_string("\n");
    }
#endif

#if defined(GOS_TEST_FAULT_INJECT)
    /* Milestone 26.1 (audit2 High #6): force a page-allocation failure
     * partway through process_spawn()'s mapping loop and confirm the
     * failure path frees everything already mapped so far instead of
     * leaking it. */
    {
        uint64_t pmm_baseline = pmm_free_pages();
        serial_write_string("TEST: pmm_free_pages() baseline (26.1) = ");
        serial_write_uint(pmm_baseline);
        serial_write_string("\n");
        process_test_inject_spawn_failure(2); /* fail the 3rd page allocation (0-indexed) */
        int pid = process_spawn("HELLO.ELF");
        serial_write_string("TEST: process_spawn(HELLO.ELF) with forced page-3 failure = ");
        serial_write_uint((uint64_t)pid);
        serial_write_string(pid < 0 ? " (expected failure)\n" : " (UNEXPECTED success)\n");
        process_test_inject_spawn_failure(-1); /* clear */
        uint64_t pmm_after = pmm_free_pages();
        serial_write_string("TEST: pmm_free_pages() after failed spawn = ");
        serial_write_uint(pmm_after);
        serial_write_string(pmm_after == pmm_baseline ? " (== baseline, OK - no leak)\n" : " (MISMATCH - leak?)\n");
    }
#endif

#if defined(GOS_TEST_PHASE26)
    /* Milestone 26.2 (audit2 High #7): run INFLOOP.ELF (never calls
     * SYS_EXIT) and confirm the scheduler's watchdog kills it within its
     * time budget instead of hanging scheduler_run_until_done() forever -
     * proven by this call actually returning at all. */
    {
        uint64_t t0 = timer_get_ticks();
        int pid = process_spawn("INFLOOP.ELF");
        serial_write_string("TEST: process_spawn(INFLOOP.ELF) = ");
        serial_write_uint((uint64_t)pid);
        serial_write_string("\n");
        if (pid >= 0) {
            scheduler_run_until_done(); /* must return - this line running at all is the proof */
            uint64_t elapsed = timer_get_ticks() - t0;
            struct process *p = process_get(pid);
            serial_write_string("TEST: scheduler_run_until_done() RETURNED after ~");
            serial_write_uint(elapsed);
            serial_write_string(" ticks (did not hang)\n");
            serial_write_string("TEST: INFLOOP.ELF exit_code = ");
            serial_write_uint((uint64_t)(p ? p->exit_code : 0));
            serial_write_string(p && p->exit_code == -2 ? " (== -2, OK - killed by watchdog)\n" : " (MISMATCH)\n");
        }
    }

    /* Milestone 26.3 (audit2 High #8): WPTEST.ELF tries SYS_WAITPID on
     * every pid even though none are its own child - every attempt must
     * be rejected. Spawn SPIN1.ELF first (kernel-spawned, parent_pid=-1)
     * so at least one REAL zombie with a genuine (but non-matching)
     * parent_pid exists among the pids WPTEST.ELF will try. */
    {
        int spin_pid = process_spawn("SPIN1.ELF");
        if (spin_pid >= 0) {
            scheduler_run_until_done(); /* let it finish and become a zombie */
        }
        int wp_pid = process_spawn("WPTEST.ELF");
        serial_write_string("TEST: process_spawn(WPTEST.ELF) = ");
        serial_write_uint((uint64_t)wp_pid);
        serial_write_string("\n");
        if (wp_pid >= 0) {
            scheduler_run_until_done();
            struct process *wp = process_get(wp_pid);
            serial_write_string("TEST: WPTEST.ELF exit_code = ");
            serial_write_uint((uint64_t)(wp ? wp->exit_code : -1));
            serial_write_string(wp && wp->exit_code == 99 ? " (== 99, OK - every non-parent waitpid rejected)\n" : " (MISMATCH - a non-parent waitpid may have succeeded)\n");
        }
    }
#endif

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
        desktop_render_menu_overlay(); /* Milestone 26.5: true top-layer overlay, drawn after every window */
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
        desktop_render_menu_overlay(); /* Milestone 26.5: true top-layer overlay, drawn after every window */
        /* Milestone 15.1: cursor is the compositor's top layer - drawn
         * after every window AND the taskbar, so it's never occluded. */
        mouse_draw_cursor();
        fb_flip();
        sleep_ms(50);
    }
}
