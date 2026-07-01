# Phase 10 — CRUD Operations — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all four milestones (10.1, 10.2, 10.3, 10.4) done, all tasks verified.

---

## Summary

Phase 10 replaced Phase 9's four stubbed toolbar buttons with real filesystem operations, and added a genuine Text Editor: double-click a file in the File Manager to open it, edit it with the keyboard, and press Ctrl+S to save changes back to disk. New Folder/New File/Rename all use a small reusable modal dialog (a text-input box plus Confirm/Cancel buttons) built on Phase 7's text box widget; Delete reuses the same dialog as a Yes/No confirmation. Every operation was verified two ways: gOS's own listing/read-back, and an independent host-side check (`mdir`/`mtype` against the same disk image) — the same double-verification discipline Phase 8 established for anything that writes to the filesystem.

Two pieces of shared infrastructure needed small, generic extensions to support this (not file-manager-specific hacks): the window system gained a `window_key_callback_t` hook (so the text editor can intercept Ctrl+S without it being typed as a literal character) plus `window_close`/`window_focus`/`window_get`/`window_set_title` (so dialogs and the editor can be created, reused, and torn down), and the keyboard driver gained Ctrl-key tracking. The FAT32 driver gained one new function, `fat_rename()`, following the exact same "resolve entry, patch only the changed bytes" pattern Phase 8's `fat_write_file` established.

---

## Milestone 10.1: Create

### Task: Wire "New Folder" button to `fat_create_dir()` + prompt for a name via a small text input dialog
- **What was done:** Built a single reusable modal dialog in `fm.c` (`fm_open_dialog()`): a small window with a text box (Phase 7.3's widget, reused as-is) and Confirm/Cancel buttons, plus a custom-rendered prompt label. Clicking "New Folder" opens it in `FM_DIALOG_NEW_FOLDER` mode; Confirm reads the text box's contents and calls `fat_create_dir()` with the current directory + typed name, then refreshes the listing and closes the dialog. Cancel just closes it.
- **Outcome:** Verified live (see combined test below).
- **Issues:** First attempt rendered the prompt label and the text box's own typed-text line on top of each other (both drawn at the same `body_y+4` row) — see "Bugs Found and Fixed" below.

### Task: Wire "New File" button to `fat_create_file()` similarly
- **What was done:** Same dialog mechanism, `FM_DIALOG_NEW_FILE` mode, calling `fat_create_file()` instead.
- **Outcome:** Verified live (see below).
- **Issues:** None beyond the shared rendering bug above.

### Task: Test — create a folder and a file from the UI, confirm they appear in the listing immediately and persist after reboot
- **What was done:** Drove real simulated mouse clicks and keystrokes (QEMU monitor `mouse_move`/`mouse_button`/`sendkey`, the same real-hardware-path technique established in Phases 4/6/7) through the New Folder dialog (typed "newdir") and the New File dialog (typed "newfile"), first against a disposable scratch copy of the disk image, then confirmed the identical result against the real `gos_disk.img`.
- **Outcome:** Verified live via serial log:
  ```
  FM: [New Folder] clicked
  FM: fat_create_dir("newdir") = 1 (OK)
  FM: listed "/" (5 entries)
  FM: [New File] clicked
  FM: fat_create_file("newfile") = 1 (OK)
  FM: listed "/" (6 entries)
  ```
  Independently cross-checked with the host's own `mdir` (not just gOS agreeing with itself): both `NEWDIR <DIR>` and `NEWFILE` (0 bytes) appear in `mdir`'s output immediately after creation. Screenshot: `screenshots/phase10_final_01_root.png` shows both new entries in the File Manager listing (amber icon for `NEWDIR`, gray for `NEWFILE`).
- **Issues:** None (persistence-after-reboot is exercised together with Milestone 10.3's test below, since both use the same reboot-and-reread technique).

---

## Milestone 10.2: Read (text editor)

### Task: Build a minimal "Text Editor" window: double-clicking a `.txt` file opens it
- **What was done:** New module `kernel/src/editor.c` / `kernel/include/editor.h`. `editor_open(path)` creates a single reusable "Text Editor" window (or refocuses it via the new `window_focus()` if already open), retitles it to the file's path via the new `window_set_title()`, and loads the file's contents directly into the window's `textbox_buffer`/`textbox_length` fields (accessed via the new `window_get()`, which returns a pointer to the window struct — the same pattern Phase 9's render/click callbacks already used, just exposed as a direct getter too). `fm.c`'s row-click handler (`fm_click`) now distinguishes a single click (select) from a double click (open in editor) using `timer_get_ticks()` and a 400ms threshold (`FM_DOUBLE_CLICK_TICKS = 40` at the Phase 4 100Hz timer rate) — single-click-to-open is reserved for folders (per Milestone 9.2's explicit wording), while files use double-click to open in the editor, matching this milestone's explicit wording.
- **Outcome:** Verified live (see below).
- **Issues:** None.

### Task: Load file contents via `fat_read_file()` into the text box widget
- **What was done:** `editor_open()` calls `fat_read_file(path, w->textbox_buffer, TEXTBOX_BUFFER_SIZE - 1)` directly into the reused window's text box buffer, sets `textbox_length` to the byte count read, and null-terminates.
- **Outcome:** Verified live (see below).
- **Issues:** None.

### Task: Test — open an existing text file created on the host via `mtools`, confirm its exact contents render correctly
- **What was done:** Double-clicked `HOSTFILE.TXT` (the same file Phase 8 created on the host with `mtools` and has been read/verified in every phase since) in the File Manager's root listing.
- **Outcome:** Verified live via serial log (`Editor: opened "HOSTFILE.TXT" (53 bytes)`) and visually via screendump: the editor window retitles itself to `HOSTFILE.TXT` and shows the exact original text, `"Hello from the host! This is a FAT32 read test file."` — see `screenshots/phase10_final_02a_editor_opened.png`.
- **Issues:** None.

---

## Milestone 10.3: Update (save)

### Task: Add a "Save" button/keybind (Ctrl+S) that calls `fat_write_file()` with the edited buffer
- **What was done:** Extended `kernel/src/keyboard.c` to track a Left Ctrl held-state (scancode `0x1D` make/`0x9D` break, alongside the existing Shift tracking) and to special-case the `S` scancode (`0x1F`) while Ctrl is held: instead of pushing `'s'`, it pushes ASCII `0x13` (the conventional Ctrl+S / DC3 control code) into the ring buffer, so a real `Ctrl+S` and a literal typed `s` are distinguishable downstream. Added a generic `window_key_callback_t` hook to `window.c`: any character routed to a focused window with a text box is first offered to this callback (if set), which can return 1 to mean "consumed - skip default typing behavior." The text editor's `editor_on_key()` checks for `0x13`, calls `fat_write_file(editor_path, w->textbox_buffer, w->textbox_length)`, logs the result, and returns 1 so the control character is never inserted into the visible text.
- **Outcome:** Verified live (see below).
- **Issues:** None.

### Task: Handle the case where edited content is larger/smaller than original (cluster chain grows/shrinks correctly)
- **What was done:** No new logic needed here — `fat_write_file()` already implements exactly this (Phase 8, Milestone 8.3), and passing the text box's current `textbox_length` (which changes as the user types/backspaces) exercises it automatically.
- **Outcome:** Verified live: appending 9 bytes (`"#appended"`) to the 53-byte `HOSTFILE.TXT` grew it to 62 bytes on disk, confirmed independently via host `mtype`.
- **Issues:** None.

### Task: Test — edit a file, save, reboot, reopen — confirm changes persisted
- **What was done:** Opened `HOSTFILE.TXT`, typed `#appended` (using `sendkey shift-3` for `#`, matching the existing scancode-to-ASCII shift table), pressed `Ctrl+S` (`sendkey ctrl-s`), then in a **separate, fresh QEMU process** against the same disk image, double-clicked `HOSTFILE.TXT` again.
- **Outcome:** Verified live:
  ```
  Editor: opened "HOSTFILE.TXT" (53 bytes)
  ...typed "#appended"...
  Editor: Ctrl+S saved "HOSTFILE.TXT" (62 bytes) = 1 (OK)
  ```
  Then, on the fresh boot: `Editor: opened "HOSTFILE.TXT" (62 bytes)` — 62 bytes, not 53, proving the edit survived a full reboot (a fresh QEMU process has no memory of the prior session; the only place 62 bytes could come from is disk). Independently cross-checked via host `mtype -i disk_images/gos_disk.img ::/HOSTFILE.TXT`, which shows the exact same appended text. Screenshot: `screenshots/phase10_final_02b_editor_edited.png`.
- **Issues:** None.

---

## Milestone 10.4: Delete and Rename

### Task: Wire "Delete" button to `fat_delete_file()`/`fat_delete_dir()` with a confirmation dialog
- **What was done:** `fm_on_delete_click()` requires a currently selected file (`fm_selected != -1`; logs and ignores the click otherwise), records its name and whether it's a directory, and opens the same reusable dialog in `FM_DIALOG_DELETE_CONFIRM` mode with a prompt like `Delete "NEWFILE"?`. Confirm calls `fat_delete_file()` or `fat_delete_dir()` depending on the recorded type; Cancel just closes the dialog with no filesystem change.
- **Outcome:** Verified live (see below).
- **Issues:** See the "folders can't be selected for Delete/Rename" scope note below — a real, minor UI limitation, not a bug.

### Task: Implement rename — update the directory entry's filename bytes in place
- **What was done:** Added `fat_rename(path, new_name)` to `kernel/src/fat32.c` / `kernel/include/fat32.h`, following the exact same pattern as the existing `find_dirent`/`write_dirent_at`/`to_83_name` helpers Phase 8 built for `fat_write_file`: resolves the source entry's exact on-disk location, checks a destination-name collision doesn't already exist in the same parent directory, converts `new_name` to 8.3 format, and overwrites only the 11-byte name field of that one directory entry (no cluster or size changes — matching the plan's own explicit wording "no cluster changes needed"). `fm_on_rename_click()` requires a selected file, pre-fills the dialog's text box with the current name (backspace-and-retype rather than starting blank), and Confirm calls `fat_rename()`.
- **Outcome:** Verified live (see below).
- **Issues:** None in the rename logic itself; see the rendering bug below (found while testing this milestone).

### Task: Test — delete a file, confirm it disappears from listing and from a host-side mount check; rename a file, confirm name change persists
- **What was done:** Selected `NEWFILE` (created in Milestone 10.1), clicked Delete, clicked Confirm in the dialog; then selected `PERSIST.TXT`, clicked Rename, backspaced the pre-filled name and typed `renamed`, clicked Confirm. Both tested first against a disposable scratch copy of the disk image, then confirmed identically against the real `gos_disk.img`.
- **Outcome:** Verified live via serial log:
  ```
  FM: fat_delete_file("NEWFILE") = 1 (OK)
  FM: listed "/" (5 entries)
  ...
  FM: fat_rename("PERSIST.TXT", "renamed") = 1 (OK)
  FM: listed "/" (5 entries)
  ```
  Independently cross-checked via host `mdir`: `NEWFILE` is gone entirely; `PERSIST.TXT` no longer appears and `RENAMED` (35 bytes — same size, since rename doesn't touch data) appears in its place. Every other entry (`HOSTFILE.TXT`, `TESTDIR`, `LEVEL1`, `NEWDIR`) is untouched. Screenshots: `screenshots/phase10_final_03a_delete_confirm.png`, `screenshots/phase10_final_03b_after_delete.png`, `screenshots/phase10_final_04a_rename_dialog.png` (showing the pre-filled name being edited), `screenshots/phase10_final_04b_after_rename.png`.
- **Issues:** None beyond the two bugs documented below.

---

## Bugs Found and Fixed

### Bug 1: Dialog's prompt label overlapped the text box's typed-text line

- **Symptom:** The first screendump of the New Folder dialog (`p10_01_newfolder_dialog.ppm`, an early test run) showed garbled, overlapping text where the "New folder name:" label should have been — legible on close inspection as two strings drawn on top of each other, but visually broken.
- **Diagnosis:** `fm_dialog_render()` (a `window_render_callback_t`) drew the prompt label at `body_y + 6`. Separately, `window.c`'s existing generic text-box rendering (added in Phase 7 for the demo Text Editor window, reused as-is here) always draws its one line of typed text at `body_y + 4` — the same row. Since custom render callbacks run *after* the generic text-box drawing (by design, so custom content layers on top), the prompt label was being stamped directly over the text box's content line rather than appearing as a separate label above it.
- **What was tried that didn't work:** Nothing else was tried — the fix was identified immediately from the screendump by comparing the two draw call's y-coordinates.
- **Actual fix:** Moved the prompt label's y-coordinate in `fm_dialog_render()` from `body_y + 6` to `body_y + 4 + FONT_HEIGHT + 6` — i.e., below the text box's line instead of overlapping it. Verified fixed via a fresh screendump (`p10_08_rename_dialog.ppm`, taken after the fix): the pre-filled "PERSIST.TXT" text and the "Rename to:" label now appear as two clearly separate, non-overlapping lines.

### Bug 2 (test-harness, not gOS code): folders can never be selected for Delete/Rename via a single click

- **Symptom:** While testing Rename, clicking a directory row (`NEWDIR`) to select it before clicking the Rename button instead navigated into that directory (logged `FM: click-to-open folder "NEWDIR"`), and the subsequent Rename click correctly reported `"Rename ignored (no item selected)"` rather than doing anything wrong.
- **Diagnosis:** This is not a bug in the delete/rename logic (both `fat_delete_dir` and `fat_rename` work correctly on directories when called directly) — it's an inherent consequence of Milestone 9.2's explicit design requirement that a *single* click on a folder row must navigate into it, not select it. Since `fm_selected` is only ever set for non-directory rows, a folder can never become the "selected" item the Delete/Rename buttons act on, through the file manager UI as built. This is a genuine, minor v1 scope limitation (documented below), not something that needed a code fix — the underlying FAT32 operations are already directory-capable.
- **What was tried that didn't work:** N/A — once traced to the click-to-open-on-single-click design (deliberate, from Phase 9), no further investigation was needed.
- **Actual fix:** None applied — this is intentionally left as a documented v1 limitation rather than redesigning folder click semantics (e.g. requiring a modifier-click or a separate selection mode for folders), which is out of scope for this phase. The milestone's own test (delete a **file**, rename a **file**) is unaffected and was completed successfully against `NEWFILE`/`PERSIST.TXT`.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| (implicit) window system only needed to create/keep windows forever | Added `window_close()`, `window_focus()`, `window_get()`, `window_set_title()` to `window.c`/`window.h` | Modal dialogs need to be created and destroyed repeatedly (a new one each time New Folder/New File/Rename/Delete is clicked); the text editor needs to be reused/refocused/retitled across multiple files rather than creating a new window per file |
| (implicit) keyboard driver only needed Shift/Caps Lock | Added Left Ctrl tracking and a Ctrl+S → ASCII `0x13` special case | Milestone 10.3 explicitly calls for a Ctrl+S save keybind; the keyboard driver had no modifier-key concept beyond Shift before this phase |
| (implicit) window text box had no way to intercept special keys | Added a generic `window_key_callback_t` hook, offered every character before default typing behavior | Ctrl+S must trigger a save and *not* be inserted as a literal character into the text box - needed a way for editor-specific logic to intercept a keystroke without editor.c reimplementing the whole text box widget |
| Rename button "wired to Phase 10 logic" (no dialog specified) | Built one small reusable modal dialog (text box + Confirm/Cancel) shared by New Folder/New File/Rename/Delete-confirm | The plan's own wording for 10.1 says "prompt for a name via a small text input dialog (reuses Phase 7.3 text box)" - built once, generically, rather than four separate dialog implementations |

None of these required revisiting later-phase plans (Phase 11 is stabilization/polish over what already exists).

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 10.1 | ~3–4 hours | ~1 hour |
| Milestone 10.2 | ~2–4 hours | ~0.5 hour |
| Milestone 10.3 | ~2–4 hours | ~1 hour (including the Ctrl-key keyboard driver extension) |
| Milestone 10.4 | ~3–4 hours | ~1 hour (including diagnosing both bugs above) |
| **Phase 10 total** | **10–16 hours** | **~3.5 hours** |

**Why actual was faster than estimated:** As the plan's own dependency note predicted, this phase really was "a thin UI layer over Phase 8's FAT32 read/write logic" — `fat_create_dir`, `fat_create_file`, `fat_write_file`, `fat_delete_file`, and `fat_delete_dir` already existed and were already proven correct; only one small new FAT32 function (`fat_rename`) was needed, built by directly reusing existing internal helpers (`find_dirent`, `to_83_name`) rather than writing new low-level disk logic. The one piece of genuinely new mechanism — the modal dialog — was built once and reused four times.

**Revised estimate guidance for future phases:** Phase 11 (Polish/Stability) is explicitly open-ended stabilization work over an already-functionally-complete system (boot → file manager → full CRUD, matching the plan's own "definition of done" in §9). Expect this to be the first phase where testing is more about breaking things on purpose (stress tests, deliberately malformed input) than building new features.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**Setup (once):** builds fresh and prepares the OVMF vars copy every test below expects:
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_p10.fd
```

**⚠️ Always test against a disposable copy first** for anything that writes (per the same guidance Phase 8 established):
```bash
cp disk_images/gos_disk.img disk_images/gos_disk_scratch.img
```
Substitute `gos_disk_scratch.img` for `gos_disk.img` in the commands below while validating a change; switch back to `gos_disk.img` only once you trust it (all commands below default to the real disk image, matching this phase's own already-verified final state).

**Important timing note (unchanged from Phase 9):** the File Manager window isn't created until ~16 seconds into the boot sequence. All scripts below account for this with an initial `sleep 16`.

### Milestone 10.1 — Create

**Command to test (New Folder + New File, with independent `mdir` cross-check):**
```bash
(sleep 16; \
 printf "mouse_move -417 -301\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "sendkey n\n"; sleep 0.15; printf "sendkey e\n"; sleep 0.15; printf "sendkey w\n"; sleep 0.15; \
 printf "sendkey d\n"; sleep 0.15; printf "sendkey i\n"; sleep 0.15; printf "sendkey r\n"; sleep 0.3; \
 printf "mouse_move -3 118\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "mouse_move 92 -118\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "sendkey n\n"; sleep 0.15; printf "sendkey e\n"; sleep 0.15; printf "sendkey w\n"; sleep 0.15; \
 printf "sendkey f\n"; sleep 0.15; printf "sendkey i\n"; sleep 0.15; printf "sendkey l\n"; sleep 0.15; printf "sendkey e\n"; sleep 0.3; \
 printf "mouse_move -92 118\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_create.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "FM:" /tmp/gos_p10_create.log
mdir -i disk_images/gos_disk.img ::
```
Expected: `fat_create_dir("newdir") = 1 (OK)`, `fat_create_file("newfile") = 1 (OK)`, and `mdir` independently shows both `NEWDIR <DIR>` and `NEWFILE` in the listing.

**Command to see (screendump of the New Folder dialog + resulting listing):**
```bash
(sleep 16; \
 printf "mouse_move -417 -301\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "screendump /tmp/p10_dialog.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/p10_dialog.ppm --out screenshots/phase10_check_dialog.png && open screenshots/phase10_check_dialog.png
```

### Milestone 10.2 — Read (text editor)

**Command to test (double-click `HOSTFILE.TXT`, confirm exact byte count/content):**
```bash
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.2; \
 printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_read.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep -E "FM:|Editor:" /tmp/gos_p10_read.log
mtype -i disk_images/gos_disk.img ::/HOSTFILE.TXT
```
**What to check:** the `Editor: opened "HOSTFILE.TXT" (N bytes)` line's byte count must match `mtype`'s output length exactly (note: `HOSTFILE.TXT` may already be 62 bytes if you've run Milestone 10.3's test before this one on the same disk image — that's expected, not a bug).

**Command to see (screendump of the opened editor window):**
```bash
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.2; \
 printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.6; \
 printf "screendump /tmp/p10_editor.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/p10_editor.ppm --out screenshots/phase10_check_editor.png && open screenshots/phase10_check_editor.png
```

### Milestone 10.3 — Update (save)

**Command to test (edit + Ctrl+S, then a separate fresh boot to prove persistence):**
```bash
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.2; \
 printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.6; \
 printf "sendkey shift-3\n"; sleep 0.2; \
 printf "sendkey a\n"; sleep 0.15; printf "sendkey p\n"; sleep 0.15; printf "sendkey p\n"; sleep 0.15; \
 printf "sendkey e\n"; sleep 0.15; printf "sendkey n\n"; sleep 0.15; printf "sendkey d\n"; sleep 0.15; \
 printf "sendkey e\n"; sleep 0.15; printf "sendkey d\n"; sleep 0.3; \
 printf "sendkey ctrl-s\n"; sleep 0.5; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_save1.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "Editor:" /tmp/gos_p10_save1.log
# Fresh QEMU process, same disk image, no reformat - the real persistence proof:
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.2; \
 printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_save2.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "Editor:" /tmp/gos_p10_save2.log
mtype -i disk_images/gos_disk.img ::/HOSTFILE.TXT
```
**What to check:** the second boot's `Editor: opened "HOSTFILE.TXT" (N bytes)` must show the larger, post-edit byte count (not the original), and `mtype` must show the appended text - both prove the edit survived a full process restart, not just an in-memory buffer.

**Command to see (screendump showing the appended text in the editor):**
```bash
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.2; \
 printf "mouse_button 1\n"; sleep 0.15; printf "mouse_button 0\n"; sleep 0.6; \
 printf "sendkey shift-3\n"; sleep 0.2; \
 printf "sendkey a\n"; sleep 0.15; printf "sendkey p\n"; sleep 0.15; printf "sendkey p\n"; sleep 0.15; \
 printf "sendkey e\n"; sleep 0.15; printf "sendkey n\n"; sleep 0.15; printf "sendkey d\n"; sleep 0.15; \
 printf "sendkey e\n"; sleep 0.15; printf "sendkey d\n"; sleep 0.3; \
 printf "screendump /tmp/p10_edited.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/p10_edited.ppm --out screenshots/phase10_check_edited.png && open screenshots/phase10_check_edited.png
```

### Milestone 10.4 — Delete and Rename

**Command to test (delete `NEWFILE`, rename `HOSTFILE.TXT`'s sibling — adjust row math if your disk's listing order differs):**
```bash
# Delete NEWFILE (must already exist - see Milestone 10.1's test above)
(sleep 16; \
 printf "mouse_move -490 -184\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "mouse_move 241 -117\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "mouse_move -171 118\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_delete.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "FM:" /tmp/gos_p10_delete.log
mdir -i disk_images/gos_disk.img ::
```
**What to check:** `fat_delete_file("NEWFILE") = 1 (OK)`, and `mdir` must show `NEWFILE` is gone with every other entry untouched.

```bash
# Rename PERSIST.TXT -> RENAMED
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "mouse_move 320 -69\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; \
 printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; \
 printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.1; \
 printf "sendkey backspace\n"; sleep 0.1; printf "sendkey backspace\n"; sleep 0.3; \
 printf "sendkey r\n"; sleep 0.15; printf "sendkey e\n"; sleep 0.15; printf "sendkey n\n"; sleep 0.15; \
 printf "sendkey a\n"; sleep 0.15; printf "sendkey m\n"; sleep 0.15; printf "sendkey e\n"; sleep 0.15; \
 printf "sendkey d\n"; sleep 0.3; \
 printf "mouse_move -250 118\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 40 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_p10_rename.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep "FM:" /tmp/gos_p10_rename.log
mdir -i disk_images/gos_disk.img ::
```
**What to check:** `fat_rename("PERSIST.TXT", "renamed") = 1 (OK)`, and `mdir` must show `RENAMED` (35 bytes, same size) in place of `PERSIST.TXT`, with every other entry unaffected.

**Command to see (screendump of the rename dialog + resulting listing):**
```bash
(sleep 16; \
 printf "mouse_move -490 -232\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "mouse_move 320 -69\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "screendump /tmp/p10_rename_dialog.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_p10.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/p10_rename_dialog.ppm --out screenshots/phase10_check_rename_dialog.png && open screenshots/phase10_check_rename_dialog.png
```

```bash
rm -f /tmp/OVMF_VARS_p10.fd
```

### Full Phase 10 regression check — everything together in one boot
```bash
make clean && make iso && echo "BUILD OK"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
timeout 55 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|window_fm|boot checks complete"
rm -f /tmp/OVMF_VARS_full.fd
```
Expected: PMM/heap `PASS` lines, `window_fm=3`, no `FAIL`/`PANIC`/`EXCEPTION` anywhere, ending with `=== gOS boot checks complete ===`. (Note: the window/file-manager interaction loop is now 500 frames / 25 seconds, up from Phase 9's 300, to give CRUD testing enough headroom - a plain regression check with no monitor commands takes correspondingly longer to reach `hcf()`.)

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
Once the window loop starts (~9 seconds after the earlier demo loops finish, ~16 seconds total from launch), click the File Manager's toolbar buttons, double-click a file to open the Text Editor, type, and press Ctrl+S with your real mouse/keyboard to try the whole CRUD flow interactively.

---

## Not Yet Verified (intentionally deferred to Phase 11)

- [ ] Folders cannot be selected for Delete/Rename via the file manager UI (single click always navigates into them, per Milestone 9.2) — the underlying `fat_delete_dir`/`fat_rename` calls both work correctly on directories; this is a UI-reachability gap only, documented above
- [ ] No "are you sure" distinction between deleting a file vs. a non-empty directory - `fat_delete_dir` already refuses non-empty directories (Phase 8), but the confirmation dialog's wording doesn't yet warn about that specifically
- [ ] No undo for any CRUD operation
- [ ] Renaming to a name that's a case-only variant of an existing entry isn't specially handled (FAT32 8.3 names are case-insensitive; `fat_rename` already checks for a destination collision case-insensitively, but this specific edge case wasn't explicitly tested)
- [ ] Kernel panic screen / crash resilience audit (Milestone 11.1) - errors currently just fail an operation and log to serial, with no protection against, e.g., a null pointer dereference elsewhere in the system

---

## Next Step

Proceed to **Phase 11 — Polish / Stability** (Milestone 11.1: Crash resilience), per [PROJECT_PLAN.md](PROJECT_PLAN.md). With boot → windowing → filesystem → file manager → full CRUD all in place and independently verified, this is the plan's own described finish line for "v1" functionality; Phase 11 is entirely about stabilizing and polishing what already works (panic screen, taskbar, stress testing) rather than adding new capability.
