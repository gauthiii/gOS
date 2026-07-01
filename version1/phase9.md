# Phase 9 — File Manager UI — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all three milestones (9.1, 9.2, 9.3) done, all tasks verified.

---

## Summary

Phase 9 turned Phase 6's window/button primitives and Phase 8's FAT32 driver into a real, navigable File Manager window: a toolbar, a live directory listing (folders visually distinguished from files), click-to-open folder navigation to arbitrary depth, an Up button, single-item selection highlighting, and four stubbed CRUD toolbar buttons ready for Phase 10 to wire up. As the plan's own dependency note predicted, this was a comparatively low-risk, fast phase — almost all of the effort went into the file manager's own logic (`kernel/src/fm.c`), not into new infrastructure. The one piece of infrastructure that did need extending was `window.c` itself: buttons alone weren't expressive enough for a scrollable row list, so a small, generic "custom render / custom click callback" mechanism was added to `struct window`, used by the file manager but not specific to it.

---

## Milestone 9.1: File manager window shell

### Task: Create a "File Manager" window with a toolbar area and a list area
- **What was done:** Added `window_set_render_callback()` / `window_set_click_callback()` to `window.c`/`window.h` — a window can now register an optional function pointer called after normal body/button drawing (for custom content) and an optional function pointer called on any body click that didn't land on a button (for custom hit-testing). This is a small, generic extension, not something baked specifically for files: any future window type could use the same two hooks. `kernel/src/fm.c` uses these to implement `fm_create_window(x, y, w, h)`, which creates a normal window via the existing `window_create()`, adds five toolbar buttons (Up, New Folder, New File, Delete, Rename) via the existing `window_add_button()`, and registers `fm_render`/`fm_click` as the two callbacks.
- **Outcome:** A `"File Manager"` window that looks and behaves like any other window (draggable by title bar, participates in z-order/click-to-focus) but additionally renders a toolbar + breadcrumb + file listing and responds to clicks on its rows.
- **Issues:** `MAX_WIDGETS_PER_WINDOW` was 4 (set in Phase 6 for a single test button); the file manager's five toolbar buttons needed more. Raised the constant to 8 in `window.h` — a one-line change with no other effects, since every window still only uses as many button slots as it explicitly adds.

### Task: On open, call `fat_list_dir("/")` and render each entry as a row
- **What was done:** `fm_refresh()` calls `fat_list_dir(fm_current_cluster, ...)` (starting at `fat32_root_cluster()`), then builds a filtered "visible" index list that excludes `.`/`..` entries (so they can't be accidentally clicked — Up is a dedicated button instead, matching Milestone 9.2's explicit requirement). `fm_render()` draws each visible entry as a row: a small 10x10 colored icon square, then the name via `fb_draw_string_clipped`.
- **Outcome:** Verified live — see the combined test below.
- **Issues:** None.

### Task: Distinguish folders vs files visually
- **What was done:** The icon square is amber (`fb_make_color(230,180,60)`) for directories (`attr & FAT32_ATTR_DIRECTORY`) and gray (`fb_make_color(160,160,160)`) for regular files — exactly the "different icon color is enough for v1" scope the plan itself calls out, no real icon graphics.
- **Outcome:** Verified visually via screendump (`screenshots/phase9_fm_01_root.png`): `TESTDIR` and `LEVEL1` show amber squares, `HOSTFILE.TXT` and `PERSIST.TXT` show gray squares.
- **Issues:** None.

---

## Milestone 9.2: Navigation

### Task: Implement click-to-open on a folder row
- **What was done:** `fm_click(win, local_x, local_y)` first checks whether the click landed below the toolbar/breadcrumb strip (`FM_LIST_TOP`); if so it computes a row index from `local_y`, looks up the corresponding filtered directory entry, and if that entry is a directory calls `fm_navigate_into()` — which pushes the current cluster and folder name onto a small stack (`fm_dir_stack`/`fm_name_stack`, depth 16), switches `fm_current_cluster` to the clicked folder's `first_cluster`, and calls `fm_refresh()` to re-list and re-render. Matches the plan's explicit single-click (not double-click) wording for 9.2.
- **Outcome:** Verified live (see below) — clicking `LEVEL1`, then the resulting single entry `LEVEL2`, then `LEVEL3` each correctly descended one level and re-rendered the new directory's contents.
- **Issues:** None.

### Task: Implement an "Up/Back" button that navigates to the parent directory
- **What was done:** The toolbar's Up button calls `fm_navigate_up()`, which pops the cluster stack (`fm_depth--`, restore `fm_current_cluster` from `fm_dir_stack[fm_depth]`) and re-lists. Deliberately does **not** rely on parsing `..` directory entries (which don't exist for the root directory anyway, since the root wasn't created via `fat_create_dir`) — the stack is authoritative and works identically at every depth including back-to-root.
- **Outcome:** Verified live: three Up clicks from `/LEVEL1/LEVEL2/LEVEL3` correctly returned to `/LEVEL1/LEVEL2`, then `/LEVEL1`, then `/` (4-entry root listing restored). A fourth Up click at the root logs `"already at root, Up ignored"` rather than underflowing the stack.
- **Issues:** None.

### Task: Implement a path breadcrumb / text label showing current directory
- **What was done:** `fm_rebuild_path_display()` joins the folder-name stack with `/` separators into `fm_path_display` (e.g. `"/LEVEL1/LEVEL2/LEVEL3"`), rendered via `fb_draw_string_clipped` just below the toolbar.
- **Outcome:** Verified visually via screendump — the breadcrumb reads exactly `/LEVEL1/LEVEL2/LEVEL3` in `screenshots/phase9_fm_04_level3.png`.
- **Issues:** None.

### Task: Test — navigate 3+ levels deep into a nested folder structure and back out correctly
- **What was done:** Since the existing `gos_disk.img` only had one level of nesting (`TESTDIR/NESTED.TXT`, from Phase 8), added a fresh 3-level tree via host-side `mtools` for this test: `mmd ::/LEVEL1`, `mmd ::/LEVEL1/LEVEL2`, `mmd ::/LEVEL1/LEVEL2/LEVEL3`, `mcopy` a `DEEP.TXT` file into `LEVEL3`. Booted gOS with the File Manager window open, and drove real simulated mouse clicks (QEMU monitor `mouse_move`/`mouse_button`, same real-hardware-path technique as Phase 6/7) through `LEVEL1` → `LEVEL2` → `LEVEL3`, confirmed `DEEP.TXT` is listed and selectable there, then clicked Up three times back to root.
- **Outcome:** Verified live via serial log — every navigation step logged the exact expected path and entry count:
  ```
  FM: listed "/" (4 entries)
  FM: click-to-open folder "LEVEL1"
  FM: listed "/LEVEL1" (1 entries)
  FM: click-to-open folder "LEVEL2"
  FM: listed "/LEVEL1/LEVEL2" (1 entries)
  FM: click-to-open folder "LEVEL3"
  FM: listed "/LEVEL1/LEVEL2/LEVEL3" (1 entries)
  FM: selected file "DEEP.TXT"
  FM: [Up] clicked
  FM: listed "/LEVEL1/LEVEL2" (1 entries)
  FM: [Up] clicked
  FM: listed "/LEVEL1" (1 entries)
  FM: [Up] clicked
  FM: listed "/" (4 entries)
  ```
  Also cross-checked independently against the host's own `mdir` (not just gOS agreeing with itself): `mdir -i disk_images/gos_disk.img ::` and `mdir -i disk_images/gos_disk.img ::/LEVEL1/LEVEL2/LEVEL3` show exactly the same root 4 entries and the same `LEVEL3` contents (`.`, `..`, `DEEP.TXT`) that gOS's listing (minus the dot entries, filtered by design) reports. Screenshots for every step saved to `screenshots/phase9_fm_01_root.png` through `phase9_fm_06_back_at_root.png`.
- **Issues:** None found in the navigation logic itself. (A bug was found and fixed in the *test-driving* mouse-coordinate math, not gOS's code — see "Bugs Found and Fixed" below.)

---

## Milestone 9.3: Selection and context actions

### Task: Implement single-item selection (highlight on click)
- **What was done:** Clicking a row whose entry is a regular file (not a directory) sets `fm_selected` to that row's index; `fm_render()` draws a highlighted background rect (`fb_make_color(60,90,140)`, a medium blue) behind the selected row before drawing its icon/text, with the text's background color also switched to match so there's no visible seam.
- **Outcome:** Verified visually via screendump (`screenshots/phase9_fm_07_hostfile_selected.png`): clicking `PERSIST.TXT`'s row highlights exactly that row in blue and no other.
- **Issues:** None.

### Task: Add toolbar buttons — "New Folder", "New File", "Delete", "Rename" (wired to Phase 10 logic)
- **What was done:** Added via the existing `window_add_button()` mechanism from Phase 6, laid out left-to-right after the Up button. Each is wired to its own stub callback (`fm_on_new_folder_click`, etc.) that logs a distinct message over serial identifying itself as a stub pending Phase 10's real `fat_create_dir`/`fat_create_file`/`fat_delete_file`/rename wiring — matching the plan's own explicit "(initially stubbed)" wording for this milestone.
- **Outcome:** Verified live (see below).
- **Issues:** None.

### Task: Test — buttons are clickable and dispatch to (initially stubbed) handler functions logged over serial
- **What was done:** Drove real simulated clicks (QEMU monitor) onto each of the five toolbar buttons in separate, focused test runs (to avoid the cursor-position drift described below) and confirmed each one's distinct log line appeared exactly once per click. Additionally recorded the disk image's MD5 checksum before and after the New Folder / New File / Delete stub clicks to independently confirm the stubs are genuinely inert (not silently touching the filesystem ahead of Phase 10).
- **Outcome:** All five buttons verified live:
  ```
  FM: [Up] clicked
  FM: [New Folder] clicked (stub - wired to real fat_create_dir in Phase 10)
  FM: [New File] clicked (stub - wired to real fat_create_file in Phase 10)
  FM: [Delete] clicked (stub - wired to real fat_delete_file/dir in Phase 10)
  FM: [Rename] clicked (stub - wired to real rename logic in Phase 10)
  ```
  MD5 of `disk_images/gos_disk.img` was identical (`209a7876780c50117f646dcdc3e56573`) before and after the New Folder/New File/Delete clicks — independent proof the stubs don't touch the disk.
- **Issues:** None.

---

## Bugs Found and Fixed

### Bug: Test-driving mouse clicks landed nowhere for the first ~9 seconds of "interactive" testing

- **Symptom:** The very first attempt to drive simulated navigation clicks (sending `mouse_move`/`mouse_button` starting 9 seconds after QEMU launch, based on the timing that worked for smaller tests in Phase 6/7) produced no navigation at all — the serial log showed three `"[Up] clicked" / "already at root, Up ignored"` pairs where folder-open and file-select clicks were expected, as if every earlier click had simply vanished.
- **Diagnosis:** This was a test-harness timing bug, not a gOS bug. Phase 9's boot sequence runs *after* several earlier phases' own multi-second demo loops that didn't exist yet when the original 9-second timing convention was established: the Milestone 5.3 bouncing-rectangle animation (2s), the Milestone 7.1 "Hello, gOS!" hold (2s), and the Milestone 6.1 mouse-tracking loop (5s) all execute serially before the window system (and therefore the File Manager) is even created — roughly 9 seconds of boot time that has nothing to do with OVMF/QEMU startup latency. Commands sent at the 9-second mark were arriving while the mouse-tracking loop (which doesn't dispatch clicks anywhere) or an even earlier stage was still running, and were silently discarded; only clicks sent late enough to land during the actual window loop (by chance, my later Up-button test commands) had any effect.
- **What was tried that didn't work:** Re-reading the click coordinates/geometry math first, suspecting a hit-testing bug in `fm_click`/`window_at_point` — but the coordinate math was independently verified correct as soon as the timing was fixed, so this line of investigation was a dead end.
- **Actual fix:** Not a code fix — a test-procedure fix. Recomputed the cumulative boot-to-window-loop delay (~9s of pre-window demo loops, plus OVMF/kernel boot overhead) and raised the initial `sleep` before the first simulated interaction from 9s to 16s in the test script. Re-running the exact same click sequence with only that timing change produced fully correct navigation results (see Milestone 9.2's test above). No production kernel code was touched to fix this.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| (implicit) window system only needed fixed buttons | Added generic `window_set_render_callback`/`window_set_click_callback` hooks to `struct window` | Buttons alone can't express a variable-length, scrollable row list; a small generic extension was less invasive than a file-manager-specific special case bolted onto `window.c` |
| (implicit) `MAX_WIDGETS_PER_WINDOW` sized for Phase 6's one test button | Raised from 4 to 8 | Five toolbar buttons (Up, New Folder, New File, Delete, Rename) needed more slots than Phase 6 ever used |
| Test disk only had one level of nesting (Phase 8's `TESTDIR/NESTED.TXT`) | Added a 3-level `LEVEL1/LEVEL2/LEVEL3/DEEP.TXT` tree via `mtools` | Milestone 9.2 explicitly requires testing 3+ levels of navigation depth; user chose to extend the existing golden `gos_disk.img` directly (rather than a separate scratch image) since this phase's operations are read-only from gOS's perspective |

None of these required revisiting later-phase plans.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 9.1 | ~4–6 hours | ~1 hour |
| Milestone 9.2 | ~4–6 hours | ~1 hour (including diagnosing the test-timing issue above) |
| Milestone 9.3 | ~4–6 hours | ~0.5 hour |
| **Phase 9 total** | **12–18 hours** | **~2.5 hours** |

**Why actual was faster than estimated:** Exactly as the plan's own dependency note predicted — "this entire phase is a thin UI layer over Phase 6 (windowing) and Phase 8 (filesystem)... should be the fastest of the 'hard' phases." Every underlying primitive (windows, buttons, click dispatch, directory listing, path resolution) already existed and was already proven correct; Phase 9's work was almost entirely new application logic in one new file (`fm.c`), not new infrastructure risk.

**Revised estimate guidance for future phases:** Phase 10 (CRUD Operations) is explicitly described the same way ("a thin UI layer over Phase 8's FAT32 read/write logic") and should follow a similar fast pattern — the four stub buttons built in this phase already have their exact call sites identified in the "Deviations"/task notes above, so Phase 10 is mostly replacing four one-line stub bodies with real `fat_create_dir`/`fat_create_file`/`fat_delete_file`/rename calls plus small text-input dialogs (reusing the Phase 7.3 text box).

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**Setup (once):** this phase's navigation test needs a 3-level-deep folder structure that didn't exist on the disk image before. If your `disk_images/gos_disk.img` doesn't have `LEVEL1/LEVEL2/LEVEL3/DEEP.TXT` yet (check with `mdir -i disk_images/gos_disk.img ::`), create it:
```bash
mmd -i disk_images/gos_disk.img ::/LEVEL1
mmd -i disk_images/gos_disk.img ::/LEVEL1/LEVEL2
mmd -i disk_images/gos_disk.img ::/LEVEL1/LEVEL2/LEVEL3
echo "Deep file content at level 3." > /tmp/deep.txt
mcopy -i disk_images/gos_disk.img /tmp/deep.txt ::/LEVEL1/LEVEL2/LEVEL3/DEEP.TXT
mdir -i disk_images/gos_disk.img ::
```

All test commands below build fresh first:
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_fm.fd
```

**Important timing note:** the File Manager window isn't created until ~9 seconds into the boot sequence (after the Phase 5/6/7 demo loops run). Any interactive test script must wait at least that long — see the "Bugs Found and Fixed" section above — before sending simulated mouse input; the commands below already account for this.

### Milestone 9.1 — File manager window shell

**Command to test (window created + root directory listed):**
```bash
timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "FM:|window_fm"
```
Expected output:
```
File Manager window created: window_fm=3
FM: listed "/" (4 entries)
```

**Command to see (screendump + open, showing folder/file icon distinction):**
```bash
(sleep 16; printf "screendump /tmp/fm_root.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/fm_root.ppm --out screenshots/phase9_fm_check.png
open screenshots/phase9_fm_check.png
```
**What to check:** a "File Manager" window with 5 toolbar buttons, a `/` breadcrumb, and 4 rows — `HOSTFILE.TXT`/`PERSIST.TXT` with **gray** icon squares, `TESTDIR`/`LEVEL1` with **amber** icon squares.

### Milestone 9.2 — Navigation

**Command to test (navigate 3 levels deep and back, with independent mtools cross-check):**
```bash
(sleep 16; \
 printf "mouse_move -490 -218\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_move 0 -50\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_move 0 -33\n"; sleep 0.3; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "quit\n") | timeout 45 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_fm_nav.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "FM:" /tmp/gos_fm_nav.log
# Independent cross-check against the host's own FAT32 tool:
mdir -i disk_images/gos_disk.img ::
mdir -i disk_images/gos_disk.img ::/LEVEL1/LEVEL2/LEVEL3
```
Expected `FM:` log (mouse coordinates target LEVEL1's row, then LEVEL2, then LEVEL3, then 3x Up):
```
FM: listed "/" (4 entries)
FM: click-to-open folder "LEVEL1"
FM: listed "/LEVEL1" (1 entries)
FM: click-to-open folder "LEVEL2"
FM: listed "/LEVEL1/LEVEL2" (1 entries)
FM: click-to-open folder "LEVEL3"
FM: listed "/LEVEL1/LEVEL2/LEVEL3" (1 entries)
FM: [Up] clicked
FM: listed "/LEVEL1/LEVEL2" (1 entries)
FM: [Up] clicked
FM: listed "/LEVEL1" (1 entries)
FM: [Up] clicked
FM: listed "/" (4 entries)
```
**What to check:** the entry counts and path strings in gOS's own log must match what `mdir` independently reports for the same directories; the sequence must end back at 4 root entries, not stuck partway or looping.

**Command to see (screendump of the deepest level, showing the breadcrumb):**
```bash
(sleep 16; \
 printf "mouse_move -490 -218\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_move 0 -50\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.5; \
 printf "screendump /tmp/fm_level3.ppm\n"; sleep 0.3; printf "quit\n") | timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/fm_level3.ppm --out screenshots/phase9_fm_level3_check.png
open screenshots/phase9_fm_level3_check.png
```
**What to check:** the breadcrumb text reads exactly `/LEVEL1/LEVEL2/LEVEL3` and the single row shown is `DEEP.TXT` with a gray (file) icon.

### Milestone 9.3 — Selection and context actions

**Command to test (all 5 toolbar buttons dispatch + disk-integrity check via MD5):**
```bash
md5 disk_images/gos_disk.img   # note the checksum first
(sleep 16; \
 printf "mouse_move -516 -302\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_move 84 0\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_move 80 0\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_move 70 0\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "mouse_move 70 0\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "quit\n") | timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_fm_buttons.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "FM:" /tmp/gos_fm_buttons.log
md5 disk_images/gos_disk.img   # must be IDENTICAL to the checksum noted above
```
Expected: `Up`, `New Folder`, `New File`, `Delete`, `Rename` each log their stub message once, and the MD5 checksum must be byte-for-byte identical before and after (stubs must not touch the disk).

**Command to see (screendump showing a selected file's highlight):**
```bash
(sleep 16; printf "mouse_move -510 -168\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.4; \
 printf "screendump /tmp/fm_selected.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_fm.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/fm_selected.ppm --out screenshots/phase9_fm_selected_check.png
open screenshots/phase9_fm_selected_check.png
```
**What to check:** exactly one row (a file, not a folder) is highlighted with a blue background band; no other row is highlighted.

```bash
rm -f /tmp/OVMF_VARS_fm.fd
```

### Full Phase 9 regression check — everything together in one boot
```bash
make clean && make iso && echo "BUILD OK"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|window_fm|FM:|boot checks complete"
rm -f /tmp/OVMF_VARS_full.fd
```
Expected: PMM/heap `PASS` lines, `window_fm=3`, `FM: listed "/" (4 entries)`, no `FAIL`/`PANIC`/`EXCEPTION` anywhere, ending with `=== gOS boot checks complete ===`.

**To watch the whole OS live** (not through automated headless commands), use `-display cocoa` with the disk attached:
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
This shows the full boot sequence and, once the window loop starts (~9 seconds in), the File Manager window alongside Windows A/B/C and the Text Editor — click and drag it with your real mouse to explore the FAT32 filesystem interactively.

---

## Not Yet Verified (intentionally deferred to Phase 10)

- [ ] New Folder / New File / Delete / Rename toolbar buttons are inert stubs — no real filesystem mutation happens yet from the file manager UI (Phase 10's explicit scope)
- [ ] No scrolling: `fm_render()` stops drawing rows that would fall past the window's bottom edge rather than scrolling; fine for this phase's test data (at most 4 visible entries per directory) but would need a scrollbar/paging mechanism for a directory with many more entries than fit in the window
- [ ] No multi-select or drag-select — only one file can be selected at a time, matching the plan's literal "single-item selection" wording

---

## Next Step

Proceed to **Phase 10 — CRUD Operations** (Milestone 10.1: Create), per [PROJECT_PLAN.md](PROJECT_PLAN.md). The four toolbar stub callbacks in `kernel/src/fm.c` (`fm_on_new_folder_click`, `fm_on_new_file_click`, `fm_on_delete_click`, `fm_on_rename_click`) are the exact call sites Phase 10 needs to replace with real `fat_create_dir`/`fat_create_file`/`fat_delete_file`/`fat_delete_dir`/rename logic and small text-input dialogs reusing the Phase 7.3 text box.
