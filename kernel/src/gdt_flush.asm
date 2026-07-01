; void gdt_flush(uint64_t gdtp_addr, uint16_t tss_selector);
; System V AMD64 ABI: rdi = gdtp_addr, si = tss_selector

bits 64
section .text
global gdt_flush

gdt_flush:
    lgdt [rdi]

    mov ax, 0x10        ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload CS via a far return, since CS cannot be loaded with `mov`.
    lea rax, [rel .flush]
    push qword 0x08      ; kernel code selector
    push rax
    retfq

.flush:
    mov ax, si
    ltr ax
    ret
