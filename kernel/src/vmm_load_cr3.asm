; void vmm_load_cr3(uint64_t phys);
bits 64
section .text
global vmm_load_cr3

vmm_load_cr3:
    mov cr3, rdi
    ret
