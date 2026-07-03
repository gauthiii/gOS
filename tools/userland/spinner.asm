; Milestones 20.1/20.3: writes a single distinctive marker character
; (chosen at assemble time via -DMARKER='X') ITERS times, with a spin
; delay between each write so real preemption has something to interrupt -
; a tight loop with no delay could plausibly finish within one time slice
; and never actually get preempted, which would prove nothing.

bits 64
%ifndef MARKER
%define MARKER 'X'
%endif
%ifndef ITERS
%define ITERS 25
%endif

global _start
section .text
_start:
    mov r15, ITERS
.loop:
    mov rax, 1              ; SYS_WRITE
    lea rdi, [rel marker_byte]
    mov rsi, 1
    int 0x80

    mov rcx, 5000000
.delay:
    dec rcx
    jnz .delay

    dec r15
    jnz .loop

    mov rax, 60              ; SYS_EXIT
    xor rdi, rdi
    int 0x80
    jmp $

section .data
marker_byte: db MARKER
