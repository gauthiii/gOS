/* gOS kernel linker script.
 * Higher-half kernel: physical load address is arbitrary (Limine picks it,
 * typically identity-mapped low physical memory during boot), but the
 * kernel is linked to run at -2GiB (0xffffffff80000000), the standard
 * x86_64 higher-half kernel base used by Limine tutorials.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD    FLAGS((1 << 2)) ;             /* Read only */
    text     PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata   PT_LOAD    FLAGS((1 << 2)) ;             /* Read only */
    data     PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;  /* Write + Read */
}

SECTIONS
{
    . = 0xffffffff80000000;

    /* Limine request structs must live in their own section/segment so the
     * bootloader can find them by scanning the ELF program headers before
     * any paging/relocation happens. */
    .requests : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}
