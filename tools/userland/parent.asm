; Milestone 20.2: spawns CHILD.ELF via SYS_SPAWN, polls SYS_WAITPID until
; the child becomes a zombie (gOS's wait is non-blocking/poll-style, not a
; true blocking syscall - see phase20.md), then prints the exact exit code
; it got back, proving the parent<->child exit-code round trip works.

bits 64
global _start
section .text
_start:
    mov rax, 2                    ; SYS_SPAWN
    lea rdi, [rel childpath]
    mov rsi, childpath_len
    int 0x80
    mov r15, rax                   ; save child pid

.waitloop:
    mov rax, 3                    ; SYS_WAITPID
    mov rdi, r15
    int 0x80
    cmp rax, -1
    je .waitloop
    mov r13, rax                   ; save exit code

    mov rax, 1                    ; SYS_WRITE "PARENT_GOT:"
    lea rdi, [rel resultmsg]
    mov rsi, resultmsg_len
    int 0x80

    mov al, r13b
    add al, '0'
    mov [rel digit], al
    mov rax, 1
    lea rdi, [rel digit]
    mov rsi, 1
    int 0x80

    mov rax, 1                    ; trailing newline
    lea rdi, [rel nl]
    mov rsi, 1
    int 0x80

    mov rax, 60                   ; SYS_EXIT
    xor rdi, rdi
    int 0x80
    jmp $

section .data
childpath: db "CHILD.ELF"
childpath_len equ $ - childpath
resultmsg: db "PARENT_GOT:"
resultmsg_len equ $ - resultmsg
nl: db 10
section .bss
digit: resb 1
