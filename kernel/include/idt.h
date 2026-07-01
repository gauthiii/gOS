#ifndef GOS_IDT_H
#define GOS_IDT_H

#include <stdint.h>

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

void idt_init(void);

/* Registers a handler for a hardware IRQ (vectors 32-47 after PIC remap). */
typedef void (*irq_handler_t)(struct interrupt_frame *frame);
void idt_register_irq_handler(uint8_t irq, irq_handler_t handler);

#endif
