#ifndef GOS_USERMODE_H
#define GOS_USERMODE_H

/* Milestone 19.1: runs a tiny hand-assembled test blob (see
 * ring3_test_blob.asm, incbin'd from build/ring3_test.bin) in ring 3, to
 * prove the GDT user segments + TSS.rsp0 wiring actually work, isolated
 * from any ELF-parsing/FAT32 concerns. The blob calls SYS_WRITE then
 * SYS_EXIT via int 0x80. Returns 1 on success (mapping succeeded and
 * control returned via SYS_EXIT), 0 on failure (e.g. out of memory). */
int usermode_run_ring3_test(void);

/* Milestone 19.3: loads a static, non-relocatable ET_EXEC ELF64 binary
 * from the given FAT32 path, maps its PT_LOAD segments and a small user
 * stack with PAGE_USER permissions, and runs it in ring 3 until it calls
 * the exit syscall. Returns 1 on success, 0 on failure (file not found,
 * malformed ELF, wrong architecture, or out of memory). */
int usermode_run_elf(const char *path);

#endif
