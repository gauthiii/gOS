#include <timer.h>
#include <idt.h>
#include <pic.h>
#include <serial.h>

/* Milestone 2.3 only needs to prove IRQ0 fires periodically; it runs at the
 * PIT's uninitialized default rate (~18.2 Hz) since reprogramming the PIT
 * divisor for a specific frequency is Phase 4's job. */
static volatile uint64_t ticks = 0;

static void timer_irq_handler(struct interrupt_frame *frame) {
    (void)frame;
    ticks++;
    if (ticks % 18 == 0) { /* roughly once a second at the default ~18.2Hz rate */
        serial_write_string("Timer tick: ");
        serial_write_uint(ticks);
        serial_write_string("\n");
    }
    pic_send_eoi(0);
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

void timer_init(void) {
    idt_register_irq_handler(0, timer_irq_handler);
    pic_clear_mask(0);
}
