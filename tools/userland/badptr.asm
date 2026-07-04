; Milestone 25.1 (audit2 Critical #1): deliberately calls SYS_WRITE with an
; unmapped pointer (virtual address 0, which nothing ever maps for a user
; process) and a nonzero length. Before the fix, the kernel would
; dereference this directly while servicing the syscall in ring 0,
; page-faulting and halting the entire machine (idt.c's page-fault handler
; unconditionally panics). After the fix, the syscall should be rejected
; (return -1 in rax) and this process should be able to continue and exit
; normally with a distinct marker code, proving the kernel survived.

bits 64
global _start
section .text
_start:
    mov rax, 1                 ; SYS_WRITE
    xor rdi, rdi                ; rdi = 0 (unmapped in every process)
    mov rsi, 8                  ; length
    int 0x80
    ; rax now holds the syscall's return value; if we're still alive here
    ; (the kernel didn't panic), print a message proving it, using a REAL
    ; mapped pointer this time, then exit with a distinctive code.
    mov rax, 1                  ; SYS_WRITE
    lea rdi, [rel msg]
    mov rsi, msglen
    int 0x80

    mov rax, 60                 ; SYS_EXIT
    mov rdi, 42                 ; distinctive marker exit code
    int 0x80
    jmp $

section .data
msg: db "BADPTR_SURVIVED: kernel rejected the bad pointer instead of faulting", 10
msglen equ $ - msg
