# Phase 16 — Window Close, Minimize & Taskbar — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Boot takes ~75-80 seconds to reach the interactive desktop (see [phase15.md](phase15.md)'s bug notes — the boot-time regression demos and stress test run first). Windows now have **two** titlebar buttons: an amber "_" minimize button and the existing red "X" close button.

---

## Summary

Phase 16 is Track B's second phase. Scope per [project-plan-2.md](project-plan-2.md): real window teardown on close, a minimize flag + trigger, and a persistent taskbar with restore/focus click handling.

**Milestone 16.1 turned out to be already done.** `window_close()` was fully hardened back in Phase 13.6 (Finding #13.6: stale-window dispatch) — it already clears `buttons[]`, callbacks, and `textbox_buffer` on every close. The plan's wording ("free any owned heap allocations e.g. `textbox_buffer` via `kfree`") assumed `textbox_buffer` was a heap pointer; it's actually a **fixed array embedded in `struct window`** (`kernel/include/window.h`), not `kmalloc`'d — there is no window-owned heap allocation anywhere in the codebase (confirmed via `grep kmalloc kernel/src/fm.c kernel/src/editor.c` — no hits). So there was nothing new to free. This milestone's real work was writing the regression test the plan asked for, to prove that with a measurement instead of just reading the code.

**Milestone 16.2** added a `minimized` flag to `struct window`, a titlebar minimize button (clarified with the user: titlebar button, not a keyboard shortcut, for discoverability and to leave room for a Phase 17 maximize button in the same row), and updated every draw/hit-test path (`window_composite`, `window_at_point`, `window_point_hits_any`, keyboard routing) to treat a minimized window as invisible-but-alive.

**Milestone 16.3** updated the taskbar to distinguish a minimized entry (dimmed background/text) from a visible one, and to restore-then-focus a minimized window's entry instead of just focusing it.

Files touched: `kernel/include/window.h`, `kernel/src/window.c`, `kernel/src/taskbar.c`, `kernel/include/heap.h`, `kernel/src/heap.c`, `kernel/src/start.c` (test hook only).

---

## Milestone 16.1: Real window teardown on close

- **What was done:** Verified (not re-implemented) that `window_close()` already leaves a slot genuinely inert. Added `heap_free_bytes()` (`kernel/src/heap.c`/`heap.h`) — sums the payload size of every free block — so the plan's requested regression test could use a real measurement instead of code review alone.
- **The actual test:** A `GOS_TEST_WINDOW_TEARDOWN_LEAK` debug build creates a window with a textbox, fills it with 30 bytes of content, closes it, and repeats 20 times, checking `heap_free_bytes()` against a pre-loop baseline after every single iteration (not just at the end — catching a leak on iteration 3 that happened to net out to zero by iteration 20 would otherwise slip through).
- **Result:** `heap_free_bytes()` was **3,341,040** both before the loop and after all 20 iterations, with zero deviation at any intermediate point. No leak — expected, since `struct window` has no heap-owned fields to leak in the first place, and the fixed-size fields were already fully cleared by Phase 13.6.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_WINDOW_TEARDOWN_LEAK"
timeout 100 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/p16_leak.log -monitor none
grep -A1 "TEST: heap_free_bytes" /tmp/p16_leak.log
```
Expected:
```
TEST: heap_free_bytes() baseline = 3341040
TEST: after 20 open/close cycles, heap_free_bytes() = 3341040 (matches baseline every iteration - no leak)
```
(Rebuild without the `-D` flag afterward for normal use — the test loop only runs in this debug build.)

**Command to see:** Non-graphical (a heap-accounting regression, not a rendered scenario). Confirm via `make debug` on a normal build that opening/closing windows (e.g. the File Manager, repeatedly, or the text editor) behaves normally with no visible degradation.

---

## Milestone 16.2: Minimized window state

- **What was done:** Added `int minimized` to `struct window`; `window_minimize()`/`window_restore()`/`window_is_minimized()` (`kernel/src/window.c`); a new titlebar minimize button (`WINDOW_MINIMIZE_BUTTON_SIZE`/`_GAP` in `window.h`) drawn immediately left of the existing close button, hit-tested in `window_system_update()` alongside the close-button check.
- Minimized windows are skipped by `window_composite()` (not drawn), `window_at_point()` and `window_point_hits_any()` (can't be clicked through), and the keyboard-routing block (a minimized-but-still-frontmost window's textbox doesn't silently eat keystrokes). State — buttons, callbacks, and textbox contents — is left completely untouched by minimize/restore; only the flag changes.
- **Test:** Opened `PERSIST.TXT` in the Text Editor via the File Manager, typed a marker string (`" unsaved"`) via real simulated keystrokes (not pre-existing file content — confirmed by an independent `mtype`/`xxd` read of the actual disk image showing the base file unchanged throughout, since this test never triggers Ctrl+S), clicked the minimize button, and confirmed via `screendump` that the editor window disappears entirely while the File Manager remains. Restoring via the taskbar (Milestone 16.3) confirmed the marker text was still present — proving state survives a minimize/restore cycle, not just that the window can be hidden.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk
S=$(mktemp -d)
(sleep 85; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.5; \
 echo "mouse_move 148 99"; sleep 0.3; echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 0.15; \
 echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 1.5; \
 echo "sendkey spc"; sleep 0.6; echo "sendkey u"; sleep 0.6; echo "sendkey n"; sleep 0.6; \
 echo "sendkey s"; sleep 0.6; echo "sendkey a"; sleep 0.6; echo "sendkey v"; sleep 0.6; \
 echo "sendkey e"; sleep 0.6; echo "sendkey d"; sleep 1; \
 echo "mouse_move 448 -19"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/minimized.ppm"; sleep 0.5; echo "quit") | timeout 140 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
mtype -i disk_images/gos_disk.img ::PERSIST.TXT   # independent check: unchanged, no save happened
open $S/minimized.ppm   # only File Manager visible; editor gone
```
Expected: `mtype` shows the original 35-byte content unchanged (proving the marker text lived only in the editor's in-memory textbox, a genuine "unsaved edit"), and the screendump shows the editor window fully gone from the desktop.

**Command to see:**
```bash
make run   # graphical QEMU window; open a file in the editor, click the amber "_" button
```
Screenshots: [screenshots/phase16_16_2_before_minimize.png](screenshots/phase16_16_2_before_minimize.png) (editor open, "unsaved" marker text visible), [screenshots/phase16_16_2_minimized.png](screenshots/phase16_16_2_minimized.png) (editor gone, File Manager unaffected, taskbar entry dimmed).

---

## Milestone 16.3: Persistent taskbar

- **What was done:** `taskbar_update()` now checks `window_is_minimized()` before deciding what a click does: a minimized entry calls `window_restore()` then `window_focus()`; an already-visible entry just calls `window_focus()` as before (unchanged behavior). `taskbar_render()` now computes "frontmost" as *visible and topmost* (a minimized window can't be the highlighted entry even if it's technically highest in z-order) and draws minimized entries with a dimmed background/foreground so they're visually distinguishable from open-but-unfocused ones.
- **Test (3 windows, 2 minimized, both restored):** Opened the File Manager, opened the Text Editor on `PERSIST.TXT`, and opened the "New Folder" dialog (a third, independent window) — confirmed all three appear in the taskbar. Minimized the dialog, then the editor, confirming each disappears from the screen (not just visually stacked-behind) while its taskbar entry dims and the other two remain correctly labeled. Clicked each minimized entry in turn: **both windows returned to their exact original geometry** (300,120 for the editor, 160,120 for the dialog — verified because neither was ever dragged, so any position drift would be visible) and each became frontmost (topmost in the compositor's draw order) after its click.
- **A test-script bug I initially mistook for a kernel bug (worth documenting):** My first attempt computed taskbar entry x-positions once and reused them for both restore clicks. That's wrong — clicking *any* taskbar entry calls `window_focus()` → `raise_to_front()`, which changes the z-order, which changes every subsequent slot's x-position (`entry_x(slot) = 4 + slot*(120+4)`, and `slot` is a z-order index, not a stable per-window ID). My second click landed on the wrong (already-frontmost) entry as a result — restoring/re-focusing the same window twice instead of the one I intended. This is expected, correct taskbar behavior (matches how real desktop taskbars reorder on focus) — the bug was in my test math, not the kernel. Fixed by recomputing each click's target from the taskbar order visible in the *previous* screendump rather than assuming a fixed slot per window.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk
S=$(mktemp -d)
(sleep 85; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.5; \
 echo "mouse_move 148 99"; sleep 0.3; echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 0.15; \
 echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 1.5; \
 echo "mouse_move 20 -57"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "mouse_move 200 30"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "mouse_move 220 0"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/both_min.ppm"; sleep 0.5; \
 echo "mouse_move -328 662"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/editor_back.ppm"; sleep 0.5; \
 echo "mouse_move -124 0"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/dialog_back.ppm"; sleep 0.5; echo "quit") | timeout 150 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
mdir -i disk_images/gos_disk.img ::   # independent check: still exactly 2 files, unmodified
open $S/both_min.ppm $S/editor_back.ppm $S/dialog_back.ppm
```
Expected: `both_min.ppm` shows only the File Manager, taskbar lists 3 dimmed-except-FM entries; `editor_back.ppm` shows the editor restored at (300,120) on top; `dialog_back.ppm` shows the dialog restored at (160,120) on top of the editor.

**Command to see:**
```bash
make run   # open File Manager + Text Editor + a New Folder/New File dialog, minimize a couple,
           # click their dimmed taskbar entries to bring them back
```
Screenshots: [screenshots/phase16_three_open.png](screenshots/phase16_three_open.png) (3 windows, dialog frontmost), [screenshots/phase16_dialog_minimized.png](screenshots/phase16_dialog_minimized.png) (dialog hidden, its entry dimmed), [screenshots/phase16_three_minimized_two.png](screenshots/phase16_three_minimized_two.png) (2 of 3 minimized, only File Manager visible), [screenshots/phase16_editor_restored_via_taskbar.png](screenshots/phase16_editor_restored_via_taskbar.png) (editor restored to its exact original position and focused), [screenshots/phase16_dialog_restored_via_taskbar.png](screenshots/phase16_dialog_restored_via_taskbar.png) (dialog also restored, now frontmost on top of the editor).

---

## Bugs found & fixed during this phase

**None in the shipped kernel code.** The one real issue encountered was in my own test script (documented above under Milestone 16.3) — a stale assumption that taskbar slot positions are stable per-window, when they're actually z-order-relative and shift on every focus change. No kernel code was affected; the fix was entirely in how the test was constructed (recompute click targets from the immediately-preceding screendump rather than a fixed slot map).

One operational note: during interactive testing I twice wrote a test edit to the shared `PERSIST.TXT` fixture on `disk_images/gos_disk.img` via a genuine Ctrl+S save (used to verify the keyboard pipeline was working before I found my screendump-timing false alarm below), and separately made a restore mistake dropping its trailing newline (34 bytes instead of 35). Both were caught by comparing an independent `mtools`/`mtype`/`xxd` read against the expected content and fixed by writing the exact original 35 bytes (`"This file persists across reboots!\n"`) back via `mcopy`, then reconfirmed via a fresh boot reporting `Editor: opened "PERSIST.TXT" (35 bytes)` — the same figure every prior phase doc back to Phase 9 has recorded. The disk image is unmodified as of this report.

A related false alarm, not a bug: an early typing test (`sendkey x` then `screendump` 0.5s later) showed no typed character on screen, which looked like keyboard-to-editor routing was broken. A same-sequence test with `sendkey ctrl-s` immediately after proved the character *had* been appended (file grew from 35 to 36 bytes on save) — the screendump was just taken before that frame's `fb_flip()` had run. Re-running with a 1-second wait before the screendump showed the character correctly. No code changes were needed; this is a test-timing artifact, not a rendering bug.

---

## Phase 16 exit criterion — met

Close (already hardened in Phase 13.6, now regression-tested with a real heap measurement), minimize, and taskbar restore/focus are all functional in QEMU, verified with real simulated mouse/keyboard input and independent `mtools` cross-checks of the underlying disk image, with **no heap growth across repeated open/close cycles** (3,341,040 bytes free before and after 20 iterations, matching every intermediate check).
