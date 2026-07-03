; Milestone 20.2: spawned by parent.asm, prints a marker then exits with a
; specific, checkable status code (7) via SYS_EXIT's rdi argument.

bits 64
global _start
section .text
_start:
    mov rax, 1               ; SYS_WRITE
    lea rdi, [rel msg]
    mov rsi, msglen
    int 0x80

    mov rax, 60              ; SYS_EXIT
    mov rdi, 7                ; exit code
    int 0x80
    jmp $

section .data
msg: db "CHILD_RUNNING", 10
msglen equ $ - msg
