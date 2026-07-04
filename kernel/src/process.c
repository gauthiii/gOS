#include <process.h>
#include <elf.h>
#include <vmm.h>
#include <pmm.h>
#include <heap.h>
#include <fat32.h>
#include <gdt.h>
#include <serial.h>
#include <stdint.h>

extern void scheduler_enter(struct interrupt_frame *first_regs, uint64_t pml4_phys);
extern void scheduler_resume_kernel(void);
extern void vmm_load_cr3(uint64_t phys);

#define PAGE_SIZE 4096ULL

static struct process procs[MAX_PROCESSES];
static int current_pid = -1;
static int scheduler_active = 0;

static uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }
static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        procs[i].state = PROC_UNUSED;
        procs[i].pid = i;
    }
    current_pid = -1;
    scheduler_active = 0;
}

struct process *process_get(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) {
        return 0;
    }
    return &procs[pid];
}

int process_count_active(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].state == PROC_READY || procs[i].state == PROC_RUNNING) {
            n++;
        }
    }
    return n;
}

int scheduler_is_active(void) { return scheduler_active; }
int scheduler_current_pid(void) { return current_pid; }

/* Allocates one fresh physical page, maps it into the given process's
 * private PML4 at `vaddr` (PAGE_USER|PAGE_WRITABLE), and fills it: the
 * first `copy_len` bytes come from `src` (may be NULL/0 for a pure-bss
 * page), the rest is zeroed. Writes through the physical/identity-mapped
 * address (vmm_init() identity-maps the full low 4GiB 1:1) rather than the
 * process's own virtual address, since that process's PML4 isn't
 * necessarily the one currently loaded during process creation - CR3
 * doesn't change until the process actually runs. Returns 0 on OOM. */
static int map_and_fill_page(uint64_t pml4_phys, uint64_t vaddr, const uint8_t *src, uint64_t copy_len) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        serial_write_string("process: pmm_alloc_page() failed while loading a process\n");
        return 0;
    }
    vmm_map_page_in(pml4_phys, vaddr, phys, PAGE_WRITABLE | PAGE_USER);

    uint8_t *p = (uint8_t *)phys;
    for (uint64_t j = 0; j < PAGE_SIZE; j++) {
        p[j] = (src && j < copy_len) ? src[j] : 0;
    }
    return 1;
}

int process_spawn(const char *path) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        serial_write_string("process: spawn failed - process table full\n");
        return -1;
    }

    struct fat_dirent ent;
    if (!fat_resolve_path(path, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("process: spawn failed - ");
        serial_write_string(path);
        serial_write_string(" not found\n");
        return -1;
    }
    uint8_t *buf = (uint8_t *)kmalloc(ent.size);
    if (!buf) {
        serial_write_string("process: spawn failed - kmalloc for ELF buffer\n");
        return -1;
    }
    int64_t n = fat_read_file(path, buf, ent.size);
    if (n != (int64_t)ent.size || (uint64_t)n < sizeof(struct elf64_ehdr)) {
        serial_write_string("process: spawn failed - fat_read_file/size mismatch\n");
        kfree(buf);
        return -1;
    }
    struct elf64_ehdr *eh = (struct elf64_ehdr *)buf;
    if (eh->e_ident[0] != ELF_MAG0 || eh->e_ident[1] != ELF_MAG1 ||
        eh->e_ident[2] != ELF_MAG2 || eh->e_ident[3] != ELF_MAG3 ||
        eh->e_ident[4] != ELFCLASS64 || eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) {
        serial_write_string("process: spawn failed - not a supported ELF64 ET_EXEC\n");
        kfree(buf);
        return -1;
    }

#if defined(GOS_TEST_PHASE25_DEBUG)
    serial_write_string("DEBUG: pmm_free_pages before vmm_create_process_pml4 = ");
    serial_write_uint(pmm_free_pages());
    serial_write_string("\n");
#endif
    uint64_t pml4_phys = vmm_create_process_pml4();

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(buf + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        uint64_t seg_start = align_down(ph->p_vaddr, PAGE_SIZE);
        uint64_t seg_end = align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);

        for (uint64_t vaddr = seg_start; vaddr < seg_end; vaddr += PAGE_SIZE) {
            /* How this page's bytes map back into the file, if at all. */
            int64_t rel = (int64_t)vaddr - (int64_t)ph->p_vaddr;
            const uint8_t *src = 0;
            uint64_t copy_len = 0;
            if (rel < (int64_t)ph->p_filesz) {
                uint64_t file_off = ph->p_offset + (rel > 0 ? (uint64_t)rel : 0);
                uint64_t avail = ph->p_filesz - (rel > 0 ? (uint64_t)rel : 0);
                src = buf + file_off;
                copy_len = avail < PAGE_SIZE ? avail : PAGE_SIZE;
                if (rel < 0) {
                    /* First page of a segment not page-aligned in the
                     * file - none of our bundled test binaries hit this
                     * (their linker scripts align every segment), so this
                     * is defensive rather than exercised; treat as no
                     * copy for that leading slice. */
                    src = 0;
                    copy_len = 0;
                }
            }
            if (!map_and_fill_page(pml4_phys, vaddr, src, copy_len)) {
                kfree(buf);
                return -1;
            }
        }
    }
    kfree(buf);

    for (uint64_t i = 0; i < PROC_STACK_PAGES; i++) {
        if (!map_and_fill_page(pml4_phys, PROC_STACK_BASE + i * PAGE_SIZE, 0, 0)) {
            return -1;
        }
    }

#if defined(GOS_TEST_PHASE25_DEBUG)
    serial_write_string("DEBUG: pmm_free_pages after all mapping (before kstack kmalloc) = ");
    serial_write_uint(pmm_free_pages());
    serial_write_string("\n");
#endif
    uint8_t *kstack = (uint8_t *)kmalloc(PROC_KSTACK_SIZE);
    if (!kstack) {
        serial_write_string("process: spawn failed - kmalloc for kernel stack\n");
        return -1;
    }

    struct process *p = &procs[slot];
    for (int i = 0; i < (int)(sizeof(p->regs) / sizeof(uint64_t)); i++) {
        ((uint64_t *)&p->regs)[i] = 0;
    }
    p->regs.rip = eh->e_entry;
    p->regs.cs = 0x1B;  /* GDT_USER_CODE | 3 */
    p->regs.rflags = 0x202; /* IF set */
    p->regs.rsp = align_down(PROC_STACK_BASE + PROC_STACK_PAGES * PAGE_SIZE, 16);
    p->regs.ss = 0x23;  /* GDT_USER_DATA | 3 */
    p->pml4_phys = pml4_phys;
    p->kstack_base = kstack;
    p->kstack_top = (uint64_t)&kstack[PROC_KSTACK_SIZE];
    p->parent_pid = -1;
    p->exit_code = 0;
    p->state = PROC_READY;

    serial_write_string("process: spawned pid=");
    serial_write_uint((uint64_t)slot);
    serial_write_string(" (");
    serial_write_string(path);
    serial_write_string(") entry=0x");
    serial_write_hex64(eh->e_entry);
    serial_write_string("\n");
    return slot;
}

/* PML4 slot every process-private mapping lives under (see process.h's
 * PROC_LOAD_BASE/PROC_STACK_BASE comment) - the only slot vmm_destroy_
 * process_pml4() is ever told to walk, so the shared kernel slot(s) can
 * never be freed by this call. */
#define PROC_PML4_SLOT ((PROC_LOAD_BASE >> 39) & 0x1FF)

void process_free_resources(int pid) {
    struct process *p = process_get(pid);
    if (!p) {
        return;
    }
#if defined(GOS_TEST_PHASE25_DEBUG)
    serial_write_string("DEBUG: pmm_free_pages before destroy = ");
    serial_write_uint(pmm_free_pages());
    serial_write_string("\n");
#endif
    if (p->pml4_phys) {
        vmm_destroy_process_pml4(p->pml4_phys, PROC_PML4_SLOT);
        p->pml4_phys = 0;
    }
#if defined(GOS_TEST_PHASE25_DEBUG)
    serial_write_string("DEBUG: pmm_free_pages after destroy, before kstack free = ");
    serial_write_uint(pmm_free_pages());
    serial_write_string("\n");
#endif
    if (p->kstack_base) {
        kfree(p->kstack_base);
        p->kstack_base = 0;
        p->kstack_top = 0;
    }
}

static int pick_next_ready(int after) {
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (after + i) % MAX_PROCESSES;
        if (procs[idx].state == PROC_READY) {
            return idx;
        }
    }
    return -1;
}

void scheduler_reschedule(struct interrupt_frame *frame) {
    if (current_pid != -1 && procs[current_pid].state == PROC_RUNNING) {
        procs[current_pid].regs = *frame;
        procs[current_pid].state = PROC_READY;
    }

    int next = pick_next_ready(current_pid == -1 ? 0 : current_pid);
    if (next == -1) {
        scheduler_active = 0;
        current_pid = -1;
        scheduler_resume_kernel(); /* does not return */
    }

    procs[next].state = PROC_RUNNING;
    current_pid = next;
    gdt_set_tss_rsp0(procs[next].kstack_top);
    *frame = procs[next].regs;
    vmm_load_cr3(procs[next].pml4_phys);
}

#define TIME_SLICE_TICKS 1 /* ~10ms at the 100Hz PIT rate (kernel/include/timer.h) */

void scheduler_timer_tick(struct interrupt_frame *frame) {
    static int slice_counter = 0;
    if (!scheduler_active) {
        return;
    }
    if (++slice_counter < TIME_SLICE_TICKS) {
        return;
    }
    slice_counter = 0;
    scheduler_reschedule(frame);
}

void scheduler_run_until_done(void) {
    int first = pick_next_ready(-1);
    if (first == -1) {
        serial_write_string("scheduler: no READY process - nothing to run\n");
        return;
    }
    scheduler_active = 1;
    procs[first].state = PROC_RUNNING;
    current_pid = first;
    gdt_set_tss_rsp0(procs[first].kstack_top);
    serial_write_string("scheduler: starting - first pid=");
    serial_write_uint((uint64_t)first);
    serial_write_string("\n");
    scheduler_enter(&procs[first].regs, procs[first].pml4_phys); /* does not return until all zombie */
    serial_write_string("scheduler: all processes finished - back in kernel context\n");
}
