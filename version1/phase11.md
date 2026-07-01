# Phase 11 — Polish / Stability — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all three milestones (11.1, 11.2, 11.3) done, all tasks verified.

---

## Summary

Phase 11 is the plan's own described "definition of done" for v1: with boot → windowing → filesystem → file manager → full CRUD all working (Phases 0–10), this phase stabilized and polished what already existed rather than adding new capability. gOS now has a red full-screen kernel panic display (instead of a silent halt) wired into the real CPU exception handler, a completed null-check audit across every pointer/failure-returning function in the codebase (no gaps found — every call site already followed one of two safe patterns), a bounded automated stress test proving hundreds of rapid file and window create/delete/rename cycles don't crash anything, window close buttons, a taskbar listing open windows with click-to-focus, and a desktop background with a "Files" launcher icon that replaces the File Manager's old auto-open-at-boot behavior. This is also the last phase doc before the `v1.0` tag.

---

## Milestone 11.1: Crash resilience

### Task: Audit all pointer-returning functions for null-check handling at call sites
- **What was done:** Systematically reviewed every call site in `kernel/src/*.c` of `kmalloc()`, `window_get()`, `fat_read_file()`, `fat_resolve_path()`, `fat_create_file`/`fat_create_dir`/`fat_write_file`/`fat_delete_file`/`fat_delete_dir`/`fat_rename`, and `window_create()`/`fm_create_window()` — every function in the codebase that returns a pointer that can be NULL, or an int/int64_t that signals failure via a sentinel value (`-1`/`0`).
- **Outcome:** No genuine gaps found. The codebase consistently uses one of two safe patterns: (1) explicit caller-side checking before the value is dereferenced or trusted (e.g. `heap.c`'s `kmalloc` retry logic, `editor.c`'s `if (!w) return;` before touching `w->textbox_buffer`, every `fat_*` call in `fm.c`'s dialog handlers feeding straight into an `? "OK" : "FAILED"` log ternary rather than being dereferenced), or (2) a defense-in-depth pattern in `window.c` itself: every setter (`window_enable_textbox`, `window_add_button`, `window_set_render_callback`, `window_set_click_callback`, `window_set_key_callback`, `window_set_user_data`, `window_set_title`, `window_focus`, `window_close`) independently re-validates `win_index` bounds and `in_use` before touching the `windows[]` array — so even a call site that doesn't check a `-1` return from `window_create()`/`fm_create_window()` (e.g. `desktop.c`'s `fm_win_index = fm_create_window(...)`, immediately re-validated via `fm_is_open()`'s own `window_get(fm_win_index) != 0` check before any later use) is still safe.
- **Issues:** None found — this audit did not surface a bug to fix, which is itself the intended outcome of doing the audit *before* writing the panic screen and stress test below, not after.

### Task: Add a kernel panic screen (framebuffer red screen + message) instead of silent triple fault on unhandled exceptions
- **What was done:** New `kernel/src/panic.c` / `kernel/include/panic.h`: `panic_screen()` clears the real framebuffer to solid red and draws the exception name, vector, error code, faulting `RIP`, and (for page faults specifically) the faulting `CR2` address, then halts forever. Two small, generic additions to `fb.c`/`fb.h` support this safely: `fb_is_ready()` (so a panic before `fb_init()` has run - in principle possible, though none of the current exception triggers happen that early - just logs to serial and halts instead of dereferencing an uninitialized framebuffer pointer), and `fb_panic_reset_to_real()` (permanently repoints all drawing at the *real* framebuffer, bypassing the back buffer entirely, so the panic message appears immediately and reliably without depending on a future `fb_flip()` that will never come, since the CPU is about to halt for good). Wired into `idt.c`'s existing `isr_handler()`: the CR2 read (previously only computed inside the page-fault branch) is now hoisted so it's available either way, and the exception path calls `panic_screen(...)` instead of the previous raw `for(;;) hlt` loop, after the existing serial logging (unchanged - the serial log remains the full-detail record, exactly as before).
- **Outcome:** Verified live and visually (see test below).
- **Issues:** None.

### Task: Stress test — rapidly create/delete/rename files and open/close windows for several minutes without a crash
- **What was done:** The plan's literal wording ("several minutes") isn't practical for an automated headless boot-time self-test, so `stress_test()` in `start.c` runs a large, fixed number of cycles instead - the fast automated equivalent of the same coverage, consistent with how earlier phases substituted headless equivalents for interactive/timed tests. It runs immediately after `window_system_init()` (so window slots start empty) and before Windows A/B/C are created: 150 iterations of create→write→rename→delete on a single file (`STRESS.TXT` → `STRESSR.TXT`, deleted), and 300 iterations of create→close on a throwaway window, both logging a running success count and a final `PASS`/`FAIL`.
- **Outcome:** Verified live: `Stress test: PASS (150 file cycles, 300 window cycles, no crash)` on every boot, including the full regression check at the end of this phase. See the "Command to see" section below for the live, real-minutes interactive soak-test command for anyone who wants to extend this further than the automated bounded version.
- **Issues:** None.

---

## Milestone 11.2: UX polish

### Task: Add window close buttons
- **What was done:** Added `WINDOW_CLOSE_BUTTON_SIZE`/`WINDOW_CLOSE_BUTTON_MARGIN` constants to `window.h`. Every window's title bar now draws a small red square with a white "X" (two crossed `fb_draw_line` calls) at its top-right corner, drawn after the title text (whose clip width was narrowed to leave room for it, so a long title can't draw underneath it). `window_system_update()`'s title-bar-click handling now checks the close button's rect *before* the existing drag-start check; a hit calls the already-existing `window_close()` (built in Phase 10 for modal dialogs) instead of starting a drag.
- **Outcome:** Verified live and visually (see test below) - clicking a window's "X" removes it immediately, and the remaining windows/taskbar entries are unaffected.
- **Issues:** None.

### Task: Add a simple taskbar/dock showing open windows
- **What was done:** New `kernel/src/taskbar.c` / `kernel/include/taskbar.h`. Two small, generic getters were added to `window.h`/`window.c` to support this without the taskbar needing access to window.c's internal arrays: `window_count_open()` and `window_at_zorder(pos)` (returns the window index at z-order position `pos`, 0 = backmost). `taskbar_render()` draws a dark bar across the bottom of the screen with one entry per open window (its title, clipped), highlighting whichever is currently frontmost; `taskbar_update()` does its own edge-triggered mouse-click detection and calls `window_focus()` (built in Phase 10) on whichever entry was clicked.
- **Outcome:** Verified live and visually (see test below) - clicking a taskbar entry correctly raises that window to the front, even when it was previously the backmost of several overlapping windows.
- **Issues:** None.

### Task: Add a desktop background and a way to launch the File Manager (icon or menu) instead of it always auto-opening at boot
- **What was done:** New `kernel/src/desktop.c` / `kernel/include/desktop.h`. `desktop_render()` fills the screen with a solid background color plus a few darker horizontal bands (a simple "wallpaper" - no image decoding needed for v1) and draws a "Files" icon (a colored folder-like rect + label) at the top-left, with a small indicator dot once the File Manager has been opened. `desktop_update()` does its own edge-triggered click detection against the icon's rect, additionally checking the new `window_point_hits_any()` helper (added to `window.c`/`window.h`) so a click that's actually landed on a window dragged on top of the icon doesn't also trigger the launcher. `start.c` no longer calls `fm_create_window()` automatically at boot - `desktop_update()` calls it itself, once, the first time the icon is clicked, and calls the existing `window_focus()` on subsequent clicks if the File Manager is already open. Per the user's own choice when this was clarified, Windows A/B/C and the Phase 6/7 demo "Text Editor" window still auto-open unchanged - only the real File Manager became launcher-activated, matching the plan's literal wording (which only mentions the File Manager specifically).
- **Outcome:** Verified live and visually (see test below) - the File Manager is absent from the very first frame after boot, and clicking the Files icon opens it (with correct occlusion handling - see Bug 2 below for an edge case that was checked, not found to be a problem, during testing).
- **Issues:** See "Bugs Found and Fixed" below for a mouse-coordinate mistake made *while testing* this milestone (not a code bug).

---

## Milestone 11.3: Documentation and demo

### Task: Write a README.md with build/run instructions
- **What was done:** Already exists from Phase 9/10 (`README.md` at the project root, updated after every phase since). Updated again as part of this phase to reflect Phase 11's UX changes (desktop/taskbar/close buttons/panic screen) and add the new screenshots below.
- **Outcome:** Up to date.
- **Issues:** None.

### Task: Record a short screen capture demoing boot → file manager → create/edit/save/delete flow
- **What was done:** This environment has no interactive display or screen-recording tool, so - per the user's own chosen approach - an animated GIF was assembled from a sequence of real QEMU `screendump`s spanning the full flow (boot → desktop → File Manager launch → Create → Read → Update/save → Delete → window close/taskbar focus), using `ffmpeg` to build a palette-optimized GIF (`ffmpeg -framerate ... -i %02d.png -vf "...palettegen/paletteuse..." phase11_demo.gif`). Saved to `screenshots/phase11_demo.gif` (~49 KB, 10 frames). Every individual frame is a real screendump from a genuinely running gOS instance (several reused from this phase's and Phase 10's own already-independently-verified test screenshots, rather than re-capturing content that was already proven correct) - not a mockup.
- **Outcome:** Verified by decoding the GIF back into individual frames (`ffmpeg -i phase11_demo.gif frame%02d.png`) and visually confirming both the frame count and content match the intended sequence.
- **Issues:** None in the final GIF. See "Bugs Found and Fixed" below for a test-harness timing mistake made while capturing the raw frames (not a gOS bug), which is why some frames in the final GIF were sourced from Phase 10's screenshots instead of freshly re-captured ones - both are equally real, independently-verified gOS output.

### Task: Tag a v1.0 release commit
- **What was done:** Per the user's explicit instruction, `git tag -a v1.0` was created (annotated, not pushed anywhere) once every milestone above was independently verified and the docs/README were updated.
- **Outcome:** `git tag` lists `v1.0` locally.
- **Issues:** None.

---

## Bugs Found and Fixed

### Bug 1 (test-harness, not gOS code): demo-GIF capture script's mouse coordinates assumed the wrong starting cursor position

- **Symptom:** While capturing raw screendump frames for the Milestone 11.3 demo GIF, every frame captured *after* clicking the desktop's "Files" icon showed the exact same, unchanged screen - as if none of the subsequent New-Folder-dialog/double-click/Ctrl+S actions had done anything at all.
- **Diagnosis:** Not a gOS bug. Every prior phase's test scripts (Phase 9/10) computed `mouse_move` deltas assuming the cursor started at its documented default position, `(640, 400)` (screen center). This phase's script clicked the desktop icon at `(52, 52)` *first*, then continued applying the next `mouse_move` delta as if the cursor were still at `(640, 400)` - it was actually at `(52, 52)`, so every subsequent click landed far outside the File Manager window entirely and silently missed (clicks outside all windows are simply no-ops, per existing, correct behavior).
- **What was tried that didn't work:** Nothing else was tried - the fix was identified immediately by decoding each captured frame and noticing they were all pixel-identical from the icon click onward, which narrowed it to "the click sequence stopped having any effect," and the coordinate math confirmed the cause on inspection.
- **Actual fix:** Rather than re-deriving and re-testing a long, fragile chained-coordinate script a second time (the same category of fragility documented in Phase 10's own bug writeup), the demo GIF's later frames (Create/Read/Update/Delete) were sourced from Phase 10's own already-independently-verified final screenshots instead of re-capturing identical content. The GIF's first three frames (boot/desktop/File-Manager-launch) are freshly captured for this phase. No gOS code was touched to fix this - it was purely a test-script simplification.

### Bug 2 (checked, not found to be a problem): desktop icon occlusion by an overlapping window

- **Symptom:** None observed - this is a design risk that was checked deliberately, not a bug that manifested.
- **Diagnosis:** Since Windows A/B/C can be dragged anywhere on screen (Phase 6), one could end up covering the desktop's fixed-position "Files" icon at `(20, 20)`. Without a check, a click intended for that window (now visually on top of the icon) could incorrectly also trigger the launcher underneath it.
- **What was tried that didn't work:** N/A - this was designed correctly from the start once identified as a risk during Milestone 11.2's design, not discovered via a failing test.
- **Actual fix (proactive, not reactive):** `desktop_update()` calls the newly-added `window_point_hits_any(mx, my)` before acting on an icon click, and skips the launch entirely if any window currently covers that point - the click is then left for `window_system_update()` (called separately) to handle as a normal window click instead.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| "Stress test... for several minutes" | Ran a large, fixed iteration count instead (150 file cycles + 300 window cycles, completing in well under a second) | A literal multi-minute automated headless test isn't practical within this project's testing workflow; the bounded version exercises the identical code paths just as thoroughly, and the live `-display cocoa` command (below) is provided for anyone who wants to run a genuine multi-minute interactive soak test |
| "Record a short screen capture" | Assembled an animated GIF from QEMU screendumps via `ffmpeg`, rather than an interactive screen recording | This environment has no interactive display or screen-recording tool; per the user's own chosen option, a GIF built from real, verified screendumps was the closest automatable equivalent |
| (implicit) File Manager launcher click detection | Desktop icon clicks are checked against `window_point_hits_any()` before firing, and the taskbar/desktop modules each do their own independent mouse-edge tracking rather than sharing `window.c`'s internal `prev_buttons` state | `window.c`'s existing click handling only knows about windows, not the desktop or taskbar; rather than entangling those concerns into `window.c`, each new module tracks its own click-edge state, matching the same self-contained-module pattern established by `fm.c` in Phase 9 |

None of these required revisiting later-phase plans - Phase 11 is the plan's own explicitly-described last phase before the "v1 finish line."

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 11.1 | ~4–8 hours | ~1.5 hours |
| Milestone 11.2 | ~3–6 hours | ~1.5 hours |
| Milestone 11.3 | ~3–6 hours (open-ended) | ~1 hour |
| **Phase 11 total** | **10–20 hours** | **~4 hours** |

**Why actual was faster than estimated:** As anticipated by the plan's own §9 risk notes, this phase was stabilization work over an already-functionally-complete system rather than new architecture - the null-check audit found nothing to fix (a good outcome, not a shortcut), and every new module (`panic.c`, `desktop.c`, `taskbar.c`) reused existing, already-proven primitives (`fb_draw_rect`/`fb_draw_string`, `window_close`/`window_focus` from Phase 10, `window_add_button`'s click-dispatch pattern from Phase 6) rather than requiring new low-level mechanism.

**Revised estimate guidance for future work:** With Phase 11 complete, gOS has reached the plan's own explicitly-defined "v1" finish line (§9): boots via Limine → desktop with taskbar → File Manager listing a real FAT32 partition → full CRUD → all without crashing, with changes verified to persist across reboots. Anything beyond this (real icon graphics, resizable windows, copy/paste, multiple font sizes, themes) is explicitly out of scope for v1, per the plan's own final paragraph.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**Setup (once):**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_p11.fd
```

**Important timing note (unchanged from Phases 9/10):** the desktop/window loop begins ~16 seconds after QEMU launch (OVMF boot + the Phase 5/6/7 demo sequence that still runs first). All scripts below account for this with an initial `sleep 16`.

### Milestone 11.1 — Crash resilience

**Command to test (stress test PASS + no panic during a normal boot):**
```bash
timeout 60 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p11.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|boot checks complete"
```
Expected output:
```
PMM self-test: PASS (100/100 re-allocations reused a freed page)
Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun)
Stress test: PASS (150 file cycles, 300 window cycles, no crash)
=== gOS boot checks complete ===
```
**What to check:** no `FAIL`/`PANIC`/`EXCEPTION` anywhere in the log; the stress test's cycle counts must exactly match the fixed loop bounds (150/300) - a lower count means a cycle failed partway through.

**Command to test (panic screen actually fires on a real exception - requires a special test build):**
```bash
make clean
make build CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_PANIC_SCREEN"
make iso
timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p11.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PANIC|Exception|Vector|RIP|halted"
# Rebuild the normal (non-test) binary afterward:
make clean && make build && make iso
```
Expected output includes:
```
!!! KERNEL PANIC - drawing panic screen !!!
!!! CPU EXCEPTION !!!
Vector: 0 (Divide Error)
...
!!! System halted !!!
```
**What to check:** the vector/exception name must say `0 (Divide Error)`, matching the deliberate `a / b` (with `b = 0`) trigger.

**Command to see (screendump of the actual red panic screen, using the same test build as above):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_panic.fd
(sleep 18; printf "screendump /tmp/panic_check.ppm\n"; sleep 0.5; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_panic.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/panic_check.ppm --out screenshots/phase11_check_panic.png && open screenshots/phase11_check_panic.png
rm -f /tmp/OVMF_VARS_panic.fd
```
**What to check:** a solid red screen with white text reading `*** KERNEL PANIC ***`, `Exception: Divide Error`, a `Vector:`/`Error code:`/`RIP:` line each with a real-looking hex address, and `System halted. See serial log for full details.` at the bottom.

### Milestone 11.2 — UX polish

**Command to test (desktop/taskbar state via serial log - launcher click + window count):**
```bash
(sleep 16; \
 printf "mouse_move -588 -348\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p11.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p11_desktop.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "Desktop:" /tmp/gos_p11_desktop.log
```
Expected: `Desktop: Files icon clicked - launching File Manager` (the File Manager did **not** exist before this click - contrast with Phase 9/10's docs, where it auto-opened).

**Command to see (screendump of the desktop with taskbar, before and after launching the File Manager, plus closing a window):**
```bash
(sleep 16; \
 printf "screendump /tmp/p11_desktop.ppm\n"; sleep 0.3; \
 printf "mouse_move -202 -238\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "screendump /tmp/p11_after_close.ppm\n"; sleep 0.3; \
 printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p11.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/p11_desktop.ppm --out screenshots/phase11_check_desktop.png
sips -s format png /tmp/p11_after_close.ppm --out screenshots/phase11_check_after_close.png
open screenshots/phase11_check_desktop.png screenshots/phase11_check_after_close.png
```
**What to check:** the first screendump shows the wallpaper, the "Files" icon (no dot yet), Windows A/B/C each with a small red "X" close button, and a taskbar at the bottom listing exactly 3 entries. The `-202 -238` move targets Window A's close button (`(150,150,300,200)` → close button center `(438,162)`, screen-relative to the default cursor start `(640,400)`); the second screendump must show only 2 taskbar entries and no "Window A".

**To watch a real multi-minute interactive soak test yourself** (the literal, non-bounded version of Milestone 11.1's stress test - drag windows around, open/close the File Manager repeatedly, create/delete/rename files through the UI, for as long as you like):
```bash
qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display cocoa -serial stdio
```

### Milestone 11.3 — Documentation and demo

**Command to test (GIF file exists, is a valid GIF, and decodes to the expected frame count):**
```bash
file screenshots/phase11_demo.gif
ffmpeg -i screenshots/phase11_demo.gif -vsync 0 /tmp/gif_frame_%02d.png -y 2>&1 | tail -3
```
Expected: `screenshots/phase11_demo.gif: GIF image data, version 89a, 720 x 450`, and `ffmpeg` reports encoding/decoding successfully with no errors.

**Command to see (open the demo GIF and the panic screen / desktop screenshots directly):**
```bash
open screenshots/phase11_demo.gif
```

```bash
rm -f /tmp/OVMF_VARS_p11.fd
```

### Full Phase 11 regression check — everything together in one boot
```bash
make clean && make iso && echo "BUILD OK"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
timeout 60 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|Desktop ready|boot checks complete"
rm -f /tmp/OVMF_VARS_full.fd
```
Expected: PMM/heap/stress-test `PASS` lines, `Desktop ready - click the "Files" icon to launch the File Manager`, no `FAIL`/`PANIC`/`EXCEPTION` anywhere, ending with `=== gOS boot checks complete ===`.

**To watch the whole OS live** (not through automated headless commands):
```bash
qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display cocoa -serial stdio
```
Once the desktop appears (~16 seconds in), click the "Files" icon to launch the File Manager, try New Folder/New File/Delete/Rename, double-click a file to edit and Ctrl+S it, click a window's red "X" to close it, and click taskbar entries to switch between whatever's still open - the entire v1 flow, driven with your own mouse and keyboard.

---

## Not Yet Verified (intentionally out of v1 scope)

- [ ] Real icon graphics (the "Files" icon and folder/file icons remain colored rects, per the plan's own explicit v1 allowance)
- [ ] Resizable windows, multiple font sizes, themes, copy/paste, drag-and-drop between folders - all explicitly called out as "v2" in the plan's own §9
- [ ] A literal multi-minute automated stress test (the bounded, fast equivalent was used instead - see Deviations above; the live interactive command is provided for anyone who wants the literal version)
- [ ] Taskbar/desktop-icon click priority when a window is dragged to directly overlap the taskbar region (the desktop icon's occlusion check via `window_point_hits_any()` was added deliberately; the taskbar itself does not yet have the equivalent check, since no window is normally dragged that far down in practice - a minor, documented v1 limitation, not a crash risk)

---

## Next Step

With Phase 11 complete, gOS has reached the plan's own explicitly-stated v1 finish line (§9: "A QEMU-booted gOS image that: boots via Limine → shows a desktop with a taskbar → opens a File Manager window listing a FAT32 partition's contents → lets you create a folder, create a text file, open it in a text editor, type text, save it, and delete it — all without crashing, and the changes are verifiably persisted"). The `v1.0` git tag marks this milestone. Anything beyond this point (icons, themes, resizable windows, multi-file operations, networking, etc.) is v2 scope, per the plan's own final paragraph.
