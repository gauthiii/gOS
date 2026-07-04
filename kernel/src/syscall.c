#include <syscall.h>
#include <process.h>
#include <vmm.h>
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

/* Milestone 25.1 (audit2 Critical #1): the PML4 whose mappings a syscall's
 * caller-supplied pointer must be validated against. When the scheduler is
 * active, that's the currently-running process's own private address
 * space; the Phase 19 single-shot ring-3 demo (usermode.c) runs before the
 * scheduler ever exists, mapping directly into the kernel's own PML4
 * instead (vmm_map_page, not vmm_map_page_in) - vmm_get_pml4_phys() covers
 * that case correctly too, since that's exactly the PML4 those pages were
 * mapped into. */
static uint64_t current_caller_pml4_phys(void) {
    if (scheduler_is_active()) {
        struct process *p = process_get(scheduler_current_pid());
        if (p) {
            return p->pml4_phys;
        }
    }
    return vmm_get_pml4_phys();
}

void syscall_dispatch(struct interrupt_frame *frame) {
    last_caller_cs = frame->cs;

    switch (frame->rax) {
        case SYS_WRITE: {
            /* Milestone 19.2/20.1, hardened in Milestone 25.1 (audit2
             * Critical #1): rdi = pointer, rsi = length. Kernel code
             * (including this handler) is mapped identically into every
             * process's private PML4, so the pointer is directly
             * dereferenceable once validated - but it MUST be validated
             * first: an unvalidated pointer let any process pass an
             * unmapped (or otherwise out-of-range) address and have the
             * kernel page-fault servicing the syscall in ring 0, which
             * (idt.c's page-fault handler unconditionally panics) froze
             * the entire machine, not just the offending process. */
            const char *buf = (const char *)frame->rdi;
            uint64_t len = frame->rsi;
            if (!vmm_range_mapped_user(current_caller_pml4_phys(), (uint64_t)buf, len)) {
                serial_write_string("syscall: SYS_WRITE rejected - pointer/length not fully mapped in caller's address space\n");
                frame->rax = (uint64_t)-1;
                break;
            }
            for (uint64_t i = 0; i < len; i++) {
                serial_write_char(buf[i]);
            }
            frame->rax = len;
            break;
        }
        case SYS_SPAWN: {
            /* Milestone 20.2, hardened in Milestone 25.1 (audit2 Critical
             * #1 - this call site was named explicitly, folding into the
             * same root cause as SYS_WRITE above): read the path directly
             * out of the caller's memory into a small fixed kernel buffer,
             * but only after confirming the requested range is actually
             * mapped and user-accessible in the caller's own address space -
             * `len` was previously trusted verbatim from the untrusted rsi
             * register, capped only against the kernel-side buffer size,
             * not against what's actually mapped in the caller. */
            char path_buf[64];
            uint64_t len = frame->rsi;
            if (len >= sizeof(path_buf)) {
                len = sizeof(path_buf) - 1;
            }
            const char *user_path = (const char *)frame->rdi;
            if (!vmm_range_mapped_user(current_caller_pml4_phys(), (uint64_t)user_path, len)) {
                serial_write_string("syscall: SYS_SPAWN rejected - pointer/length not fully mapped in caller's address space\n");
                frame->rax = (uint64_t)-1;
                break;
            }
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
                    /* Milestone 25.2 (audit2 Critical #2): reclaim this
                     * process's address space and kernel stack right now,
                     * not at waitpid/reap time - waitpid may never be
                     * called at all (the kernel-mode Terminal's `run`
                     * command never does), and every completed process was
                     * otherwise leaking permanently. Safe to free the
                     * address space we're still technically running under:
                     * we're executing shared kernel code (identical in
                     * every process's PML4), not anything private to this
                     * process's own slot, and scheduler_reschedule() right
                     * below loads a different CR3 before any of these now-
                     * freed pages could be reused by an intervening
                     * allocation (none happens between here and there).
                     * Freeing the kernel stack we're currently executing
                     * ON is similarly safe here: kfree() only edits heap
                     * block-header metadata, never the freed payload
                     * itself, and nothing allocates from the heap between
                     * this call and the iretq that eventually resumes a
                     * different process on its own separate stack. */
                    process_free_resources(pid);
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
