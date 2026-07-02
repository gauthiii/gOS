#include <gdt.h>
#include <stdint.h>
#if defined(GOS_TEST_STACK_OVERFLOW)
#include <serial.h>
#endif

/* Standard 8-byte GDT descriptor, used for null/code/data entries. */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* 16-byte TSS descriptor (long mode TSS descriptors are twice the size of a
 * normal segment descriptor because they carry a full 64-bit base address). */
struct tss_entry_descriptor {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Minimal 64-bit TSS. rsp0 will be used in later phases for privilege-level
 * switches (syscalls/interrupts from user mode); IST slots reserved for
 * Phase 2's later use routing double-fault/NMI to a known-good stack. */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_layout {
    struct gdt_entry null;
    struct gdt_entry kernel_code;
    struct gdt_entry kernel_data;
    struct gdt_entry user_code;
    struct gdt_entry user_data;
    struct tss_entry_descriptor tss;
} __attribute__((packed));

static struct gdt_layout gdt;
static struct gdt_ptr gdtp;
static struct tss tss;

/* Static ~16KiB kernel stack used as TSS.rsp0 until Phase 3's memory manager
 * can dynamically allocate per-task/interrupt stacks. */
static uint8_t tss_stack[16384] __attribute__((aligned(16)));

/* Phase 12, Milestone 12.5: dedicated IST1 stack for double-fault/NMI, so
 * a stack-overflow-triggered double fault doesn't reuse the same
 * already-overflowed stack (which risks a triple fault/reset instead of
 * the panic screen actually running). Separate static region from
 * tss_stack so it isn't itself exhausted by whatever overflowed rsp0. */
static uint8_t ist1_stack[16384] __attribute__((aligned(16)));

static void gdt_set_entry(struct gdt_entry *e, uint8_t access, uint8_t granularity) {
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = access;
    e->granularity = granularity;
    e->base_high = 0;
}

extern void gdt_flush(uint64_t gdtp_addr, uint16_t tss_selector);

void gdt_init(void) {
    gdt_set_entry(&gdt.null, 0x00, 0x00);

    /* Kernel code: present, ring0, code, executable, readable, long-mode (L bit) */
    gdt_set_entry(&gdt.kernel_code, 0x9A, 0x20);
    /* Kernel data: present, ring0, data, writable */
    gdt_set_entry(&gdt.kernel_data, 0x92, 0x00);
    /* User code: present, ring3, code, executable, readable, long-mode */
    gdt_set_entry(&gdt.user_code, 0xFA, 0x20);
    /* User data: present, ring3, data, writable */
    gdt_set_entry(&gdt.user_data, 0xF2, 0x00);

    tss.rsp0 = (uint64_t)&tss_stack[sizeof(tss_stack)];
    tss.ist1 = (uint64_t)&ist1_stack[sizeof(ist1_stack)];
    tss.iomap_base = sizeof(struct tss);

    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(struct tss) - 1;

    gdt.tss.length = tss_limit & 0xFFFF;
    gdt.tss.base_low = tss_base & 0xFFFF;
    gdt.tss.base_mid = (tss_base >> 16) & 0xFF;
    gdt.tss.flags1 = 0x89; /* present, ring0, 64-bit TSS (available) */
    gdt.tss.flags2 = (tss_limit >> 16) & 0x0F;
    gdt.tss.base_high = (tss_base >> 24) & 0xFF;
    gdt.tss.base_upper = (tss_base >> 32) & 0xFFFFFFFF;
    gdt.tss.reserved = 0;

    gdtp.limit = sizeof(struct gdt_layout) - 1;
    gdtp.base = (uint64_t)&gdt;

    gdt_flush((uint64_t)&gdtp, GDT_TSS);

#if defined(GOS_TEST_STACK_OVERFLOW)
    serial_write_string("DEBUG: tss.ist1=0x");
    serial_write_hex64(tss.ist1);
    serial_write_string(" ist1_stack top=0x");
    serial_write_hex64((uint64_t)&ist1_stack[sizeof(ist1_stack)]);
    serial_write_string("\n");
#endif
}

void gdt_set_tss_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
