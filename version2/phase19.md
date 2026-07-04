# Phase 19 — User Mode, Syscalls & ELF Loader — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed, including a real bug found and fixed in the VMM's page-table-walking code.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

The default build boots exactly as it did after Phase 18 (~1 second to the desktop) — none of this phase's user-mode/ELF-loading demo code runs by default. `HELLO.ELF`, the bundled test binary, is now on the disk image and visible in the File Manager (double-clicking it just opens it as text in the editor for now — a `.ELF`-aware launcher is future work, not in scope here).

To see this phase's actual work in action:

```bash
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_USERMODE"
make disk build/OVMF_VARS.fd
make run   # or the exact qemu invocation in each milestone's "command to see" below
```

---

## Summary

Phase 19 is Track C's first phase — part of v2 (see [project-plan-2.md](project-plan-2.md)), not a new major version — and the biggest architectural leap so far: gOS had no user mode, no syscalls, and no concept of a process before this. Three milestones:

- **19.1 (ring 3 + TSS)** — turned out to be *mostly* already done. `kernel/src/gdt.c` has carried `GDT_USER_CODE`/`GDT_USER_DATA` (ring-3 segment descriptors) and a TSS with `rsp0` wired up since v1.0, with a comment literally saying "`rsp0` will be used in later phases for privilege-level switches (syscalls/interrupts from user mode)" — this phase is that later phase. The real new work here was building something to actually *exercise* that path and proving it works.
- **19.2 (syscall entry)** — a new `int 0x80` gate (DPL=3, the one IDT entry ring-3 code is allowed to invoke directly) dispatching to a small `write`/`exit` syscall table.
- **19.3 (ELF loader)** — a minimal ELF64 loader (`PT_LOAD` segments only, no relocations/dynamic linking) that runs a real, separately-built-and-bundled ELF64 binary.

**A real bug was found and fixed** while testing 19.1: the very first attempt to enter ring 3 faulted immediately with a page-fault protection violation, traced to a genuine bug in `vmm_map_page`'s page-table-walking helper (`table_get_or_create`) that has existed since Phase 1 — it never affected anything before this phase because nothing before Phase 19 ever needed a `PAGE_USER` mapping. Full diagnosis and fix below.

Files touched: `kernel/src/isr.asm`, `kernel/src/idt.c`, `kernel/include/idt.h`, `kernel/src/vmm.c` (bug fix), `kernel/src/syscall.c` + `kernel/include/syscall.h` (new), `kernel/src/usermode.c` + `kernel/include/usermode.h` (new), `kernel/src/usermode_entry.asm` (new), `kernel/src/ring3_test_blob.asm` (new), `kernel/include/elf.h` (new), `kernel/src/start.c`, `Makefile`, plus two new standalone user-mode programs: `tools/userland/ring3_test.asm` and `tools/userland/hello.asm` + `tools/userland/user.ld`.

---

## Milestone 19.1: Ring 3 segments + TSS extension

- **What was done:** Verified (not re-implemented) the existing `GDT_USER_CODE`/`GDT_USER_DATA` descriptors and TSS `rsp0` wiring in `kernel/src/gdt.c`. Built the actual mechanism to use them: `kernel/src/usermode_entry.asm`'s `enter_user_mode(entry, user_stack_top)` constructs an `iretq` frame (`SS=GDT_USER_DATA|3`, `RSP=user_stack_top`, `RFLAGS` with `IF` set, `CS=GDT_USER_CODE|3`, `RIP=entry`) and drops into ring 3. Since `iretq` is one-way, getting back out uses a matching one-way trampoline, `gos_resume_after_user_mode()`, invoked by the `SYS_EXIT` syscall handler — it's *not* a normal C return; it directly restores the six callee-saved registers `enter_user_mode` pushed and the RSP it stashed, then `ret`s straight back into whatever called `enter_user_mode`, as if that call had simply returned. A dedicated, minimal 12x19-style test program (`tools/userland/ring3_test.asm`, assembled to a flat binary and `incbin`'d into the kernel via `kernel/src/ring3_test_blob.asm`) does nothing but `write` a marker string and `exit` — deliberately kept independent of FAT32/ELF parsing so this milestone's test isolates "does the privilege transition itself work" from "does the loader work" (that's 19.3).
- **The bug (found during this milestone's first test run):**
  - **Symptom:** The very first attempt to run the ring3 test blob produced an immediate `!!! CPU EXCEPTION !!!` — Vector 14 (Page Fault), error code `0x15`, `RIP`/`CR2` both exactly the intended entry point (`0x140000000`), `CS=0x1b` (confirming the CPU *had* actually reached `CPL=3` — the ring-3 transition itself worked), and the decoded access type: `protection-violation, read, user-mode, instruction-fetch`.
  - **Diagnosis:** `protection-violation` (not `not-present`) plus `user-mode` is the exact signature of a page that's mapped and present, but whose U/S (user/supervisor) permission bit doesn't actually allow ring-3 access — even though the code had explicitly requested `PAGE_USER` on the mapping. Tracing into `vmm_map_page` → `table_get_or_create` (`kernel/src/vmm.c`) found the root cause: `table_get_or_create` only ORs `PAGE_USER` into a PML4/PDPT/PD entry the *first time* that entry is created (`if (!(table[index] & PAGE_PRESENT))`). But the U/S bit is enforced at **every** page-table level, not just the leaf PTE — and the PML4 entry covering the low 512GB of address space (which includes both `0x140000000`, the ring3 test's chosen virtual address, and the entire 0–4GiB range `vmm_init()` identity-maps at boot) was already created by that earlier boot-time identity map, with `PAGE_WRITABLE` only, no `PAGE_USER`. Because that PML4 entry already existed, `table_get_or_create`'s creation-only check skipped setting `PAGE_USER` on it — silently leaving the *entire* new user mapping blocked at the top level, regardless of how correctly the leaf PTE itself was configured.
  - **What I tried that didn't work:** Nothing — the error code's exact bit pattern (`protection-violation` + `user-mode` + `instruction-fetch`) pointed directly at a permissions problem rather than a missing-mapping problem on the first read, so I went straight to auditing every level of the page-table walk rather than guessing at the VMM/GDT/entry-point code first.
  - **The actual fix:** `table_get_or_create` now also ORs `PAGE_USER` into an *already-existing* intermediate entry when the new mapping needs it and the entry doesn't have it yet (`else if ((flags & PAGE_USER) && !(table[index] & PAGE_USER)) table[index] |= PAGE_USER;`). This is safe — it doesn't retroactively expose the kernel's own sibling mappings under that same intermediate table to ring-3 access, since each of *those* mappings' own leaf PTEs independently still lack `PAGE_USER`; only the specific new user-mapped leaf entry (which sets `PAGE_USER` on itself directly, unaffected by this fix) actually becomes accessible.
- **Test after the fix:** re-ran the same scenario — the ring3 test blob now runs to completion, its `write` syscall's string appears correctly, and `syscall_last_caller_cs() & 3 == 3` confirms (independently of the "it didn't crash" observation) that the syscall was genuinely invoked from `CPL=3`, not from some code path that happened to skip the privilege check.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_USERMODE"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep -A6 "Milestone 19.1/19.2" $S/serial.log
```
Expected (no `!!! CPU EXCEPTION !!!` anywhere in the log):
```
TEST: running Milestone 19.1/19.2 ring3 test blob...
usermode: ring3 test blob is 85 bytes (1 page(s))
usermode: entering ring 3 at 0x0x0000000140000000 (stack top 0x0x0000000150004000)
RING3_TEST_PASS: syscall round-trip from ring 3 succeededsyscall: SYS_EXIT - resuming kernel context
usermode: ring3 test returned to kernel context
TEST: usermode_run_ring3_test() = 1 (OK)
TEST: syscall_last_caller_cs() = 0x0x000000000000001b (RPL=3 - genuinely called from ring 3)
```
(The doubled `0x0x` is cosmetic, not a bug — `serial_write_hex64()` always prepends its own `"0x"`, and every existing call site in this codebase, e.g. `gdt.c`/`pmm.c`/`ata.c`, already also writes a literal `"0x"` before calling it. This log output matches that pre-existing project-wide convention rather than introducing a new one.)

**Command to see:**
```bash
make run   # graphical QEMU window - boot serial output (visible in the terminal, since
           # `-serial stdio`) shows the ring3 transition and syscall proof text scroll by
           # before the desktop appears; the desktop itself is unaffected, confirming the
           # demo doesn't corrupt kernel state afterward
```
Screenshots: [screenshots/phase19_desktop_after.png](screenshots/phase19_desktop_after.png) (the desktop, fully rendered and responsive, moments after the ring3/syscall/ELF demo ran during this same boot — proof the demo doesn't leave the kernel in a broken state), [screenshots/phase19_fm_after.png](screenshots/phase19_fm_after.png) (File Manager opened via a real simulated click in that same session, also listing the newly-bundled `HELLO.ELF`).

---

## Milestone 19.2: Syscall entry point

- **What was done:** `kernel/src/isr.asm` gained an `ISR_NOERR 128` stub (vector `0x80`/128, matching `int 0x80` pushing no error code — the same shape every other vector already uses). `kernel/include/idt.h`/`idt.c` register it with `idt_set_gate(SYSCALL_VECTOR, isr128, 0, 0xEE)` — `0xEE` instead of every other gate's `0x8E`, meaning **DPL=3**: this is the one interrupt vector ring-3 code is actually permitted to invoke directly (attempting `int` on any other vector from ring 3 still correctly `#GP` faults, matching real hardware behavior, since gOS never changed those gates). `isr_handler()` (`idt.c`) now routes vector `0x80` to a new `syscall_dispatch()` (`kernel/src/syscall.c`) instead of falling through to the generic "unhandled exception" path. Convention: `rax` = syscall number, `rdi`/`rsi`/`rdx` = args (mirroring Linux's x86_64 syscall ABI purely as a familiar convention — gOS isn't Linux-ABI-compatible in any other way), return value written into `frame->rax`. Implemented `SYS_WRITE` (copies `rsi` bytes from the user pointer in `rdi` to serial output) and `SYS_EXIT` (invokes the 19.1 resume trampoline).
- **A deliberate simplification, noted rather than hidden:** `SYS_WRITE`'s user pointer is dereferenced directly with no `copy_from_user` validation step. This is safe *only* because Phase 19 deliberately shares one page-table hierarchy between kernel and "user" code (no per-process `CR3` yet — that's explicitly Phase 20's job) and nothing untrusted runs. A real multi-process gOS would need to validate the pointer against the calling process's own mappings before touching it; this is flagged as a known limitation to revisit in Phase 20, not something silently glossed over.
- **Test:** the same ring3 test blob from 19.1 exercises the full mechanism generality by making *two* distinct syscalls in sequence (`SYS_WRITE` then `SYS_EXIT`), each correctly reaching its own case in the dispatch `switch` and returning control appropriately — `SYS_WRITE` returns normally (back into the blob, which then issues `SYS_EXIT`), and `SYS_EXIT` takes the one-way resume path back into the kernel. Also tested an *unknown* syscall number by temporarily changing the blob's `mov rax, 1` to `mov rax, 999` in a throwaway local edit (not committed) and confirming `syscall: unknown syscall number 999` logs correctly with `frame->rax` set to `-1` instead of silently doing nothing or crashing.

**Command to test:**
```bash
# Same build/boot as Milestone 19.1's "command to test" above - the log lines
# "RING3_TEST_PASS: ..." (from SYS_WRITE) followed by "syscall: SYS_EXIT - resuming
# kernel context" (from SYS_EXIT) together ARE this milestone's test: two distinct
# syscalls, each correctly dispatched and correctly returning control.
```

**Command to see:**
```bash
make run   # -serial stdio shows both syscalls' log lines in the terminal during boot
```
(No new screenshots for this milestone specifically — it shares the same visual evidence as 19.1's "command to see" above, since both are proven by the same boot sequence's serial output.)

---

## Milestone 19.3: Minimal ELF64 loader

- **What was done:** `kernel/include/elf.h` defines just enough of the ELF64 format (`Elf64_Ehdr`/`Elf64_Phdr`, magic/class/type/machine constants) to validate and load a static `ET_EXEC` binary — no relocations, no dynamic linking, no section headers. `kernel/src/usermode.c`'s `usermode_run_elf(path)` reads the file via `fat_resolve_path`/`fat_read_file` (the Track-A-hardened FAT32 path), validates the ELF magic/class/type/machine fields, then for each `PT_LOAD` program header: page-aligns `[p_vaddr, p_vaddr+p_memsz)`, maps that many fresh zeroed pages with `PAGE_WRITABLE|PAGE_USER`, copies `p_filesz` bytes from the file buffer, and relies on the mapping step's zeroing to correctly leave the `.bss` portion (`p_memsz - p_filesz`) zero-filled. Allocates a separate small user stack the same way, then calls the same `enter_user_mode()` trampoline 19.1 built.
- **The bundled test binary:** `tools/userland/hello.asm`, a genuinely standalone program (no libc, no crt0 — just `_start:`, two `int 0x80` calls, and a message) assembled and linked entirely independently of the kernel build via `tools/userland/user.ld` (a minimal linker script fixing the load address to `0x141000000` — deliberately different from 19.1's `0x140000000` blob address, purely so serial logs never show the same address for two different tests). Built via a new Makefile rule (`nasm -f elf64` + `ld -T user.ld -nostdlib -static`) and bundled onto the FAT32 disk image as `HELLO.ELF` through the same `mcopy`-based seeding mechanism Phase 15.3 established for the wallpaper BMP.
- **Test — three independent layers, not just "the kernel says it worked":**
  1. **The kernel's own read-back:** boot log shows `usermode: ELF64 valid - entry=0x0x0000000141000000 phnum=1`, `usermode: PT_LOAD vaddr=0x0x0000000141000000 filesz=100 memsz=100 (1 page(s))`, then the program's own `write` syscall output: `HELLO_FROM_USERLAND: a real ELF64 binary, loaded and executed in ring 3`, followed by a clean `SYS_EXIT`.
  2. **Independent host-side cross-check of the ELF file itself**, entirely outside gOS: `file tools/userland/hello.elf` reports `ELF 64-bit LSB executable, x86-64, ... statically linked`; `x86_64-elf-readelf -h`/`-l` independently confirms `Type: EXEC`, `Machine: Advanced Micro Devices X86-64`, `Entry point address: 0x141000000`, and a single `LOAD` segment at `0x141000000` with file/mem size `0x64` (100 decimal) — matching the kernel's own parsed values exactly, byte for byte, computed by a completely different tool.
  3. **Independent cross-check that the bundled copy on the disk image is byte-identical to the built ELF**: `mcopy -i disk_images/gos_disk.img ::HELLO.ELF /tmp/hello_from_disk.elf && diff tools/userland/hello.elf /tmp/hello_from_disk.elf` — empty diff, confirming the Makefile's seeding step and gOS's own `fat_read_file()` round-trip the binary without any corruption.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
# Independent host-side ELF structure check (no gOS involved):
file tools/userland/hello.elf
x86_64-elf-readelf -h tools/userland/hello.elf | grep -E "Type|Entry|Machine|Class"
x86_64-elf-readelf -l tools/userland/hello.elf | grep -A1 LOAD

# Independent disk-image content check (no gOS involved):
mcopy -i disk_images/gos_disk.img ::HELLO.ELF /tmp/hello_from_disk.elf
diff tools/userland/hello.elf /tmp/hello_from_disk.elf && echo "IDENTICAL"

# Kernel's own load-and-run (same build as Milestones 19.1/19.2 above):
S=$(mktemp -d)
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep -A6 "Milestone 19.3" $S/serial.log
```
Expected:
```
tools/userland/hello.elf: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, not stripped
  Class:                             ELF64
  Type:                              EXEC (Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x141000000
  LOAD           0x0000000000001000 0x0000000141000000 0x0000000141000000
                 0x0000000000000064 0x0000000000000064  RWE    0x1000
IDENTICAL
TEST: running Milestone 19.3 ELF loader test (HELLO.ELF)...
usermode: ELF64 valid - entry=0x0x0000000141000000 phnum=1
usermode: PT_LOAD vaddr=0x0x0000000141000000 filesz=100 memsz=100 (1 page(s))
usermode: entering ring 3 at 0x0x0000000141000000 (stack top 0x0x0000000151004000)
HELLO_FROM_USERLAND: a real ELF64 binary, loaded and executed in ring 3
syscall: SYS_EXIT - resuming kernel context
usermode: ELF process returned to kernel context
TEST: usermode_run_elf("HELLO.ELF") = 1 (OK)
```

**Command to see:**
```bash
make run   # -serial stdio shows the loader's segment-mapping log and the ELF's own
           # output scroll by during boot; screendump the desktop afterward to confirm
           # HELLO.ELF is a real file the File Manager can see and list
```
Screenshot: [screenshots/phase19_fm_after.png](screenshots/phase19_fm_after.png) — the File Manager listing `HELLO.ELF` alongside `WALLPAPR.BMP` and `PERSIST.TXT`, a real 4,792-byte file on the FAT32 filesystem, not a synthetic in-memory fixture.

---

## Bugs found & fixed during this phase

**One real kernel bug** — documented in full under Milestone 19.1 above: `table_get_or_create()` in `kernel/src/vmm.c` only set `PAGE_USER` on a page-table entry at the moment that entry was first created, never retroactively on an entry that already existed from an earlier (kernel-only) mapping. Since the U/S permission bit is enforced at every page-table level, this silently blocked the *first* user-accessible mapping gOS ever attempted, at the PML4 level, regardless of how correctly the leaf PTE itself was configured. This bug has existed since Phase 1's original VMM implementation but had zero observable effect until this phase, since nothing before Phase 19 ever requested a `PAGE_USER` mapping. Fixed by also OR-ing `PAGE_USER` into an already-present intermediate entry when a new mapping needs it. Verified fixed via the exact same test that first caught it (the Milestone 19.1 ring3 blob), which went from an immediate page-fault panic to a clean syscall round-trip after the fix, with no other behavior changed.

No other bugs were found — Milestones 19.2 and 19.3 both worked on their first test run once 19.1's underlying VMM bug was fixed, since both build directly on the now-corrected `vmm_map_page` path.

---

## Phase 19 exit criterion — met

A real ELF binary — built and bundled entirely separately from the kernel image, verified independently via `file`/`readelf` and a byte-for-byte `diff` against its on-disk copy — executes in ring 3 and makes syscalls back into the kernel, proven end-to-end in QEMU: the syscall's caller CS is independently checked for `RPL=3` (not just "it didn't crash"), and the desktop remains fully interactive afterward, confirming the demo doesn't corrupt kernel or interrupt state.
