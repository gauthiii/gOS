#include <syscall.h>
#include <process.h>
#include <serial.h>

/* Milestone 19.1's trampoline (usermode_entry.asm) - restores the kernel
 * context that was suspended when enter_user_mode() ran. Still used for
 * the Phase 19 single-process demo (usermode.c), which never activates
 * the scheduler; Phase 20's SYS_EXIT instead calls scheduler_reschedule()
 * whenever the scheduler is active, see below. */
extern void gos_resume_after_user_mode(void);

static uint64_t last_caller_cs = 0;

uint64_t syscall_last_caller_cs(void) {
    return last_caller_cs;
}

void syscall_dispatch(struct interrupt_frame *frame) {
    last_caller_cs = frame->cs;

    switch (frame->rax) {
        case SYS_WRITE: {
            /* Milestone 19.2/20.1: rdi = pointer, rsi = length. The pointer
             * is a user-space virtual address; it's directly dereferenceable
             * here because kernel code (including this syscall handler)
             * is mapped identically into every process's private PML4
             * (vmm_create_process_pml4() shallow-copies the kernel's own
             * top-level entries) - so no copy_from_user step is needed to
             * reach kernel memory, but note this does NOT validate that
             * the pointer belongs to the CALLING process's own segments;
             * a hostile process could pass an address mapped into another
             * process's private region and this would still (harmlessly,
             * since it's kernel-readable either way) print whatever bytes
             * happen to be there. Real pointer-ownership validation is
             * out of scope for this phase. */
            const char *buf = (const char *)frame->rdi;
            uint64_t len = frame->rsi;
            for (uint64_t i = 0; i < len; i++) {
                serial_write_char(buf[i]);
            }
            frame->rax = len;
            break;
        }
        case SYS_SPAWN: {
            /* Milestone 20.2: read the path directly out of the caller's
             * memory (same trust model as SYS_WRITE above) into a small
             * fixed kernel buffer, then spawn it as a new process. */
            char path_buf[64];
            uint64_t len = frame->rsi;
            if (len >= sizeof(path_buf)) {
                len = sizeof(path_buf) - 1;
            }
            const char *user_path = (const char *)frame->rdi;
            for (uint64_t i = 0; i < len; i++) {
                path_buf[i] = user_path[i];
            }
            path_buf[len] = '\0';

            int child_pid = process_spawn(path_buf);
            if (child_pid >= 0) {
                struct process *child = process_get(child_pid);
                struct process *parent = process_get(scheduler_current_pid());
                if (child && parent) {
                    child->parent_pid = parent->pid;
                }
            }
            frame->rax = (uint64_t)(int64_t)child_pid;
            break;
        }
        case SYS_WAITPID: {
            int target_pid = (int)frame->rdi;
            struct process *target = process_get(target_pid);
            if (!target || target->state != PROC_ZOMBIE) {
                frame->rax = (uint64_t)-1; /* not exited yet - caller should retry */
                break;
            }
            frame->rax = (uint64_t)(int64_t)target->exit_code;
            target->state = PROC_UNUSED; /* reap */
            break;
        }
        case SYS_EXIT:
            if (scheduler_is_active()) {
                int pid = scheduler_current_pid();
                struct process *p = process_get(pid);
                serial_write_string("syscall: SYS_EXIT pid=");
                serial_write_uint((uint64_t)pid);
                serial_write_string(" exit_code=");
                serial_write_uint(frame->rdi);
                serial_write_string("\n");
                if (p) {
                    p->exit_code = (int)frame->rdi;
                    p->state = PROC_ZOMBIE;
                }
                scheduler_reschedule(frame); /* may not return (see scheduler_entry.asm) */
            } else {
                serial_write_string("syscall: SYS_EXIT - resuming kernel context\n");
                gos_resume_after_user_mode(); /* does not return here */
            }
            break;
        default:
            serial_write_string("syscall: unknown syscall number ");
            serial_write_uint(frame->rax);
            serial_write_string("\n");
            frame->rax = (uint64_t)-1;
            break;
    }
}
