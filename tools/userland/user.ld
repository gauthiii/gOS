/* Minimal linker script for Milestone 19.3's bundled user-mode ELF64
 * binaries: a single fixed load address (0x141000000 - distinct from the
 * Milestone 19.1 ring3 test blob's 0x140000000, purely so serial logs
 * never show the same address for two different tests), static/
 * non-relocatable (ET_EXEC), no dynamic linking. */
ENTRY(_start)

SECTIONS
{
    . = 0x141000000;

    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
