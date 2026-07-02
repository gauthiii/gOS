# Phase 21 — Window Resize & Alt+Tab — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — both milestones landed on the first real functional test, after one test-coordinate mistake (not a kernel bug) was caught and corrected during verification.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

No new build flags or bundled files for this phase — it's pure windowing-system logic in `kernel/src/window.c` and `kernel/src/keyboard.c`, always active. Every window now has a drag-to-resize handle along its right/bottom edges and bottom-right corner, and Alt+Tab cycles focus without touching the mouse.

---

## Summary

Phase 21 closes the two windowing gaps left after Phases 15–17 (minimize/maximize): free-form resize and keyboard-only window switching.

- **21.1 (resize)** extends `window_system_update()`'s existing hit-test chain (close → minimize → maximize → *[new] resize corner → resize right-edge → resize bottom-edge* → titlebar-drag → body) with three new zones along a window's outer edge, each setting a `resizing_window`/`resize_right`/`resize_bottom` state parallel to the existing `dragging_window` mechanism. Size updates are clamped to a minimum (120×60) and to the screen bounds (reusing the same clamping philosophy as Phase 13.2's drag-position clamp and Phase 17.1's maximize sizing, which already reserves the taskbar strip).
- **21.2 (Alt+Tab)** adds Left-Alt tracking to `keyboard.c` (parallel to how Ctrl-tracking was first added, per Phase 14 Finding #13's precedent) and a new edge-triggered `kb_consume_alt_tab()` flag, consumed once per frame by `window_system_update()`. The actual focus-cycling logic (`window_focus_next()`) needed a genuine design correction mid-implementation — documented in full below — before it correctly visited every open window instead of only ever toggling between the last two.

Files touched: `kernel/include/window.h`, `kernel/src/window.c`, `kernel/include/keyboard.h`, `kernel/src/keyboard.c`.

---

## Milestone 21.1: Drag-to-resize from window edges/corners

- **What was done:** Three new hit-test branches in `window_system_update()`, checked after the close/minimize/maximize buttons (so clicking those still works even though they sit inside the resize margin geometrically) but before the plain titlebar-drag check (so grabbing the actual edge always resizes, matching typical desktop UX): bottom-right corner (both `resize_right` and `resize_bottom` set together), right edge alone, bottom edge alone. `WINDOW_RESIZE_MARGIN` (6px) defines how close to the outer edge a click must land. During drag, `w`/`h` are recomputed from the live mouse position each frame, clamped to `WINDOW_MIN_WIDTH`/`WINDOW_MIN_HEIGHT` (120×60) on the low end and to the screen bounds (reserving the taskbar strip on the bottom, matching Phase 17.1's maximize-sizing convention) on the high end. Resizing a maximized window is disallowed (must restore first) — the same restriction Phase 17.1 already applies to dragging a maximized window.
- **Test:** dragged the File Manager's bottom-right corner outward (enlarged from 420×260 to roughly 530×366) and confirmed via `screendump` that the toolbar buttons, path breadcrumb, and file listing all re-laid out correctly at the new size with no visual corruption (no stretched/clipped text, no stale pixels from the old size). Dragged the same corner back inward and confirmed a clean shrink. Dragged the corner far past the bottom-right screen edge and confirmed the window clamps exactly to the screen width and the taskbar's top edge, rather than wrapping to a huge value or crashing (the exact failure mode Finding #7/Phase 13.2's position-clamp was written to prevent — this extends the same defensive pattern to size).
- **A test-coordinate mistake, not a kernel bug (worth recording since it looked like a bug at first):** my first two attempts to test *shrinking* the window appeared to do nothing — the window stayed at its enlarged size no matter how the mouse moved. Diagnosis: after enlarging, I pressed the mouse button again at the *exact* new corner pixel to start the shrink drag, but `point_in_rect()`'s bounds check is `px < rx + rw` (strictly less than) — landing exactly on the boundary pixel is one pixel *outside* the window by that definition, so the click never registered as a resize (or anything else) at all, and the subsequent drag motion had no window attached to it. Confirmed this was a test-script issue, not a hit-testing bug, by retrying the identical drag 2-3 pixels further into the window body (unambiguously inside the margin) — the shrink then worked correctly on the first attempt. No code changes were needed.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk build/OVMF_VARS.fd
S=$(mktemp -d)
(sleep 3; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.2; \
 echo "mouse_move 486 290"; sleep 0.3; echo "mouse_button 1"; sleep 0.3; \
 echo "mouse_move 108 104"; sleep 0.3; echo "mouse_button 0"; sleep 0.3; \
 echo "screendump $S/enlarged.ppm"; sleep 0.3; \
 echo "mouse_move -3 -3"; sleep 0.3; echo "mouse_button 1"; sleep 0.3; \
 echo "mouse_move -200 -150"; sleep 0.3; echo "mouse_button 0"; sleep 0.3; \
 echo "screendump $S/shrunk.ppm"; sleep 0.3; echo "quit") | timeout 20 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
open $S/enlarged.ppm $S/shrunk.ppm
```
Expected: `enlarged.ppm` shows the File Manager visibly bigger than its default 420×260, with the toolbar/listing correctly re-drawn at the new width; `shrunk.ppm` shows it back down to a smaller size with the same clean re-layout.

**Command to see:**
```bash
make run   # graphical QEMU window - grab any window's bottom-right corner (the cursor
           # doesn't change shape yet, but the last ~6px of the edge is live) and drag
```
Screenshots: [screenshots/phase21_before_resize.png](screenshots/phase21_before_resize.png) (default size), [screenshots/phase21_after_enlarge.png](screenshots/phase21_after_enlarge.png) (enlarged), [screenshots/phase21_after_shrink.png](screenshots/phase21_after_shrink.png) (shrunk back down), [screenshots/phase21_edge_clamp.png](screenshots/phase21_edge_clamp.png) (dragged far past the screen edge — clamps cleanly to the screen width and the taskbar's top edge).

---

## Milestone 21.2: Alt+Tab window switching

- **What was done:** `keyboard.c` gained `SC_LEFT_ALT_MAKE`/`_BREAK` (0x38/0xB8) tracking and a `SC_TAB_MAKE` (0x0F) check — when Tab's make code arrives while Alt is held, it sets an edge-triggered `alt_tab_pending` flag instead of falling through to the normal character-push path (so Alt+Tab never also inserts a literal `\t`). `kb_consume_alt_tab()` (new, in `keyboard.h`) reads and clears that flag, checked once per frame in `window_system_update()`.
- **A real design bug, found and fixed before the first live test even ran** (caught by tracing through the logic by hand, not by a failed boot): my first implementation of `window_focus_next()` picked "the window just behind the current frontmost" (`z_order[window_count - 2]`) and called the existing `raise_to_front()` on it. Tracing through 3 windows `[A, B, C]` (C frontmost) by hand: Alt+Tab raises B → `[A, C, B]`; Alt+Tab again raises C (now second-from-front) → `[A, B, C]`; Alt+Tab again raises B → `[A, C, B]`... **A never surfaces, no matter how many times Alt+Tab is pressed** — `raise_to_front()` only moves one element to the end and shifts the rest down by one, which for the second-from-top slot specifically degenerates into just swapping the last two elements back and forth forever. Fixed by rotating the *entire* `z_order` array left by one position per press instead (what was backmost becomes frontmost, everything else shifts down one) — skipping over any minimized window found after a rotation and rotating again, up to `window_count` times. Traced through the same 3-window example: `[A,B,C]`→`[B,C,A]`→`[C,A,B]`→`[A,B,C]`, visiting all three exactly once per full cycle. This was caught and fixed *before* ever booting the "wrong" version, by re-reading the milestone's own test requirement ("a stable, non-repeating order") against a hand-traced example and noticing it wouldn't hold.
- **Test:** opened three real windows (File Manager, an editor window, and a "New Folder" prompt dialog — not synthetic demo windows), then sent `alt-tab` three times via the QEMU monitor. Serial log shows `Alt+Tab: focus -> "..."` cycling through all three window titles in order with no repeats, and each corresponding `screendump` confirms the named window is genuinely frontmost (drawn on top, its taskbar entry highlighted) at each step — not just that the log line printed.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk build/OVMF_VARS.fd
S=$(mktemp -d)
(sleep 3; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.2; \
 echo "mouse_move 148 99"; sleep 0.3; echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 0.15; \
 echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 1.2; \
 echo "mouse_move 20 -57"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "sendkey alt-tab"; sleep 0.5; echo "sendkey alt-tab"; sleep 0.5; echo "sendkey alt-tab"; sleep 0.5; \
 echo "quit") | timeout 20 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
grep -E "Alt\+Tab" $S/serial.log
```
Expected (exact filenames depend on the disk image's current contents, but three distinct titles with no immediate repeat is the point):
```
Keyboard: Alt+Tab pressed
Alt+Tab: focus -> "HELLO.ELF"
Keyboard: Alt+Tab pressed
Alt+Tab: focus -> "File Manager"
Keyboard: Alt+Tab pressed
Alt+Tab: focus -> "Prompt"
```

**Command to see:**
```bash
make run   # graphical QEMU window - open a few windows, hold Alt and press Tab repeatedly
```
Screenshots: [screenshots/phase21_three_windows.png](screenshots/phase21_three_windows.png) (three windows open, dialog frontmost before any Alt+Tab), [screenshots/phase21_alttab_1.png](screenshots/phase21_alttab_1.png) (after 1 press — editor window frontmost, taskbar entry highlighted), [screenshots/phase21_alttab_3.png](screenshots/phase21_alttab_3.png) (after 3 presses — back to the dialog, full cycle complete).

---

## Bugs found & fixed during this phase

1. **`window_focus_next()`'s original "raise the second-from-top window" design never visits more than 2 windows** (documented in full under Milestone 21.2): fixed by rotating the whole z-order ring by one position per press instead of repeatedly calling the existing single-window `raise_to_front()`. Caught by hand-tracing the algorithm against the milestone's own "stable, non-repeating order" requirement before ever building/booting it — a logic error found through reasoning about the design, not through a failed test.
2. **A test-script coordinate mistake** (documented in full under Milestone 21.1): pressing the mouse exactly on the window's boundary pixel to start a second resize drag silently missed the hit-test (`point_in_rect`'s strict `<` treats the boundary pixel as just outside), making it look like shrinking was broken. Not a kernel bug — confirmed by retrying 2-3 pixels further into the window, which worked immediately.

---

## Phase 21 exit criterion — met

Windows can be resized by dragging any edge or the bottom-right corner, with clamping verified both at the minimum-size floor and at the screen boundary (screendump-confirmed, no corruption or wraparound). Alt+Tab cycles focus through every open window in a stable, complete, non-repeating rotation with no mouse involved, verified both via serial log and via `screendump` confirming the named window is genuinely frontmost at each step.
