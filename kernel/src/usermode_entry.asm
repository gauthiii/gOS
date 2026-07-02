; Milestone 19.1: the ring0->ring3 transition trampoline, and the matching
; one-way "resume" path SYS_EXIT uses to get back out.
;
; GDT_USER_CODE=0x18, GDT_USER_DATA=0x20 (kernel/include/gdt.h) - hardcoded
; here with |3 (RPL=3) rather than included from C, matching this project's
; existing convention of raw, header-independent .asm trampolines
; (gdt_flush.asm, idt_load.asm, vmm_load_cr3.asm all do the same).

bits 64
section .text

global enter_user_mode
global gos_resume_after_user_mode

; void enter_user_mode(uint64_t entry, uint64_t user_stack_top);
; System V AMD64: rdi = entry, rsi = user_stack_top.
;
; This never "returns" in the normal call/ret sense - it iretq's into ring
; 3. The only way back to the C code that called this is via SYS_EXIT
; invoking gos_resume_after_user_mode below, which resumes execution right
; after the `call enter_user_mode` site, as if this function had simply
; returned normally once the user-mode program finished.
enter_user_mode:
    ; Save every callee-saved register the System V ABI requires this
    ; function to preserve, so gos_resume_after_user_mode can restore them
    ; and the caller sees a normal, ABI-correct "return".
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel kernel_resume_rsp], rsp

    ; Load user data segment selectors before dropping to ring 3 - CS is
    ; set via the iretq frame below, but DS/ES/FS/GS must be loaded
    ; explicitly (iretq only restores SS from the frame, not the others).
    mov ax, 0x23        ; GDT_USER_DATA | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iretq pops, in order: RIP, CS, RFLAGS, RSP, SS - so push in the
    ; reverse order.
    push qword 0x23             ; SS = GDT_USER_DATA | 3
    push rsi                     ; RSP = user_stack_top
    pushfq
    pop rax
    or rax, 0x200                ; ensure IF is set in the frame we drop into
    push rax                     ; RFLAGS
    push qword 0x1B              ; CS = GDT_USER_CODE | 3
    push rdi                     ; RIP = entry
    iretq

; void gos_resume_after_user_mode(void);
; Called from syscall.c's SYS_EXIT handler, which is running in ring 0 on
; the current interrupt stack (TSS.rsp0's stack - the CPU switched to it
; automatically on the ring3->ring0 transition through the syscall gate).
; Since we're already in ring 0, getting back to enter_user_mode's caller
; is just a stack switch + the callee-saved-register epilogue + ret - no
; iretq needed for this direction (iretq is only required to safely drop
; privilege, not to raise it back to a privilege we're already running at).
;
; This deliberately bypasses isr_common_stub's normal iretq epilogue
; entirely - syscall_dispatch() calls this instead of returning, so
; isr_handler's caller (isr_common_stub) never resumes.
gos_resume_after_user_mode:
    mov rsp, [rel kernel_resume_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    sti                  ; the syscall gate is an interrupt gate (clears IF
                          ; on entry); restore it before resuming normal
                          ; kernel flow, which expects interrupts enabled
    ret

section .bss
align 8
kernel_resume_rsp: resq 1
