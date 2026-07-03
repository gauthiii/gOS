# Phase 17 — Maximize & Polish (optional/stretch) — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — the sole milestone (17.1) landed cleanly, well within budget, on top of an unregressed Phase 16 taskbar.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Boot takes ~75-80 seconds to reach the interactive desktop (see [phase15.md](phase15.md)'s bug notes). Windows now have **three** titlebar buttons, left to right: a teal square maximize/restore toggle, an amber "_" minimize, and the red "X" close.

---

## Summary

Phase 17 is Track B's third and final planned phase — explicitly scoped as optional/stretch, gated on Phase 16 landing cleanly (it did; see [phase16.md](phase16.md)). It adds one milestone: a maximize/restore toggle that fills the screen (minus the taskbar) and returns to the exact prior geometry.

**Trigger mechanism** (clarified with the user before implementing): a titlebar toggle button, consistent with Phase 16's minimize-button pattern, rather than double-click-to-maximize. It sits immediately left of the minimize button, giving every window a real three-button titlebar for the first time.

Files touched: `kernel/include/window.h`, `kernel/src/window.c`, `kernel/src/start.c` (test hook only).

---

## Milestone 17.1: Maximize/restore toggle

- **What was done:** Added `maximized` plus `restore_x/y/w/h` fields to `struct window`; a new `window_maximize_toggle()`/`window_is_maximized()` API (`kernel/src/window.c`); a third titlebar button (`WINDOW_MAXIMIZE_BUTTON_SIZE`/`_GAP` in `window.h`) drawn left of minimize, hit-tested in `window_system_update()` alongside the other two. The button glyph switches between a single hollow square (maximize) and two overlapping offset squares (restore) so the two states are visually distinguishable at a glance, matching the existing close/minimize convention of simple glyphs over text.
- **Toggle semantics:** the first click saves the window's *current* x/y/w/h into `restore_x/y/w/h`, then sets `x=0, y=0, w=fb_width()`, and `h = fb_height() - WINDOW_TITLEBAR_HEIGHT - TASKBAR_HEIGHT` (the body height, since the titlebar is drawn above it and the taskbar strip must stay clear). The second click reads `restore_x/y/w/h` back verbatim and clears the flag — an exact round-trip by construction, not by re-deriving geometry from anything else.
- **Interaction with dragging:** a maximized window can no longer be dragged by its titlebar (checked in `window_system_update()` before arming `dragging_window`) — dragging a screen-filling window doesn't mean anything until it's restored first, and this avoids a maximized window silently developing a geometry `restore_*` never agreed to.
- **Interaction with minimize (Phase 16):** minimizing/restoring only ever touches the `minimized` flag, never `x/y/w/h` — so a maximized window that gets minimized and then restored via the taskbar comes back still maximized, at the same full-screen geometry, with no special-casing needed.
- **Test (both an exact numeric proof and a live visual one):**
  1. *Numeric round-trip:* a `GOS_TEST_MAXIMIZE_ROUNDTRIP` debug build creates a window at an arbitrary, deliberately non-round position (137, 211, 333×141 — chosen specifically to not coincide with any other constant in the codebase, so a bug that accidentally hard-codes a "reset to default" value would be caught), logs geometry before maximize, after maximize, and after restore, and asserts the restored values match the originals exactly.
  2. *Live visual:* opened the Text Editor via the File Manager, clicked the new maximize button — confirmed via `screendump` that the window fills the entire screen down to (but not overlapping) the taskbar, with the wallpaper and File Manager both fully hidden underneath. Clicked the button again (now showing the restore glyph) — confirmed via a second `screendump` that the window returns to its exact prior position and size, on top of the File Manager exactly as before.
  3. *Independent cross-check:* `mtools`' `mdir`/`mtype` confirmed the underlying disk image (`PERSIST.TXT`, 35 bytes) was completely untouched by this test — this exercises only in-memory window geometry, no filesystem writes.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_MAXIMIZE_ROUNDTRIP"
timeout 100 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/p17_roundtrip.log -monitor none
grep "TEST:" /tmp/p17_roundtrip.log
```
Expected:
```
TEST: before maximize x=137 y=211 w=333 h=141
TEST: after maximize x=0 y=0 w=1280 h=748 maximized=1
TEST: after restore x=137 y=211 w=333 h=141 maximized=0 (exact round-trip - OK)
```
(1280×800 is this build's framebuffer resolution; `748 = 800 - 24 (titlebar) - 28 (taskbar)`. Rebuild without the `-D` flag afterward for normal use.)

**Command to see:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk
S=$(mktemp -d)
(sleep 85; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.5; \
 echo "mouse_move 148 99"; sleep 0.3; echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 0.15; \
 echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 1.5; \
 echo "screendump $S/before.ppm"; sleep 0.5; \
 echo "mouse_move 428 -19"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/maximized.ppm"; sleep 0.5; \
 echo "mouse_move 600 -120"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1; \
 echo "screendump $S/restored.ppm"; sleep 0.5; echo "quit") | timeout 130 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
open $S/before.ppm $S/maximized.ppm $S/restored.ppm
```
For live interactive use instead of a scripted screendump:
```bash
make debug   # or: make run   # real graphical QEMU window; click the teal square button on any titlebar
```
Screenshots: [screenshots/phase17_before_max.png](screenshots/phase17_before_max.png) (editor at its normal 380×220 size, three-button titlebar visible), [screenshots/phase17_maximized.png](screenshots/phase17_maximized.png) (fills the screen exactly down to the taskbar, wallpaper/File Manager fully hidden), [screenshots/phase17_restored.png](screenshots/phase17_restored.png) (back to its exact original position and size, on top of the File Manager).

---

## Bugs found & fixed during this phase

**None.** The implementation passed both the numeric round-trip test and the live visual test on the first attempt, with no regressions to Phase 16's minimize/taskbar behavior (verified by the same File-Manager-plus-editor scenario used throughout Phase 16's testing, and by the disk image remaining byte-identical throughout — confirmed via independent `mtools` `mdir`/`mtype`, not just the OS's own read-back).

---

## Phase 17 exit criterion — met

Maximize/restore round-trips geometry exactly, proven both numerically (a debug build logging x/y/w/h at all three points and asserting equality) and visually (screendump before, during, and after). No regressions surfaced in Phase 16's taskbar/minimize state, so per the plan's own risk note this phase did not need to be cut.

---

## Track B status

With Phase 17 complete, **all of Track B (Phases 15-17) is done**: real arrow cursor, gradient/BMP wallpaper, window minimize, persistent taskbar restore/focus, and maximize/restore. Per [project-plan-2.md](project-plan-2.md)'s Milestone 14.0, the README's "Further Reading"/architecture description should get its final post-Track-B pass reflecting the complete v2 feature set — done as part of this same update.
