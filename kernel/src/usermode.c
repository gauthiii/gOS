#include <usermode.h>
#include <elf.h>
#include <vmm.h>
#include <pmm.h>
#include <heap.h>
#include <fat32.h>
#include <serial.h>
#include <stdint.h>

extern void enter_user_mode(uint64_t entry, uint64_t user_stack_top);

/* Milestone 19.1: the tiny ring3-test machine code blob, incbin'd from
 * build/ring3_test.bin (assembled separately as a flat binary - see
 * ring3_test_blob.asm and the Makefile rule for tools/userland). */
extern const uint8_t ring3_test_blob[];
extern const uint8_t ring3_test_blob_end[];

#define PAGE_SIZE 4096ULL

#define RING3_TEST_VADDR 0x0000000140000000ULL
#define RING3_TEST_STACK_VADDR 0x0000000150000000ULL
#define ELF_STACK_VADDR 0x0000000151000000ULL
#define USER_STACK_PAGES 4 /* 16 KiB - this phase's programs do nothing recursive/deep */

static uint64_t align_down(uint64_t v, uint64_t align) {
    return v & ~(align - 1);
}

static uint64_t align_up(uint64_t v, uint64_t align) {
    return (v + align - 1) & ~(align - 1);
}

/* Maps `count` freshly-allocated, zeroed 4KiB pages starting at `vaddr`
 * (which must be page-aligned) with PAGE_USER|PAGE_WRITABLE. Since this
 * runs in ring 0, the newly mapped pages are also directly writable by the
 * kernel right now (PAGE_USER only restricts ring-3 access) - no HHDM
 * indirection needed to zero/fill them. Returns 1 on success, 0 on PMM
 * exhaustion (already-mapped pages are left in place, not rolled back -
 * this phase has no process teardown yet, so a failed one-shot boot-time
 * test leaking a handful of pages is an acceptable, explicitly-scoped
 * limitation rather than a real leak in ongoing operation). */
static int map_user_pages(uint64_t vaddr, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            serial_write_string("usermode: pmm_alloc_page() failed while mapping user pages\n");
            return 0;
        }
        vmm_map_page(vaddr + i * PAGE_SIZE, phys, PAGE_WRITABLE | PAGE_USER);
        uint8_t *p = (uint8_t *)(vaddr + i * PAGE_SIZE);
        for (uint64_t j = 0; j < PAGE_SIZE; j++) {
            p[j] = 0;
        }
    }
    return 1;
}

int usermode_run_ring3_test(void) {
    uint64_t blob_size = (uint64_t)(ring3_test_blob_end - ring3_test_blob);
    uint64_t pages_needed = align_up(blob_size, PAGE_SIZE) / PAGE_SIZE;

    serial_write_string("usermode: ring3 test blob is ");
    serial_write_uint(blob_size);
    serial_write_string(" bytes (");
    serial_write_uint(pages_needed);
    serial_write_string(" page(s))\n");

    if (!map_user_pages(RING3_TEST_VADDR, pages_needed)) {
        return 0;
    }
    if (!map_user_pages(RING3_TEST_STACK_VADDR, USER_STACK_PAGES)) {
        return 0;
    }

    uint8_t *dest = (uint8_t *)RING3_TEST_VADDR;
    for (uint64_t i = 0; i < blob_size; i++) {
        dest[i] = ring3_test_blob[i];
    }

    uint64_t stack_top = align_down(RING3_TEST_STACK_VADDR + USER_STACK_PAGES * PAGE_SIZE, 16);

    serial_write_string("usermode: entering ring 3 at 0x");
    serial_write_hex64(RING3_TEST_VADDR);
    serial_write_string(" (stack top 0x");
    serial_write_hex64(stack_top);
    serial_write_string(")\n");

    enter_user_mode(RING3_TEST_VADDR, stack_top);

    serial_write_string("usermode: ring3 test returned to kernel context\n");
    return 1;
}

int usermode_run_elf(const char *path) {
    struct fat_dirent ent;
    if (!fat_resolve_path(path, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("usermode: ");
        serial_write_string(path);
        serial_write_string(" not found\n");
        return 0;
    }

    uint8_t *buf = (uint8_t *)kmalloc(ent.size);
    if (!buf) {
        serial_write_string("usermode: kmalloc for ELF file buffer failed\n");
        return 0;
    }
    int64_t n = fat_read_file(path, buf, ent.size);
    if (n != (int64_t)ent.size) {
        serial_write_string("usermode: fat_read_file size mismatch\n");
        kfree(buf);
        return 0;
    }

    if ((uint64_t)n < sizeof(struct elf64_ehdr)) {
        serial_write_string("usermode: file too small to be an ELF64 header\n");
        kfree(buf);
        return 0;
    }
    struct elf64_ehdr *eh = (struct elf64_ehdr *)buf;
    if (eh->e_ident[0] != ELF_MAG0 || eh->e_ident[1] != ELF_MAG1 ||
        eh->e_ident[2] != ELF_MAG2 || eh->e_ident[3] != ELF_MAG3) {
        serial_write_string("usermode: missing ELF magic\n");
        kfree(buf);
        return 0;
    }
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB) {
        serial_write_string("usermode: not a 64-bit little-endian ELF\n");
        kfree(buf);
        return 0;
    }
    if (eh->e_type != ET_EXEC) {
        serial_write_string("usermode: not ET_EXEC (only static, non-relocatable executables are supported)\n");
        kfree(buf);
        return 0;
    }
    if (eh->e_machine != EM_X86_64) {
        serial_write_string("usermode: not EM_X86_64\n");
        kfree(buf);
        return 0;
    }

    serial_write_string("usermode: ELF64 valid - entry=0x");
    serial_write_hex64(eh->e_entry);
    serial_write_string(" phnum=");
    serial_write_uint(eh->e_phnum);
    serial_write_string("\n");

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(buf + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }

        uint64_t seg_start = align_down(ph->p_vaddr, PAGE_SIZE);
        uint64_t seg_end = align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        uint64_t seg_pages = (seg_end - seg_start) / PAGE_SIZE;

        serial_write_string("usermode: PT_LOAD vaddr=0x");
        serial_write_hex64(ph->p_vaddr);
        serial_write_string(" filesz=");
        serial_write_uint(ph->p_filesz);
        serial_write_string(" memsz=");
        serial_write_uint(ph->p_memsz);
        serial_write_string(" (");
        serial_write_uint(seg_pages);
        serial_write_string(" page(s))\n");

        if (!map_user_pages(seg_start, seg_pages)) {
            kfree(buf);
            return 0;
        }

        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        for (uint64_t j = 0; j < ph->p_filesz; j++) {
            dest[j] = buf[ph->p_offset + j];
        }
        /* map_user_pages() already zeroed every page it mapped, so the
         * bss portion (p_memsz - p_filesz) is correctly zero-filled with
         * no extra work here. */
    }

    if (!map_user_pages(ELF_STACK_VADDR, USER_STACK_PAGES)) {
        kfree(buf);
        return 0;
    }
    uint64_t stack_top = align_down(ELF_STACK_VADDR + USER_STACK_PAGES * PAGE_SIZE, 16);

    uint64_t entry = eh->e_entry;
    kfree(buf); /* segments are already copied into their mapped pages - the raw file buffer is no longer needed */

    serial_write_string("usermode: entering ring 3 at 0x");
    serial_write_hex64(entry);
    serial_write_string(" (stack top 0x");
    serial_write_hex64(stack_top);
    serial_write_string(")\n");

    enter_user_mode(entry, stack_top);

    serial_write_string("usermode: ELF process returned to kernel context\n");
    return 1;
}
