/* Shared linker script for every Phase 20 multi-process test program.
 * Origin matches PROC_LOAD_BASE (kernel/include/process.h) - PML4 index 1,
 * a slot every process's private page tables get their own independent
 * copy of (see vmm_create_process_pml4()'s comment), so multiple processes
 * can all load at this exact same virtual address with zero collision -
 * each one's mapping lives in its own address space. */
ENTRY(_start)

SECTIONS
{
    . = 0x8000000000;

    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
