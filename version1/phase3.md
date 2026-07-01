# Phase 3 — Memory Management (Physical + Virtual) — Completion Report

**Date completed:** 2026-06-30
**Status:** ✅ Complete — all three milestones (3.1, 3.2, 3.3) done, all tasks verified.

---

## Summary

Phase 3 is the plan's own flagged highest-risk phase, and it earned that reputation during this session: two real bugs were found and fixed while testing, not just "everything worked first try" as Phase 2 was. gOS now has a bitmap physical memory allocator, its own page tables (fully replacing Limine's), and a working kernel heap with corruption-detecting guards — the foundation every later phase (drivers, the file manager, the windowing system) will allocate memory through. Unlike Phases 0-2, this report includes real bug writeups because real bugs happened, and the whole point of the "test before marking done" discipline is to catch exactly this kind of thing before it becomes a Phase 6 mystery crash.

---

## Milestone 3.1: Physical memory allocator (PMM) works

### Task: Parse the Limine memory map to find usable RAM regions
- **What was done:** Wrote `kernel/include/pmm.h` / `kernel/src/pmm.c`. `pmm_init()` takes the already-parsed `limine_memmap_response*` (reusing the same structure Phase 1 already logs) plus the HHDM offset (a new Limine request added specifically for this milestone — see below) and scans all entries to determine the address range the bitmap needs to cover.
- **Outcome:** Initially worked, but see the bug writeup below — the first version scanned *all* memmap entry types, not just usable ones.
- **Issues:** **Bug #1 (found via the smoke test, not by inspection).** QEMU/OVMF reports a `RESERVED` memmap entry describing a 64-bit PCI MMIO window with a base address near 1 TiB (`0xfd00000000`). The first version of `pmm_init()` computed `highest_addr` across *all* memmap entries regardless of type, so the bitmap was sized to cover that entire 1 TiB span — a 32 MiB bitmap (`total_pages: 268435456`) to track a system with only 207 MiB of actual usable RAM. Fixed by restricting the `highest_addr` scan to `LIMINE_MEMMAP_USABLE` entries only, since those are the only addresses the allocator will ever hand out. This dropped the bitmap to a sane 7160 bytes and `total_pages` to 57274 (~224 MiB span, matching the real hardware). This wasn't just an efficiency bug — a 32 MiB bitmap eating into a 207 MiB usable pool would have left dramatically less real memory for everything else built on top of the PMM in later phases.

### Task: Implement a bitmap-based physical page allocator
- **What was done:** Classic one-bit-per-4KiB-page bitmap, addressed via the HHDM offset (physical address + `hhdm_offset` = a virtual address Limine already maps for us, readable/writable before gOS has any page tables of its own — this is exactly why Milestone 3.1 must come before 3.2, not after: the PMM needs *some* way to write to physical memory, and it borrows Limine's HHDM mapping to do it). Added a `search_hint` cursor that advances on allocation and rewinds on free, so repeated alloc/free cycles don't rescan from bit 0 every time, while still guaranteeing freed pages are found again (required for the smoke test below to mean anything).
- **Outcome:** `pmm_alloc_page()` / `pmm_free_page()` implemented and functional.
- **Issues:** See Bug #2 immediately below — this task's implementation is where the actual defect lived, even though it was caught by the smoke test task, not this one.

### Task: Reserve pages already used by kernel image, Limine reclaimable regions, and framebuffer
- **What was done:** Bitmap defaults every bit to "used" (`0xFF`) before any region is examined; only `LIMINE_MEMMAP_USABLE` entries get their bits cleared. This means kernel image (`KERNEL_AND_MODULES`), framebuffer, ACPI (`ACPI_NVS`/`ACPI_RECLAIMABLE`), and `BOOTLOADER_RECLAIMABLE` regions are all conservatively left marked used/reserved — matching the plan's own minimum-viable guidance (reclaiming bootloader-only regions is an optimization for a later phase, not required now).
- **Outcome:** Correct by construction — this is a straightforward "default to used, selectively free" design, not a place where subtle physical-address-range bugs could easily hide.
- **Issues:** None beyond Bug #1 above, which affected sizing, not the reservation logic itself.

### Task: Smoke test — allocate 100 pages, free, allocate again, confirm reuse
- **What was done:** Wrote `pmm_self_test()`, called automatically during boot (not a separate manual step): allocates 100 pages checking for duplicates, verifies the free-page counter dropped by exactly 100, frees all of them and verifies the counter returned to baseline, then re-allocates 100 more and checks how many addresses match a previously-freed page.
- **Outcome (after fixing the bug found by this exact test):** `PMM self-test: PASS (100/100 re-allocations reused a freed page)`.
- **Issues:** **Bug #2 (found immediately on first boot with the self-test wired in).** The very first call to `pmm_alloc_page()` returned physical address `0x0` — a completely legitimate address (physical page 0 is real, usable memory) — but the self-test (and the function's own contract) treats a return value of `0` as the "out of memory" sentinel. Since physical page 0 genuinely was the first free page the bitmap scan found, the first allocation was indistinguishable from failure, and the self-test immediately reported `FAIL (allocation returned 0 / OOM)` despite the allocator working correctly in every other respect. **Fix:** permanently reserve physical page 0 in `pmm_init()` (a single `bitmap_set(0)` call, standard practice in real allocators for exactly this reason — using 0 as a null/failure sentinel requires 0 to never be a valid successful return value). After the fix, the self-test passed cleanly. This is a good example of why "always test before marking done" matters: the bug was invisible from reading the code (the logic is correct for every page except page 0) and only surfaced by actually running the allocator.

---

## Milestone 3.2: Virtual memory / paging is under kernel control

### Task: Write page table structures and `vmm_map_page`
- **What was done:** Wrote `kernel/include/vmm.h` / `kernel/src/vmm.c`. Implements the standard 4-level x86_64 walk (PML4 → PDPT → PD → PT), with `table_get_or_create()` allocating a fresh physical page from the Phase 3.1 PMM (zeroed via HHDM) whenever an intermediate table doesn't exist yet. `vmm_map_page(virt, phys, flags)` maps a single 4KiB page. A second internal helper, `vmm_map_2mb()`, sets the PS (huge page) bit at the PD level to map 2MiB in one entry — used only for the bulk identity/HHDM mapping below, where per-4KiB permission granularity isn't needed and would have cost thousands of extra page-table-page allocations.
- **Outcome:** Both mapping paths work; verified indirectly by everything downstream (kernel continuing to run, heap allocations succeeding) since a broken page table walk would manifest as an immediate page fault or silent memory corruption.
- **Issues:** None once the huge-page approach was chosen — an earlier design consideration (map the entire 4GiB with 4KiB pages) was rejected before writing any code, since it would have consumed roughly 16MiB of the 207MiB usable pool just on page-table pages for no benefit at this phase.

### Task: Identity-map or higher-half-map the kernel using Limine's provided base addresses
- **What was done:** Two things happen in `vmm_init()`: (1) identity-map **and** HHDM-map the first 4GiB of physical address space using 2MiB pages — this deliberately covers all installed RAM (207MiB), the framebuffer (physical base `0x80000000`), and low MMIO in one pass, and critically *replicates* the HHDM mapping the PMM already depends on, so `pmm_alloc_page`'s physical-to-virtual translation keeps working after gOS's own page tables take over; (2) map the kernel's actual higher-half virtual range (`0xffffffff80000000` and up) to its real physical load location at 4KiB granularity, using two new linker symbols (`__kernel_virt_start`, `__kernel_virt_end`, added to `kernel/linker.ld`) to know exactly how much of the kernel image needs mapping, and a new Limine request (`limine_kernel_address_request`) to learn where Limine actually put it in physical memory.
- **Outcome:** Verified live: `VMM: identity + HHDM mapped first 4GiB (2MiB pages)` then `VMM: kernel higher-half mapped (44 KiB)` — 44 KiB matching the current small kernel image size.
- **Issues:** None. **Scope decision, not a bug:** the 4GiB identity/HHDM mapping limit and the uniform read-write-execute kernel mapping (no separate W^X permissions for `.text`/`.rodata`/`.data`) are both deliberate v1 minimum-viable choices, documented as comments directly in `vmm.c` — fine-grained page permissions and >4GiB physical memory support are real gaps but are hardening/scalability improvements appropriate for a later phase, not required for the kernel to function correctly now on this target hardware profile (207MiB RAM, well under 4GiB).

### Task: Load a new CR3 and confirm the kernel keeps running
- **What was done:** Added `kernel/src/vmm_load_cr3.asm` (a two-instruction `mov cr3, rdi` / `ret`), called at the end of `vmm_init()` after all mappings are in place.
- **Outcome:** Verified live: `VMM: new CR3 loaded, kernel still running` — and notably, the boot log continued past this point with the memory map/framebuffer info already printed earlier, timer ticks kept incrementing for 10+ seconds *after* the switch, and no page fault occurred. The fact that interrupts kept firing correctly post-switch is a stronger confirmation than the plan's minimum bar ("serial log after switch = success") — it proves the IDT, GDT/TSS stack, and now the new page tables are all simultaneously consistent, not just that the CPU didn't immediately triple-fault.
- **Issues:** None — this task passed cleanly on the first attempt once Bug #1 and Bug #2 (both in the PMM, which this milestone depends on) were already fixed.

### Task: Hook the page fault handler to log CR2 and access type
- **What was done:** Extended the page-fault branch of `isr_handler()` (in `kernel/src/idt.c`, originally written in Phase 2) to decode the hardware error code's bit fields per the Intel SDM's documented layout: bit 0 (present vs. protection-violation), bit 1 (read vs. write), bit 2 (supervisor vs. user mode), and bit 4 (instruction fetch, logged only when set).
- **Outcome:** Verified live by re-running Phase 2's existing `GOS_TEST_PAGE_FAULT` deliberate-fault trigger with the new decoder in place: `Access type: not-present, write, supervisor-mode` — correctly matching the test code's actual behavior (a supervisor-mode write to an unmapped address).
- **Issues:** None.

---

## Milestone 3.3: Kernel heap allocator exists

### Task: Implement a simple heap backed by `vmm_map_page` calls
- **What was done:** Wrote `kernel/include/heap.h` / `kernel/src/heap.c`. Chose a freelist allocator (first-fit search, splits blocks that are larger than needed, coalesces forward with the next block on free) over a plain bump allocator specifically because the milestone's own stress-test requirement ("allocate/free a mix... verify no corruption") implies real reuse, which a bump allocator can't provide. The heap starts at a fixed virtual address (`0xffffffff90000000`, chosen to avoid colliding with the 4GiB identity/HHDM range or the kernel's own higher-half image) and grows on demand: `heap_grow()` pulls fresh physical pages from `pmm_alloc_page()` and maps them via `vmm_map_page()`, so the heap literally cannot exist without both of the previous two milestones working correctly first.
- **Outcome:** `Heap initialized: 16344 bytes available (64 MiB virtual ceiling)` on boot (4 pages mapped initially, minus header/footer overhead).
- **Issues:** None in this task specifically — see the stress test below, which is where any real bugs would have shown up, and didn't.

### Task: Implement `kmalloc` / `kfree`
- **What was done:** Every block carries a header (magic number, size, free flag, next-pointer) and a footer (a second, different magic number placed immediately after the payload). `kmalloc` searches the freelist, splits oversized blocks, and grows the heap automatically if no block is large enough. `kfree` validates both the header and footer magic *before* doing anything else — a mismatch is logged as a specific, actionable message (`"buffer underrun likely"` vs. `"buffer overrun likely"` depending on which guard failed) rather than silently corrupting the freelist or crashing.
- **Outcome:** Functions correctly under the stress test below, including the heap-growth path (triggered mid-test by the mix of small and large allocations).
- **Issues:** None. **Scope decision, not a bug:** coalescing only happens forward (with the next block), not backward — a full doubly-linked implementation would coalesce both directions and reduce fragmentation further, but forward-only coalescing is sufficient for correctness and was a deliberate, documented simplification to keep this milestone's scope contained (consistent with the plan's own risk note against over-engineering the memory layer).

### Task: Stress test — mixed small/large alloc/free with corruption detection
- **What was done:** Wrote `heap_self_test()`: 300 randomized alloc/free cycles using a simple deterministic PRNG (so results are reproducible run-to-run), alternating small (8-64 byte) and large (512-4096 byte) allocations across up to 32 simultaneously-live blocks, writing a distinct byte pattern into every allocated block and verifying that pattern is still intact immediately before freeing it (this catches corruption from a *neighboring* block's overrun, not just a block's own guard). After the 300-cycle loop, the test does something the milestone's literal wording doesn't explicitly demand but which meaningfully strengthens the verification: it deliberately overruns a 16-byte allocation's footer by 8 bytes, then calls `kfree()` on it and confirms the corruption-detection code path actually fires (rather than just existing, unused, in the source).
- **Outcome:** Verified live:
  ```
  Heap self-test: 300 alloc/free cycles, mixed small/large blocks...
  Heap self-test: deliberately overrunning a buffer to verify guard detection...
  HEAP CORRUPTION: footer magic mismatch at kfree() - buffer overrun likely
  Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun)
  ```
  The `HEAP CORRUPTION` line appearing is the test *passing*, not failing — it's the deliberate-overrun sub-test's expected output, confirmed by the final `PASS` line only appearing after that detection was verified to have actually happened (not just logged unconditionally).
- **Issues:** None — this task passed on the first attempt, likely because the header/footer guard design was written test-first in the sense that the deliberate-corruption check was planned as part of the implementation, not bolted on afterward.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| Bitmap PMM sized from "the Limine memory map" (unspecified which entries) | Sized from `USABLE` entries only | **Bug fix.** Including all entry types (specifically a high-address PCI MMIO `RESERVED` region near 1TiB) bloated the bitmap to 32MiB for no benefit; restricting to USABLE entries is both correct and matches what the plan's own smoke test implicitly needs (reasonable memory usage) |
| (implicit) `pmm_alloc_page` returns a physical address, `0` presumably meaning failure | Physical page 0 explicitly reserved and never handed out | **Bug fix.** Page 0 is a real, legitimately allocatable address; without reserving it, the first-ever allocation could be indistinguishable from an OOM failure, as the self-test caught immediately |
| Identity-map "using Limine's provided base addresses" (no explicit range given) | Capped to the first 4GiB of physical address space | Deliberate v1 minimum-viable scope decision, appropriate for a 207MiB-RAM target system; documented in code comments, not left as an unstated assumption |
| (implicit) heap corruption stress test | Added a deliberate-overrun sub-test proving the guard actually fires, beyond just running clean cycles | Strengthens the verification the plan's own task already asks for ("verify no corruption via a canary/guard pattern") — a guard that's never actually tested against real corruption is a weaker proof than one confirmed to catch it |

None of these required revisiting later-phase plans. The two bug fixes were caught and resolved *during* this session's testing, before Milestone status was ever marked done — exactly the workflow the user asked for ("always test everything is working before updating status").

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 3.1 | ~5–8 hours | ~1.5 hours (including finding and fixing both bugs) |
| Milestone 3.2 | ~6–9 hours | ~1.5 hours |
| Milestone 3.3 | ~5–7 hours | ~1 hour |
| **Phase 3 total** | **16–24 hours** | **~4 hours** |

**Why actual was faster than estimated despite hitting real bugs:** The 16-24 hour estimate assumes the kind of debugging cycle that makes Phase 3 the plan's own flagged highest-risk phase — paging bugs in particular are notorious for causing silent memory corruption that doesn't manifest as a fault until much later, sometimes not until a completely unrelated later phase, making root-causing painful. Both bugs found in this session were caught immediately by their own milestone's smoke test, with clear, specific failure messages (`"allocation returned 0 / OOM"`) that pointed directly at the actual defect — neither required the multi-hour "why did this random later thing break" debugging session the estimate implicitly budgets for. This is a direct result of writing the self-tests as an integral, automatic part of boot (not a separate manual step run occasionally) and running them immediately after writing each piece of new code, rather than writing all of Phase 3 first and testing at the end.

**Revised estimate guidance for future phases:** Do not read this as "Phase 3 is actually easy" — two real, non-trivial bugs did occur, exactly where the plan warned they would (a PMM edge case and a memory-map parsing assumption). The speed came from immediate, specific-feedback testing catching them within minutes of introduction, not from the underlying work being simple. Phase 4 (drivers) and Phase 6 (windowing) both involve state machines (PS/2 protocols, mouse/window interaction) that are harder to self-test automatically the way a memory allocator's invariants can be checked programmatically — budget the plan's existing estimates for those phases at face value.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

### Milestone 3.1 — Physical memory allocator (PMM) works

**1. Build and boot, checking the PMM init + self-test output:**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PMM|PANIC|EXCEPTION"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
PMM: highest physical address: 0x000000000dfba000
PMM: bitmap size: 7160 bytes, placed at physical 0x0000000000000000
PMM: total pages: 57274, used: 4277, free: 52997
PMM self-test: allocating 100 pages...
PMM self-test: freeing all 100 pages...
PMM self-test: re-allocating 100 pages to confirm reuse...
PMM self-test: PASS (100/100 re-allocations reused a freed page)
```
**What to check:**
- `highest physical address` should be well under 1GiB (roughly matching total installed RAM) — if you ever see a value near `0x10000000000` (1TiB), the bitmap-sizing bug has regressed (Bug #1 above).
- `bitmap size` should be a few KiB, not tens of MiB.
- The final line must say `PASS`. If it instead says `FAIL (allocation returned 0 / OOM)`, the page-0 reservation fix (Bug #2 above) has regressed — check that `pmm_init()` still contains the explicit `bitmap_set(0)` call.
- No `PANIC` or `EXCEPTION` lines should appear anywhere in this filtered output.

### Milestone 3.2 — Virtual memory / paging is under kernel control

**1. Boot and confirm the CR3 switch survives, with interrupts still firing afterward:**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "VMM|Timer tick|PANIC|EXCEPTION"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
VMM: identity + HHDM mapped first 4GiB (2MiB pages)
VMM: kernel higher-half mapped (44 KiB)
VMM: new CR3 loaded, kernel still running
Timer tick: 18
Timer tick: 36
... (continuing to increment) ...
```
**What to check:** `Timer tick:` lines must continue appearing *after* `VMM: new CR3 loaded`. If ticks stop right after the CR3 switch (or the kernel hangs with no further output at all), the new page tables are missing a mapping the interrupt handler or its stack depends on — most likely the kernel higher-half mapping or the TSS stack (which lives in `.bss`, inside the kernel's mapped range).

**2. Confirm the page fault handler's access-type decoding (reuses Phase 2's deliberate-fault test):**
```bash
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_PAGE_FAULT"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -A1 "CR2"
rm -f /tmp/OVMF_VARS_test.fd
make clean && make iso   # IMPORTANT: restore the normal, non-test build afterward
```
Expected output:
```
CR2 (faulting address): 0x00000deadbeef000
Access type: not-present, write, supervisor-mode
```
**What to check:** The access type must read exactly `not-present, write, supervisor-mode` — this test writes to an unmapped address from kernel (ring 0) code, so any other combination (e.g., `protection-violation` instead of `not-present`, or `read` instead of `write`) indicates the error-code bit decoding in `idt.c` is wrong.

### Milestone 3.3 — Kernel heap allocator exists

**1. Boot and check the heap init + stress test output:**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -A3 "Heap"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
Heap initialized: 16344 bytes available (64 MiB virtual ceiling)
Heap self-test: 300 alloc/free cycles, mixed small/large blocks...
Heap self-test: deliberately overrunning a buffer to verify guard detection...
HEAP CORRUPTION: footer magic mismatch at kfree() - buffer overrun likely
Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun)
```
**What to check:**
- The `HEAP CORRUPTION` line **must** appear, and it must be immediately followed by `PASS` — this is the deliberate-overrun sub-test working correctly, not a real bug. If `HEAP CORRUPTION` does *not* appear, the guard detection itself is broken (a real regression), and you'll instead see `FAIL (deliberate overrun was NOT detected by guard)`.
- If any other `FAIL` message appears (`payload content corrupted before free`, `kmalloc returned NULL unexpectedly`, `unexpected corruption detected during normal use`), that indicates real corruption or an allocator bug introduced by a code change — treat it as a genuine regression, not expected output.

### Quick regression check for all of Phase 3 together
```bash
make clean && make iso && echo "BUILD OK"
```
Then run the Milestone 3.1 and 3.3 QEMU commands above (both safe against a normal, non-`#ifdef` build) together in one boot to see the full PMM → VMM → heap chain succeed in sequence:
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PMM|VMM|Heap|PANIC|EXCEPTION"
rm -f /tmp/OVMF_VARS_test.fd
```
All three subsystems' PASS/success lines should appear in order (PMM self-test, then VMM CR3 confirmation, then heap self-test), with no `PANIC` or `EXCEPTION` lines anywhere.

---

## Not Yet Verified (intentionally deferred to Phase 4+)

- [ ] Physical memory above 4GiB — the identity/HHDM mapping is capped at 4GiB (v1 scope decision); a system with more RAM or high MMIO BARs would need additional mapping logic not yet written
- [ ] Fine-grained page permissions (W^X) — kernel `.text`/`.rodata`/`.data` are currently all mapped read-write-execute; no security hardening applied yet
- [ ] Backward heap coalescing — only forward coalescing is implemented; long-running alloc/free patterns could fragment more than a fully-coalescing allocator would (acceptable for now, worth revisiting if a later phase's allocation patterns show heavy fragmentation)
- [ ] Reclaiming `BOOTLOADER_RECLAIMABLE` memmap regions back into the usable pool — currently permanently reserved, leaving some usable RAM (roughly the space Limine's own boot-time structures occupied) unavailable to the PMM

---

## Next Step

Proceed to **Phase 4 — Basic Drivers (Serial, Timer, Keyboard)** (Milestone 4.1: PIT timer driver with tick counting), per [PROJECT_PLAN.md](PROJECT_PLAN.md). Serial (1.2) and a basic timer tick (2.3) already exist from earlier phases; Phase 4 will extend the timer to a specific programmed frequency and add the PS/2 keyboard driver that Phase 7's text input will depend on.
