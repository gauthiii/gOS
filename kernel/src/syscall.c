#include <syscall.h>
#include <serial.h>

/* Milestone 19.1's trampoline (usermode_entry.asm) - restores the kernel
 * context that was suspended when enter_user_mode() ran, then returns
 * control to whatever called it. Never returns to syscall_dispatch/
 * isr_handler; this is a one-way jump out of the interrupt path entirely,
 * not a normal C call. */
extern void gos_resume_after_user_mode(void);

static uint64_t last_caller_cs = 0;

uint64_t syscall_last_caller_cs(void) {
    return last_caller_cs;
}

void syscall_dispatch(struct interrupt_frame *frame) {
    last_caller_cs = frame->cs;

    switch (frame->rax) {
        case SYS_WRITE: {
            /* Milestone 19.2: rdi = pointer, rsi = length. The pointer is a
             * user-space virtual address, but since Phase 19 deliberately
             * shares one set of page tables between kernel and "user" code
             * (no per-process CR3 yet - that's Phase 20's job), it's
             * directly dereferenceable here with no copy_from_user step.
             * That simplification is safe only because nothing untrusted
             * runs yet; a real multi-process gOS would need to validate
             * this pointer against the calling process's own mappings
             * before touching it. */
            const char *buf = (const char *)frame->rdi;
            uint64_t len = frame->rsi;
            for (uint64_t i = 0; i < len; i++) {
                serial_write_char(buf[i]);
            }
            frame->rax = len; /* return value: bytes written */
            break;
        }
        case SYS_EXIT:
            serial_write_string("syscall: SYS_EXIT - resuming kernel context\n");
            gos_resume_after_user_mode(); /* does not return here */
            break;
        default:
            serial_write_string("syscall: unknown syscall number ");
            serial_write_uint(frame->rax);
            serial_write_string("\n");
            frame->rax = (uint64_t)-1;
            break;
    }
}
