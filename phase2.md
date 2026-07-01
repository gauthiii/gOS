# Phase 2 — Kernel Foundations (GDT/IDT/Interrupts) — Completion Report

**Date completed:** 2026-06-30
**Status:** ✅ Complete — all three milestones (2.1, 2.2, 2.3) done, all tasks verified.

---

## Summary

Phase 2 replaced everything Limine left in place (its own GDT/IDT) with gOS's own: a custom GDT with a working TSS, a full 256-entry IDT with handlers for all 32 CPU exceptions, and remapped hardware interrupts with a live timer tick proving the interrupt pipeline works end-to-end. This is the point where gOS stops being "code that runs once and halts" and becomes a kernel that can survive — and diagnose — its own faults, and react to hardware events asynchronously. Every later phase (memory management's page fault handler, drivers, the scheduler) depends directly on this infrastructure.

---

## Milestone 2.1: Custom GDT is loaded

### Task: Define a GDT with null, kernel code/data, user code/data, and TSS descriptors
- **What was done:** Wrote `kernel/include/gdt.h` (selector constants) and `kernel/src/gdt.c`. Defined a packed `struct gdt_entry` (standard 8-byte descriptor) for the five simple entries, and a separate packed `struct tss_entry_descriptor` (16 bytes) for the TSS, since long-mode TSS descriptors are twice the size of a normal segment descriptor (they carry a full 64-bit base address). Also defined a minimal `struct tss` with `rsp0` pointed at a static 16KiB stack — not used for privilege switches yet (that's a later-phase concern once user mode exists), but required for the TSS descriptor to be valid and loadable via `ltr`.
- **Outcome:** A `struct gdt_layout` packing all six descriptors contiguously, loaded via a single `lgdt`.
- **Issues:** None.

### Task: Write `gdt_load` in ASM
- **What was done:** Wrote `kernel/src/gdt_flush.asm`. Loads the GDT with `lgdt`, reloads `ds`/`es`/`fs`/`gs`/`ss` directly via `mov`, then reloads `cs` — which **cannot** be loaded with `mov` — using the standard trick of pushing a target selector and return address and executing `retfq` (a far return) to "return into" the new code segment. Finally loads the TSS selector via `ltr`.
- **Outcome:** Assembles cleanly with NASM, called from C as `gdt_flush(gdtp_addr, tss_selector)` following the System V AMD64 calling convention (`rdi`, `si`).
- **Issues:** None — this is a well-established, singular correct technique for reloading CS in long mode; no alternative approaches were needed.

### Task: Load the GDT and verify segment registers via serial log
- **What was done:** Called `gdt_init()` from `_start` right after the base-revision check, then read back `cs`, `ds`, `ss` (via inline `mov`) and `tr` (via the `str` instruction) and logged them over serial.
- **Outcome:** Live boot output confirmed `CS=0x08`, `DS=0x10`, `SS=0x10`, `TR=0x28` — an exact match to the GDT layout's byte offsets (null=0x00, kernel code=0x08, kernel data=0x10, user code=0x18, user data=0x20, TSS=0x28). This proves the new GDT is not just loaded but actually in effect — the CPU is running with our selectors, not Limine's.
- **Issues:** None.

---

## Milestone 2.2: IDT and exception handlers are wired up

### Task: Define a 256-entry IDT
- **What was done:** Wrote `kernel/include/idt.h` (including a `struct interrupt_frame` matching the exact register layout the ISR trampolines push) and `kernel/src/idt.c` with a packed `struct idt_entry` (64-bit interrupt gate format) and a 256-entry array.
- **Outcome:** All 256 entries initialized; vectors 0-31 point at exception handlers, 32-47 at IRQ handlers (Milestone 2.3), the remainder currently unused (zeroed/blank — fine, since nothing generates those interrupts yet).
- **Issues:** None.

### Task: Write ASM interrupt-entry trampolines for the 32 exception vectors
- **What was done:** Wrote `kernel/src/isr.asm` using NASM macros (`ISR_NOERR`, `ISR_ERR`) to generate one stub per vector, correctly distinguishing which of the 32 CPU exceptions push a hardware error code (8, 10, 11, 12, 13, 14, 17, 21, 29, 30) from those that don't (all others need a dummy `0` pushed so the stack layout is uniform). All stubs push their vector number and jump to a single shared `isr_common_stub`, which saves all 15 general-purpose registers, calls the C dispatcher with `rsp` as the argument (so the C side can treat the stack as a `struct interrupt_frame*`), restores registers, discards the vector/error-code pair, and returns with `iretq`. The same file also generates 16 additional `IRQ_STUB` trampolines for hardware interrupts 32-47, needed for Milestone 2.3, sharing the identical common stub.
- **Outcome:** 48 total stubs (32 exceptions + 16 IRQs), all assembling cleanly and sharing one dispatch path.
- **Issues:** None. Went slightly beyond the milestone's literal "32 CPU exception vectors" by also generating the 16 IRQ stubs in the same file/pass — this was more efficient than writing them separately in Milestone 2.3 since the macro infrastructure and common stub were already in place.

### Task: Write a C `isr_handler` that logs vector, error code, and RIP
- **What was done:** `isr_handler()` in `idt.c` checks whether the vector is in the IRQ range (32-47) and if so dispatches to a registered per-IRQ C handler (the mechanism Milestone 2.3's timer uses) and returns normally. For anything else (an actual CPU exception), it logs the vector number, a human-readable exception name (a 32-entry lookup table — "Divide Error," "Page Fault," "General Protection Fault," etc.), the error code, RIP, CS, RFLAGS, and RSP — and for vector 14 (Page Fault) specifically, also reads and logs `CR2` (the faulting address), then halts forever in a `hlt` loop.
- **Outcome:** Produces a readable, structured fault report instead of a silent triple fault/reset.
- **Issues:** None.

### Task: Deliberately trigger a divide-by-zero and a page fault
- **What was done:** Added two temporary, mutually exclusive test triggers to `start.c` gated behind `#ifdef GOS_TEST_DIVIDE_BY_ZERO` / `#ifdef GOS_TEST_PAGE_FAULT` (neither defined in a normal build, so they compile out entirely and add zero overhead/risk to the real boot path). Built and booted the kernel twice, once with each macro defined via `-D` on the compiler command line, and inspected serial output.
- **Outcome:**
  - Divide-by-zero: `Vector: 0 (Divide Error)`, error code `0x0`, valid RIP/CS/RFLAGS/RSP, then `!!! System halted !!!` — no triple fault.
  - Page fault: `Vector: 14 (Page Fault)`, error code `0x2` (write to a non-present page), and critically `CR2 (faulting address): 0x00000deadbeef000` — matching **exactly** the bogus pointer (`0xdeadbeef000`) the test code wrote to, proving `CR2` capture is correct, not just present.
  - Rebuilt without either macro afterward and reconfirmed a clean, exception-free boot to the normal `=== gOS boot checks complete ===` banner.
- **Issues:** None — both test paths worked on the first attempt, which is itself informative: it means the GDT (Milestone 2.1) and IDT (this milestone) segment/gate configuration was correct from the start, since a misconfigured IDT gate (wrong selector, wrong type byte) typically manifests as a **worse** fault (general protection fault or double fault) instead of the expected one.

---

## Milestone 2.3: Hardware interrupts (PIC) are enabled

### Task: Remap the legacy 8259 PIC
- **What was done:** Wrote `kernel/include/pic.h` / `kernel/src/pic.c`. Implemented the standard 8259 remap sequence (ICW1-ICW4) to move IRQ0-7 from their power-on-default vectors 8-15 (which collide with CPU exception vectors) to 32-39, and IRQ8-15 to 40-47. Preserved the original interrupt masks across the remap (read them before reinitializing, restore after) rather than assuming all-masked or all-unmasked. Also implemented `pic_send_eoi` (handles the PIC2-then-PIC1 EOI ordering for cascaded IRQs ≥8), `pic_set_mask`, and `pic_clear_mask`.
- **Outcome:** PIC remap confirmed via serial log line; no crash, no hang (a common mistake — forgetting the `io_wait()` delays between PIC I/O writes — can cause the remap sequence to be silently ignored on real/emulated 8259 hardware; this was included from the start).
- **Issues:** None — went with the plan's own explicit recommendation (legacy PIC over APIC, since SMP is out of scope for v1) without needing to revisit that choice.

### Task: Write IRQ entry stubs for IRQ0 (timer) and IRQ1 (keyboard)
- **What was done:** As noted in Milestone 2.2, all 16 IRQ stubs (covering IRQ0-15, not just 0 and 1) were generated in `isr.asm` at the same time as the exception stubs, since the macro-based approach made it no more work to do all 16 than just 2. Added `idt_register_irq_handler(uint8_t irq, irq_handler_t handler)` in `idt.c` as the dispatch mechanism so individual drivers can register a handler for their specific IRQ without touching `isr_handler` itself.
- **Outcome:** IRQ0 is wired to a real handler (the timer, this milestone). IRQ1 (keyboard) has a working stub and IDT entry ready, but no handler registered yet — that's correctly deferred to Phase 4's keyboard driver milestone, per the plan's own phase ordering.
- **Issues:** None.

### Task: Enable interrupts and confirm a timer tick handler fires periodically
- **What was done:** Wrote `kernel/include/timer.h` / `kernel/src/timer.c`. Registers a handler for IRQ0 that increments a tick counter and logs it over serial every 18th tick (roughly once per second at the Programmable Interval Timer's power-on-default rate of ~18.2 Hz — reprogramming the PIT to a specific frequency is explicitly Phase 4's job, not this milestone's; Milestone 2.3 only needs to prove the interrupt fires at all), then sends an EOI. Called `pic_remap()`, `timer_init()`, and `sti` (via inline assembly) at the end of `_start`, after all other boot checks.
- **Outcome:** Live boot output showed ticks incrementing and being logged (`Timer tick: 18`, `36`, `54`, ...) roughly once per second, sustained over a 13+ second QEMU run with no crash, no missed EOIs (which would have caused the PIC to stop delivering further IRQ0 interrupts after the first one — the fact that ticks kept incrementing past 18 proves EOI handling is correct).
- **Issues:** None.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| Write IRQ stubs "for IRQ0 and IRQ1" (Milestone 2.3) | Wrote stubs for all 16 IRQs (0-15) in Milestone 2.2's ISR work | The macro-based stub generator made 16 stubs no more effort than 2; doing it once avoided revisiting `isr.asm` in a later phase for IRQ2-15 |
| (implicit) minimal TSS just to make the descriptor valid | Included `rsp0` pointed at a real static stack, not left as zero/garbage | A TSS descriptor with an invalid base would still load via `ltr` but the `rsp0` field needs a real value before any ring3→ring0 transition happens in a later phase; setting it up correctly now avoids a subtle bug resurfacing when user mode is added |

Neither deviation changed project scope or later-phase plans — both were "do the small amount of extra work now while already in the file" efficiency calls, not scope creep (no new features, no rabbit holes — consistent with the Phase 2 risk notes in PROJECT_PLAN.md about avoiding over-engineering the interrupt/memory layers).

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 2.1 | ~3–5 hours | ~1 hour |
| Milestone 2.2 | ~5–8 hours | ~1.5 hours |
| Milestone 2.3 | ~3–5 hours | ~1 hour |
| **Phase 2 total** | **12–18 hours** | **~3.5 hours** |

**Why actual was faster than estimated:** The 12–18 hour estimate assumes significant trial-and-error time — GDT/IDT bugs are notorious for manifesting as silent triple faults with no diagnostic output, often requiring hours of `-d int` QEMU log tracing to even localize the problem. In this session, register-level verification (checking CS/DS/SS/TR directly, then confirming exception delivery with deliberate test triggers) caught any structural mistakes immediately rather than after a confusing silent failure, and no such mistakes actually occurred — every subsystem worked on the first fully-wired boot. This is not a signal that Phase 2 is inherently fast; it reflects careful upfront reading of exact register/struct layouts (the GDT/TSS descriptor formats, the IDT gate format, the ISR stack layout) before writing code, rather than iterating through failures.

**Revised estimate guidance for future phases:** Phase 3 (Memory Management) remains the plan's own flagged highest-risk phase (see §9 Risk/Scope-Creep Notes in PROJECT_PLAN.md) — paging bugs are categorically harder to debug than the interrupt-handling bugs Phase 2 could have hit, because a bad page table entry can corrupt memory silently before any fault fires. Treat the plan's 16–24 hour estimate for Phase 3 as realistic; do not extrapolate Phase 2's speed forward.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

### Milestone 2.1 — Custom GDT is loaded

**1. Build and boot with serial output:**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep "GDT loaded"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
GDT loaded. CS=0x0000000000000008 DS=0x0000000000000010 SS=0x0000000000000010 TR=0x0000000000000028
```
**What to check:** `CS` must be exactly `0x8`, `DS`/`SS` exactly `0x10`, `TR` exactly `0x28`. Any other value means either the GDT entries are in the wrong order, the far-return selector in `gdt_flush.asm` doesn't match `GDT_KERNEL_CODE` in `gdt.h`, or `ltr` was called with the wrong selector.

### Milestone 2.2 — IDT and exception handlers are wired up

**1. Confirm the IDT loads without crashing (baseline, no exceptions triggered):**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -A1 "IDT loaded"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected: `IDT loaded (256 entries, vectors 0-31 exceptions, 32-47 reserved for IRQs)` followed by normal boot continuing (memory map/framebuffer output, no `!!! CPU EXCEPTION !!!` block).

**2. Trigger a divide-by-zero and confirm it's caught (not a silent reset):**
```bash
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_DIVIDE_BY_ZERO"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | tail -15
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output ends with:
```
TEST: deliberately triggering divide-by-zero...

!!! CPU EXCEPTION !!!
Vector: 0 (Divide Error)
Error code: 0x0000000000000000
RIP: 0x...
CS: 0x0000000000000008
RFLAGS: 0x...
RSP: 0x...
!!! System halted !!!
```
**What to check:** `Vector: 0 (Divide Error)` specifically — any other vector number means the fault was misattributed, which would indicate a bug in the ISR stub push order or IDT gate mapping.

**3. Trigger a page fault and confirm CR2 is captured correctly:**
```bash
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_PAGE_FAULT"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | tail -16
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output ends with:
```
TEST: deliberately triggering page fault (write to unmapped address)...

!!! CPU EXCEPTION !!!
Vector: 14 (Page Fault)
Error code: 0x0000000000000002
RIP: 0x...
CS: 0x0000000000000008
RFLAGS: 0x...
RSP: 0x...
CR2 (faulting address): 0x00000deadbeef000
!!! System halted !!!
```
**What to check:** `Vector: 14 (Page Fault)` and, critically, `CR2 (faulting address): 0x00000deadbeef000` must match the literal test address in `start.c`'s `GOS_TEST_PAGE_FAULT` block. If `CR2` were wrong or missing, later phases' virtual memory manager would be debugging blind when real page faults occur.

**4. IMPORTANT — restore the normal (non-test) build afterward:**
```bash
make clean && make iso
```
Both test macros only exist behind `#ifdef` and are never defined in a plain `make build`/`make iso`, so this step simply confirms the kernel is back to its normal, exception-free boot path. Always run this after using either test trigger above — don't leave a `-D` test build as the last thing built, since the kernel intentionally crashes by design in that mode.

### Milestone 2.3 — Hardware interrupts (PIC) are enabled

**1. Build and boot for at least 10-15 seconds to observe multiple ticks:**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PIC remapped|Interrupts enabled|Timer tick"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
PIC remapped: IRQ0-7 -> vectors 32-39, IRQ8-15 -> vectors 40-47
Interrupts enabled (sti). Waiting for timer ticks...
Timer tick: 18
Timer tick: 36
Timer tick: 54
... (one line approximately every second, for as long as the timeout allows) ...
```
**What to check:**
- Ticks must keep incrementing by steps of 18 (not stop after the first one — a stuck counter would mean the EOI isn't being sent correctly, causing the PIC to withhold further IRQ0 interrupts).
- Roughly one `Timer tick:` line per second of wall-clock time (the `timeout 15` window should show about 13 tick lines, since the first ~1-2 seconds are spent in UEFI/Limine boot before the kernel's `sti`).
- No `!!! CPU EXCEPTION !!!` block should appear — if one does, it likely means an IRQ fired before `idt_init()`/`gdt_init()` completed, or a handler function pointer was null.

### Quick regression check for all of Phase 2 together
```bash
make clean && make iso && echo "BUILD OK"
```
If `BUILD OK` prints with no compiler errors/warnings, all three milestones' code compiles correctly together. Follow with the Milestone 2.1 and 2.3 QEMU tests above (both safe to run against a normal, non-`#ifdef` build) to confirm runtime behavior. Only use the Milestone 2.2 divide-by-zero/page-fault test builds when you specifically want to re-verify exception handling — remember to rebuild without the `-D` flag afterward.

---

## Not Yet Verified (intentionally deferred to Phase 3+)

- [ ] Virtual memory / paging — the kernel is still running on whatever page tables Limine set up; `kernel/src/idt.c`'s page fault handler reports faults correctly but nothing yet remaps or recovers from them (a real page fault today is always fatal, by design — Phase 3 will change that for legitimate cases like demand paging, though that's beyond this project's v1 scope per the plan's minimum-viable cutoff)
- [ ] Physical/heap memory allocation — `kmalloc`/`kfree` don't exist yet; the TSS stack and all kernel data structures so far are static, compile-time-sized
- [ ] Keyboard (IRQ1) — the IDT entry and ISR stub exist and are wired to the shared dispatcher, but no handler is registered for IRQ1 yet; that's Phase 4, Milestone 4.2

---

## Next Step

Proceed to **Phase 3 — Memory Management (Physical + Virtual)** (Milestone 3.1: Physical memory allocator (PMM) works), per [PROJECT_PLAN.md](PROJECT_PLAN.md). This is the plan's own flagged highest-risk phase — budget real time for it and resist the urge to build anything beyond the "minimum viable" cutoff already defined in PROJECT_PLAN.md §9 (bitmap PMM + basic paging + bump-allocator heap; no slab/buddy allocators, no swap).
