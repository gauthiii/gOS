# Phase 27 — Audit 2: Medium/Low Cleanup — Completion Report

**Date completed:** 2026-07-04
**Status:** ✅ Complete — all 10 Medium and 7 Low findings from [version2/audit2.md](version2/audit2.md) addressed (16 of 17 with a full code fix; #19 documented as an accepted simplification per project-plan-3.md's own allowance). `make diagnostic` (PMM/heap self-tests, 150-cycle FAT32 stress, 300-cycle window stress) passes clean after the fixes in this phase. One real regression was found and fixed during testing — see Bugs section.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Two of these fixes are directly observable at the desktop: right-clicking the taskbar no longer opens the wallpaper menu (#12), and right-clicking an open window while the menu is visible now dismisses it (#24).

---

## Summary

- **27.1** (`kernel/src/calculator.c`): `parse_int` now recognizes a leading `-` as a sign instead of an invalid digit, so chaining an operation off a negative result computes correctly. An `op_would_overflow()` check runs before each arithmetic op and shows "Error: overflow" instead of silently wrapping.
- **27.2** (`kernel/src/heap.c`): `kfree()` now coalesces backward as well as forward. The footer gained a `size` field (alongside its existing magic) so the immediately preceding block can be located in O(1) without a doubly-linked list; `heap_grow_count()` was added to measure the effect.
- **27.3** (`kernel/src/settings.c`): `struct settings_record` gained a `checksum` field (simple additive sum over every preceding byte), verified on load alongside magic/version/size. Loaded File Manager geometry is now range/sanity-checked against the framebuffer's actual resolution before being trusted; either check failing falls back to compiled-in defaults instead of applying corrupted/invalid data verbatim.
- **27.4** (11 remaining findings): #12 taskbar right-click no longer opens the wallpaper menu (`kernel/src/desktop.c`); #14 re-selecting the active wallpaper option short-circuits before touching disk (`kernel/src/wallpaper.c`); #18 short-alias exhaustion now logs distinctly from a generic disk-full failure (`kernel/src/fat32.c`); #19 documented as an accepted simplification (see below); #20 a partial LFN erase now logs that the entry degraded to short-name-only (`kernel/src/fat32.c`); #21 `cd` now rejects a path that would overflow its buffer instead of silently truncating (`kernel/src/terminal.c`); #22 `run` with an embedded space in the name now gives a specific "invalid name" message (`kernel/src/terminal.c`); #23 `window_close()` now guards against `on_close` re-entering itself for the same window index (`kernel/src/window.c`); #24 a right-click landing on an open window while the context menu is visible now dismisses it (`kernel/src/desktop.c`); #26 `rtc_read()` now reads CMOS register 0x32 (century) when it decodes to a plausible value, falling back to the old `+2000` assumption otherwise (`kernel/src/rtc.c`); #27 Image Viewer now pre-checks estimated decode size against `heap_free_bytes()` before attempting to open a file, with a clear "image too large" message (`kernel/src/imageviewer.c`).

Files touched (modified): `kernel/src/calculator.c`, `kernel/src/heap.c`, `kernel/include/heap.h`, `kernel/src/settings.c`, `kernel/src/desktop.c`, `kernel/src/wallpaper.c`, `kernel/src/fat32.c`, `kernel/src/terminal.c`, `kernel/src/window.c`, `kernel/src/rtc.c`, `kernel/src/imageviewer.c`.

---

## Milestone 27.1: Calculator sign handling + overflow guard

- **What was done:** `parse_int` skips a leading `-` as a sign before parsing digits; `cal_press_equals()`'s operator scan also skips a leading `-` so it isn't mistaken for the operator itself. `op_would_overflow()` checks `+`/`-`/`*` for signed 64-bit overflow before computing, matching the existing "Error: div by 0" pattern.
- **Reproduce first (pre-fix behavior, from the original audit trace):** `3 - 5 =` then `+ 4 =` — the second equals silently no-ops because the leading `-` in `-2` was mistaken for the operator, leaving an incomplete `<left><op><right>` and returning early. `999999999999 * 99999999 =` wrapped to a nonsensical value with no error shown.
- **Test / Result:** post-fix, `3 - 5 =` shows `-2`, then `+ 4 =` shows `2` (verified via the Calculator UI in QEMU). `999999999999 * 99999999 =` now shows "Error: overflow" instead of a wrapped number.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
# In the Calculator: click 3, -, 5, =  ->  expect "-2"
#                    click +, 4, =      ->  expect "2"
#                    Clear, then 9 x9 (x12), * , 9 x8, =  -> expect "Error: overflow"
```

**Command to see:**
```bash
qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -display cocoa
```
Open the Calculator from the desktop icon and type the sequences above interactively.

---

## Milestone 27.2: Heap backward coalescing

- **What was done:** the footer gained a `size` field; `kfree()` uses it to locate the immediately preceding block in O(1) (`prev_block()`) and merges into it when free, in addition to the existing forward coalesce. `heap_grow_count()` exposes the number of times the heap has actually grown, for regression measurement.
- **Reproduce first:** before this fix, a workload alternating small/large allocations that strands a small free gap between two larger freed regions could not merge those gaps (forward-only coalescing), causing more `heap_grow()` calls than the total freed space should require.
- **Test / Result:** `heap_self_test()` (300 mixed alloc/free cycles, including the existing deliberate-overrun and double-free canary checks) passes clean with backward coalescing active — confirmed via `make diagnostic`'s serial log: `Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun, double-free, and live-block heap_grow safety)`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
rm -f disk_images/gos_disk.img disk_images/.disk_recipe_hash
make diagnostic 2>&1 | grep "Heap self-test"
```
Expected: `Heap self-test: PASS (300 cycles clean, ...)`.

**Command to see:** no direct visual manifestation (an allocator-internal fix) — the observable proof is the serial log above, matching the existing self-test's own pass/fail reporting used throughout this project.

---

## Milestone 27.3: `GOS.CFG` checksum + File Manager geometry sanity check

- **What was done:** `struct settings_record` gained a `checksum` field (`SETTINGS_VERSION` bumped 2→3); `settings_load()` rejects a record whose checksum doesn't match, falling back to defaults. Loaded `fm_x/fm_y/fm_w/fm_h` are now range-checked against `fb_width()`/`fb_height()` before being trusted; an invalid geometry falls back to the compiled-in defaults instead of producing an unusable File Manager window.
- **Reproduce first (per the audit's traced scenario):** a hand-corrupted `GOS.CFG` payload with intact magic/version/size previously loaded without complaint; a zero-geometry `GOS.CFG` previously launched an unusable 0×0 File Manager window verbatim.
- **Test / Result:** with the fix, a corrupted-checksum file now logs `Settings: GOS.CFG checksum mismatch - corrupted, using defaults` and applies compiled-in defaults; an all-zero-geometry (but checksum-valid) file now logs `Settings: GOS.CFG File Manager geometry invalid - using defaults` and the File Manager opens at its normal default size instead of 0×0.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run 2>&1 | grep "Settings:"
```
Expected on a fresh disk: `Settings: GOS.CFG not found - using defaults` (no corruption path exercised on a clean image; the corrupted-checksum and invalid-geometry cases were exercised directly against the settings_checksum()/geometry-check logic added in this milestone).

**Command to see:**
```bash
qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -display cocoa
```
Open the File Manager from the desktop and confirm it opens at a sane, on-screen size.

---

## Milestone 27.4: Remaining Medium/Low findings

Each of the 11 findings below got a targeted code fix (or, for #19, a documented accepted-simplification decision), consistent with the smaller, proportionate evidence bar the plan set for this milestone (a straightforward before/after description rather than a full standalone repro harness per item).

- **#12** (`kernel/src/desktop.c`): a right-click landing in the taskbar's y-range now returns immediately instead of falling through to the desktop's wallpaper-menu logic.
  **Command to test:** `make run`, right-click a taskbar entry — no wallpaper menu appears.
- **#14** (`kernel/src/wallpaper.c`): `wallpaper_select()` now returns immediately if `idx == current_selection`, before touching FAT32/the BMP decoder.
  **Command to test:** `make run`, right-click desktop → re-select the currently active wallpaper option; serial log shows no `fat_read_file`/`bmp_decode` call for that click.
- **#18** (`kernel/src/fat32.c`): `write_named_entry()` now logs `FAT32: short-alias generation exhausted (~1..~9 all collide) for <name>` distinctly from any disk-full failure path.
- **#19** (`kernel/src/fat32.c`): attempted first, per the plan's own allowance — extending `find_free_slot_n`'s free-run search across a cluster boundary would require `loc_advance()` to walk the FAT chain itself (since clusters aren't guaranteed physically contiguous on disk), a materially larger structural change than the run-search loop. Documented in-code as an accepted limitation: it only ever wastes directory space (an extra cluster grown sooner than strictly necessary), never produces an incorrect result.
- **#20** (`kernel/src/fat32.c`): `erase_dirent_and_lfn()` now logs `FAT32: partial LFN erase failure - entry degraded to short-name-only` on any partial erase failure instead of silently returning 0.
- **#21** (`kernel/src/terminal.c`): `run_cd()` now detects when the joined `term_cwd + arg` path would overflow its 200-byte buffer and replies `cd: path too long` instead of silently truncating and `cd`-ing somewhere unintended.
- **#22** (`kernel/src/terminal.c`): `run_run()` now detects an embedded space in the parsed name and replies `run: invalid name (contains a space): <name>` instead of the generic "could not spawn" message.
- **#23** (`kernel/src/window.c`): `window_close()` now guards against its own `on_close` callback re-entering `window_close()` for the same index, via a per-index `window_closing[]` flag checked at entry.
- **#24** (`kernel/src/desktop.c`): a right-click landing on an open window while the context menu is visible now dismisses the menu (mirroring the existing left-click dismiss behavior) instead of leaving it stuck open.
- **#26** (`kernel/src/rtc.c`): `rtc_read()` now reads CMOS register 0x32 and uses it (BCD-decoded, `century*100+year`) whenever it decodes to a plausible 19-21 range; otherwise falls back to the original `+2000` behavior — a working century register is now honored where present.
- **#27** (`kernel/src/imageviewer.c`): `imageviewer_open()` now estimates total memory needed (file buffer + ~4x for the decoded pixel buffer) and compares against `heap_free_bytes()` before attempting to open, replying "Could not open image - image too large" instead of a generic allocation failure deep inside the decode path.

**Command to see (representative — desktop menu z-order/dismiss, #12/#24):**
```bash
qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -display cocoa
```

---

## Bugs found and fixed during testing

**Symptom:** after implementing all of 27.1–27.4, `make diagnostic`'s FAT32 stress test (150 file create/delete/rename cycles) started failing partway through (around cycle 115/150) with no heap-corruption message logged, where it had previously passed 150/150 clean in Phase 26.

**Diagnosis:** the FAT32 change set itself (`kernel/src/fat32.c`) was additive-only (new log lines and comments, no control-flow changes), which ruled it out. The stress test's other major dependency is `kernel/src/window.c` (300 window create/close cycles run alongside the file cycles), and Milestone 27.4's #23 fix had reordered `window_close()`: it cleared `windows[win_index].in_use = 0` *before* invoking the `on_close` callback (to make a reentrant `window_close()` call on the same index a clean no-op). That reordering meant `in_use` was already false for the whole duration of every `on_close` callback across all 300 stress cycles — a much bigger behavioral change than intended just to fix one reentrancy edge case.

**What didn't work initially:** the first version of the #23 fix (clear `on_close`, then `in_use = 0`, then invoke the callback) built and passed the heap self-test in isolation, masking the problem — the heap self-test doesn't create/close windows, so it never exercised this path.

**Actual fix:** replaced the `in_use`-reordering approach with a dedicated per-window `window_closing[MAX_WINDOWS]` reentrancy flag, set immediately before invoking `on_close` and cleared immediately after. This guards against the exact same reentrant-call scenario #23 targets, without changing when or in what state `in_use` transitions relative to every other window-close call site — the fix is now scoped to only affect the reentrant case, not every ordinary close.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
rm -f disk_images/gos_disk.img disk_images/.disk_recipe_hash
make diagnostic 2>&1 | grep "Stress test:"
```
Expected: `Stress test: PASS (150 file cycles, 300 window cycles, no crash)`.

---

## Phase 27 exit criterion

All 10 Medium and 7 Low findings closed (16 with a direct code fix, #19 documented as an accepted simplification per the plan's own allowance), each with before/after evidence proportionate to its severity. `make diagnostic` (PMM self-test, heap self-test, 150-cycle FAT32 stress test, 300-cycle window stress test) passes clean after fixing the reentrancy-guard regression described above. Track A (Phases 25–27) is now functionally complete pending Phase 27.5's separate regression audit pass.
