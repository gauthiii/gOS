; Milestone 19.3: a genuine, separately-built ELF64 "hello from userland"
; program - proves the loader in kernel/src/usermode.c actually parses
; PT_LOAD segments and jumps to whatever entry point the ELF specifies,
; rather than relying on any hardcoded address (unlike the Milestone 19.1
; ring3_test.asm blob, which is copied directly into a kernel-chosen
; address with no ELF involved at all).

bits 64

global _start
section .text
_start:
    mov rax, 1                 ; SYS_WRITE
    lea rdi, [rel msg]
    mov rsi, msglen
    int 0x80

    mov rax, 60                ; SYS_EXIT
    int 0x80

    jmp $                      ; unreachable

section .data
msg: db "HELLO_FROM_USERLAND: a real ELF64 binary, loaded and executed in ring 3", 10
msglen equ $ - msg
