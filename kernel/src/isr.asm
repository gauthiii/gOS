; ISR/IRQ entry trampolines. Vectors 0-31 are CPU exceptions; vectors 32-47
; are remapped hardware IRQs (Milestone 2.3). Each stub pushes a vector
; number (and a dummy error code if the CPU doesn't push one itself), then
; falls into the common stub which saves full register state and calls the
; C-level dispatcher.

bits 64
section .text

extern isr_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0        ; dummy error code
    push qword %1        ; vector number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1        ; vector number (CPU already pushed error code)
    jmp isr_common_stub
%endmacro

%macro IRQ_STUB 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp isr_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Remapped hardware IRQs 0-15 -> vectors 32-47.
IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; Milestone 19.2: vector 0x80 (128), the syscall gate. Uses the same
; ISR_NOERR shape (int 0x80 pushes no error code) and the same common
; stub as every other vector - the DPL=3 IDT gate (set in idt.c) is what
; actually allows ring-3 code to invoke it; the entry mechanics here are
; identical to a CPU exception or IRQ landing on this stub.
ISR_NOERR 128

isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16          ; discard vector + error_code
    iretq
