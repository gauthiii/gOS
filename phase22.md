# Phase 22 — RTC Driver, Taskbar Clock & Settings Persistence — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed on the first real test, no bugs found.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

The taskbar now shows a live clock, right-aligned. Press **F2** to toggle between the bundled wallpaper and the plain gradient — the choice, along with the File Manager's current position/size, is saved automatically to `GOS.CFG` on the FAT32 disk and restored on the next boot.

---

## Summary

Phase 22 closes Track D's clock/settings gap. Before implementing, two genuinely underspecified parts of the plan were clarified with the user:
1. **Wallpaper "choice"** — gOS has no multi-wallpaper picker UI, so the user confirmed the real, minimal interpretation: a toggle between the bundled BMP and a plain gradient (F2), not an invented picker.
2. **Save trigger** — gOS has no real shutdown/reboot hook to "save on", so the user confirmed auto-save-on-change instead of inventing a fake graceful-shutdown trigger.

- **22.1 (RTC driver)** reads the CMOS real-time clock (ports 0x70/0x71), handling the update-in-progress race (re-read until two consecutive reads agree), BCD-to-binary conversion, and 12-hour/PM-bit normalization via Status Register B.
- **22.2 (taskbar clock)** renders a live `HH:MM:SS` right-aligned in the taskbar, re-reading the RTC at most once per second (gated by the PIT tick count, not re-touching CMOS every ~50ms frame for a display that only has one-second resolution anyway).
- **22.3 (settings persistence)** — a fixed-size binary `GOS.CFG` record (magic + version + `gradient_forced` + File Manager geometry), auto-saved whenever the F2 toggle fires or the File Manager's position/size actually changes (checked once per frame in `desktop.c`, itself a no-op if nothing changed — no per-frame FAT32 writes when nothing's moving).

Files touched (new): `kernel/include/rtc.h`, `kernel/src/rtc.c`, `kernel/include/settings.h`, `kernel/src/settings.c`. Files touched (modified): `kernel/src/taskbar.c`, `kernel/include/wallpaper.h`/`wallpaper.c`, `kernel/include/keyboard.h`/`keyboard.c`, `kernel/src/desktop.c`, `kernel/src/start.c`.

---

## Milestone 22.1: CMOS RTC driver

- **What was done:** `rtc_read()` (`kernel/src/rtc.c`) polls Status Register A's "update in progress" bit, reads seconds/minutes/hours/day/month/year, re-reads once more, and loops until two consecutive readings agree (the standard technique for avoiding a read that straddles the RTC's internal update tick). Status Register B's bits then decide whether to BCD-decode the raw values (bit 2) and whether to interpret the hour's bit 7 as a 12-hour-mode PM flag (bit 1) — masking that PM bit off *before* BCD-converting the hour and re-applying it after, since it isn't itself part of the BCD digits.
- **Test:** booted with QEMU's `-rtc base=2026-03-15T14:22:07` (a value QEMU is told to present, not something gOS could ever fabricate on its own) and logged the driver's parsed result directly to serial via a temporary `GOS_TEST_RTC` debug hook.
- **Result:** `TEST: RTC read - 2026-3-15 14:22:9` — year/month/day and hour/minute match the requested base value **exactly**; the 2-second difference in the seconds field is exactly the real wall-clock delay between QEMU starting the guest and this log line executing (boot isn't instantaneous), not a parsing error — confirmed by the fact every other field, including the minute, matches perfectly (a real bug in the seconds path specifically would be an odd and unlikely failure mode to produce a value that's merely a *few seconds ahead*, consistent with elapsed real time, rather than a symptom like all-zero, garbled BCD digits, or an obviously wrong magnitude).

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_RTC"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M -rtc base=2026-03-15T14:22:07 \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST: RTC" $S/serial.log
```
Expected: `TEST: RTC read - 2026-3-15 14:22:X` where `X` is a small number ≥ 7 (the exact requested second plus whatever real boot delay elapsed) — year/month/day/hour/minute must match exactly.

**Command to see:** non-graphical by nature (a single log line, not a rendered scene) — Milestone 22.2 below is where this becomes visible on screen.

---

## Milestone 22.2: Taskbar clock widget

- **What was done:** `taskbar_render()` (`kernel/src/taskbar.c`) now draws a right-aligned `HH:MM:SS` string, refreshed via `clock_update_if_due()` — which only calls `rtc_read()` once real time has actually advanced a full second (tracked via the PIT tick count, not by re-touching the CMOS ports on every ~50ms frame for a display that can't show sub-second resolution anyway).
- **Test:** booted with `-rtc base=2026-03-15T14:22:00` (precise QEMU-controlled starting time) and took two `screendump`s exactly 10 seconds apart (via the QEMU monitor's own `sleep`-gated script, not relying on gOS's own timing).
- **Result:** `14:22:04` → `14:22:14` — **exactly** 10 seconds of displayed advance for a 10-second real gap, independently timed by the host script issuing the two `screendump` commands, not by anything gOS itself reported.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk build/OVMF_VARS.fd   # normal (non-test) build
S=$(mktemp -d)
(sleep 6; echo "screendump $S/before.ppm"; sleep 10; echo "screendump $S/after.ppm"; sleep 0.3; \
 echo "quit") | timeout 25 qemu-system-x86_64 \
  -M q35 -m 256M -rtc base=2026-03-15T14:22:00 \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
open $S/before.ppm $S/after.ppm
```
Expected: the taskbar clock in `after.ppm` reads exactly 10 seconds later than `before.ppm`.

**Command to see:**
```bash
make run   # graphical QEMU window - watch the taskbar clock's seconds field tick upward live
```
Screenshots: [screenshots/phase22_clock_before.png](screenshots/phase22_clock_before.png) (`14:22:04`), [screenshots/phase22_clock_after.png](screenshots/phase22_clock_after.png) (`14:22:14`, exactly 10 seconds later).

---

## Milestone 22.3: Settings persistence

- **What was done:** `settings.c` defines a fixed-size, packed `struct settings_record` (magic `'GOSG'` + version + `gradient_forced` byte + File Manager `x`/`y`/`w`/`h`) written to `GOS.CFG` on the FAT32 root. `settings_load()` runs once at boot (after `wallpaper_init()`, so a persisted `gradient_forced=1` correctly overrides whatever the BMP loader just decided) and applies the wallpaper mode immediately; the File Manager geometry is just cached until `desktop.c` actually creates that window. `settings_save()` is called from exactly two places: `keyboard.c`'s F2 handler (via `desktop.c`), and `settings_record_fm_geometry()` — invoked once per frame while the File Manager is open, but it only actually triggers a FAT32 write if the geometry genuinely differs from what's already recorded, so dragging/resizing doesn't spam writes every single frame once the window is stationary again (it does write once per *changed* frame during an active drag, which is more chatty than strictly necessary but still correct and simple).
- **Test — three independent layers:**
  1. **The kernel's own behavior:** opened the File Manager, pressed F2 (wallpaper switched from the bundled BMP to the plain gradient), then dragged the window to `(200,150)` and resized it to `500×300`. Serial log shows `Settings: saved GOS.CFG` firing after the toggle and after each geometry change.
  2. **Independent cross-check of the raw file, entirely outside gOS:** `mcopy`'d `GOS.CFG` off the disk image and inspected it with `xxd` — every field matches by hand: magic bytes `47 4f 53 47` (`'GOSG'` read as a little-endian `uint32_t` = `0x47534F47`, exactly `SETTINGS_MAGIC`), version `1`, `gradient_forced=1`, and the geometry fields decode to exactly `x=200, y=150, w=500, h=300` — the precise values I dragged/resized to, verified byte-for-byte with a tool that has no idea what gOS's in-memory state was.
  3. **The real persistence proof — a separate, fresh QEMU process** against the same disk image: booted with no interaction at all, and the desktop came up showing the **gradient** (not the BMP) immediately, with no F2 press in this session. Opening the File Manager placed it at exactly `(200,150)` sized `500×300` — not the compiled-in default `(120,60,420,260)` — and the serial log confirms: `Settings: loaded GOS.CFG - gradient_forced=1 fm=(200,150,500x300)`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk build/OVMF_VARS.fd
# Boot 1: toggle wallpaper, move/resize the File Manager, then quit.
S=$(mktemp -d)
(sleep 5; \
 echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.5; \
 echo "sendkey f2"; sleep 0.6; \
 echo "mouse_move 98 20"; sleep 0.3; echo "mouse_button 1"; sleep 0.3; \
 echo "mouse_move 80 90"; sleep 0.3; echo "mouse_button 0"; sleep 0.5; \
 echo "mouse_move 385 267"; sleep 0.3; echo "mouse_button 1"; sleep 0.3; \
 echo "mouse_move 85 45"; sleep 0.3; echo "mouse_button 0"; sleep 0.8; \
 echo "quit") | timeout 20 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial1.log -monitor stdio >/dev/null

# Independent, host-side raw file check (no gOS involved):
mcopy -i disk_images/gos_disk.img ::GOS.CFG /tmp/gos_cfg.bin
xxd /tmp/gos_cfg.bin

# Boot 2: a completely fresh QEMU process, same disk, no interaction.
S2=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S2/serial2.log -monitor none
grep "Settings: loaded" $S2/serial2.log
```
Expected `xxd` output includes `474f 5347 0100 0000 0100 0000` (magic+version+gradient_forced=1) and, later in the dump, `c800 0000 0000 0000` (200), `9600 0000 0000 0000` (150), `f401 0000 0000 0000` (500), `2c01 0000 0000 0000` (300) — each an exact little-endian 8-byte integer. Expected `serial2.log` line: `Settings: loaded GOS.CFG - gradient_forced=1 fm=(200,150,500x300)`.

**Command to see:**
```bash
make run   # graphical QEMU window; press F2 to toggle wallpaper, drag/resize the File
           # Manager, then quit and re-run `make run` again (same disk image) - the
           # wallpaper choice and window geometry are both exactly as you left them
```
Screenshots: [screenshots/phase22_before_toggle.png](screenshots/phase22_before_toggle.png) (BMP wallpaper, default File Manager position), [screenshots/phase22_after_toggle.png](screenshots/phase22_after_toggle.png) (F2 pressed — gradient now showing, same session), [screenshots/phase22_final_state.png](screenshots/phase22_final_state.png) (File Manager moved/resized to `(200,150,500×300)`), [screenshots/phase22_restored_wallpaper.png](screenshots/phase22_restored_wallpaper.png) (a **fresh** boot — gradient already showing, no F2 pressed this session), [screenshots/phase22_restored_fm.png](screenshots/phase22_restored_fm.png) (same fresh boot — File Manager opens directly at the persisted `(200,150,500×300)`, and `GOS.CFG` itself is visible as a real file in the listing).

---

## Bugs found & fixed during this phase

**None.** All three milestones passed their first real test, including the independent host-side `xxd` cross-check of `GOS.CFG`'s raw bytes matching by hand. The two underspecified design questions (wallpaper "choice" mechanism, save trigger) were resolved with the user *before* implementation via `AskUserQuestion`, not discovered as bugs during testing.

---

## Phase 22 exit criterion — met

A working taskbar clock reflecting real time (proven by an exact 10-second advance between two precisely-timed `screendump`s against a QEMU-controlled `-rtc` base), and user preferences (wallpaper mode, File Manager geometry) that survive a reboot — verified independently via `mtools`' `xxd` on the raw `GOS.CFG` bytes, not just gOS's own serial log, and confirmed end-to-end with a genuinely separate, fresh QEMU process restoring both settings with no interaction.
