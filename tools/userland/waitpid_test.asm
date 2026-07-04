; Milestone 26.3 (audit2 High #8): tries SYS_WAITPID on every possible pid
; (0..7, MAX_PROCESSES) even though none of them are this process's own
; child (it never called SYS_SPAWN itself) - every single attempt must be
; rejected (return -1) by the parent-pid ownership check, even for a pid
; that IS a genuine zombie (e.g. a kernel-spawned test ELF that already
; exited), since kernel-spawned processes have parent_pid=-1, which can
; never equal this process's own (non-negative) pid. If even one attempt
; returns a non-negative value, that's the ownership check failing to
; reject a non-parent reap.

bits 64
global _start
section .text
_start:
    xor r15, r15            ; r15 = pid counter, 0..7

.loop:
    mov rax, 3               ; SYS_WAITPID
    mov rdi, r15
    int 0x80
    ; rax = result. If rax != -1 (i.e. not 0xFFFFFFFFFFFFFFFF), that pid's
    ; waitpid unexpectedly succeeded - print an UNEXPECTED marker.
    cmp rax, -1
    je .next
    mov rax, 1                ; SYS_WRITE
    lea rdi, [rel unexpected_msg]
    mov rsi, unexpected_msglen
    int 0x80

.next:
    inc r15
    cmp r15, 8
    jl .loop

    mov rax, 1                 ; SYS_WRITE
    lea rdi, [rel done_msg]
    mov rsi, done_msglen
    int 0x80

    mov rax, 60                 ; SYS_EXIT
    mov rdi, 99                 ; marker exit code
    int 0x80
    jmp $

section .data
unexpected_msg: db "WAITPID_TEST: UNEXPECTED - a non-parent waitpid succeeded", 10
unexpected_msglen equ $ - unexpected_msg
done_msg: db "WAITPID_TEST: all non-parent waitpid attempts correctly rejected", 10
done_msglen equ $ - done_msg
