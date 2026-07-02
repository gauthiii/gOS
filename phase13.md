# Phase 13 — High-Severity Audit Fixes — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all six High-severity findings from [version1/audit.md](version1/audit.md) fixed, each verified with a live QEMU reproduction of the original bug (where feasible) followed by a confirmed fix.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```
Builds the kernel, packages it with Limine into a bootable ISO, creates/reuses `disk_images/gos_disk.img`, and launches a real graphical QEMU window. Sanity-checks all six Phase 13 fixes together in the normal boot path (no `GOS_TEST_*` flags).

Other useful targets: `make build` (compile only), `make iso` (build ISO without launching), `make debug` (like `make run` but with a `-s -S` GDB stub), `make clean` (remove build artifacts, not the disk image).

---

## Summary

Phase 13 is Track A, Phase 2 of [project-plan-2.md](project-plan-2.md): the six High-severity findings from the post-v1.0 kernel audit. Two of the six (13.1 ATA and 13.4 `create_entry`) hit a genuine limit of what's testable inside QEMU's emulated hardware — real drive-reported I/O failures can't be cleanly fault-injected from the guest, so those were verified via a combination of regression testing (proving the new check-path executes correctly on every real operation) and code review, the same approach established for the equally hard-to-fault-inject ATA scenario in Phase 12. The other four milestones got full live before/after reproductions, and two of those (13.3 and 13.5) needed a redesigned test after the first attempt didn't actually exercise the bug — documented below with the same rigor as the fixes themselves.

---

## Milestone 13.1: ATA write-path ERR/DF checking

### Task: Check ERR/DF status bits after the cache-flush completes
- **What was done:** `ata_write_sector` (`kernel/src/ata.c`) only checked the `BSY` bit clearing after `ATA_CMD_CACHE_FLUSH` — a drive-reported write/flush failure (`ERR` or `DF` bits set) was silently treated as success.
- **The actual fix:** Added a status read immediately after `BSY` clears, checking `ATA_STATUS_ERR | ATA_STATUS_DF` and returning failure (with a serial log) if either is set.

### Test: regression-test the real write path, confirm the check executes on every operation
- **Why not a live fault injection:** QEMU's emulated IDE/PIO disk doesn't expose a clean way to force a real drive-reported write error from inside the guest (same limitation as any ATA-level fault scenario). Consistent with the plan's own acknowledgment of this difficulty, tested via regression + direct instrumentation instead.
- **What was done:** Added a temporary debug counter logging every post-flush status byte read. Booted normally (`GOS_TEST_ATA_STATUS_CHECK`) through the full boot sequence, including the 150-cycle stress test.
- **Result:** The status-check code path executed **1525 times** across the boot (every real disk write), every single read showed `status=0x50` (RDY+SRV, no ERR/DF), and every FAT create/write/delete/rename operation still succeeded exactly as before — no false negatives introduced. Cross-checked independently via `mtools`: `PERSIST.TXT` intact on disk after the run.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_ata_test.img
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_ATA_STATUS_CHECK"
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_ata_test.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -c "DEBUG: ATA post-flush"
```
Expected: a count in the thousands (every write during boot + stress test), and `grep -E "Stress test:|Desktop ready"` on the same run should show `PASS`/`Desktop ready` with no regressions. Cross-check: `mdir -i /tmp/gos_disk_ata_test.img | grep -i persist` should show `PERSIST.TXT` unchanged.

### Command to see
This milestone is non-graphical (disk I/O internals). Confirm via `make debug` and watch the serial output scroll alongside the graphical QEMU window, matching the project's existing convention for non-visual self-tests.

---

## Milestone 13.2: Window drag negative-coordinate clamping

### Task: Clamp window x/y during drag instead of letting them go negative
- **What was done:** The drag-update path in `window_system_update()` (`kernel/src/window.c`) set `windows[dragging_window].x/y` directly from mouse position minus drag offset, with no bounds check. A negative `int64_t` reaching `fb_draw_rect`'s `uint64_t` parameters wraps to a huge value, making the draw loop's start already past its end.
- **The actual fix:** Clamp `new_x`/`new_y` to `>= 0`, and cap the upper bound at `fb_width()/fb_height() - WINDOW_TITLEBAR_HEIGHT` so a window can't be dragged so far off-screen that its titlebar (the only drag handle) becomes unreachable.

### Test: reproduce the original disappearing-window bug, then confirm the fix
- **What was done:** Used QEMU's monitor `mouse_move`/`mouse_button` commands (the same real-hardware-path mechanism established in Phase 6) to click Window A's titlebar and drag it by `(-1000, -1000)` — far past the top-left corner.
- **Pre-fix reproduction:** Temporarily reverted the clamp. **Result: Window A's entire body vanished — only a disconnected fragment of its close button rendered at the top-left, floating with no window body beneath it.** A real, concrete visual corruption matching the audit's "wraps to a huge value... window silently fails to draw at all."
- **Post-fix verification:** Same drag sequence, fixed code. Window A renders correctly, clipped at `(0,0)`, fully visible and focused on top of the other windows.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso
(sleep 20; printf "mouse_move -340 -240\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.3; \
 printf "mouse_move -1000 -1000\n"; sleep 0.3; printf "screendump /tmp/gos_drag_after.ppm\n"; sleep 0.5; \
 printf "mouse_button 0\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -device piix3-ide,id=ide -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0 \
  -cdrom build/gos.iso -display none -serial file:/tmp/gos_drag_serial.log -monitor stdio
```
**What to check:** the screendump should show Window A's titlebar and body both fully rendered, clipped flush against the top-left corner — not vanished, not just a floating button fragment.

### Command to see
```bash
make debug   # real graphical QEMU window; drag any window off-screen with the mouse live
```
Screenshots: [screenshots/phase13_13.2_drag_before.png](screenshots/phase13_13.2_drag_before.png) (initial desktop) and [screenshots/phase13_13.2_drag_clamped_fixed.png](screenshots/phase13_13.2_drag_clamped_fixed.png) (Window A correctly clamped and clipped after the drag).

---

## Milestone 13.3: `heap_grow` free-block check

### Task: Check the highest-address block's `is_free` before extending it
- **What was done:** `kmalloc`'s heap-growth path (`kernel/src/heap.c`) found the last (highest-address) block and unconditionally extended its `size` to cover newly-mapped pages, regardless of whether that block was actually free.
- **The actual fix:** Check `b->is_free` before extending; if the block is in-use, append a brand-new free block header at the newly-mapped space instead of mutating the live block's reported size.

### Test: two failed repro designs, then a precise white-box test
- **Attempt 1 (failed to trigger the bug):** Allocated a large "sentinel" block, then a second large block, expecting the second allocation's growth to find the sentinel as the last node. **It didn't** — `kmalloc`'s own split logic always leaves a fresh *free* leftover block as the new tail after a split, so the next growth just legitimately extended that free leftover, never touching the sentinel.
- **Attempt 2 (root-caused via debug instrumentation):** Added a temporary debug print of the last block's `is_free` state at each growth. Confirmed it was `is_free=1` (a free split-off leftover) on *every* growth call in the test — proving the allocator's own fragmentation behavior made the buggy code path effectively unreachable through the public `kmalloc` API alone in this test scenario.
- **Attempt 3 (the working test):** Since `heap_self_test()` lives in the same translation unit as the allocator internals, directly walked to the real tail block and set `tail->is_free = 0` (simulating "something else holds this live"), stamped known data into it, then triggered a large `kmalloc` to force real growth.
- **Pre-fix reproduction:** Temporarily reverted the `is_free` check. **Result: `kmalloc` returned `NULL` — a spuriously reported OOM** — even though `Total usable memory: 202 MiB` and `PMM: ... free: 51760` (pages) had already been logged earlier in the same boot. This is a direct, literal reproduction of the audit's own wording: *"kmalloc can spuriously report OOM despite genuinely free address space existing."*
- **Post-fix verification:** Same test, fixed code. The large allocation succeeds, the artificially-marked-live tail block's size and stamped data are byte-for-byte unchanged, and the new allocation's own data reads back correctly. `Heap self-test: PASS` including this new check, on every boot.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean && make run 2>&1 | grep -A3 "Heap self-test: forcing heap_grow"
```
Expected:
```
Heap self-test: forcing heap_grow to extend past a live block...
Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun, double-free, and live-block heap_grow safety)
```
This runs on **every** normal boot (no `#ifdef` needed) — a permanent regression guard, following the same pattern as the Phase 12 overrun/double-free self-tests.

### Command to see
Non-graphical (heap internals). Confirm via `make debug` and watch the `Heap self-test: ...` lines in the serial output.

---

## Milestone 13.4: `create_entry` rollback completeness

### Task: Roll back a directory-growth cluster if `find_free_slot` fails partway
- **What was done:** `find_free_slot` (`kernel/src/fat32.c`) could allocate and link a new directory-growth cluster (when the current directory chain runs out of space), but if anything failed afterward — the zero-fill write, the FAT link write, or the subsequent read-back — the function returned failure while leaving that cluster permanently allocated and linked into the parent directory's chain, contributing nothing.
- **The actual fix:** Added a `grown_this_iter` flag tracking whether the current loop iteration just allocated+linked a growth cluster. On any of the three failure paths after that point, roll back: `fat_free_chain()` the orphaned cluster (and, for the read-failure case, also restore `prev_cluster`'s FAT entry back to end-of-chain to fully unlink it).

### Test: real success-path regression test; failure-path rollback verified via code review
- **Why the failure branches aren't live-tested:** All three rollback branches guard against genuine ATA write/read failures — the same category of hard-to-fault-inject scenario as Milestone 13.1. The plan's own suggested test (shrinking free-cluster count to simulate disk-full) actually exercises a *different*, already-correct branch (`fat_alloc_cluster()` returning 0, which has nothing to roll back) — not the write/link/read-failure branches this fix targets.
- **What was done instead:** Added a debug test that creates 20 files in `TESTDIR` (`kernel/src/start.c`), forcing `TESTDIR`'s single 512-byte/16-entry directory cluster to genuinely exceed capacity and trigger a real `find_free_slot` growth cycle, then deletes all 20 to restore the original disk state.
- **Result:** All 20 files created successfully (confirming the growth path itself still works correctly after the edit), all 20 deleted successfully, and — cross-checked independently via `mtools` — free space on the disk image was **exactly identical** before and after (`66,055,168 bytes free` both times), confirming no cluster leaked from the successful growth-and-cleanup cycle.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_growth_test.img
mdir -i /tmp/gos_disk_growth_test.img   # note "bytes free" before
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_CREATE_ENTRY_ROLLBACK"
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_growth_test.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A5 "TEST: forcing TESTDIR"
mdir -i /tmp/gos_disk_growth_test.img   # confirm "bytes free" matches before
```
Expected: `TEST: directory growth create loop = 1 (all 20 created OK)`, `TEST: directory growth cleanup = 1 (all 20 deleted OK)`, and identical `bytes free` in both `mdir` invocations.

### Command to see
Non-graphical (filesystem internals). Confirm via `make debug` watching serial output, and independently via `mdir -i <image>` from the host afterward.

---

## Milestone 13.5: `vmm_unmap_page()` + TLB invalidation

### Task: Implement page unmapping with `invlpg`
- **What was done:** `kernel/src/vmm.c` had no way to unmap a page at all — `vmm_map_page` could overwrite a PTE to point at a new physical page, but a stale TLB entry for that virtual address could still resolve to the old physical frame.
- **The actual fix:** Added `vmm_unmap_page(virt)`, which walks the existing page tables (without creating any missing levels, unlike the mapping path), clears the PT entry, and issues `invlpg` for that virtual address. Correctly no-ops if any level isn't present, and explicitly declines (documented, not silently wrong) if the address falls under a 2MiB huge page, since nothing in the codebase currently needs 4KiB-granularity unmapping within a huge-page region.

### Test: a subtle test design flaw caught before it mattered, then a clean repro
- **First test design (would have been a false negative):** Map virtual address to page A, write a value, unmap, remap to page B, **write** a different value through the same pointer, then read back and compare. This doesn't actually test anything — if the TLB is stale, both the write and the read-back go through the *same* stale mapping, so the read-back always matches whatever was just written, regardless of whether the TLB is stale or fresh.
- **The corrected design:** Seed page A and page B's content *directly* via their identity-mapped physical addresses (an always-fresh path, since `vmm_init` identity-maps the first 4GiB) — never through the scratch virtual address. Map the scratch address to A, read through it once to warm the TLB. Unmap, remap to B. Read through the scratch address again — **with no intervening write** — so the result purely reflects which physical frame the TLB currently resolves to.
- **Pre-fix reproduction:** Temporarily removed the `invlpg` call (kept the PTE clear). **Result: the final read returned `0xAAAA...` (page A's seeded value) instead of `0xBBBB...` (page B's) — a genuine, reproduced stale TLB entry.**
- **Post-fix verification:** Same test, `invlpg` restored. Final read correctly returns `0xBBBB...`, confirming the remap took effect immediately.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_VMM_UNMAP"
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A4 "TEST: vmm_unmap_page"
```
Expected:
```
TEST: vmm_unmap_page - phys_a=0x... phys_b=0x...
TEST: scratch_virt mapped to page A, primed read = 0xaaaaaaaaaaaaaaaa
TEST: after unmap+remap to page B (no write in between), read = 0xbbbbbbbbbbbbbbbb (correct - reflects page B)
```
**What to check:** the third line must show `0xbbbb...` and "(correct...)" — `0xaaaa...` and "(STALE...)" is exactly the pre-fix failure mode.

### Command to see
Non-graphical (paging internals, no visual surface). Confirm via `make debug` watching serial output.

---

## Milestone 13.6: Stale-window-after-close in the button-dispatch loop

### Task: Fully clear a closed window's state; re-check `in_use` mid-dispatch
- **What was done:** `window_close()` (`kernel/src/window.c`) only cleared `in_use`, leaving `buttons[]`, `custom_click`, `custom_render`, and `textbox_buffer` untouched. Separately, the button-dispatch loop in `window_system_update()` kept iterating a window's remaining button rects after one button's `on_click()` callback closed the window from inside itself.
- **The actual fix:** `window_close()` now clears every button's `in_use`/`on_click`, plus `has_textbox`, `textbox_length`, `textbox_buffer[0]`, `custom_render`, `custom_click`, `custom_key`, and `user_data` — leaving the slot genuinely inert. The dispatch loop now re-checks `win->in_use` at the top of each iteration and `break`s if a previous button's callback closed the window.

### Test: reproduce the exact overlapping-button scenario from the audit, then confirm the fix
- **Repro setup:** Added a temporary debug window ("Stale Test") with two buttons at the *exact same rect* — button 0 (dispatched first) closes the window from inside its own `on_click()`; button 1 (dispatched second, same rect) just logs and increments a counter.
- **Pre-fix reproduction:** Temporarily reverted both the dispatch-loop check and `window_close()`'s full-clear. Simulated a real mouse click at the overlap point via QEMU's monitor `mouse_move`/`mouse_button`. **Result: both `"TEST: stale-dispatch button 0 fired"` and `"TEST: stale-dispatch button 1 fired (BUG if this appears...)"` printed** — the stale post-close dispatch happened exactly as the audit described.
- **Post-fix verification:** Same click sequence, fixed code. **Only button 0's message printed** — button 1 never fired. A screendump before the click shows the "Stale Test" window open with its overlapping "Second" button visible; a screendump after shows the window fully gone from both the desktop and the taskbar.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_STALE_WINDOW_DISPATCH"
(sleep 20; printf "mouse_move 160 174\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.3; \
 printf "mouse_button 0\n"; sleep 0.5; printf "quit\n") | timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -device piix3-ide,id=ide -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0 \
  -cdrom build/gos.iso -display none -serial file:/tmp/gos_stale_dispatch_serial.log -monitor stdio
grep "TEST: stale-dispatch" /tmp/gos_stale_dispatch_serial.log
```
Expected: only one line, `TEST: stale-dispatch button 0 fired (closing window)` — the "BUG if this appears" line for button 1 must **not** print.

### Command to see
```bash
make debug   # real graphical QEMU window; the overlapping-button test window is visible at (700, 500) titled "Stale Test"
```
Screenshots: [screenshots/phase13_13.6_stale_dispatch_before.png](screenshots/phase13_13.6_stale_dispatch_before.png) ("Stale Test" window open, "Second" button visible on top) and [screenshots/phase13_13.6_stale_dispatch_after_fixed.png](screenshots/phase13_13.6_stale_dispatch_after_fixed.png) (window fully closed and gone from the taskbar after the click).

**Restore the normal build afterward** (as with all `GOS_TEST_*` flags used in this phase): `make clean && make iso`.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| 13.1 test: force ERR bit check to run against a real post-flush status read | Instrumented every real write during a full boot instead of a single forced call | More thorough regression coverage (1525 real writes exercised) than a single synthetic check, for the same effort |
| 13.3 test: allocate a block, make it the heap's highest-address block, force `heap_grow` | First two attempts (sequential large kmallocs) never actually reached a live last-block, because kmalloc's own split-then-leftover behavior always leaves a free tail; redesigned as a direct internal-state test | The public `kmalloc`/`kfree` API's own fragmentation behavior made the literal scenario unreachable through normal use - required white-box access to the allocator's internals (available since the test lives in the same file) to construct deterministically |
| 13.4 test: simulate disk-full by shrinking a scratch image's free cluster count | Forced a real directory-growth cycle (20 files in TESTDIR) instead, since disk-full exercises a different (already-correct) branch than the one this fix targets | The plan's suggested test doesn't actually exercise the write/link/read-failure rollback branches this milestone's fix adds - those need a genuine ATA I/O fault, same limitation as 13.1 |
| 13.5 test: map A, write, remap to B, write, read back | First design (write-then-read through the same pointer after remap) can't distinguish stale vs. fresh TLB, since both operations go through the same resolution; redesigned to seed via identity-mapped physical aliases and read-only after remap | A write-after-remap is self-consistent regardless of TLB staleness - the write itself lands wherever the (possibly stale) TLB entry points, and reading back through the same entry always matches |

None of these changed the actual fixes delivered - every root cause was confirmed and closed as scoped. As with Phase 12, the deviations were entirely in *how to reliably reproduce* each bug live, preserved here as useful diagnostic detail.

---

## Effort Estimate vs. Actual

| | Original estimate (project-plan-2.md) | Actual |
|---|---|---|
| Milestone 13.1 | included in Phase 13's 10–14h | ~45 minutes |
| Milestone 13.2 | included in Phase 13's 10–14h | ~1.5 hours |
| Milestone 13.3 | included in Phase 13's 10–14h | ~2.5 hours (two failed repro designs) |
| Milestone 13.4 | included in Phase 13's 10–14h | ~1.5 hours |
| Milestone 13.5 | included in Phase 13's 10–14h | ~1.5 hours (one test-design flaw caught before it mattered) |
| Milestone 13.6 | included in Phase 13's 10–14h | ~1.5 hours |
| **Phase 13 total** | **10–14 hours** | **~9.25 hours** |

**Why some milestones took longer than their fixes alone would suggest:** As with Phase 12, the actual code changes were small (a few lines to a couple dozen per milestone); the time went into designing reproductions that genuinely exercise the described failure mode. Milestone 13.3 in particular required understanding the allocator's own fragmentation behavior deeply enough to realize the public API couldn't reach the buggy state at all, then using file-scope access to force it directly - a more rigorous test than the milestone's literal wording called for, but necessary to actually prove the fix works rather than just assuming it does.

---

## Per-Milestone Testing Instructions

See each milestone's **Command to test** / **Command to see** sections above — self-contained and copy-pasteable from the project root (`cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`).

### Quick regression check for all of Phase 13 together
```bash
make clean && make run 2>&1 | grep -E "PANIC|EXCEPTION|Stress test:|Desktop ready"
```
Expected — a clean, normal boot with none of the six `GOS_TEST_*` macros defined:
```
Stress test: PASS (150 file cycles, 300 window cycles, no crash)
Desktop ready - click the "Files" icon to launch the File Manager
```
No `PANIC` or `EXCEPTION` lines should appear. All test scaffolding added in this phase (`GOS_TEST_ATA_STATUS_CHECK`, `GOS_TEST_CREATE_ENTRY_ROLLBACK`, `GOS_TEST_VMM_UNMAP`, `GOS_TEST_STALE_WINDOW_DISPATCH`) is `#ifdef`-gated and compiles out entirely in a normal build, confirming none of it leaked into the default build path. (Milestones 13.2 and 13.3's fixes have no gated test scaffolding — 13.2 was tested via live mouse simulation against the normal build, and 13.3's test is a permanent, always-on part of `heap_self_test()`.)

---

## Not Yet Verified (intentionally deferred to Phase 14+)

- [ ] The 12 Medium/Low findings from `audit.md` — Phase 14
- [ ] README.md update reflecting Track A progress — first task of Phase 14 per project-plan-2.md, a living task re-touched at the end of Track A and again at the end of Track B

---

## Next Step

Proceed to **Phase 14 — Medium/Low Audit Cleanup**, per [project-plan-2.md](project-plan-2.md). With all 5 Critical and 6 High findings closed, Phase 14 is the last Track A phase before Track B (new features) can begin — project-plan-2.md's priority rule requires all of Track A complete before Phase 15/16 work starts, since Phase 16 (window close/minimize/taskbar) directly builds on this phase's Milestone 13.6 fix (`window_close()`'s now-complete teardown) and Phase 12's Milestone 12.3 fix (`kfree` double-free detection).
