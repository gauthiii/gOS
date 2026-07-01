#include <keyboard.h>
#include <idt.h>
#include <pic.h>

#define PS2_DATA_PORT 0x60

#define SC_LEFT_SHIFT_MAKE  0x2A
#define SC_LEFT_SHIFT_BREAK 0xAA
#define SC_RIGHT_SHIFT_MAKE 0x36
#define SC_RIGHT_SHIFT_BREAK 0xB6
#define SC_CAPS_LOCK_MAKE   0x3A
#define SC_BACKSPACE_MAKE   0x0E
#define SC_ENTER_MAKE       0x1C
#define SC_BREAK_BIT        0x80

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* US QWERTY scancode set 1, unshifted. Index = make code. 0 = unmapped. */
static const char scancode_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0, /* rest unmapped: F-keys, numpad, etc. */
};

static const char scancode_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,
};

static volatile int shift_held = 0;
static volatile int caps_lock_on = 0;

#define RING_BUFFER_SIZE 256
static volatile char ring_buffer[RING_BUFFER_SIZE];
static volatile uint32_t ring_head = 0; /* next write index */
static volatile uint32_t ring_tail = 0; /* next read index */

static void ring_push(char c) {
    uint32_t next = (ring_head + 1) % RING_BUFFER_SIZE;
    if (next == ring_tail) {
        return; /* buffer full, drop the character */
    }
    ring_buffer[ring_head] = c;
    ring_head = next;
}

static int ring_pop(char *out) {
    if (ring_tail == ring_head) {
        return 0; /* empty */
    }
    *out = ring_buffer[ring_tail];
    ring_tail = (ring_tail + 1) % RING_BUFFER_SIZE;
    return 1;
}

static int is_letter_scancode(uint8_t sc) {
    char c = scancode_ascii[sc];
    return (c >= 'a' && c <= 'z');
}

static void keyboard_irq_handler(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t sc = inb(PS2_DATA_PORT);

    if (sc == SC_LEFT_SHIFT_MAKE || sc == SC_RIGHT_SHIFT_MAKE) {
        shift_held = 1;
    } else if (sc == SC_LEFT_SHIFT_BREAK || sc == SC_RIGHT_SHIFT_BREAK) {
        shift_held = 0;
    } else if (sc == SC_CAPS_LOCK_MAKE) {
        caps_lock_on = !caps_lock_on;
    } else if (!(sc & SC_BREAK_BIT)) {
        /* Make code (key press) for a non-modifier key. */
        if (sc < 128) {
            int use_shift = shift_held;
            if (is_letter_scancode(sc) && caps_lock_on) {
                use_shift = !use_shift; /* caps lock inverts shift for letters only */
            }
            char c = use_shift ? scancode_ascii_shift[sc] : scancode_ascii[sc];
            if (c != 0) {
                ring_push(c);
            }
        }
    }
    /* Break codes for non-modifier keys are ignored - we only translate
     * on press, matching the ring buffer's "stream of typed characters"
     * model rather than tracking full key-up/key-down state. */

    pic_send_eoi(1);
}

void keyboard_init(void) {
    idt_register_irq_handler(1, keyboard_irq_handler);
    pic_clear_mask(1);
}

int kb_has_char(void) {
    return ring_tail != ring_head;
}

char kb_getchar(void) {
    char c;
    while (!ring_pop(&c)) {
        __asm__ volatile ("hlt");
    }
    return c;
}
