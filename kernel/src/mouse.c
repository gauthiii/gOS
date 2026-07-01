#include <mouse.h>
#include <idt.h>
#include <pic.h>
#include <fb.h>
#include <serial.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ps2_wait_input_clear(void) {
    for (int timeout = 100000; timeout > 0; timeout--) {
        if (!(inb(PS2_STATUS_PORT) & 0x02)) {
            return;
        }
    }
}

static void ps2_wait_output_full(void) {
    for (int timeout = 100000; timeout > 0; timeout--) {
        if (inb(PS2_STATUS_PORT) & 0x01) {
            return;
        }
    }
}

static void mouse_write(uint8_t data) {
    ps2_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0xD4); /* next byte on 0x60 goes to the mouse */
    ps2_wait_input_clear();
    outb(PS2_DATA_PORT, data);
}

static uint8_t mouse_read(void) {
    ps2_wait_output_full();
    return inb(PS2_DATA_PORT);
}

static int64_t cursor_x = 400;
static int64_t cursor_y = 300;
static uint8_t buttons = 0;

static uint8_t packet[3];
static int packet_index = 0;

static void clamp_cursor(void) {
    int64_t max_x = (int64_t)fb_width() - 1;
    int64_t max_y = (int64_t)fb_height() - 1;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x > max_x) cursor_x = max_x;
    if (cursor_y > max_y) cursor_y = max_y;
}

static void mouse_irq_handler(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t data = inb(PS2_DATA_PORT);

#if defined(GOS_DEBUG_MOUSE_RAW)
    serial_write_string("RAWBYTE[");
    serial_write_uint((uint64_t)packet_index);
    serial_write_string("]: ");
    serial_write_hex64(data);
    serial_write_string("\n");
#endif

    /* Byte 0 of a real packet always has bit 3 set; if we see a byte with
     * that bit clear while expecting byte 0, the stream is desynced (can
     * happen right after enabling the device) - drop it and stay at index 0
     * until a valid-looking first byte arrives.
     *
     * 0xFA (the PS/2 command ACK code) is explicitly rejected too: testing
     * showed the 0xF4 "enable reporting" command's ACK byte can arrive
     * asynchronously via the interrupt path shortly after mouse_init()
     * finishes (rather than being reliably caught by the polling read
     * during the handshake), and 0xFA's bit 3 happens to be set, so the
     * generic bit-3 check alone doesn't catch it - it would otherwise be
     * accepted as a real byte 0, permanently shifting every subsequent
     * packet's framing by one byte. */
    if (packet_index == 0 && (!(data & 0x08) || data == 0xFA)) {
#if defined(GOS_DEBUG_MOUSE_RAW)
        serial_write_string("RAWBYTE: dropped (desync or stray ACK)\n");
#endif
        pic_send_eoi(12);
        return;
    }

    packet[packet_index++] = data;
    if (packet_index == 3) {
        packet_index = 0;

#if defined(GOS_DEBUG_MOUSE_RAW)
        serial_write_string("RAWPKT: ");
        serial_write_hex64(packet[0]);
        serial_write_string(" ");
        serial_write_hex64(packet[1]);
        serial_write_string(" ");
        serial_write_hex64(packet[2]);
        serial_write_string("\n");
#endif

        uint8_t status = packet[0];
        int64_t dx = packet[1];
        int64_t dy = packet[2];

        if (status & 0x10) dx -= 256; /* X sign bit */
        if (status & 0x20) dy -= 256; /* Y sign bit */

        if (!(status & 0x40) && !(status & 0x80)) { /* ignore overflowed packets */
            cursor_x += dx;
            cursor_y -= dy; /* PS/2 Y is inverted relative to screen coordinates */
            clamp_cursor();
        }

        buttons = status & 0x07;
    }

    pic_send_eoi(12);
}

void mouse_init(void) {
    /* Force IRQ12 masked for the duration of the handshake below, which
     * reads command ACK bytes via direct polling (mouse_read()). If IRQ12
     * were already unmasked (e.g. inherited from firmware/BIOS defaults
     * preserved across pic_remap()), the interrupt handler would race the
     * polling reads for the same physical byte on port 0x60 - whichever
     * side loses that race desyncs the handshake, which then misaligns
     * every subsequent 3-byte packet's framing (movement bytes get
     * misread as button-state bytes, etc). Masking first guarantees the
     * handshake is 100% polling-only with no race possible. */
    pic_set_mask(12);

    /* Enable the second PS/2 port (the mouse). */
    ps2_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0xA8);

    /* Read the controller configuration byte, enable IRQ12 (bit 1) and
     * ensure the mouse clock isn't disabled (bit 5 clear), write it back. */
    ps2_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0x20);
    uint8_t config = mouse_read();
    config |= 0x02;
    config &= ~0x20;
    ps2_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0x60);
    ps2_wait_input_clear();
    outb(PS2_DATA_PORT, config);

    /* Set defaults, then enable data reporting (streaming packets). */
    mouse_write(0xF6);
    mouse_read(); /* ACK */
    mouse_write(0xF4);
    mouse_read(); /* ACK */

    cursor_x = (int64_t)fb_width() / 2;
    cursor_y = (int64_t)fb_height() / 2;

    /* Drain any stray byte(s) left in the PS/2 output buffer before
     * enabling interrupt-driven packet processing. Testing found the
     * 0xF4 command's ACK (0xFA) byte posts to port 0x60 with a small
     * delay after mouse_write()'s polling read already gave up looking
     * for it - the byte then arrives moments later, right as interrupts
     * get enabled, and is consumed by the IRQ handler as if it were a
     * real packet's first byte. Because 0xFA (0b11111010) happens to
     * have bit 3 set, it passes the "is this plausibly a real byte 0"
     * sanity check, so the framing silently shifts by one byte forever
     * after instead of being caught and dropped. Actively waiting (not
     * just instantly checking once) across several short delays below
     * reliably catches this straggler before interrupts are enabled. */
    for (int i = 0; i < 8; i++) {
        outb(0x80, 0); /* ~1us port I/O delay, standard PC idiom */
        outb(0x80, 0);
        if (inb(PS2_STATUS_PORT) & 0x01) {
            inb(PS2_DATA_PORT);
            i = 0; /* a byte was found - reset the wait in case another follows */
        }
    }
    packet_index = 0;

    idt_register_irq_handler(12, mouse_irq_handler);
    pic_clear_mask(2);  /* cascade line for the slave PIC (IRQ8-15) */
    pic_clear_mask(12);

    serial_write_string("Mouse driver initialized (IRQ12 unmasked, cursor at center)\n");
}

int64_t mouse_x(void) { return cursor_x; }
int64_t mouse_y(void) { return cursor_y; }
uint8_t mouse_buttons(void) { return buttons; }

void mouse_draw_cursor(void) {
    const int64_t size = 10;
    fb_draw_rect((uint64_t)cursor_x, (uint64_t)cursor_y, size, size, fb_make_color(255, 255, 255));
    fb_draw_rect_outline((uint64_t)cursor_x, (uint64_t)cursor_y, size, size, fb_make_color(0, 0, 0), 1);
}
