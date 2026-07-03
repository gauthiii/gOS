; Milestone 20.1: bootstraps the very first process into ring 3 (the only
; case with no live interrupt frame to hijack yet - every subsequent
; context switch just overwrites the CURRENT interrupt frame in place from
; C, inside scheduler_reschedule(), and falls through to isr_common_stub's
; normal epilogue naturally).

bits 64
section .text

global scheduler_enter
global scheduler_resume_kernel
extern isr_common_epilogue

; void scheduler_enter(struct interrupt_frame *first_regs, uint64_t pml4_phys);
; System V AMD64: rdi = first_regs, rsi = pml4_phys.
; first_regs must point at a struct laid out exactly like idt.h's
; interrupt_frame (r15..r15 down to rax, vector, error_code, rip, cs,
; rflags, rsp, ss) - process_spawn() builds one by hand for a brand-new
; process. Never "returns" in the normal sense; see scheduler_resume_kernel.
scheduler_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel sched_kernel_rsp], rsp

    mov cr3, rsi
    mov rsp, rdi
    jmp isr_common_epilogue

; void scheduler_resume_kernel(void);
; Called by scheduler_reschedule() (kernel/src/scheduler.c) when no process
; is left READY - resumes scheduler_run_until_done()'s caller as if
; scheduler_enter() had simply returned normally, the same one-way-trampoline
; technique Phase 19's usermode_entry.asm uses for its single-process case.
scheduler_resume_kernel:
    mov rsp, [rel sched_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    sti
    ret

section .bss
align 8
sched_kernel_rsp: resq 1
