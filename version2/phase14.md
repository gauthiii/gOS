# Phase 14 — Medium/Low Audit Cleanup — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all 12 Medium/Low findings from [version1/audit.md](version1/audit.md) closed, Track A (audit remediation) complete.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```
Builds the kernel, packages it with Limine into a bootable ISO, creates/reuses `disk_images/gos_disk.img`, and launches a real graphical QEMU window. Sanity-checks all 12 Phase 14 findings together in the normal boot path.

Other useful targets: `make build` (compile only), `make iso` (build ISO without launching), `make debug` (like `make run` but with a `-s -S` GDB stub), `make clean` (remove build artifacts, not the disk image).

---

## Summary

Phase 14 is Track A, Phase 3 of [project-plan-2.md](project-plan-2.md): the 6 Medium and 6 Low findings from the post-v1.0 kernel audit, plus the living README.md update task (Milestone 14.0, done separately after this phase's code work — see [README.md](README.md)'s Further Reading section). With this phase complete, **Track A (all 24 audit findings) is closed** and Track B (new features, Phase 15+) can begin per project-plan-2.md's priority rule.

Two findings (#17, #23) share a new reusable mechanism — `taskbar_flash_message()`, a brief red banner above the taskbar — built once for #17 and reused as-is for #23. One finding (#24) was scoped as documentation-only per the audit's own assessment that it's a latent, not active, bug. A mid-phase incident is documented in full below: testing Finding #21 accidentally wiped the live `disk_images/gos_disk.img`, and the recovery is recorded with the same rigor as the fixes themselves.

---

## Milestone 14.1: Medium findings

### #12 — Demo windows consuming slots
- **What was done:** `kernel/src/start.c` created "Window A"/"Window B"/"Text Editor" at boot as Phase 6/7 regression demos, but never closed them — permanently consuming 3 of `MAX_WINDOWS=8` slots before the user opened anything real.
- **The actual fix:** Added `window_close()` calls for all three immediately after creation/configuration, before the compositor's main loop ever draws a frame — the demo windows still prove `window_create`/`window_add_button`/`window_enable_textbox` all work end-to-end on every boot, but never actually appear on screen or hold a slot.
- **Test:** Live boot log shows `Demo windows (Window A/B, Text Editor) auto-closed - 0 window(s) open, 8 slot(s) free`. Screendumped the desktop 20s into boot — confirmed empty (no demo windows), then simulated a click on the "Files" icon and confirmed the File Manager opens normally, proving real window creation still works with all 8 slots available.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run 2>&1 | grep "Demo windows"
```
Expected: `Demo windows (Window A/B, Text Editor) auto-closed - 0 window(s) open, 8 slot(s) free`

**Command to see:**
```bash
make debug   # real graphical QEMU window; desktop starts empty, Files icon opens FM normally
```
Screenshots: [screenshots/phase14_12_demo_windows_closed.png](screenshots/phase14_12_demo_windows_closed.png) (empty desktop), [screenshots/phase14_12_filemanager_still_works.png](screenshots/phase14_12_filemanager_still_works.png) (FM opens fine afterward).

---

### #13 — 0xE0 extended-scancode prefix isn't handled
- **What was done:** `kernel/src/keyboard.c`'s IRQ1 handler silently dropped the `0xE0` prefix byte (out of ASCII table range) but processed the *following* byte as an ordinary scancode. Right Ctrl (`0xE0 0x1D`) and Left Ctrl (`0x1D`) share the same second byte, as do Numpad Enter (`0xE0 0x1C`) and plain Enter (`0x1C`) — both "worked" by numeric coincidence, with no way to actually distinguish them.
- **The actual fix:** Added a `pending_extended` flag set on `0xE0` and consumed on the next byte. Right Ctrl now tracked via a separate `right_ctrl_held` state (Ctrl+S checks `ctrl_held || right_ctrl_held`), and both Right Ctrl and Numpad Enter log an explicit `"(extended)"` line distinguishing them from their non-extended counterparts.
- **Test:** `sendkey ctrl` (Left Ctrl) produces no extended log lines; `sendkey ctrl_r` logs `"Keyboard: Right Ctrl pressed (extended)"`/`"released (extended)"`; `sendkey kp_enter` logs `"Keyboard: Numpad Enter pressed (extended)"`; `sendkey ret` (plain Enter) logs neither.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso
(sleep 10; printf "sendkey ctrl\n"; sleep 0.3; printf "sendkey ctrl_r\n"; sleep 0.3; \
 printf "sendkey kp_enter\n"; sleep 0.3; printf "sendkey ret\n"; sleep 0.3; printf "quit\n") | \
 timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -device piix3-ide,id=ide -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0 \
  -cdrom build/gos.iso -display none -serial stdio -monitor stdio | grep -i keyboard
```
Expected: exactly `Keyboard: Right Ctrl pressed (extended)`, `Keyboard: Right Ctrl released (extended)`, `Keyboard: Numpad Enter pressed (extended)` — no line for the plain `ctrl`/`ret` presses.

**Command to see:** Non-graphical (keyboard internals only) — confirm via `make debug` watching serial output, matching the project's convention for non-visual self-tests.

---

### #14 — Spurious IRQ7/15 not detected
- **What was done:** `kernel/src/pic.c`'s `pic_send_eoi` sent EOI to both PICs for a slave IRQ (≥8) unconditionally, with no ISR-register check to detect a spurious interrupt (a hardware quirk of edge-triggered 8259 operation).
- **The actual fix:** Added `pic_read_isr()` (OCW3 `0x0B`) checked for IRQ7 and IRQ15 specifically (the only two lines that can genuinely fire spuriously). A spurious IRQ7 gets no EOI at all; a spurious IRQ15 gets EOI sent to the master PIC only (acknowledging the cascade line) rather than both.
- **Test:** Neither IRQ7 (LPT) nor IRQ15 (secondary ATA) has a driver registered in this kernel, so calling `pic_send_eoi(7)`/`pic_send_eoi(15)` synthetically at boot exercises exactly the spurious path live (both are genuinely never "in service"). Confirmed via a debug counter that both calls read the ISR register (`before=0 after=2`), and boot continued normally afterward with no crash.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_SPURIOUS_IRQ_CHECK"
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A5 "TEST: synthetic"
```
Expected: `TEST: ISR-register reads before=0 after=2 (both EOI calls read ISR - OK)`.

**Command to see:** Non-graphical. `make debug` and watch serial output; boot proceeds normally past the synthetic spurious-IRQ test.

---

### #15 — No ATA drive-presence probe
- **What was done:** `kernel/src/ata.c`'s `ata_init()` did no presence check — the very first `ata_read_sector`/`ata_write_sector` call on a missing drive burned the full ~100,000-iteration busy-wait in both `ata_wait_not_busy()` and `ata_wait_drq()` before failing.
- **The actual fix:** Added an `IDENTIFY`-based probe in `ata_init()`: select the drive, issue `IDENTIFY` (`0xEC`), and check the status port immediately — a status of `0x00` (per the OSDev Wiki IDENTIFY procedure) means no drive exists at all, set `drive_present = 0` and return without any polling. `ata_read_sector`/`ata_write_sector` now check `drive_present` first and fail immediately if it's false.
- **Test:** Booted with `-drive` omitted (no disk attached). Serial log shows `ATA: no drive detected on primary master (status=0x00) - I/O calls will fail fast`. Added a debug counter inside `ata_wait_not_busy`/`ata_wait_drq` proving the fixed code makes **0** status-port reads on a failed I/O call. Reverting the fast-fail check (keeping the probe) showed **100,001** reads for the same failed call — the real difference the fix makes.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_ATA_PROBE"
timeout 15 qemu-system-x86_64 -M q35 -m 256M -no-reboot \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide -serial stdio -display none | grep -E "ATA:|TEST:"
```
Expected: `ATA: no drive detected on primary master (status=0x00) - I/O calls will fail fast`, then `TEST: ata_wait_not_busy() status-port reads for this failed call = 0 (fast-fail - probe correctly skipped the busy-wait)`.

**Command to see:** Non-graphical (disk I/O internals, and this specific scenario has no disk attached at all so nothing renders). Confirm via `make debug` on a normal (disk-attached) build instead, watching `ATA: primary master initialized ..., drive detected` in serial output.

---

### #16 — `stress_test()` file leak on partial failure
- **What was done:** The create→write→rename→delete cycle in `kernel/src/start.c`'s `stress_test()` broke on the first failed step with no cleanup, leaving whichever of `STRESS.TXT`/`STRESSR.TXT` the cycle got up to permanently on disk.
- **The actual fix:** Added a cleanup block after the loop: on any failure, call `fat_delete_file()` on both possible names, ignoring which one (if either) actually existed — a safe, complete cleanup regardless of which specific step failed.
- **Test:** Added a debug hook forcing failure right after `fat_create_file` on iteration 5. Kernel log confirms `Stress test: cleaned up STRESS.TXT/STRESSR.TXT after partial failure`. **Independently cross-checked via `mtools`**: `mdir` on the resulting image shows no `STRESS.TXT`/`STRESSR.TXT` at all. Reverting the cleanup and re-running the same forced failure left `STRESS.TXT` (0 bytes, from the create step) visible in `mdir` — the real leak the fix closes.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_stressleak.img
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_STRESS_LEAK"
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_stressleak.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -E "TEST:|Stress test:"
mdir -i /tmp/gos_disk_stressleak.img | grep -i stress   # expect no output - no leaked file
```
Expected: `Stress test: cleaned up STRESS.TXT/STRESSR.TXT after partial failure`, and the `mdir` grep for "stress" produces no output.

**Command to see:** Non-graphical (filesystem internals). Confirm via `make debug` watching serial output, and independently via `mdir -i <image>` from the host afterward.

---

### #17 — Unchecked `window_create()` return value
- **What was done:** `fm.c`'s dialog open, `editor.c`'s editor open, and `desktop.c`'s File Manager launch all called `window_create()`/`fm_create_window()` without checking for `-1`. Every callee guards internally against a `-1` index, so this never crashed, but hitting `MAX_WINDOWS=8` produced no dialog, no window, and no error — the app just appeared to do nothing.
- **The actual fix:** Added a new reusable mechanism, `taskbar_flash_message()` (`kernel/src/taskbar.c`), showing a brief red banner above the taskbar for ~2 seconds. All three call sites (`desktop.c`, `editor.c`, `fm.c`'s `fm_open_dialog`) now check for `-1`, log to serial, and call `taskbar_flash_message()`.
- **Test:** Filled all 8 window slots with debug "Filler" windows, then attempted to open the File Manager. Confirmed `fm_create_window()` returns `-1` and both the serial log and the on-screen flash fire correctly.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_WINDOW_CREATE_FEEDBACK"
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A5 "TEST: filled"
```
Expected: `TEST: filled 8 window slot(s) (8/8 open)`, `TEST: fm_create_window() returned 4294967295 (-1, correctly failed)`, `TEST: fm_create_window() failed (MAX_WINDOWS exhausted) - matches desktop.c's real failure path`.

**Command to see:**
```bash
make debug   # real graphical QEMU window
```
Screenshot: [screenshots/phase14_17_windowcreate_flash.png](screenshots/phase14_17_windowcreate_flash.png) — 8 "Filler" windows in the taskbar plus the red banner `"Could not open File Manager - too many windows open"`.

---

## Milestone 14.2: Low findings

### #18 — Rapid-reopen unsaved-edit loss
- **What was done:** `fm.c`'s double-click handler re-armed `fm_last_click_row`/`fm_last_click_tick` on *every* click, including the double-click event itself — a third rapid click within `FM_DOUBLE_CLICK_TICKS` (~400ms) of the second click re-triggered `editor_open()`, silently reloading from disk and discarding unsaved edits.
- **The actual fix:** On a double-click, invalidate `fm_last_click_row` to `-1` (an impossible row) instead of re-arming it to the current click. A subsequent rapid click now registers as a fresh single click (selection only), not a continuation of the double-click pair.
- **A real diagnostic detour:** Verifying the *pre-fix* bug proved harder to script reliably than expected. QEMU monitor `mouse_button`/`sendkey` commands don't map cleanly onto the kernel's internal 100Hz tick-based `FM_DOUBLE_CLICK_TICKS` window — several attempts at reproducing the pre-fix failure with real timing (including typing a marker string between clicks) landed outside the 400ms window regardless of whether the fix was active, making the pre/post comparison inconclusive via that specific method. The **fix itself was conclusively verified** via a different, reliable run: double-click `HOSTFILE.TXT` to open it in the editor, type a marker (`qqq`), then rapidly click the same row again — the serial log shows `"FM: selected file"` (not `"double-click-to-open"`) for that critical third click, and a screendump confirms the marker text survives in the editor's textbox. The fix's correctness is also directly verifiable by inspection: reverting it (moving the re-arm back into the `if (is_double_click)` branch) reintroduces exactly the mechanism the audit describes.
- **Test:** See above — log + screenshot confirm the fix's intended behavior; live pre-fix timing reproduction was attempted but proved too timing-sensitive to script deterministically via QEMU's monitor interface.

**Command to test:** (fixed-behavior verification, not a strict pre/post diff — see note above)
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_test18.img
make iso
(sleep 20; printf "mouse_move -588 -355\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.1; printf "mouse_button 0\n"; sleep 1.0; \
 printf "mouse_move 148 89\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 0.15; \
 printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 1.0; \
 printf "mouse_move -140 652\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 1.0; \
 printf "mouse_move 140 -652\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 0.15; \
 printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 0.1; \
 printf "sendkey q\n"; sleep 0.05; printf "sendkey q\n"; sleep 0.05; printf "sendkey q\n"; sleep 0.05; \
 printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 1.0; \
 printf "mouse_move -140 652\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.05; printf "mouse_button 0\n"; sleep 1.0; \
 printf "quit\n") | timeout 40 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -device piix3-ide,id=ide -drive id=gosdisk,file=/tmp/gos_disk_test18.img,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0 \
  -cdrom build/gos.iso -display none -serial stdio -monitor stdio | grep -iE "FM:|Editor:"
```
Expected final line: `FM: selected file "HOSTFILE.TXT"` (not another `double-click-to-open`).

**Command to see:**
```bash
make debug   # real graphical QEMU window; double-click a file, type text, click it again rapidly
```
Screenshot: [screenshots/phase14_18_unsaved_edit_survives.png](screenshots/phase14_18_unsaved_edit_survives.png) — editor shows the original file content plus the typed `qqq` marker, proving the rapid third click did not reload and discard it.

---

### #19 — Inconsistent "PANIC" logging
- **What was done:** Several non-fatal read failures (`ATA: PANIC - failed to read sector 0`, three `FAT32: PANIC - failed to read ...` boot-demo lines) printed the literal string `"PANIC"` but did **not** halt — boot correctly continued past them. This made grepping logs for `"PANIC"` an unreliable signal of whether the kernel actually halted.
- **The actual fix:** Relabeled all four non-halting sites from `"PANIC"` to `"ERROR ... (non-fatal, boot continues)"`. Every remaining `"PANIC"` string in the codebase now genuinely halts (`hcf()` or an inline `hlt` loop) — confirmed via full code review of every `grep -n "PANIC"` hit.
- **Test:** `grep -n "PANIC" kernel/src/*.c` and manually traced each of the 6 remaining hits (`start.c` Limine/memmap/framebuffer/HHDM/kernel-address checks, `start.c`'s `fat32_init()` failure, `heap.c`'s `heap_init` failure, `fb.c`'s `fb_backbuffer_init` failure) — all confirmed to call `hcf()` or an equivalent halt loop immediately after logging.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
grep -n "PANIC" kernel/src/*.c
```
Expected: every hit is either inside a `!x_response` / allocation-failure check immediately followed by `hcf()`/`for(;;){hlt}`, or is the (now correctly halting, per Phase 12) `fat32_init()` failure check. No hit should be a recoverable/continuing path.

**Command to see:** Documentation/logging-only change — no visual surface. Confirm via `make debug` and reading the serial output for the "ERROR" relabeling on a normal boot (no ATA/FAT32 read failures occur on a working disk, so these specific lines won't print during a normal run — see Phase 12's `phase12.md` Milestone 12.2 for a live corrupted-filesystem repro showing the *halting* `FAT32: PANIC` path).

---

### #20 — `pmm_init` bitmap-placement ordering hazard
- **What was done:** `kernel/src/pmm.c`'s bitmap-placement search picked the first USABLE region large enough for the bitmap, without checking whether that region overlapped Limine's own `memmap->entries[]` array — which the same function keeps reading from *after* the bitmap is placed and zero-initialized. An overlap would have corrupted the memmap data mid-function.
- **The actual fix:** Compute the memmap entries array's physical range (`(uint64_t)memmap->entries - hhdm_off` through `+ entry_count * sizeof(pointer)`, since Limine places its response structures within the HHDM-covered range) and skip any USABLE candidate region that overlaps it during bitmap placement. A permanent serial log line reports the excluded range on every boot.
- **Test:** Booted normally and confirmed the new log line `PMM: memmap entries array physical range 0x... - 0x... (bitmap placement excludes this range)` appears, plus the existing `PMM self-test: PASS` continues to pass — no candidate region needed to be skipped on this system's actual memory layout (the audit itself called this "plausible but not confirmed-triggered"), but the guard is now permanently in place and provably executes every boot.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run 2>&1 | grep -A1 "PMM: memmap entries array"
```
Expected: `PMM: memmap entries array physical range 0x... - 0x... (bitmap placement excludes this range)`, followed by normal `PMM: highest physical address: ...` / `PMM self-test: PASS` output.

**Command to see:** Non-graphical (PMM internals, runs before the framebuffer is even initialized). Confirm via `make debug` watching serial output.

---

### #21 — Disk image Makefile target not idempotent/versioned
- **What was done:** The `Makefile`'s `$(DISK_IMG)` target had no dependency tracking at all — if the seed recipe (image size, `mformat` arguments) ever changed, an existing checkout would silently keep using a stale image built with the old recipe.
- **The actual fix:** Added `DISK_RECIPE` (the exact recipe text as a Makefile variable) and `DISK_RECIPE_HASH` (a SHA-256 of that text). A new `check-disk-recipe` phony target compares the current hash against one stored in `disk_images/.disk_recipe_hash`; if they differ (including "first run," when the hash file doesn't exist yet), it deletes `$(DISK_IMG)`, forcing the existing `$(DISK_IMG)` rule to rebuild it. An unrelated `make`/`make run` with an unchanged recipe leaves an existing image (and its data) untouched, exactly as before.
- **A real incident during testing:** Verifying this finding required simulating "first run" (deleting the hash file) and "recipe changed" (editing `DISK_RECIPE`) scenarios — both of which correctly triggered a rebuild **on the live `disk_images/gos_disk.img`** rather than a scratch copy, wiping the host-seeded test fixtures (`HOSTFILE.TXT`, `TESTDIR/NESTED.TXT`, the `LEVEL1/LEVEL2/LEVEL3` tree from Phase 9's original setup) that aren't recreated by the kernel itself. This was caught immediately, flagged to the user, and resolved by re-seeding equivalent fixtures via `mtools` using the exact commands documented in `version1/phase8.md`/`version1/phase9.md`. `PERSIST.TXT` self-healed automatically on the next boot (the kernel creates it if missing). A full boot afterward confirmed all FAT32 self-tests pass again with no `ERROR`/`PANIC` lines.
- **Test (redone correctly, via scratch copies):** (1) Confirmed an unchanged-recipe `make disk` leaves the image's mtime and contents untouched. (2) Using a **separate scratch copy of the Makefile** (not the live one), changed the `mformat` volume label and confirmed `make disk` printed `"Disk image seed recipe changed (or first run) - forcing rebuild"` and produced a new image with the changed label — proving the detection and rebuild both work — then restored the original Makefile and disk image from a pre-test backup.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
# Unchanged-recipe run should NOT touch the image:
BEFORE=$(stat -f %m disk_images/gos_disk.img); make disk; AFTER=$(stat -f %m disk_images/gos_disk.img)
[ "$BEFORE" = "$AFTER" ] && echo "OK: unchanged recipe left image untouched"
```
Expected: `OK: unchanged recipe left image untouched`. (To test the rebuild-on-change path, edit `DISK_RECIPE` in a **scratch copy** of the Makefile, run `make -f <scratch-makefile> disk`, and confirm the log line + volume-label change — do not run this against the live Makefile/image without backing both up first.)

**Command to see:** Build-tooling only, no visual/runtime surface — verify via `mdir -i disk_images/gos_disk.img` from the host showing the expected volume label/contents.

---

### #22 — `fb_backbuffer_init()` re-entrancy guard
- **What was done:** `kernel/src/fb.c`'s `fb_backbuffer_init()` unconditionally called `kmalloc()` for the back buffer — fine since it's called exactly once at boot today, but a second call would leak a multi-MB allocation (the old `back_buffer` pointer silently overwritten, no `kfree()`).
- **The actual fix:** Added an `if (back_buffer)` guard at the top that logs and returns early on a repeat call, instead of allocating again.
- **Test:** Added a debug hook calling `fb_backbuffer_init()` a second time immediately after the real call, comparing `pmm_free_pages()` before and after. Confirmed **unchanged** free-page count (`50676` both times) and the guard's log line (`"FB: fb_backbuffer_init() called again - already initialized, ignoring"`) fires correctly.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_FB_BACKBUFFER_REENTRY"
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A6 "TEST: calling"
```
Expected: `TEST: PMM free pages before=50676 after=50676 (unchanged - no double allocation)` (exact page count may vary slightly by system, but before/after must match).

**Command to see:** Non-graphical (heap/PMM internals). Confirm via `make debug` watching serial output.

---

### #23 — `fm_open_dialog` "one at a time" guard gives no feedback
- **What was done:** `fm.c`'s `fm_open_dialog()` correctly dropped a second dialog request while one was already open, but gave no feedback at all — clicking a second toolbar button (e.g. "New File" while "New Folder"'s dialog is open) did nothing visible.
- **The actual fix:** Reused the `taskbar_flash_message()` mechanism built for #17. The guard now logs to serial and shows `"A dialog is already open - close it first"` as an on-screen flash.
- **Test:** Opened the File Manager, clicked "New Folder" (opens a dialog), then immediately clicked "New File" (should be dropped). Serial log confirms both the click and the drop: `FM: [New Folder] clicked`, `FM: [New File] clicked`, `FM: fm_open_dialog() ignored - a dialog is already open`. Screendump confirms the red flash banner renders on screen.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_test23.img
make iso
(sleep 20; printf "mouse_move -588 -355\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.1; printf "mouse_button 0\n"; sleep 1.5; \
 printf "mouse_move 171 54\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.1; printf "mouse_button 0\n"; sleep 1.0; \
 printf "mouse_move 89 0\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.1; printf "mouse_button 0\n"; sleep 1.0; \
 printf "quit\n") | timeout 35 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -device piix3-ide,id=ide -drive id=gosdisk,file=/tmp/gos_disk_test23.img,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0 \
  -cdrom build/gos.iso -display none -serial stdio -monitor stdio | grep "FM:"
```
Expected: `FM: [New Folder] clicked`, `FM: [New File] clicked`, `FM: fm_open_dialog() ignored - a dialog is already open`.

**Command to see:**
```bash
make debug   # real graphical QEMU window; open a dialog, click another toolbar button
```
Screenshot: [screenshots/phase14_23_dialog_guard_flash.png](screenshots/phase14_23_dialog_guard_flash.png) — the red banner `"A dialog is already open - close it first"` visible above the taskbar.

---

### #24 — Double-click identity is row-index-based, not filename-based
- **What was done:** Documentation-only, per the chosen approach and the audit's own assessment that this is a latent fragility, not an active bug (safe today only because every mutation path happens to call `fm_refresh()` first, resetting the click-tracking state).
- **The actual fix:** Added a comment block at `fm_last_click_row`'s declaration in `kernel/src/fm.c` documenting the invariant explicitly: row-index-based identity is only safe because `fm_refresh()` resets `fm_last_click_row` to `-1` on every mutation before the listing could change under a user's fingers, and any future mutation path that skips `fm_refresh()` would silently break this.
- **Test:** Code review — traced all 5 mutation paths (`fat_create_dir`, `fat_create_file`, `fat_rename`, `fat_delete_dir`/`fat_delete_file`, `fm_navigate_into`/`fm_navigate_up`) and confirmed each calls `fm_refresh()` immediately after its FAT32 operation, before returning control to the click handler.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
grep -n "fm_refresh();" kernel/src/fm.c
```
Expected: 6 call sites — `fm_navigate_into`, `fm_navigate_up` (via shared `fm_refresh()` calls), the New Folder/New File/Rename confirm handlers, and the Delete confirm handler — each immediately following the corresponding `fat_*` mutation call.

**Command to see:** Documentation-only change, no runtime behavior difference — nothing to observe visually.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| #18 test: triple-click with typed text, confirm survival | First reliable positive-evidence run succeeded, but subsequent attempts to also capture the pre-fix negative case via the same script were timing-flaky regardless of fix state | QEMU monitor `mouse_button`/`sendkey` command timing doesn't map deterministically onto the kernel's internal 100Hz tick-based 400ms window; documented honestly rather than forcing an unreliable repro |
| #21 test: verify recipe-change detection | First two verification attempts ran directly against the live `disk_images/gos_disk.img`, wiping host-seeded test fixtures not recreated by the kernel | Should have used a scratch copy from the start, per the pattern established in every other phase; caught immediately, flagged to the user, and fully recovered via `mtools` re-seeding using the exact commands documented in `version1/phase8.md`/`version1/phase9.md` |
| #24: switch to filename-based tracking | Kept as documentation-only, matching the recommended/chosen option | Audit itself explicitly assessed this as latent, not active - the invariant (every mutation path calls `fm_refresh()`) holds today and is now explicitly documented for future maintainers |

---

## Effort Estimate vs. Actual

| | Original estimate (project-plan-2.md) | Actual |
|---|---|---|
| Milestone 14.1 (6 Medium findings) | included in Phase 14's 14–20h | ~5.5 hours |
| Milestone 14.2 (6 Low findings) | included in Phase 14's 14–20h | ~4.5 hours (incl. the #21 disk-image recovery detour) |
| **Phase 14 total** | **14–20 hours** | **~10 hours** |

**Why some findings took longer than their fixes alone would suggest:** As with Phases 12/13, most individual code changes were small; the time went into designing genuine reproductions (#15's busy-wait counter, #16's forced mid-cycle failure, #17/#23's real MAX_WINDOWS-exhaustion and dialog-guard scenarios via simulated mouse input) rather than accepting "code compiles" as sufficient. The #21 incident added real, if brief, recovery time but is documented in full rather than glossed over, consistent with the project's standing practice of recording symptom/diagnosis/fix for anything unexpected encountered during verification.

---

## Per-Milestone Testing Instructions

See each finding's **Command to test** / **Command to see** sections above — self-contained and copy-pasteable from the project root (`cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`).

### Quick regression check for all of Phase 14 together
```bash
make clean && make run 2>&1 | grep -E "PANIC|EXCEPTION|Stress test:|Desktop ready|Demo windows"
```
Expected — a clean, normal boot with none of the `GOS_TEST_*` macros defined:
```
Demo windows (Window A/B, Text Editor) auto-closed - 0 window(s) open, 8 slot(s) free
Stress test: PASS (150 file cycles, 300 window cycles, no crash)
Desktop ready - click the "Files" icon to launch the File Manager
```
No `PANIC` or `EXCEPTION` lines should appear. All test scaffolding added in this phase (`GOS_TEST_SPURIOUS_IRQ_CHECK`, `GOS_TEST_ATA_PROBE`, `GOS_TEST_STRESS_LEAK`, `GOS_TEST_WINDOW_CREATE_FEEDBACK`, `GOS_TEST_FB_BACKBUFFER_REENTRY`) is `#ifdef`-gated and compiles out entirely in a normal build.

---

## Track A Complete

With Phase 14 done, all 24 findings from `version1/audit.md` are closed:
- ✅ 5 Critical (Phase 12)
- ✅ 6 High (Phase 13)
- ✅ 6 Medium + 6 Low (Phase 14, this document)

Per project-plan-2.md's priority rule, **Track B (new features) may now begin** — Phase 15 (Cursor & Wallpaper) has no dependency on Track A fixes, but Phase 16 (Window Close, Minimize & Taskbar) directly depends on Phase 12's `kfree` double-free fix and Phase 13's stale-window-dispatch fix, both already in place.

---

## Not Yet Verified (intentionally deferred to Track B)

- [ ] Phase 15 — Cursor & Wallpaper
- [ ] Phase 16 — Window Close, Minimize & Taskbar
- [ ] Phase 17 — Maximize & Polish (optional/stretch)
- [ ] README.md's final Track-B-complete re-touch (Milestone 14.0 is a living task, re-touched again at the end of Track B per project-plan-2.md)

---

## Next Step

Proceed to **Phase 15 — Cursor & Wallpaper**, the first Phase of Track B, per [project-plan-2.md](project-plan-2.md). This is the first new-feature phase since v1.0 — the real arrow cursor and wallpaper layer have no dependency on any Track A fix, so they can be built without further audit-remediation prerequisites.
