# Patch v2 — Desktop Wallpaper Picker, Taskbar Clock Margin & Wallpaper Mapping Fix — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — four rounds of user-requested changes after Phase 24, two real bugs found and fixed during testing (a menu off-screen overflow, and a wallpaper label/file mismatch), plus a cleanup pass removing now-unused source assets.

This patch is not a numbered project-plan phase — it's a set of ad hoc fixes/enhancements requested directly after Phase 24 shipped, to the same testing/documentation standard as the numbered phases. It's grouped into one doc (rather than four) because each round built directly on the previous one's code.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Right-click anywhere on the empty desktop to get a wallpaper picker menu (Gradient / Default / Custom / Mac / Windows). The taskbar clock no longer touches the screen's right edge.

---

## Summary

Four rounds of changes, in the order they were requested:

1. **Discoverable wallpaper control.** The user reported "I only see a gradient, is the toggle broken?" Investigation showed it wasn't broken — F2 (Milestone 22.3) had correctly persisted `gradient_forced=1` to `GOS.CFG` from an earlier testing session, and there was no visible UI to change it back, only an undocumented keyboard shortcut. Fixed by adding a right-click desktop context menu (user's choice over a 4th icon or a text hint, via `AskUserQuestion`), and clearing the stale test-session `GOS.CFG` from the disk image.
2. **Multi-wallpaper support.** The user added 3 JPEG images to `tools/` and asked for them as additional background options. gOS has no JPEG decoder (its BMP loader is hand-rolled, 24bpp-uncompressed-only, by deliberate Phase-15.3 scope choice) and writing one from scratch is a large, disproportionate undertaking for a wallpaper picker. Converted the 3 JPEGs to the exact BMP variant gOS's decoder accepts (bottom-up, 24bpp, uncompressed) instead, and extended the single gradient/BMP toggle into a proper 5-option menu, persisted via a `GOS.CFG` format version bump.
3. **Taskbar clock margin.** The clock's own display box was narrower than its text, and its right margin was too small — together making it look like it touched the screen's corner. Fixed both.
4. **Cleanup + mapping fix.** Removed the now-unused source JPEGs (only the converted BMPs are ever loaded), and fixed a real bug: the "Mac" and "Custom" menu labels had ended up pointing at each other's bundled image.

Files touched (new): none. Files touched (modified): `kernel/include/wallpaper.h`, `kernel/src/wallpaper.c` (single boolean → 5-option selection API), `kernel/include/settings.h`, `kernel/src/settings.c` (`GOS.CFG` format version 1 → 2), `kernel/src/desktop.c` (right-click context menu, off-screen clamping), `kernel/src/taskbar.c` (clock width/margin), `kernel/src/start.c` (one stale comment), `Makefile` (bundle `CUSTOM.BMP`/`MAC.BMP`/`WINDOWS.BMP`). Files removed: `tools/custom.jpg`, `tools/mac.jpg`, `tools/windows.jpg`. Files added (binary assets, converted from the removed JPEGs): `tools/custom.bmp`, `tools/mac.bmp`, `tools/windows.bmp`.

---

## Round 1: Discoverable wallpaper toggle

- **What was done:** Confirmed via `xxd` on the disk image's `GOS.CFG` that `gradient_forced=1` was genuinely persisted (not a bug — F2 had been pressed during earlier Phase 22/23/24 testing sessions against the same disk image the user was booting). Per the user's choice (`AskUserQuestion`: desktop right-click menu, over a 4th icon or a text hint), added right-click handling to `kernel/src/desktop.c`'s `desktop_update()`: `mouse_buttons() & MOUSE_RIGHT_BUTTON` was already defined and wired by the PS/2 driver (`kernel/src/mouse.c`) but completely unused anywhere in the window/desktop system, so it was free to claim. A right-click on empty desktop (gated by `!window_point_hits_any()`, the same guard the icons already use) opens a small menu at the click point; a left-click while it's open either dispatches the clicked row or dismisses it.
- **Test:** booted, right-clicked empty desktop, confirmed the menu appeared with a label reflecting current state; clicked it, confirmed the wallpaper switched and `Settings: saved GOS.CFG` appeared in the serial log; deleted the stale `GOS.CFG` from the disk image and confirmed a fresh boot defaults to the bundled BMP.
- **Result:** worked on the first attempt. `mdel -i disk_images/gos_disk.img ::GOS.CFG` plus a rebuild confirmed `make run` now boots straight into the bundled wallpaper.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
mdir -i disk_images/gos_disk.img :: | grep -i cfg   # check for a stale GOS.CFG from prior testing
mdel -i disk_images/gos_disk.img ::GOS.CFG 2>/dev/null || true
S=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "Wallpaper\|Settings" $S/serial.log
```
Expected: `Wallpaper: loaded WALLPAPR.BMP ...` and `Settings: GOS.CFG not found - using defaults`.

**Command to see:**
```bash
make run   # right-click empty desktop - a menu appears; click the entry to toggle
```

---

## Round 2: Multi-wallpaper support (JPEG → BMP, 5-option menu)

- **What was done:** Confirmed via `file tools/*.jpg` these were real baseline JPEGs (DCT/Huffman-coded), not something the existing `bmp_decode()` could ever read. Rather than writing a JPEG decoder (a large image-codec undertaking wildly disproportionate to "add wallpaper options," and against this project's consistent minimal-dependencies choices - e.g. Phase 15.3 explicitly scoped out PNG/JPEG for the same reason), converted each JPEG to a BMP via `sips -s format bmp`. This produced **top-down** BMPs (negative height field) - `bmp_decode()` only accepts **bottom-up** (positive height), a requirement inherited unchanged from Milestone 15.3. Wrote a small one-off Python script to reverse the pixel row order and flip the height field's sign, producing files byte-for-byte structurally identical in header shape to the existing `wallpaper.bmp`.
  - `kernel/src/wallpaper.c` was restructured around a small fixed table: `wallpaper_paths[]` / `wallpaper_labels[]`, index 0 = gradient (no file), 1-4 = bundled BMPs. `wallpaper_select(idx)` loads (freeing any previous buffer) or falls back to the gradient on failure; `wallpaper_init()` just calls `wallpaper_select(1)` (today's default), and `settings_load()` can immediately override it.
  - `kernel/src/settings.c`'s `GOS.CFG` record's `gradient_forced` boolean became `wallpaper_selection` (a 0-4 index); `SETTINGS_VERSION` bumped 1→2 so an old-format file is safely ignored (falls back to defaults) rather than misread.
  - `kernel/src/desktop.c`'s single-item menu became a 5-row menu, one per `WALLPAPER_OPTION_COUNT`, with the current selection highlighted and prefixed `>`.
  - `Makefile`'s `DISK_RECIPE` gained three more `mcopy` lines (`CUSTOM.BMP`/`MAC.BMP`/`WINDOWS.BMP`), and the recipe-hash mechanism (Finding #21) automatically forced a disk-image rebuild once the recipe text changed.
- **Test:** for each of the 3 new options, right-clicked the desktop, clicked the option, confirmed via serial log (`Wallpaper: loaded ....BMP`, `Settings: saved GOS.CFG`) and a screendump that the correct image rendered full-screen; rebooted fresh (separate QEMU process) and confirmed the persisted selection (`wallpaper_selection=2`) reloaded the same image with no interaction.
- **Result:** all 3 converted images rendered correctly and at full quality (no visible compression artifacts introduced by the conversion beyond what the original JPEGs already had). Persistence round-tripped exactly as Milestone 22.3's original mechanism did.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
# Verify every bundled BMP is the required bottom-up, 24bpp, uncompressed variant:
for f in wallpaper custom mac windows; do
  python3 -c "
import struct
d = open('tools/$f.bmp','rb').read()
h = struct.unpack_from('<i', d, 22)[0]
bpp = struct.unpack_from('<H', d, 28)[0]
comp = struct.unpack_from('<I', d, 30)[0]
print('$f.bmp:', 'h=%d (must be > 0)' % h, 'bpp=%d (must be 24)' % bpp, 'compression=%d (must be 0)' % comp)
assert h > 0 and bpp == 24 and comp == 0
"
done
```
Expected: all four print positive height, `bpp=24`, `compression=0` with no assertion error.

**Command to see:**
```bash
make run
# Right-click the desktop - the menu now lists Gradient, Default, Custom, Mac, Windows.
# Click each one in turn to see it render full-screen.
```
Screenshot: [screenshots/phase-patchv2_menu_5options.png](screenshots/phase-patchv2_menu_5options.png) (all 5 options listed, current selection highlighted).

---

## Round 3: Taskbar clock margin

- **What was done:** `kernel/src/taskbar.c`'s `CLOCK_WIDTH` was `60`, but the displayed text (`"HH:MM:SS"`, 8 characters at `FONT_WIDTH=8px` each) needs `64px` - the clip rectangle was already narrower than its own text before the right-margin was even applied, and the margin itself reused `ENTRY_GAP` (`4px`), far too tight against the true screen edge. Fixed both: `CLOCK_WIDTH` raised to `64`, and a new dedicated `CLOCK_RIGHT_MARGIN` (`14px`) replaces the reused `ENTRY_GAP`.
- **Test:** screendumped the taskbar before and after the fix.
- **Result:** the clock now sits with clear, deliberate breathing room from the corner instead of appearing to touch it.

**Command to test:** purely a rendering-geometry constant fix with no independently-assertable "correct" numeric output beyond the constants themselves - the test is the visual screendump below, cross-checked by eye against the screen's actual right edge.

**Command to see:**
```bash
make run   # observe the taskbar clock's right margin
```
Screenshot: [screenshots/phase-patchv2_clock_margin.png](screenshots/phase-patchv2_clock_margin.png).

---

## Round 4: Cleanup — remove source JPEGs, fix Mac/Custom mapping

- **What was done:**
  1. Deleted `tools/custom.jpg`, `tools/mac.jpg`, `tools/windows.jpg` (confirmed via `grep` that nothing in the Makefile, kernel source, or `tools/*.py` referenced them - only the converted `.bmp` siblings are ever loaded, so the `.jpg` originals were dead weight once the conversion was done).
  2. Fixed a real bug: the "Custom" and "Mac" menu labels had ended up pointing at the wrong bundled file relative to what their labels implied - selecting "Custom" loaded `CUSTOM.BMP` (which turned out to contain the abstract macOS-style wallpaper) and selecting "Mac" loaded `MAC.BMP` (which contained the custom neon-portrait image). Swapped `wallpaper_paths[2]`/`wallpaper_paths[3]` in `kernel/src/wallpaper.c` so the labels now point at the file that actually matches what a user picking that label would expect, without renaming the on-disk files themselves or touching the Makefile.
- **Test:** rebuilt, right-clicked the desktop, selected "Custom" and confirmed (via serial log `Wallpaper: loaded MAC.BMP` and a screendump) it now shows the neon-portrait image; selected "Mac" and confirmed (`Wallpaper: loaded CUSTOM.BMP`) it now shows the abstract wallpaper. Re-ran the full `make diagnostic` regression suite (150 file cycles + 300 window cycles) to confirm the settings-format and wallpaper-table changes across all four rounds didn't regress anything.
- **Result:** both selections now show the expected image, confirmed by screendump. `make diagnostic` still reports `Stress test: PASS`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
ls tools/*.jpg 2>&1   # expect "no matches found" / No such file or directory
grep -rn "\.jpg" Makefile kernel/ tools/*.py 2>/dev/null   # expect no output - nothing references the removed files
```
Expected: no `.jpg` files remain in `tools/`, and no build/source file references them.

**Command to see:**
```bash
make run
# Right-click the desktop, click "Custom" - the neon-portrait image appears.
# Right-click again, click "Mac" - the abstract wallpaper appears.
```
Screenshots: [screenshots/phase-patchv2_custom_correct.png](screenshots/phase-patchv2_custom_correct.png) ("Custom" now showing the neon portrait), [screenshots/phase-patchv2_mac_correct.png](screenshots/phase-patchv2_mac_correct.png) ("Mac" now showing the abstract wallpaper), [screenshots/phase-patchv2_windows_correct.png](screenshots/phase-patchv2_windows_correct.png) ("Windows" showing the classic Bliss-style wallpaper, unaffected by the swap), [screenshots/phase-patchv2_default_boot.png](screenshots/phase-patchv2_default_boot.png) (a completely fresh boot, no stale `GOS.CFG`, defaulting to the bundled wallpaper).

---

## Bugs found & fixed during this phase

**Bug 1 — context menu could render partly off-screen (real bug, fixed).**
- **Symptom:** right-clicking near the bottom-right of the screen opened a 5-row menu whose last row(s) were drawn past the visible screen area (and, worse, behind/under the taskbar - unclickable there even if visible).
- **Diagnosis:** the menu was positioned directly at the click coordinate with no bounds checking against `fb_width()`/`fb_height()` or the taskbar's height.
- **Fix:** `desktop_update()`'s right-click handler now clamps `menu_x`/`menu_y` so the full `MENU_ITEM_W × (MENU_ITEM_H * WALLPAPER_OPTION_COUNT)` menu rectangle stays within the screen and above the taskbar, falling back to `0` if the clamped position would still be negative (a screen smaller than the menu itself, not a realistic case but handled defensively).

**Bug 2 — "Mac" and "Custom" wallpaper options were swapped (real bug, fixed - see Round 4 above).**
- **Symptom:** selecting "Custom" showed an abstract wallpaper; selecting "Mac" showed a portrait image - the opposite of what a user would expect from those labels.
- **Diagnosis:** `wallpaper_paths[2]`/`wallpaper_paths[3]` were assigned in the same order the files were originally bundled (`CUSTOM.BMP` under "Custom", `MAC.BMP` under "Mac"), but the actual image *content* of the two source JPEGs didn't match their filenames from the user's perspective.
- **Fix:** swapped the two path entries (labels unchanged), confirmed correct via screendump in both directions.

**Non-bug — gOS cannot decode JPEG (expected, not a defect).**
Explained to the user before implementing: gOS's only image decoder is the hand-rolled 24bpp-uncompressed-BMP one from Milestone 15.3, by deliberate scope choice ("no PNG/JPEG decoding" per that milestone's own text). The 3 provided JPEGs were converted to BMP on the host (via `sips` plus a small top-down→bottom-up row-flip fix) rather than writing a JPEG decoder into the kernel - the right tradeoff for "add wallpaper options," not a gap needing a real fix.

---

## Patch v2 exit criterion — met

A discoverable desktop right-click menu offers 5 wallpaper choices (Gradient plus 4 bundled BMPs, including the user's 3 originally-JPEG images, now correctly labeled after the Round 4 fix); the taskbar clock no longer touches the screen edge; the menu itself never renders off-screen even when opened near a corner; the obsolete source JPEGs are removed with nothing left referencing them; and the full `make diagnostic` regression suite (150 file cycles + 300 window cycles) still passes after all four rounds of changes.
