#include <timer.h>
#include <idt.h>
#include <pic.h>
#include <serial.h>

/* Milestone 20.1: called on every PIT tick so the scheduler can preempt a
 * running process after its time slice - declared extern rather than via a
 * registration callback, matching this project's preference for direct,
 * simple wiring over indirection when there's only ever one consumer. */
extern void scheduler_timer_tick(struct interrupt_frame *frame);

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQUENCY_HZ 1193182

static volatile uint64_t ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void timer_irq_handler(struct interrupt_frame *frame) {
    ticks++;
    if (ticks % PIT_FREQUENCY_HZ == 0) { /* once a second at the configured rate */
        serial_write_string("Timer tick: ");
        serial_write_uint(ticks);
        serial_write_string("\n");
    }
    /* EOI must be sent BEFORE scheduler_timer_tick(), not after: if every
     * process has exited, rescheduling takes a one-way jump straight back
     * into scheduler_run_until_done()'s caller (scheduler_resume_kernel(),
     * kernel/src/scheduler_entry.asm) and never returns here - sending EOI
     * afterward would leave IRQ0 permanently unacknowledged on the PIC in
     * that case, stalling every future timer tick. */
    pic_send_eoi(0);
    scheduler_timer_tick(frame);
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

static void pit_set_frequency(uint32_t hz) {
    uint32_t divisor = PIT_BASE_FREQUENCY_HZ / hz;
    /* 0x36 = channel 0, lobyte/hibyte access mode, mode 3 (square wave) */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_init(void) {
    idt_register_irq_handler(0, timer_irq_handler);
    pit_set_frequency(PIT_FREQUENCY_HZ);
    pic_clear_mask(0);
}

void sleep_ms(uint32_t ms) {
    uint64_t target = ticks + ((uint64_t)ms * PIT_FREQUENCY_HZ) / 1000;
    while (ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void timer_self_test(void) {
    serial_write_string("Timer self-test: sleeping 2000ms (tick=");
    serial_write_uint(ticks);
    serial_write_string(")...\n");
    uint64_t before = ticks;
    sleep_ms(2000);
    uint64_t after = ticks;
    serial_write_string("Timer self-test: woke up (tick=");
    serial_write_uint(after);
    serial_write_string("), ");
    serial_write_uint(after - before);
    serial_write_string(" ticks elapsed at ");
    serial_write_uint(PIT_FREQUENCY_HZ);
    serial_write_string("Hz (expected ~200)\n");
}
