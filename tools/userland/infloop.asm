; Milestone 26.2 (audit2 High #7): deliberately never calls SYS_EXIT - an
; infinite loop, simulating a hung/buggy user-mode program. Before the
; scheduler watchdog (process.c's run_deadline_tick), running this would
; hang scheduler_run_until_done() forever, freezing the entire desktop
; with no recovery. After the fix, the scheduler should kill it after its
; time budget expires and return control to the kernel.

bits 64
global _start
section .text
_start:
    mov rax, 1                 ; SYS_WRITE - prove it started running at all
    lea rdi, [rel msg]
    mov rsi, msglen
    int 0x80
.spin:
    jmp .spin                  ; never exits

section .data
msg: db "INFLOOP_STARTED: about to spin forever unless killed", 10
msglen equ $ - msg
