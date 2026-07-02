; Milestone 19.1: the smallest possible ring-3 program, assembled to a flat
; binary (nasm -f bin) and incbin'd directly into the kernel image by
; kernel/src/ring3_test_blob.asm - no FAT32/ELF parsing involved, so this
; isolates "does the ring0->ring3 transition and syscall gate actually
; work" from "does the ELF loader work" (that's Milestone 19.3, tested
; separately with a real bundled ELF binary).
;
; Only RIP-relative addressing is used, so the `org` value below doesn't
; actually affect correctness (see kernel/src/usermode.c's comment) - it's
; kept matching RING3_TEST_VADDR purely for readability.

bits 64
org 0x140000000

_start:
    mov rax, 1               ; SYS_WRITE
    lea rdi, [rel msg]
    mov rsi, msglen
    int 0x80

    mov rax, 60              ; SYS_EXIT
    int 0x80

    ; unreachable - SYS_EXIT never returns to here
    jmp $

msg: db "RING3_TEST_PASS: syscall round-trip from ring 3 succeeded"
msglen equ $ - msg
