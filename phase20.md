# Phase 20 — Preemptive Multitasking & Process Management — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed, with full per-process page-table isolation (the larger of the two scope options discussed with the user before starting), including one real assembler bug found and fixed.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

The default build boots exactly as it did after Phase 19 (~1 second to the desktop) — none of this phase's process/scheduler demo code runs by default. `SPIN1-5.ELF`, `CHILD.ELF`, and `PARENT.ELF` are now on the disk image and visible in the File Manager.

To see this phase's actual work in action:

```bash
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_MULTITASKING"
make disk build/OVMF_VARS.fd
make run
```

---

## Summary

Phase 20 turns Phase 19's single ring-3 program into genuine multitasking: a process table, a real timer-driven preemptive scheduler, and `spawn`/`exit`/`waitpid` syscalls. Before starting, the user confirmed the bigger of two scoping options: **real per-process page-table isolation** (a separate `CR3`/PML4 per process, not Phase 19's single shared address space), rather than the smaller "shared page tables, real preemption only" cut.

- **20.1 (process table & context switching)** — a `struct process` PCB (PID, saved `interrupt_frame`, private `pml4_phys`, private kernel stack, state) and a genuine timer-driven context switch. The clever part: since a saved `interrupt_frame` has the exact byte layout `isr_common_stub`'s epilogue already knows how to restore, both "resume a preempted process" (overwrite the live interrupt frame in place, let the normal epilogue run) and "bootstrap a brand-new process" (point `RSP` at a hand-built frame, jump straight into that same epilogue) share the identical restore path with zero duplicated logic.
- **20.2 (process lifecycle syscalls)** — `SYS_SPAWN` (load an ELF into a fresh address space, return its pid), `SYS_WAITPID` (poll-style: returns the child's exit code once it's a zombie, `-1` otherwise — see the deliberate non-blocking design note below), and `SYS_EXIT` extended to mark the process a zombie and immediately reschedule instead of Phase 19's one-shot "resume kernel" trampoline.
- **20.3 (fairness under load)** — 5 concurrent processes, verified no starvation and a fully responsive desktop afterward.

**A real bug was found and fixed** during 20.1's very first build attempt — a NASM assembler quirk, not a logic bug, but a genuine "found it during testing, not before" case worth documenting in full below.

Files touched (new): `kernel/include/process.h`, `kernel/src/process.c`, `kernel/src/scheduler_entry.asm`, plus `tools/userland/spinner.asm`, `child.asm`, `parent.asm`, `proc.ld`. Files touched (modified): `kernel/include/gdt.h`/`gdt.c` (TSS `rsp0` setter), `kernel/include/vmm.h`/`vmm.c` (per-process PML4 creation + mapping), `kernel/src/isr.asm` (exposed the shared epilogue label), `kernel/src/timer.c` (scheduler tick hook), `kernel/include/syscall.h`/`syscall.c` (spawn/waitpid, scheduler-aware exit), `kernel/src/start.c`, `Makefile`.

---

## Milestone 20.1: Process table & context switching

- **What was done:** `struct process` (`kernel/include/process.h`) holds a PID, state (`READY`/`RUNNING`/`ZOMBIE`/`UNUSED`), a full saved `struct interrupt_frame`, a private `pml4_phys`, a private ~8KiB kernel stack, and parent/exit-code fields. `vmm_create_process_pml4()` (`kernel/src/vmm.c`) allocates a fresh PML4 and shallow-copies the kernel's own top-level entries into it — every process's address space resolves kernel code/data/identity-map/HHDM identically (shared, trusted), while process-private mappings (an ELF's segments, its stack) deliberately live under **PML4 index 1** (`PROC_LOAD_BASE = 0x8000000000`), a slot `vmm_init()` never touches, so each process gets its own independent, genuinely private page-table chain there — confirmed by two processes (`SPIN1.ELF`/`SPIN2.ELF`) loading at the **exact same virtual address** with zero collision, because their PML4s are separate. A new `gdt_set_tss_rsp0()` swaps the TSS's `rsp0` on every context switch, so each process's ring3→ring0 transitions land on its own kernel stack. The scheduler itself (`scheduler_reschedule()` in `process.c`) is elegantly simple: since a saved `interrupt_frame` is byte-for-byte the same stack shape `isr_common_stub`'s existing pop-and-`iretq` epilogue expects, the timer tick just overwrites the live frame in place with the next process's saved registers and switches `CR3` — the interrupt's own normal return path does the rest. Bootstrapping the very first process (no live interrupt frame exists yet) uses a new `scheduler_enter()` trampoline (`kernel/src/scheduler_entry.asm`) that points `RSP` at a hand-built frame and jumps straight into that same shared epilogue (exposed as `isr_common_epilogue` in `isr.asm`) — one restore path, two entry points, no duplicated logic.
- **The bug (found on the very first build attempt):**
  - **Symptom:** `nasm -f elf64 tools/userland/spinner.asm` failed with `invalid 64-bit effective address` on the line `lea rdi, [rel ch]`, and a second, consequent error on the `ch: db MARKER` data label a few lines later — despite the exact same `lea rdi, [rel <label>]` pattern already working fine in three other files (`ring3_test.asm`, `hello.asm`, `child.asm`) written in this same phase.
  - **Diagnosis:** the label was named `ch` — which is also a real x86 register name (`CH`, the 8-bit high byte of `CX`). NASM's addressing-mode parser matched `ch` against the register operand first, tried to build a 64-bit effective address out of an 8-bit register, and rejected it — rather than falling back to treating it as a forward-referenced label, which is what every other identical-looking `[rel <label>]` line in the other files does successfully (none of them happened to pick a label name that collides with a register).
  - **What I tried that didn't work:** ruled out a shell-quoting problem with the `-DMARKER='X'` command-line define first (since that's the one thing this file has that the others don't) by re-running `nasm` directly on the file with no `-D` overrides at all — it failed identically, isolating the problem to the file's own source rather than the build invocation.
  - **The actual fix:** renamed the label from `ch` to `marker_byte` (and the corresponding `[rel ch]` reference) — no other change needed, confirmed by re-assembling the file in isolation before touching anything else.
- **A separate, real design bug (not an assembler quirk) found during the first live test:** the very first boot of the fixed build ran to completion with no crashes, but Milestone 20.1's two processes printed their entire marker sequences **back-to-back** (`111111...` then `222222...`), not interleaved — meaning no preemption was actually happening, even though nothing crashed. Diagnosis: the spin-delay loop between each write (300,000 `dec`/`jnz` cycles) plus the 100Hz timer's ~10-20ms slice granularity meant an entire 20-iteration process could complete before a second timer tick ever fired — confirmed by grepping the serial log for `Timer tick:` lines during the test window and finding **zero** (fewer than 100 ticks, i.e. under 1 real second, elapsed for all three milestones combined). Fixed by increasing the spin-delay to 5,000,000 cycles and reducing the scheduler's time slice from 2 ticks to 1 (`TIME_SLICE_TICKS` in `process.c`) — re-ran the identical test and got genuinely interleaved output (`12212121212...`), confirmed by an actual `Timer tick: 100` line now appearing mid-test.
- **Test:** two processes (`SPIN1.ELF` marker `'1'`, `SPIN2.ELF` marker `'2'`) spawned into their own private address spaces at the identical virtual address; serial log shows their output genuinely interleaved (not sequential), and both reach `SYS_EXIT` cleanly.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_MULTITASKING"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
sed -n '/Milestone 20.1/,/Milestone 20.1 complete/p' $S/serial.log
```
Expected (exact digits/tick counts will vary run to run, but the interleaving pattern — never 20 of one digit before the other starts — is the point):
```
TEST: Milestone 20.1 - spawning SPIN1.ELF and SPIN2.ELF...
process: spawned pid=0 (SPIN1.ELF) entry=0x0x0000008000000000
process: spawned pid=1 (SPIN2.ELF) entry=0x0x0000008000000000
TEST: spin1=0 spin2=1
scheduler: starting - first pid=0
12212121212Timer tick: 100
121221121212121212212121212syscall: SYS_EXIT pid=1 exit_code=0
11syscall: SYS_EXIT pid=0 exit_code=0
scheduler: all processes finished - back in kernel context
TEST: Milestone 20.1 complete
```

**Command to see:**
```bash
make run   # -serial stdio shows the interleaved digits scroll by live in the terminal
           # during boot; the desktop itself appears afterward, unaffected
```
Screenshot: [screenshots/phase20_fm.png](screenshots/phase20_fm.png) — the File Manager listing every bundled test binary (`SPIN1-5.ELF`, `CHILD.ELF`, `PARENT.ELF`) as real files on the FAT32 filesystem.

---

## Milestone 20.2: Process lifecycle syscalls

- **What was done:** `SYS_SPAWN` (`syscall.c`) reads a path string directly out of the caller's memory (same trust model as Phase 19's `SYS_WRITE` — no ownership validation, documented as out of scope) into a small kernel buffer, then calls `process_spawn()`. `SYS_WAITPID` is **deliberately non-blocking/poll-style**, not a true blocking syscall: it checks the target pid's state and returns its exit code (reaping the zombie slot) if it's already `PROC_ZOMBIE`, or `-1` if not — the calling user program is expected to loop on it. This is a real, documented scope simplification: a true blocking `wait` needs a wait-queue and the ability to mark a process `BLOCKED` (not just `READY`/`RUNNING`) and wake it on a child's exit, which is more machinery than this phase's budget covered; polling achieves the same observable result (the exact exit code round-trips correctly) with much less new code, at the cost of the waiting process burning CPU time in its poll loop instead of yielding cleanly.
- **Test:** `PARENT.ELF` spawns `CHILD.ELF` via `SYS_SPAWN`, polls `SYS_WAITPID` in a loop, and once it gets a real (non -1) value, prints `PARENT_GOT:<digit>`. `CHILD.ELF` calls `SYS_EXIT` with `rdi=7`. Serial log shows `PARENT_GOT:7` — the exact, specific status code round-tripped from child to parent, not just "wait returned something."

**Command to test:**
```bash
# Same build as Milestone 20.1's "command to test" above.
S=$(mktemp -d)  # (reuse the build from above, or rebuild per that command)
grep -A4 "Milestone 20.2" $S/serial.log
```
Expected:
```
TEST: Milestone 20.2 - spawning PARENT.ELF (spawns CHILD.ELF itself)...
process: spawned pid=2 (PARENT.ELF) entry=0x0x0000008000000000
TEST: parent_pid=2
scheduler: starting - first pid=2
process: spawned pid=3 (CHILD.ELF) entry=0x0x0000008000000000
```
...followed eventually by:
```
PARENT_GOT:7
syscall: SYS_EXIT pid=3 exit_code=7
syscall: SYS_EXIT pid=2 exit_code=0
TEST: Milestone 20.2 complete
```

**Command to see:**
```bash
make run   # -serial stdio shows "PARENT_GOT:7" scroll by during boot
```
(No new screenshot for this milestone specifically — verified via serial output, same as Phase 19's syscall milestones.)

---

## Milestone 20.3: Scheduler fairness under load

- **What was done:** No new code beyond 20.1/20.2 — this milestone is a stress test of the existing scheduler with 5 concurrent processes (`SPIN1-5.ELF`, markers `'1'`-`'5'`).
- **Test:** spawned all 5 simultaneously; serial log shows heavy interleaving across all five markers throughout the run (a representative excerpt: `1122334455123451234512344512345122345123455112345123344512345122345123455123451123344512...`), and every process reaches its own `SYS_EXIT` — none hang or silently disappear. Manually counted markers in a raw excerpt: roughly 17-18 occurrences each in a mid-stream slice (consistent with fair round-robin — the exact count depends on where the slice boundary falls relative to each process's remaining iterations, not a bias toward any one process). Confirmed the desktop's own main loop is fully alive and interactive **after** this demo completes, the same way Phase 19 proved it: a screendump plus a real simulated mouse click on the Files icon, which successfully opened the File Manager.

**Command to test:**
```bash
# Same build as above.
grep -A15 "Milestone 20.3" $S/serial.log | grep "syscall: SYS_EXIT"
```
Expected: exactly 5 `SYS_EXIT` lines, one per spawned process (pids will vary), each `exit_code=0`.

**Command to see:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean
make iso CFLAGS="... -DGOS_TEST_MULTITASKING"   # full flags as in Milestone 20.1's test
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
(sleep 5; echo "screendump $S/desktop.ppm"; sleep 0.5; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/fm.ppm"; sleep 0.5; echo "quit") | timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
open $S/desktop.ppm $S/fm.ppm   # desktop fully rendered and clickable moments after 8 processes ran
```
Screenshot: [screenshots/phase20_fm.png](screenshots/phase20_fm.png) — same screenshot referenced in Milestone 20.1, taken after all three milestones' processes (8 total across the whole boot) completed.

---

## Bugs found & fixed during this phase

1. **NASM label/register-name collision** (documented in full under Milestone 20.1): a data label named `ch` was silently ambiguous with the 8-bit register `CH`, causing `invalid 64-bit effective address` on an otherwise-correct `lea rdi, [rel ch]` line. Fixed by renaming the label to `marker_byte`. Worth remembering for any future asm in this project: avoid label names that shadow real x86 register names (`ah`/`al`/`ax`/`bh`/`bl`/`bx`/`ch`/`cl`/`cx`/`dh`/`dl`/`dx`, etc.).
2. **Insufficient spin delay masked real preemption** (documented in full under Milestone 20.1): the scheduler worked correctly from the start, but the test workload was too fast to actually exercise it — both processes completed within a single time slice, producing sequential (not interleaved) output that looked like a working-but-untested scheduler rather than a broken one. Caught by checking the serial log's own `Timer tick:` counter (zero ticks elapsed = no real preemption opportunity occurred), not by assuming the sequential output was fine. Fixed by increasing the workload's spin-delay and shortening the scheduler's time slice.

No bugs were found in Milestones 20.2 or 20.3 — both worked on their first test run once 20.1's underlying scheduler and per-process VMM code were correct.

---

## Phase 20 exit criterion — met

Multiple independent user-mode processes — each with its own private page tables, confirmed by two processes loading at the identical virtual address with no collision — run concurrently under genuine, timer-driven preemptive scheduling (verified via interleaved serial output, not just "it didn't crash"), without starving each other (5-process fairness test, all reaching `SYS_EXIT`) or the desktop's own responsiveness (screendump + a real simulated click succeeding immediately afterward).
