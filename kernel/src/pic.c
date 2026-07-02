#include <pic.h>
#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define ICW1_ICW4      0x01
#define ICW1_INIT      0x10
#define ICW4_8086      0x01
#define PIC_READ_ISR   0x0B /* OCW3: next read of the command port returns the In-Service Register */

#if defined(GOS_TEST_SPURIOUS_IRQ_CHECK)
#include <serial.h>
static uint32_t isr_read_count = 0;
#endif

/* Legacy 8259 PIC remap target: IRQ0-7 -> vectors 32-39, IRQ8-15 -> 40-47.
 * Without this remap, hardware IRQs collide with CPU exception vectors 0-31. */
#define PIC1_OFFSET 0x20
#define PIC2_OFFSET 0x28

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    outb(PIC1_DATA, 4); /* tell PIC1 there's a PIC2 at IRQ2 */
    io_wait();
    outb(PIC2_DATA, 2); /* tell PIC2 its cascade identity */
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static uint8_t pic_read_isr(uint16_t command_port) {
    outb(command_port, PIC_READ_ISR);
    uint8_t isr = inb(command_port);
#if defined(GOS_TEST_SPURIOUS_IRQ_CHECK)
    isr_read_count++;
#endif
    return isr;
}

void pic_send_eoi(uint8_t irq) {
    /* IRQ7 (master) and IRQ15 (slave) are the two legacy 8259 lines that
     * can fire "spuriously" (no real device asserted them - a hardware
     * quirk of edge-triggered PIC operation). A spurious IRQ has no
     * corresponding bit set in the ISR, and per the standard protocol
     * must NOT be EOI'd as if it were real: a spurious IRQ7 needs no EOI
     * at all, and a spurious IRQ15 needs EOI sent to the master PIC only
     * (to acknowledge the cascade line) but never to the slave PIC -
     * sending EOI to both risks the PIC losing track of pending priority. */
    if (irq == 7) {
        uint8_t isr1 = pic_read_isr(PIC1_COMMAND);
        if (!(isr1 & (1 << 7))) {
            return; /* spurious IRQ7 - no EOI needed */
        }
    } else if (irq == 15) {
        uint8_t isr2 = pic_read_isr(PIC2_COMMAND);
        if (!(isr2 & (1 << 7))) {
            outb(PIC1_COMMAND, PIC_EOI); /* acknowledge the cascade line only */
            return;
        }
    }

    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

#if defined(GOS_TEST_SPURIOUS_IRQ_CHECK)
uint32_t pic_debug_isr_read_count(void) {
    return isr_read_count;
}
#endif

void pic_set_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t irq_line = irq < 8 ? irq : irq - 8;
    uint8_t value = inb(port) | (1 << irq_line);
    outb(port, value);
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t irq_line = irq < 8 ? irq : irq - 8;
    uint8_t value = inb(port) & ~(1 << irq_line);
    outb(port, value);
}
