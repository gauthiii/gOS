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

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Milestone 11.1 stress test: the plan calls for "rapidly create/delete/
 * rename files and open/close windows for several minutes without a
 * crash." A literal several-minute run isn't practical for an automated
 * headless boot-time self-test, so this runs a large, fixed number of
 * cycles instead (fast - seconds, not minutes) as the automated
 * equivalent; see phase11.md for the live -display cocoa command used to
 * additionally soak-test this interactively for real minutes. Must run
 * after fat32_init() and window_system_init(), and with no other windows
 * open yet (so window slots are free to cycle through). */
static void stress_test(void) {
    serial_write_string("Stress test: running file + window create/delete/rename cycles...\n");

    int file_cycles_ok = 0;
    int file_ok = 1;
    for (int i = 0; i < 150; i++) {
        if (!fat_create_file("STRESS.TXT")) { file_ok = 0; break; }
        if (!fat_write_file("STRESS.TXT", (const uint8_t *)"x", 1)) { file_ok = 0; break; }
        if (!fat_rename("STRESS.TXT", "STRESSR.TXT")) { file_ok = 0; break; }
        if (!fat_delete_file("STRESSR.TXT")) { file_ok = 0; break; }
        file_cycles_ok++;
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

    heap_init();
    heap_self_test();

    timer_self_test();

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
            serial_write_string("ATA: PANIC - failed to read sector 0\n");
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

        static uint8_t file_buf[512];
        int64_t n = fat_read_file("HOSTFILE.TXT", file_buf, sizeof(file_buf) - 1);
        if (n < 0) {
            serial_write_string("FAT32: PANIC - failed to read HOSTFILE.TXT\n");
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
            serial_write_string("FAT32: PANIC - failed to read TESTDIR/NESTED.TXT\n");
        } else {
            file_buf[n] = '\0';
            serial_write_string("FAT32: read TESTDIR/NESTED.TXT (");
            serial_write_uint((uint64_t)n);
            serial_write_string(" bytes): \"");
            serial_write_string((const char *)file_buf);
            serial_write_string("\"\n");
        }

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
            serial_write_string("FAT32: PANIC - failed to read PERSIST.TXT\n");
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
    } else {
        serial_write_string("FAT32: PANIC - not a valid FAT32 filesystem\n");
    }

    fb_init(fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
            fb->red_mask_shift, fb->green_mask_shift, fb->blue_mask_shift);
    fb_clear(fb_make_color(0, 64, 128)); /* dark blue, to prove real pixel writes over the raw framebuffer */

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

    mouse_init();

    /* Milestone 6.1 live test: redraw the cursor at its current tracked
     * position for ~5 seconds (100 frames @ 50ms), logging whenever it
     * moves. Real movement is injected externally via the QEMU monitor's
     * `mouse_move`/`mouse_button` commands during this window - see
     * phase6.md for the exact test procedure. */
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

    /* Milestones 6.2/6.3/6.4: two overlapping windows (proving z-order and
     * click-to-focus), one draggable by its title bar, one with a button
     * (proving hit-testing/dispatch). Three windows are created (matching
     * the plan's explicit Milestone 6.3 test requirement) so click-to-focus
     * exercises reordering a window out of the middle/back of the z-order,
     * not just a trivial two-element swap. Driven interactively for ~10
     * seconds via QEMU monitor mouse_move/mouse_button commands - see
     * phase6.md for the exact test procedure. */
    window_system_init();
    stress_test();
    int win_a = window_create(150, 150, 300, 200, fb_make_color(70, 70, 200), fb_make_color(30, 30, 60), "Window A");
    int win_b = window_create(400, 250, 280, 180, fb_make_color(200, 70, 70), fb_make_color(60, 30, 30), "Window B");
    int win_c = window_create(280, 350, 260, 160, fb_make_color(70, 200, 120), fb_make_color(30, 60, 40), "Text Editor");
    window_add_button(win_a, 20, 20, 120, 30, fb_make_color(80, 200, 80), "Click Me", on_test_button_click);
    window_enable_textbox(win_c);
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
     * A/B/C above still auto-open unchanged (they're Phase 6/7 regression
     * demos, not the File Manager). A taskbar across the bottom of the
     * screen (kernel/src/taskbar.c) lists whatever windows are currently
     * open and lets you click an entry to bring it to front. */
    serial_write_string("Desktop ready - click the \"Files\" icon to launch the File Manager\n");

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
        fb_flip();
        sleep_ms(50);
    }
}
