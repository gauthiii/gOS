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

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
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

    serial_write_string("=== gOS boot checks complete ===\n");

    hcf();
}
