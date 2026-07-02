#ifndef GOS_SYSCALL_H
#define GOS_SYSCALL_H

#include <idt.h>

/* Milestone 19.2: minimal syscall numbers, deliberately matching Linux's
 * x86_64 numbering for `write`/`exit` (1 and 60) purely as a familiar
 * convention - gOS's ABI isn't Linux-compatible in any other way. */
#define SYS_WRITE 1
#define SYS_EXIT  60

/* Dispatches a syscall trapped via SYSCALL_VECTOR (int 0x80). Convention:
 * rax = syscall number, rdi/rsi/rdx = args, return value written back into
 * frame->rax (restored into the caller's rax on iretq). SYS_EXIT does not
 * return to isr_handler at all - see usermode_entry.asm's
 * gos_resume_after_user_mode. */
void syscall_dispatch(struct interrupt_frame *frame);

/* Returns the CS selector value that was active in the frame of the most
 * recent syscall (before dispatch touched anything) - Milestone 19.1's
 * test reads this and checks the low 2 bits (RPL) to independently prove
 * the calling code was genuinely executing at CPL=3, not just that a
 * syscall fired. */
uint64_t syscall_last_caller_cs(void);

#endif
