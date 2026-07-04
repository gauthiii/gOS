# Phase 15 — Cursor & Wallpaper — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed, including the 15.3 stretch goal (BMP image wallpaper loaded off FAT32).

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Builds the kernel, packages it with Limine into a bootable ISO, seeds `disk_images/gos_disk.img` (now including `WALLPAPR.BMP`), and launches a real graphical QEMU window. Note: boot takes ~75 seconds to reach the interactive desktop (the Phase 6/8 regression demos and stress test run first) — the wallpaper appears when the desktop loop starts.

---

## Summary

Phase 15 is the first Track B (new features) phase, unblocked by Track A's completion in Phase 14. Three changes:

1. **Milestone 15.1** — the old 10x10 white-square cursor was replaced with a classic 12x19 arrow sprite (black outline, white fill, transparent background), and cursor drawing moved from `window_composite()` to the compositor's *true* top layer: the main loop now calls `mouse_draw_cursor()` after `taskbar_render()`, so the cursor is never occluded by the taskbar (it previously was, since the taskbar drew after the compositor).
2. **Milestone 15.2** — new `kernel/src/wallpaper.c` module; the desktop background is now a per-scanline vertical gradient (deep blue → teal) instead of v1's solid fill + horizontal bands.
3. **Milestone 15.3 (stretch)** — a minimal hand-rolled BMP loader (24bpp, uncompressed, bottom-up only — no PNG/JPEG). `tools/make_wallpaper.py` generates a 1280x800 dusk-mountains BMP; the Makefile's disk-seed recipe `mcopy`s it onto the FAT32 image as `WALLPAPR.BMP`; `wallpaper_init()` loads it at boot via the post-Phase-12/13 hardened FAT32 read path and blits it (centered/clipped if size ≠ screen) each frame. Missing or malformed file falls back gracefully to the 15.2 gradient.

Files touched: `kernel/src/mouse.c`, `kernel/src/window.c`, `kernel/src/start.c`, `kernel/src/desktop.c`, new `kernel/src/wallpaper.c` + `kernel/include/wallpaper.h`, `Makefile`, new `tools/make_wallpaper.py` + `tools/wallpaper.bmp`.

**Heads-up:** the disk-seed recipe change intentionally triggers Finding #21's staleness mechanism — the first `make run`/`make disk` after pulling this phase deletes and rebuilds `disk_images/gos_disk.img` (existing files on the image are lost, by design, so the image picks up `WALLPAPR.BMP`).

---

## Milestone 15.1: Real arrow-shaped mouse cursor

- **What was done:** `mouse_draw_cursor()` (`kernel/src/mouse.c`) drew a 10x10 white square with a black outline. It was also called from inside `window_composite()` (`kernel/src/window.c`), which runs *before* `taskbar_render()` in the main loop — so the taskbar drew over the cursor whenever the mouse was in the bottom 28px of the screen.
- **The actual fix:** A 12x19 arrow bitmap (`'X'` black outline / `'W'` white fill / `'.'` transparent) as a static char-array in `mouse.c`, rendered pixel-by-pixel with transparency (underlying desktop/window/taskbar pixels show through the sprite's empty corners); hotspot is the tip at (0,0), matching where clicks hit-test. The draw call moved out of `window_composite()` into the main loop (`kernel/src/start.c`), after `taskbar_render()` — the genuine top layer. `fb_put_pixel`'s bounds check clips the sprite cleanly at screen edges.
- **Test:** Booted headless, screendumped, injected `mouse_move 200 -100` then `mouse_move -350 250` via the QEMU monitor, screendumping after each move. The arrow rendered at each expected position ((640,400) → (840,300) → (490,550)) with no leftover pixels at the previous frame's position (full-frame redraw + back-buffer flip). A second run parked the cursor over the File Manager window body and then over the taskbar strip — the arrow renders on top of both.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk
S=$(mktemp -d)
(sleep 80; echo "screendump $S/a.ppm"; sleep 1; echo "mouse_move 200 -100"; sleep 1; \
 echo "screendump $S/b.ppm"; sleep 1; echo "quit") | timeout 110 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
open $S/a.ppm $S/b.ppm   # arrow at screen center in a.ppm; moved right+up, no trail, in b.ppm
```
Expected: `a.ppm` shows the white arrow with black outline at (640,400); `b.ppm` shows it at (840,300) with no leftover cursor pixels at the old position.

**Command to see:**
```bash
make run   # graphical QEMU window; move the mouse across windows, desktop, and the taskbar
```
Screenshots: [screenshots/phase15_cursor_a.png](screenshots/phase15_cursor_a.png), [screenshots/phase15_cursor_b.png](screenshots/phase15_cursor_b.png) (two positions, no trails), [screenshots/phase15_win.png](screenshots/phase15_win.png) (cursor on top of a window), [screenshots/phase15_taskbar.png](screenshots/phase15_taskbar.png) (cursor on top of the taskbar — the case the old draw order got wrong).

---

## Milestone 15.2: Desktop wallpaper layer (gradient)

- **What was done:** `desktop_render()` (`kernel/src/desktop.c`) painted a solid fill plus darker horizontal bands every 40px.
- **The actual fix:** New `wallpaper_render()` (`kernel/src/wallpaper.c`), called first in `desktop_render()` before any windows composite. The base layer is a per-scanline vertical gradient interpolating (18,42,66) at the top to (32,120,130) at the bottom, drawn as 800 one-pixel-tall `fb_draw_rect` scanlines.
- **Test:** Booted against a scratch FAT32 image with *no* `WALLPAPR.BMP` (so the gradient path, not the image path, renders — see 15.3's fallback). Screendump shows the full-screen gradient with no banding artifacts, the Files icon and taskbar on top, and no bleed-through at window edges (the File Manager window in the 15.3 screenshots sits on the wallpaper with clean borders).

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso
S=$(mktemp -d); truncate -s 64M $S/empty.img && mformat -F -i $S/empty.img -v GOSDISK ::
(sleep 80; echo "screendump $S/gradient.ppm"; sleep 1; echo "quit") | timeout 110 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=$S/empty.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
grep Wallpaper $S/serial.log && open $S/gradient.ppm
```
Expected serial line: `Wallpaper: WALLPAPR.BMP not found - using gradient fallback`, and the screendump shows the blue→teal vertical gradient full-screen.

**Command to see:**
```bash
make run   # with the normal (seeded) disk image the BMP renders instead; to see the
           # gradient live, temporarily delete WALLPAPR.BMP via the File Manager and reboot
```
Screenshot: [screenshots/phase15_gradient_fallback.png](screenshots/phase15_gradient_fallback.png).

---

## Milestone 15.3 (stretch): BMP image wallpaper off FAT32

- **What was done:** No image wallpaper existed; this milestone was gated on the Phase 12/13 FAT32 hardening (underflow fix, chain-walk cycle bounds) since it reads a ~3 MB file through those exact paths at every boot.
- **The actual fix:**
  - `tools/make_wallpaper.py` generates `tools/wallpaper.bmp` — 1280x800 (exactly the framebuffer size Limine provides), 24bpp, uncompressed, bottom-up: the only BMP variant the kernel loader accepts, by design.
  - The Makefile's `DISK_RECIPE` now `mcopy`s it onto the image as `::WALLPAPR.BMP` (8.3 name, matching the FAT32 driver), and `$(DISK_IMG)` depends on the BMP (which itself rebuilds from the script). Editing the recipe changed `DISK_RECIPE_HASH`, so Finding #21's staleness check automatically rebuilt existing images — the mechanism working exactly as built.
  - `wallpaper_init()` (called in `start.c` once heap + FAT32 are up): `fat_resolve_path` for the size, `kmalloc` a file buffer, `fat_read_file`, then a strict header parse (`BM` magic, BITMAPINFOHEADER, 1 plane, 24bpp, compression 0, positive dims, pixel-data length check against the actual file size). Rows are converted once at load time — bottom-up→top-down flip, BGR→`fb_make_color()` native pixel format — into a `uint32_t` pixel buffer, and the raw file buffer is `kfree`d. Any parse failure logs a specific reason and leaves the gradient active.
  - `wallpaper_render()` blits the converted buffer (centered and clipped if its size ever differs from the screen; gradient letterboxes any uncovered area).
- **Test (three boots + one independent cross-check):**
  1. *Normal seeded image:* serial logs `Wallpaper: loaded WALLPAPR.BMP (1280x800, 24bpp BMP)`; screendump shows the dusk-mountains scene full-screen with icon/taskbar/cursor on top, and the File Manager listing `WALLPAPR.BMP` in the root.
  2. *Independent cross-check (not the OS verifying itself):* a host-side Python script compared ~2000 randomly sampled pixels of the QEMU screendump against the source `tools/wallpaper.bmp` (decoding the BMP independently) — **0 mismatches**, i.e. the kernel's BMP decode + blit is byte-exact end-to-end. `mdir` (mtools) independently confirmed the file on the image before boot.
  3. *Missing file:* scratch image with no `WALLPAPR.BMP` → `not found - using gradient fallback`, gradient renders, no crash (15.2's screenshot).
  4. *Malformed file:* copied the seeded image and overwrote the BMP's `BM` magic bytes with `XX` directly in the image (hex-edit via `dd`) → serial logs `Wallpaper: missing 'BM' magic` then `load failed - using gradient fallback`, boot continues to a fully working desktop on the gradient.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk
mdir -i disk_images/gos_disk.img ::   # independent (mtools) check: WALLPAPR.BMP, 3072054 bytes
S=$(mktemp -d)
(sleep 80; echo "screendump $S/wall.ppm"; sleep 1; echo "quit") | timeout 110 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
grep Wallpaper $S/serial.log
```
Expected: `Wallpaper: loaded WALLPAPR.BMP (1280x800, 24bpp BMP)` and `wall.ppm` matches `tools/wallpaper.bmp` pixel-for-pixel outside the icon/taskbar/cursor regions.

**Command to see:**
```bash
make run   # graphical QEMU window: dusk-mountains wallpaper behind the whole desktop
```
Screenshots: [screenshots/phase15_desktop.png](screenshots/phase15_desktop.png) (wallpaper + cursor), [screenshots/phase15_win.png](screenshots/phase15_win.png) (File Manager over the wallpaper, listing WALLPAPR.BMP), [screenshots/phase15_badbmp_fallback.png](screenshots/phase15_badbmp_fallback.png) (corrupted-BMP boot on the gradient).

---

## Bugs found & fixed during this phase

**None in the new code** — all three milestones passed their tests on the first post-implementation boot. Two pre-existing behaviors worth recording:

1. **Latent draw-order flaw fixed by design in 15.1 (not a new bug, but a real one):** the v1 cursor was drawn inside `window_composite()`, which the main loop runs *before* `taskbar_render()` — so the cursor disappeared under the taskbar whenever the mouse entered the bottom 28px. Symptom: cursor vanishes near the screen bottom. Diagnosis: call-order inspection of the main loop. Fix: cursor drawing moved to the main loop after `taskbar_render()` (verified by [screenshots/phase15_taskbar.png](screenshots/phase15_taskbar.png), cursor visibly on top of the taskbar).
2. **Test-procedure gotcha (no code change):** the first headless test run screendumped at t=32s and captured only the boot-time regression demos — the desktop isn't reached until ~75s because the Phase 8 stress test (1500+ ATA writes) runs at every boot. All test commands in this doc use an 80-second wait for this reason. If boot-time keeps growing, gating `stress_test()` behind a debug flag is a good Phase 16 candidate.

---

## Phase 15 exit criterion — met

Cursor and wallpaper render correctly in QEMU (screendump-verified, plus an independent host-side pixel comparison against the source image); the 15.3 stretch goal landed without eating into Phase 16's budget, with graceful fallback verified for both missing and corrupted wallpaper files.
