#include <limine.h>
#include <stdint.h>
#include <serial.h>

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

    serial_write_string("=== gOS boot checks complete ===\n");

    hcf();
}
