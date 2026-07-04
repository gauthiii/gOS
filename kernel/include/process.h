#ifndef GOS_PROCESS_H
#define GOS_PROCESS_H

#include <stdint.h>
#include <idt.h>

#define MAX_PROCESSES 8
#define PROC_KSTACK_SIZE 8192

/* Milestone 20.1: process-private virtual addresses deliberately live
 * under PML4 index 1 (0x0000008000000000 = exactly 512GiB), a slot
 * vmm_init() never touches - every process's PML4 starts as a shallow
 * copy of the kernel's own (sharing kernel code/data/identity-map/HHDM
 * entries), so slot 1 is the only region genuinely private per process;
 * anywhere under slot 0 would alias the shared kernel identity map instead
 * of getting its own page tables. */
#define PROC_LOAD_BASE  0x0000008000000000ULL
#define PROC_STACK_BASE 0x0000008001000000ULL
#define PROC_STACK_PAGES 4

enum proc_state { PROC_UNUSED = 0, PROC_READY, PROC_RUNNING, PROC_ZOMBIE };

struct process {
    int pid;
    enum proc_state state;
    struct interrupt_frame regs; /* saved on every preemption/syscall reschedule; restored on resume */
    uint64_t pml4_phys;
    uint64_t kstack_top;
    void *kstack_base;   /* Milestone 25.2: the kmalloc()'d pointer kstack_top was computed from - kstack_top alone isn't enough to kfree() it */
    int parent_pid;
    int exit_code;
};

void process_init(void);

/* Loads an ELF from `path` (FAT32) into a fresh, private address space and
 * creates a new PROC_READY process. Returns pid (>=0) or -1 on failure
 * (file not found, malformed ELF, out of memory, or process table full). */
int process_spawn(const char *path);

struct process *process_get(int pid);
int process_count_active(void); /* READY or RUNNING, not ZOMBIE/UNUSED */

/* Milestone 25.2 (audit2 Critical #2): frees every physical page this
 * process's address space owns (via vmm_destroy_process_pml4()) and its
 * kmalloc'd kernel stack. Called once, right when a process transitions to
 * PROC_ZOMBIE (see syscall.c's SYS_EXIT handler) - NOT at reap/waitpid
 * time, so memory is reclaimed the moment a process actually exits
 * regardless of whether anything ever calls waitpid on it (e.g. the
 * kernel-mode Terminal's `run` command never does). Leaves `state`,
 * `exit_code`, and `pid` intact so a subsequent waitpid/reap still sees a
 * valid zombie; `pml4_phys`/`kstack_top`/`kstack_base` are zeroed since
 * they no longer point at anything live. Safe to call at most once per
 * process (idempotent no-op if called again, since the freed fields are
 * zeroed and vmm_destroy_process_pml4/kfree are both no-ops on 0/NULL in
 * every path this codebase exercises). */
void process_free_resources(int pid);

/* Runs the scheduler (real timer-driven preemption) until every spawned
 * process has reached PROC_ZOMBIE, then returns - the calling kernel code
 * resumes exactly where it left off, as if this were a normal blocking
 * function call. No-op if no process is currently READY. */
void scheduler_run_until_done(void);

/* Called from two places: the timer IRQ (preemption) and SYS_EXIT/
 * SYS_WAITPID reschedule points (voluntary yield). Saves `frame` into the
 * outgoing process (if one is running), picks the next READY process
 * round-robin, and overwrites `frame` in place with its saved registers -
 * the caller's own interrupt-return path then resumes AS that process.
 * If no process is READY, this does not return (see scheduler_entry.asm's
 * scheduler_resume_kernel). */
void scheduler_reschedule(struct interrupt_frame *frame);

int scheduler_is_active(void);
int scheduler_current_pid(void);

/* Registered with timer.c as the PIT tick hook; gates scheduler_reschedule()
 * behind a fixed time-slice (a handful of ticks) rather than rescheduling
 * on literally every 10ms tick, so preempted processes get a visible
 * slice of runtime each before switching, not constant thrashing. No-op
 * if the scheduler isn't active. */
void scheduler_timer_tick(struct interrupt_frame *frame);

#endif
