# Phase 26 — Audit 2: High-Severity Fixes — Completion Report

**Date completed:** 2026-07-04
**Status:** ✅ Complete — all 6 High findings from [version2/audit2.md](version2/audit2.md) fixed, each with a QEMU-verified reproduction-then-fix test. One test-design issue (not a kernel bug) found and corrected during testing — see Bugs section.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Two of these six fixes are directly observable: right-click the desktop near an open window (the menu now always renders on top, never hidden underneath), and in the Terminal, try backspacing past the prompt (it now stops cleanly at the prompt boundary instead of eating into prior output).

---

## Summary

Same standard as Phase 25: a demonstrated reproduction before the fix, a passing test after.

- **26.1** frees everything a failed `process_spawn()` attempt already mapped (reusing Phase 25.2's `vmm_destroy_process_pml4()`), instead of abandoning it.
- **26.2** adds `process_kill()` and a scheduler watchdog (`kernel/src/process.c`): `scheduler_run_until_done()` now sets a ~5-second time budget, and `scheduler_reschedule()` force-kills whatever's still running if that budget is exceeded — the only way an infinite-looping `run` command can end today, short of Phase 29's real `SYS_KILL`.
- **26.3** adds a parent-pid ownership check to `SYS_WAITPID` (`kernel/src/syscall.c`) — a process can now only reap its own children.
- **26.4** adds `rollback_partial_entries()` (`kernel/src/fat32.c`), which marks every slot a failed multi-entry LFN write already committed back to deleted (`0xE5`) instead of leaving a truncated, checksum-orphaned run behind.
- **26.5** moves the desktop's wallpaper-menu drawing from `desktop_render()` (bottom compositing layer, before windows) to a new `desktop_render_menu_overlay()` called after `window_composite()` — a true top-layer overlay, like the mouse cursor.
- **26.6** adds a tracked prompt-end offset to the Terminal (`kernel/src/terminal.c`), refusing to backspace at or before it.

Three new debug-only test ELFs were added: `tools/userland/badptr.asm` (from Phase 25, reused), `tools/userland/waitpid_test.asm` (tries `SYS_WAITPID` on every pid despite owning none of them), and `tools/userland/infloop.asm` (never calls `SYS_EXIT`, to test the watchdog). Fault injection (`GOS_TEST_FAULT_INJECT`, introduced in Phase 25) was extended to cover `write_dirent_at` in addition to the erase path, and a new `process_test_inject_spawn_failure()` hook simulates PMM exhaustion at an exact point in `process_spawn()`.

Files touched (new): `tools/userland/waitpid_test.asm`, `tools/userland/infloop.asm`. Files touched (modified): `kernel/include/process.h`, `kernel/src/process.c`, `kernel/src/syscall.c`, `kernel/include/fat32.h`, `kernel/src/fat32.c`, `kernel/include/desktop.h`, `kernel/src/desktop.c`, `kernel/src/terminal.c`, `kernel/src/start.c`, `Makefile`.

---

## Milestone 26.1: Free partially-built address space on `process_spawn()` failure

- **What was done:** every failure path in `process_spawn()`'s PT_LOAD/stack-mapping loops now calls `vmm_destroy_process_pml4(pml4_phys, PROC_PML4_SLOT)` before returning `-1`, instead of abandoning the partially-built address space. A debug-only `process_test_inject_spawn_failure(after_n_pages)` hook simulates a page-allocation failure at an exact point.
- **Test:** captured a `pmm_free_pages()` baseline, forced the 3rd page allocation during a `HELLO.ELF` spawn to fail, confirmed the spawn correctly fails, and confirmed free-page count returns to the exact baseline.
- **Result:** `pmm_free_pages() after failed spawn = 48882 (== baseline, OK - no leak)`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_FAULT_INJECT"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST: pmm_free_pages() baseline (26.1)\|TEST: process_spawn(HELLO.ELF) with forced\|TEST: pmm_free_pages() after failed spawn" $S/serial.log
```
Expected: the "after failed spawn" line matches the baseline line exactly.

**Command to see:** no direct visual manifestation (a kmalloc/pmm-alloc-count fix) — the observable proof is the serial log above.

---

## Milestone 26.2: `SYS_KILL` + a way for Terminal's `run` to use it

- **What was done:** `process_kill(pid)` marks a process `PROC_ZOMBIE` with a distinguishable exit code (`-2`) and immediately reclaims its resources. `scheduler_run_until_done()` sets a `run_deadline_tick` (5 seconds at the 100Hz PIT rate); `scheduler_reschedule()` checks it on every reschedule point and kills whatever's currently running if the budget is exceeded. The Terminal now reports a killed process distinctly (`"was killed (did not exit within the time budget)"`) instead of printing `-2` as if it were an ordinary exit code.
- **Test:** `INFLOOP.ELF` (new — prints a marker then spins forever) spawned and run; confirmed `scheduler_run_until_done()` actually returns (proving the call itself didn't hang), and the process's exit code is `-2`.
- **Result:** `scheduler_run_until_done() RETURNED after ~500 ticks (did not hang)`; `INFLOOP.ELF exit_code = -2 (== -2, OK - killed by watchdog)`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_PHASE26"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST: scheduler_run_until_done\|TEST: INFLOOP" $S/serial.log
```
Expected: both lines quoted in the Result section above.

**Command to see:**
```bash
make run
# Click Terminal, type: run Infloop.Elf   [Enter]
# Wait ~5 seconds - "process 0 was killed (did not exit within the time
# budget)" appears, and the desktop remains fully interactive throughout.
```
No screenshot captured for this milestone specifically (a ~5-second wait mid-command is awkward to screendump meaningfully beyond the same responsiveness already shown for 25.1) — the serial-log proof above is the primary evidence, matching how Milestone 25.2 was verified.

---

## Milestone 26.3: `SYS_WAITPID` parent-pid ownership check

- **What was done:** `SYS_WAITPID`'s handler now also requires `target->parent_pid == scheduler_current_pid()`, rejecting (`-1`) any attempt to reap a zombie that isn't the caller's own child.
- **Test:** `WPTEST.ELF` (new) never spawns anything itself, then loops `SYS_WAITPID` over every pid `0..7` — none of which are its own child. A real zombie (`SPIN1.ELF`, kernel-spawned, `parent_pid=-1`) exists among those pids when the test runs.
- **Result:** `WAITPID_TEST: all non-parent waitpid attempts correctly rejected`; `WPTEST.ELF exit_code = 99 (== 99, OK - every non-parent waitpid rejected)`. Re-ran the pre-existing Phase 20 `PARENT.ELF`/`CHILD.ELF` demo (a genuine parent reaping its own real child) to confirm no regression: `PARENT_GOT:7` — the legitimate case still round-trips the exact exit code.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_PHASE26"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "WAITPID_TEST\|TEST: WPTEST" $S/serial.log
# Independent regression check - the legitimate parent/child case:
make iso CFLAGS="... -DGOS_TEST_MULTITASKING"   # (full flags as above, swap the -D)
# re-run the same way; expect "PARENT_GOT:7" in the log.
```
Expected: the two lines in the Result section, plus `PARENT_GOT:7` from the regression check.

**Command to see:** a syscall-level access-control check has no direct visual manifestation — the serial log is the authoritative evidence, cross-checked against the legitimate-case regression above.

---

## Milestone 26.4: Fix `write_named_entry`'s multi-slot LFN write partial-failure sentinel

- **What was done:** `rollback_partial_entries(first, count)` marks every slot already committed in a failed multi-entry LFN write back to `0xE5` (deleted), for both the "failed mid-LFN-entry" and "failed writing the final SFN" cases.
- **Test:** created three sentinel files, then fault-injected a write failure on the 2nd write of a long-filename create (letting the first LFN entry through, then failing) in the same directory. Confirmed the create failed, and — the property that actually matters — every sentinel remained resolvable and the directory's total entry count was completely unchanged before vs. after.
- **Result:** `sentinels still resolvable after the failed LFN create = 1 (OK)`; `directory entry count before vs after failed create = 22 vs 22 (OK - unchanged)`.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_FAULT_INJECT"
make disk build/OVMF_VARS.fd
S=$(mktemp -d)
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST: fat_create_file(long name)\|TEST: sentinels\|TEST: directory entry count" $S/serial.log
```
Expected: the three lines in the Result section above.

**Command to see:**
```bash
mdir -i disk_images/gos_disk.img :: | grep -i SENT
# Expect SENT1.TXT, SENT2.TXT, SENT3.TXT all present, independently
# confirmed via mtools rather than gOS's own listing.
```

---

## Milestone 26.5: Fix desktop right-click menu z-order and hit-test correctness

- **What was done:** the wallpaper menu's drawing code moved out of `desktop_render()` (called before `window_composite()`) into a new `desktop_render_menu_overlay()`, called after `window_composite()`/`taskbar_render()` — the same top-layer position the mouse cursor already occupies. This alone resolves both halves of the original finding: once the menu is genuinely drawn on top, a click landing in its visible footprint is now consistent with what's on screen — there's no longer a "click hits something invisible" mismatch to separately guard against.
- **Test:** right-clicked empty desktop just above an open File Manager window, positioned so the menu's downward extent would overlap the window.
- **Result:** the menu renders fully visible on top of the File Manager window and both other desktop icons (screendump), and a subsequent click within the overlapping region correctly selected a menu row ("Mac"), not the window underneath.

**Command to test:** a rendering-order/z-order fix has no independent non-visual assertion — the screendump below *is* the test.

**Command to see:**
```bash
make run
# Open File Manager, then right-click just above its titlebar (close
# enough that the menu's downward extent would overlap the window) -
# the menu now renders fully on top, never hidden underneath.
```
Screenshots: [screenshots/phase26_menu_over_window.png](screenshots/phase26_menu_over_window.png) (menu fully visible on top of the File Manager window and taskbar icons), [screenshots/phase26_menu_click_works.png](screenshots/phase26_menu_click_works.png) (a click in the overlapping region correctly selects a menu row, confirmed via the serial log showing `wallpaper selection now 3 (Mac)`).

---

## Milestone 26.6: Terminal prompt-boundary guard

- **What was done:** `terminal_on_key()` now intercepts `'\b'` directly: if `textbox_length <= prompt_end_offset` (a new tracked value, updated every time `term_print_prompt()` runs), the backspace is consumed and does nothing, instead of falling through to the shared textbox handler's unconditional delete.
- **Test:** typed `ls`, pressed Enter, then sent 40 backspace keypresses (far more than needed to erase through the 3-character prompt `"/> "` if unguarded), then typed `help` and pressed Enter.
- **Result:** the prompt line remains fully intact after 40 backspaces (screendump), and `help` parses and executes exactly as typed — `Terminal: command "help"` in the serial log, with the correct command list printed, not a corrupted mixture of scrollback and typed text.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run   # (normal build - this is pure UI behavior, no debug flag needed)
```
Then, via a scripted QEMU monitor session: type `ls` + Enter, send 40 `sendkey backspace`, then type `help` + Enter; grep the serial log for `Terminal: command` and confirm it reads exactly `"help"`.

**Command to see:**
```bash
make run
# Click Terminal, type: ls   [Enter]
# Press Backspace ~40 times (far more than the prompt's own length)
# Type: help   [Enter]
```
Screenshots: [screenshots/phase26_backspace_guard.png](screenshots/phase26_backspace_guard.png) (prompt `/>` still fully intact after 40 backspaces), [screenshots/phase26_help_after_backspace.png](screenshots/phase26_help_after_backspace.png) (`help` command parses and executes correctly afterward).

---

## Bugs found & fixed during this phase

**Test-design issue (not a kernel bug) — extending fault injection to `write_dirent_at` shifted where a shared countdown landed, breaking an existing Phase 25 test.**
- **Symptom:** after adding fault injection to `write_dirent_at` (needed for Milestone 26.4's own test), Phase 25.3's "1 transient erase failure" test started failing (`fat_rename with 1 transient erase failure = 0 (FAIL)`), despite no change to the actual `fat_rename` fix itself.
- **Diagnosis:** the fault-injection countdown is a single shared global, and `fat_rename`'s own `write_named_entry` call (writing the *new* entry) now also consumes it — for a short (non-LFN) name like the test's `RENTEST.TXT`, that's exactly one write, happening *before* `erase_dirent_and_lfn` gets its turn. A countdown of `0` ("fail the very next write") now fired on that earlier write step instead of the intended erase step, so the rename failed for a different (though still simple) reason than the test meant to exercise.
- **What was tried that didn't work:** nothing else was tried — the serial log's `[fault injection] simulated write failure` line appearing before any erase-related log output made the cause immediately clear.
- **Fix:** changed the test's injected countdown from `0` to `1` (`fat32_test_inject_write_failure(1)`), explicitly letting the rename's own single write through before failing the first erase write — restoring the test's original intent. Documented the shared-countdown behavior in a code comment at the call site so a future test author doesn't hit the same surprise.

---

## Phase 26 exit criterion — met

All 6 High findings closed, each with a QEMU-verified reproduction-then-fix test: a failed process spawn no longer leaks memory; a hung/infinite-looping process is now forcibly killed by a scheduler watchdog after a bounded time budget, proven by `scheduler_run_until_done()` actually returning; `SYS_WAITPID` can no longer reap a zombie that isn't the caller's own child (with the legitimate parent/child case re-confirmed unaffected); a partially-failed long-filename write can no longer hide or corrupt sibling directory entries; the desktop's context menu is now a genuine top-layer overlay that can never render invisibly underneath a window; and the Terminal can no longer have its prompt backspaced away, corrupting the next command's parsing. `make diagnostic`'s full regression suite (150 file cycles + 300 window cycles) passes unchanged.
