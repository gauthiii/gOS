#include <idt.h>
#include <gdt.h>
#include <serial.h>
#include <panic.h>
#include <syscall.h>
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void idt_load(uint64_t idtp_addr);

/* ISR stub symbols defined in isr.asm */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

extern void isr128(void); /* Milestone 19.2: syscall gate stub */

static const char *exception_name(uint64_t vector) {
    static const char *names[32] = {
        "Divide Error", "Debug", "NMI", "Breakpoint",
        "Overflow", "BOUND Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD FP Exception",
        "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "VMM Communication Exception", "Security Exception", "Reserved"
    };
    if (vector < 32) return names[vector];
    return "Unknown";
}

static irq_handler_t irq_handlers[16];

void idt_register_irq_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t ist, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low = addr & 0xFFFF;
    idt[vector].selector = GDT_KERNEL_CODE;
    idt[vector].ist = ist;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void isr_handler(struct interrupt_frame *frame) {
    if (frame->vector >= 32 && frame->vector < 48) {
        uint8_t irq = (uint8_t)(frame->vector - 32);
        if (irq_handlers[irq] != 0) {
            irq_handlers[irq](frame);
        }
        return;
    }

    if (frame->vector == SYSCALL_VECTOR) {
        syscall_dispatch(frame);
        return;
    }

    serial_write_string("\n!!! CPU EXCEPTION !!!\n");
    serial_write_string("Vector: ");
    serial_write_uint(frame->vector);
    serial_write_string(" (");
    serial_write_string(exception_name(frame->vector));
    serial_write_string(")\nError code: ");
    serial_write_hex64(frame->error_code);
    serial_write_string("\nRIP: ");
    serial_write_hex64(frame->rip);
    serial_write_string("\nCS: ");
    serial_write_hex64(frame->cs);
    serial_write_string("\nRFLAGS: ");
    serial_write_hex64(frame->rflags);
    serial_write_string("\nRSP: ");
    serial_write_hex64(frame->rsp);

    uint64_t cr2 = 0;
    if (frame->vector == 14) { /* Page Fault */
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_write_string("\nCR2 (faulting address): ");
        serial_write_hex64(cr2);

        /* Page fault error code bit layout (Intel SDM Vol. 3A, 4.7):
         * bit0: 0=not-present, 1=protection violation
         * bit1: 0=read, 1=write
         * bit2: 0=supervisor, 1=user
         * bit4: 1=instruction fetch */
        uint64_t ec = frame->error_code;
        serial_write_string("\nAccess type: ");
        serial_write_string((ec & (1ULL << 0)) ? "protection-violation" : "not-present");
        serial_write_string(", ");
        serial_write_string((ec & (1ULL << 1)) ? "write" : "read");
        serial_write_string(", ");
        serial_write_string((ec & (1ULL << 2)) ? "user-mode" : "supervisor-mode");
        if (ec & (1ULL << 4)) {
            serial_write_string(", instruction-fetch");
        }
    }

    serial_write_string("\n!!! System halted !!!\n");

    /* Milestone 11.1: show a red full-screen panic display instead of
     * silently halting with only a serial log - panic_screen() never
     * returns (it halts internally, whether or not the framebuffer was
     * ready to draw to). */
    panic_screen(exception_name(frame->vector), frame->vector, frame->error_code,
                 frame->rip, frame->vector == 14, cr2);
}

void idt_init(void) {
    idt_set_gate(0, isr0, 0, 0x8E);
    idt_set_gate(1, isr1, 0, 0x8E);
    /* Vector 2 (NMI) and vector 8 (Double Fault) use IST1 - a dedicated
     * stack (set up in gdt_init()'s TSS) instead of the current stack.
     * A stack-overflow-triggered double fault must not run its handler
     * on the same already-overflowed stack, or the handler's own prologue
     * would immediately fault again -> triple fault / CPU reset instead
     * of the panic screen ever running. */
    idt_set_gate(2, isr2, 1, 0x8E);
    idt_set_gate(3, isr3, 0, 0x8E);
    idt_set_gate(4, isr4, 0, 0x8E);
    idt_set_gate(5, isr5, 0, 0x8E);
    idt_set_gate(6, isr6, 0, 0x8E);
    idt_set_gate(7, isr7, 0, 0x8E);
    idt_set_gate(8, isr8, 1, 0x8E);
    idt_set_gate(9, isr9, 0, 0x8E);
    idt_set_gate(10, isr10, 0, 0x8E);
    idt_set_gate(11, isr11, 0, 0x8E);
    idt_set_gate(12, isr12, 0, 0x8E);
    idt_set_gate(13, isr13, 0, 0x8E);
    idt_set_gate(14, isr14, 0, 0x8E);
    idt_set_gate(15, isr15, 0, 0x8E);
    idt_set_gate(16, isr16, 0, 0x8E);
    idt_set_gate(17, isr17, 0, 0x8E);
    idt_set_gate(18, isr18, 0, 0x8E);
    idt_set_gate(19, isr19, 0, 0x8E);
    idt_set_gate(20, isr20, 0, 0x8E);
    idt_set_gate(21, isr21, 0, 0x8E);
    idt_set_gate(22, isr22, 0, 0x8E);
    idt_set_gate(23, isr23, 0, 0x8E);
    idt_set_gate(24, isr24, 0, 0x8E);
    idt_set_gate(25, isr25, 0, 0x8E);
    idt_set_gate(26, isr26, 0, 0x8E);
    idt_set_gate(27, isr27, 0, 0x8E);
    idt_set_gate(28, isr28, 0, 0x8E);
    idt_set_gate(29, isr29, 0, 0x8E);
    idt_set_gate(30, isr30, 0, 0x8E);
    idt_set_gate(31, isr31, 0, 0x8E);

    idt_set_gate(32, irq0, 0, 0x8E);
    idt_set_gate(33, irq1, 0, 0x8E);
    idt_set_gate(34, irq2, 0, 0x8E);
    idt_set_gate(35, irq3, 0, 0x8E);
    idt_set_gate(36, irq4, 0, 0x8E);
    idt_set_gate(37, irq5, 0, 0x8E);
    idt_set_gate(38, irq6, 0, 0x8E);
    idt_set_gate(39, irq7, 0, 0x8E);
    idt_set_gate(40, irq8, 0, 0x8E);
    idt_set_gate(41, irq9, 0, 0x8E);
    idt_set_gate(42, irq10, 0, 0x8E);
    idt_set_gate(43, irq11, 0, 0x8E);
    idt_set_gate(44, irq12, 0, 0x8E);
    idt_set_gate(45, irq13, 0, 0x8E);
    idt_set_gate(46, irq14, 0, 0x8E);
    idt_set_gate(47, irq15, 0, 0x8E);

    /* Milestone 19.2: DPL=3 (0xEE = present, DPL=3, 64-bit interrupt gate)
     * instead of the DPL=0 (0x8E) every other gate uses - this is the one
     * vector ring-3 code is actually allowed to invoke via `int 0x80`. A
     * ring-3 `int` on any other vector still faults with a #GP, matching
     * real hardware/OS behavior. */
    idt_set_gate(SYSCALL_VECTOR, isr128, 0, 0xEE);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;

    idt_load((uint64_t)&idtp);

#if defined(GOS_TEST_STACK_OVERFLOW)
    serial_write_string("DEBUG: idt[8].ist=");
    serial_write_uint(idt[8].ist);
    serial_write_string(" idt[2].ist=");
    serial_write_uint(idt[2].ist);
    serial_write_string("\n");
#endif
}
