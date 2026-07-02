# gOS — Project Plan v2

**Created:** 2026-07-01 (following the read-only code audit in [audit.md](version1/audit.md), performed against the v1.0 kernel — Phases 0–11 complete)

---

## 1. Project Overview

gOS v1.0 is a from-scratch x86_64 OS: UEFI boot via Limine, kernel in C (freestanding) + NASM, a bitmap PMM/paging VMM/heap allocator, PIT/keyboard/serial/ATA drivers, a FAT32 filesystem with CRUD, a framebuffer compositor with draggable windows, and a file manager + text editor UI. All 12 phases of [PROJECT_PLAN.md](version1/PROJECT_PLAN.md) (Phases 0–11) are done.

A subsequent read-only audit ([audit.md](version1/audit.md)) reviewed the v1.0 kernel and produced **24 findings**: 5 Critical, 6 High, 6 Medium, 6 Low, plus 6 items explicitly checked and ruled out. **None of these findings have been fixed yet.**

### Priority rule

**Track A (audit remediation) blocks Track B (new features). No Track B work starts until Phase 14 is complete.**

This isn't just discipline for its own sake — Track B features touch code the audit already flagged as fragile:

- **Phase 16 (window close/minimize/taskbar)** builds directly on `window_close`'s incomplete teardown (audit #11) and the stale-window-after-close dispatch bug (audit #11) — building a taskbar's restore/focus logic on top of that state machine before it's fixed would compound the bug, not just inherit it.
- **Phase 16**'s window teardown also touches `kfree` — audit #3's missing double-free check means a teardown bug here could silently corrupt the heap instead of crashing cleanly.
- **Phase 15/16** dragging and window geometry work sits on top of audit #7's unclamped drag-coordinate bug.
- Any Track B feature that touches files (wallpaper bundled as a bitmap on the FAT32 image, Phase 15's stretch goal) rides on FAT32 code paths flagged by audit #1 and #4 (unsigned underflow, unbounded chain walks).

Fixing Track A first means Track B is built on a kernel whose fragile paths are already hardened, rather than adding new call sites to code that's known to be unsafe.

---

## 2. Phases (dependency order)

| # | Phase | Track | Depends on |
|---|-------|-------|-----------|
| 12 | Critical Audit Fixes | A | v1.0 (Phase 0–11) |
| 13 | High-Severity Audit Fixes | A | 12 |
| 14 | Medium/Low Audit Cleanup + README update | A | 13 |
| 15 | Cursor & Wallpaper | B | 14 |
| 16 | Window Close, Minimize & Taskbar | B | 14, 12 (kfree fix), 13 (stale-window fix) |
| 17 | Maximize & Polish (optional/stretch) | B | 16 |

---

## 3–4. Milestones & Tasks per Phase

### Phase 12 — Critical Audit Fixes ✅ Complete — see [phase12.md](phase12.md)
**Estimated time: 10–15 hours (~1.5–2 weeks) — actual: ~9.5 hours**

**Milestone 12.1: FAT32 write-path corruption fixed**
- [x] Fix `fat_write_file` unsigned underflow (`kernel/src/fat32.c:709-710`) — guard `existing_count == 0` as a distinct branch (allocate a fresh first cluster) instead of computing `existing_count - 1` unconditionally
- [x] Test: in QEMU, create a zero-byte file with `first_cluster = 0` (e.g. via `fat_create_file` without writing, or by seeding the disk image externally with `mtools`/`touch`), then write data to it via the editor; confirm no wild FAT-table write and the write succeeds correctly — verify via `mdir`/`mtype` on the built image afterward
- [x] Add a regression note/comment at the guard citing the on-disk-zero-length-file case

**Milestone 12.2: FAT32 init failure actually halts**
- [x] Fix `kernel/src/start.c:406` vs `:502` — make `fat32_init()` failure call `hcf()` (or otherwise prevent fallthrough) instead of only logging "PANIC"
- [x] Move/guard `stress_test()` so it's unreachable when `fat32_init()` failed, not just visually inside an `if` block that doesn't actually gate execution
- [x] Test: in QEMU, boot against a disk image with a deliberately corrupted BPB (e.g. zero out the FAT32 signature bytes with a hex editor on a scratch copy of the image) and confirm the kernel halts at the PANIC screen instead of continuing into `stress_test()`/FAT calls — check serial log shows no further FAT32 log lines after the panic

**Milestone 12.3: `kfree` double-free detection**
- [x] Add an `is_free` check to `kfree` (`kernel/src/heap.c:124-154`) — panic or reject the free (per project convention) if the block is already marked free, before any coalescing happens
- [x] Test: in QEMU, add a temporary debug hook (or a startup self-test invoked once) that calls `kmalloc` then `kfree` twice on the same pointer; confirm the second `kfree` is caught (panic/log) instead of silently coalescing into a live neighboring block; remove/gate the test hook behind a debug flag once verified

**Milestone 12.4: FAT chain-walk cycle detection**
- [x] Add a visited-cluster bound (either a hard iteration cap derived from total cluster count, or a visited-set/Floyd's cycle check) to `fat_list_dir`, `fat_read_file`, `find_dirent`, `fat_free_chain`, and `find_free_slot`'s unbounded `for(;;)` (`kernel/src/fat32.c:178,297,431,381,479-514`)
- [x] Test: in QEMU, seed a scratch copy of the disk image with a cluster whose FAT entry points back to an earlier cluster in its own chain (edit the FAT table directly with a hex editor), boot, and trigger a directory listing / file read over that chain; confirm the kernel logs a cycle-detected error and returns instead of hanging — verify via QEMU wall-clock (kernel remains responsive to input) rather than a hard freeze

**Milestone 12.5: IST for double-fault/NMI**
- [x] Wire up TSS `ist1` (`kernel/src/gdt.c:41-47`) to a dedicated, statically-allocated stack; set the double-fault and NMI IDT gates (`kernel/src/idt.c`) to use `ist=1` instead of `ist=0`
- [x] Test: in QEMU, deliberately trigger a stack overflow (e.g. unbounded recursion in a debug-only test function) that causes a double fault; confirm via QEMU monitor (`info registers`, checking `RSP`) that the double-fault handler runs on the dedicated IST stack (a distinct address range from the faulting stack) and the panic screen renders instead of a triple fault/reset

**Phase 12 exit criterion:** ✅ all 5 Critical findings closed, each with a QEMU-verified reproduction-then-fix test passing. Full writeup, including two repro redesigns and one real test-harness ordering bug found along the way: [phase12.md](phase12.md).

---

### Phase 13 — High-Severity Audit Fixes ✅ Complete — see [phase13.md](phase13.md)
**Estimated time: 10–14 hours (~1.5–2 weeks) — actual: ~9.25 hours**

**Milestone 13.1: ATA write-path error checking**
- [x] In `ata_write_sector` (`kernel/src/ata.c`), check `ERR`/`DF` status bits after the `ATA_CMD_CACHE_FLUSH` completes, not just `BSY` clearing; propagate a failure return up through `fat_write_file`/directory-entry writers
- [x] Test: in QEMU, this is hard to fault-inject on a virtual ATA device directly — instead, add a temporary debug build that forces the ERR bit check path to run against a real post-flush status read, confirm the status-read code path executes and returns success on the normal (working) disk; note in the fix commit that true fault-injection would require QEMU's `-drive` error-injection options (`werror=stop`) as a follow-up if regression testing is ever needed

**Milestone 13.2: Window drag negative-coordinate clamping**
- [x] Clamp window x/y to `>= 0` (and reasonable upper bounds against screen width/height) in the drag-update path (`kernel/src/window.c:281-282`) before it reaches `fb_draw_rect`'s `uint64_t` params
- [x] Test: in QEMU, drag a window fully past the top-left screen edge using the mouse; confirm the window remains visibly rendered (clipped at 0,0) instead of disappearing, by visual inspection of a QEMU screenshot (`screendump`) before/after the fix

**Milestone 13.3: `heap_grow` free-block check**
- [x] In `heap_grow` (`kernel/src/heap.c:112-121`), check the highest-address block's `is_free` flag before extending it; if it's in-use, append a new free block header instead of mutating the live allocation's `size`
- [x] Test: in QEMU, add a temporary debug sequence that allocates a block, deliberately makes it the heap's highest-address block, keeps it allocated, then forces `heap_grow` (e.g. by exhausting remaining free space with further allocations); confirm the live block's reported size is unchanged after grow and the new space appears as a separate free block (verify via a heap-walk debug dump over serial)

**Milestone 13.4: `create_entry` rollback completeness**
- [x] In `create_entry` (`kernel/src/fat32.c:591-600`), track the directory-growth cluster allocation and free it (via `fat_free_chain` or equivalent) if the subsequent `find_free_slot` step fails, mirroring the existing data-cluster rollback
- [x] Test: in QEMU, fill a directory to force `find_free_slot`'s directory-growth path, then induce a failure partway (e.g. simulate disk-full by shrinking a scratch image's free cluster count) and confirm via a post-boot FAT free-cluster count check (dump free cluster count over serial before/after the failed create) that no cluster leaked

**Milestone 13.5: `vmm_unmap_page()` + TLB invalidation**
- [x] Implement `vmm_unmap_page(virt)` in `kernel/src/vmm.c`, clearing the relevant PTE and issuing `invlpg` for the unmapped address
- [x] Test: in QEMU, add a temporary debug call that maps a virtual address to physical page A, writes a known value, remaps the same virtual address to physical page B via unmap+map, writes a different value, then reads back through the virtual address; confirm the read reflects page B's value (no stale TLB entry) — without the fix, this same test should reproducibly show the stale page A value first, then pass after the fix

**Milestone 13.6: Stale-window-after-close in button-dispatch loop**
- [x] In `window_close` (`kernel/src/window.c:175-196`), clear `buttons[]`, `custom_click`, `custom_render`, `textbox_buffer` (not just `in_use`) so a closed slot is fully inert
- [x] In the button-dispatch loop (`kernel/src/window.c:260-268`), re-check `win->in_use` after each `on_click()` callback before continuing to iterate that window's remaining button rects, so a self-closing callback can't cause further stale dispatch
- [x] Test: in QEMU, modify a debug/test dialog to have two overlapping button rects where the first button's callback closes the window; click the overlap point and confirm (via serial log added temporarily) the second button's callback does NOT fire post-close; revert the debug overlap after confirming

**Phase 13 exit criterion:** ✅ all 6 High findings closed, each with a QEMU-verified test (13.1/13.4's specific failure branches verified via regression testing + code review, since they require genuine ATA I/O faults not practical to inject on QEMU's emulated disk). Full writeup, including two redesigned reproductions and one test-design flaw caught before it mattered: [phase13.md](phase13.md).

---

### Phase 14 — Medium/Low Audit Cleanup ✅ Complete — see [phase14.md](phase14.md)
**Estimated time: 14–20 hours (~2–2.5 weeks) — actual: ~10 hours**

**Milestone 14.0: README.md updated for current state** *(living task — re-touched at end of Phase 14 and again at end of Track B)*
- [x] Update `README.md` with gOS's architecture, bootloader (Limine/UEFI), language (C + NASM), filesystem (FAT32), windowing capabilities, and a "known limitations" section reflecting the Track A fixes landed so far (Phases 12–13) plus any Phase 14 items done at time of writing
- [x] Re-touch at the end of Phase 14 (Track A complete) to reflect the full audit-remediated state
- [x] Re-touch at the end of Track B (Phase 16, or 17 if attempted) to reflect final v2 feature set

**Milestone 14.1: Medium findings**
- [x] #12 Demo windows consuming slots (`kernel/src/start.c:503-505`) — auto-close demo windows ("Window A"/"Window B"/demo Text Editor) at end of boot sequence, or gate them behind a debug build flag. Test: boot in QEMU, confirm `window_create()` for a new real window succeeds without hitting `MAX_WINDOWS=8` prematurely (verify via serial window-count log)
- [x] #13 0xE0 extended-scancode prefix (`kernel/src/keyboard.c`) — track a "pending extended" flag across the IRQ1 handler's two-byte sequence, distinguish Right Ctrl/Numpad Enter from their non-extended counterparts. Test: QEMU monitor `sendkey ctrl_r` vs `sendkey ctrl` and confirm distinct handling via serial log
- [x] #14 Spurious IRQ7/15 not detected (`kernel/src/pic.c`) — read the ISR register before sending EOI, and for a spurious IRQ15 send EOI to the master PIC only. Test: this is hard to fault-inject in QEMU directly; verify via code inspection plus a serial log line confirming the ISR-register read executes on every EOI call during normal operation
- [x] #15 No ATA drive-presence probe (`kernel/src/ata.c` `ata_init`) — add an `IDENTIFY`-based fast-fail check before falling into the full busy-wait path. Test: in QEMU, boot with `-drive` omitted (no disk attached) on a scratch config and confirm `ata_init` fails fast (check serial timestamp delta) instead of burning the full busy-wait
- [x] #16 `stress_test()` file leak on partial failure (`kernel/src/start.c:87-93`) — add cleanup (delete any partially-created `STRESS.TXT`/`STRESSR.TXT`) on the `break` path. Test: temporarily force a mid-loop failure (e.g. stub a write call to fail) and confirm via `mdir` on the built image that no stress-test file remains afterward
- [x] #17 Unchecked `window_create()` return value (`fm.c`, `editor.c`, `start.c` call sites) — check for `-1`/failure at each call site and log or show user feedback (e.g. reuse the dialog "ignored" pattern from #23) instead of silently no-oping. Test: in QEMU, open windows until `MAX_WINDOWS=8` is hit, then attempt "New Folder"; confirm a serial log line (or on-screen indication) reports the failure instead of doing nothing visibly

**Milestone 14.2: Low findings**
- [x] #18 Rapid-reopen unsaved-edit loss (`fm.c` double-click handler + `editor_open()`) — don't re-arm the double-click timer on a double-click event itself (only on single clicks), so a third rapid click doesn't re-trigger `editor_open()`. Test: in QEMU, open a file in the editor, type unsaved text, triple-click the file entry rapidly in the file manager, confirm the unsaved text survives (editor is not silently reloaded)
- [x] #19 Inconsistent "PANIC" logging (ATA sector-0 read failure, some FAT32 boot-demo read failures) — audit all literal `"PANIC"` log call sites and ensure each either calls `hcf()` or is relabeled (e.g. `"ERROR"`) if it's meant to be recoverable. Test: `grep -n "PANIC" kernel/src/**/*.c` and manually confirm each remaining "PANIC" site halts, in QEMU where feasible
- [x] #20 `pmm_init` bitmap-placement ordering hazard — explicitly exclude the physical range backing Limine's `memmap->entries[]` array from the bitmap's placement search. Test: in QEMU, add a temporary serial assertion comparing the chosen bitmap physical range against the memmap array's physical range at `pmm_init` time, confirm no overlap, then remove the assertion (or keep it behind a debug flag)
- [x] #21 Disk image Makefile target not idempotent/versioned — add a version stamp file inside the built image (or a Makefile dependency on the seed recipe's content hash) so a changed recipe forces a rebuild instead of silently reusing a stale image. Test: change the seed recipe, run `make`, confirm the disk image target rebuilds (check file mtime/hash changes) rather than being skipped
- [x] #22 `fb_backbuffer_init()` re-entrancy guard — add an already-initialized check (return early or assert) if called a second time. Test: temporarily call `fb_backbuffer_init()` twice at boot in a debug build, confirm no double allocation occurs (check heap free-byte count over serial before/after), then revert the double-call
- [x] #23 `fm_open_dialog` "one at a time" guard gives no feedback — add a visible/logged indication (status bar text, or a brief flash) when a second dialog request is dropped. Test: in QEMU, open a dialog, click a second toolbar button that would open another dialog, confirm feedback appears (serial log at minimum, ideally on-screen)
- [x] #24 Double-click identity is row-index-based, not filename-based — document the `fm_refresh()` invariant with a comment at each mutation call site, or (if time allows) switch tracking to filename-based identity for robustness. Test: code review confirmation that all mutation paths call `fm_refresh()`; no QEMU test needed if left as documentation-only, since audit confirms this is not an active bug

**Phase 14 exit criterion:** ✅ all 12 Medium+Low findings closed or explicitly documented (see [phase14.md](phase14.md), including a documented mid-phase incident and recovery for #21's testing), README.md updated to reflect the audit-remediated state. **Track A complete.**

---

### Phase 15 — Cursor & Wallpaper ✅ Complete — see [phase15.md](phase15.md)
**Estimated time: 6–10 hours (~1–1.5 weeks)**

**Milestone 15.1: Real arrow-shaped mouse cursor**
- [x] Design/hardcode a small arrow-cursor bitmap (monochrome or 2-color, e.g. 12x19px) as a static array in the compositor source
- [x] Render the cursor in the compositor's top layer (drawn last, after all windows) at the current mouse position, replacing whatever placeholder cursor behavior exists today
- [x] Test: in QEMU, move the mouse across window boundaries and the desktop background; confirm the arrow cursor renders correctly on top of both, with no leftover pixels from the previous frame's cursor position (verify via `screendump` before/after movement)

**Milestone 15.2: Desktop wallpaper layer**
- [x] Add a wallpaper draw call in the compositor, executed before window compositing, starting with a solid color or simple vertical/horizontal gradient
- [x] Test: in QEMU, boot with no windows open and confirm the wallpaper renders full-screen; open a window and confirm it draws on top without wallpaper bleed-through at the edges

**Milestone 15.3 (stretch): Minimal raw/BMP image wallpaper**
- [x] Write a minimal hand-rolled loader for an uncompressed BMP (or a custom raw RGB format bundled at build time) — no PNG/JPEG decoding
- [x] Bundle the image file on the FAT32 disk image via the Makefile's image-seeding step
- [x] Load the file via the (now-hardened, post-Phase-12/13) FAT32 read path at boot and blit it as the wallpaper
- [x] Test: in QEMU, confirm the bundled image renders correctly as wallpaper on boot; if the FAT32 read fails (missing file), confirm graceful fallback to the solid-color wallpaper from 15.2 rather than a crash

**Phase 15 exit criterion:** ✅ cursor and wallpaper render correctly in QEMU (screendump + independent host-side pixel comparison against the source BMP); stretch goal landed, with graceful fallback verified for both missing and corrupted wallpaper files. Full writeup: [phase15.md](phase15.md).

---

### Phase 16 — Window Close, Minimize & Taskbar ✅ Complete — see [phase16.md](phase16.md)
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 16.1: Real window teardown on close**
- [x] Extend `window_close` (already touched in Phase 13.6) to fully clear `buttons[]`, free any owned heap allocations (e.g. `textbox_buffer` via `kfree`), and reset all callback pointers, leaving the slot genuinely inert and reusable
- [x] Test: in QEMU, open and close a window with a textbox (e.g. the editor) 20 times in a loop; confirm heap free-byte count (via serial debug dump) returns to its pre-loop baseline after each close, with no growth (no leak) and no corruption on the 20th reopen

**Milestone 16.2: Minimized window state**
- [x] Add a `minimized` flag to the window struct; compositor's draw list skips windows with `minimized == true`
- [x] Add a minimize trigger (button or keyboard shortcut) that sets the flag without closing/tearing down the window (state, buttons, textbox contents all preserved)
- [x] Test: in QEMU, minimize a window with unsaved editor text, confirm it disappears from the screen (`screendump` check), then restore it (via a temporary debug key) and confirm the unsaved text is still present

**Milestone 16.3: Persistent taskbar**
- [x] Add a taskbar region (e.g. bottom strip) drawn by the compositor showing one entry per open window (including minimized ones), labeled by window title
- [x] Wire up click handling on taskbar entries: clicking a minimized window's entry restores and focuses it; clicking an already-visible window's entry focuses it (brings to front)
- [x] Test: in QEMU, open 3 windows, minimize 2, confirm the taskbar shows all 3 entries; click each minimized entry and confirm it restores to its prior geometry and gains focus (verify z-order via `screendump`)

**Phase 16 exit criterion:** ✅ close/minimize/taskbar all functional in QEMU with no heap growth across repeated open/close/minimize cycles (3,341,040 bytes free before and after 20 open/close iterations). Full writeup: [phase16.md](phase16.md).

---

### Phase 17 — Maximize & Polish (optional/stretch) ✅ Complete — see [phase17.md](phase17.md)
**Estimated time: 6–10 hours (~1–1.5 weeks) — only scope in if Phase 16 lands cleanly**

**Milestone 17.1: Maximize/restore toggle**
- [x] Add a maximize trigger that stores the window's pre-maximize geometry (x, y, width, height) and resizes it to fill the screen (minus taskbar height)
- [x] Add a restore trigger that reads back the stored geometry and returns the window to its prior size/position
- [x] Test: in QEMU, maximize a window, confirm it fills the screen above the taskbar; restore it and confirm exact geometry match against the pre-maximize state (log geometry values over serial before maximize and after restore, diff them)

**Phase 17 exit criterion:** ✅ maximize/restore round-trips geometry exactly (137,211,333×141 → full-screen 0,0,1280×748 → back to 137,211,333×141, byte-for-byte); no regressions in Phase 16's taskbar/minimize state. Full writeup: [phase17.md](phase17.md).

---

## 5. Estimated Time Summary

Assuming the same ~7.5 hrs/week pace as the v1 plan:

| Phase | Estimated hours | Estimated weeks |
|---|---|---|
| 12 — Critical Audit Fixes | 10–15 | 1.5–2 |
| 13 — High-Severity Audit Fixes | 10–14 | 1.5–2 |
| 14 — Medium/Low Audit Cleanup | 14–20 | 2–2.5 |
| **Track A total** | **34–49** | **~5–6.5** |
| 15 — Cursor & Wallpaper | 6–10 | 1–1.5 |
| 16 — Window Close, Minimize & Taskbar | 14–20 | 2–2.5 |
| 17 — Maximize & Polish (optional) | 6–10 | 1–1.5 |
| **Track B total (incl. stretch)** | **26–40** | **~4–5.5** |
| **Grand total** | **60–89** | **~9–12** |

---

## 6. Status Tracker

| Phase | Milestone | Task | Status | Notes |
|---|---|---|---|---|
| 12 | 12.1 FAT32 write-path fix | Fix `fat_write_file` underflow | Done | Also fixed cluster-0 chain-walk guard; see [phase12.md](phase12.md) |
| 12 | 12.1 FAT32 write-path fix | QEMU test: zero-length file write | Done | Confirmed pre-fix wild write hit FAT2 mirror sector 2048; mtools cross-check |
| 12 | 12.2 FAT32 init halts | Fix `fat32_init` failure to call `hcf()` | Done | |
| 12 | 12.2 FAT32 init halts | Guard `stress_test()` unreachable on failure | Done | `hcf()` halts before stress_test() is reached |
| 12 | 12.2 FAT32 init halts | QEMU test: corrupted BPB halts kernel | Done | Pre-fix build reported false "Stress test: PASS" on invalid FS |
| 12 | 12.3 kfree double-free | Add `is_free` check to `kfree` | Done | Plus a permanent double-free self-test in `heap_self_test()` |
| 12 | 12.3 kfree double-free | QEMU test: double-free caught | Done | Pre-fix repro showed real pointer aliasing + data corruption |
| 12 | 12.4 FAT cycle detection | Add cycle/bound check to chain-walk functions | Done | Shared `chain_step_limit_exceeded()` helper, all 5 sites |
| 12 | 12.4 FAT cycle detection | QEMU test: cyclic FAT chain doesn't hang | Done | Pre-fix build hung 35+s confirmed via `info registers` |
| 12 | 12.5 IST double-fault | Wire TSS `ist1` + IDT gates | Done | Dedicated 16KiB `ist1_stack` |
| 12 | 12.5 IST double-fault | QEMU test: stack overflow uses IST stack | Done | Pre-fix triple-faulted (frozen, no panic screen); post-fix RSP confirmed inside ist1_stack bounds |
| 13 | 13.1 ATA write ERR/DF | Check ERR/DF after cache flush | Done | Fault injection not practical on QEMU IDE; 1525-write regression test instead |
| 13 | 13.1 ATA write ERR/DF | Verify status-read path executes | Done | |
| 13 | 13.2 Drag clamping | Clamp window drag x/y | Done | Also capped upper bound at screen edge minus titlebar height |
| 13 | 13.2 Drag clamping | QEMU test: window visible after edge drag | Done | Pre-fix: window body vanished, only close-button fragment floated |
| 13 | 13.3 heap_grow free check | Add `is_free` check before block extension | Done | See [phase13.md](phase13.md) for two failed repro attempts before the working one |
| 13 | 13.3 heap_grow free check | QEMU test: grow doesn't corrupt live block | Done | Pre-fix: kmalloc spuriously reported OOM with 202MiB free |
| 13 | 13.4 create_entry rollback | Free directory-growth cluster on failure | Done | 3 rollback branches added to `find_free_slot` |
| 13 | 13.4 create_entry rollback | QEMU test: no cluster leak on partial failure | Done | Failure branches need ATA faults (code review); success path live-tested via 20-file forced growth |
| 13 | 13.5 vmm_unmap_page | Implement unmap + invlpg | Done | |
| 13 | 13.5 vmm_unmap_page | QEMU test: remap reflects new page, no stale TLB | Done | First test design was a false negative (write-after-remap self-consistent); redesigned |
| 13 | 13.6 Stale-window dispatch | Clear window state fully on close | Done | |
| 13 | 13.6 Stale-window dispatch | Re-check `in_use` in dispatch loop | Done | |
| 13 | 13.6 Stale-window dispatch | QEMU test: overlapping-button close scenario | Done | Pre-fix: both callbacks fired via real simulated mouse click |
| 14 | 14.0 README update | Update README (post-Track-A pass) | Done | Track B re-touch still pending |
| 14 | 14.1 Medium fixes | #12 Demo windows auto-close | Done | Closed before compositor draws first frame; see [phase14.md](phase14.md) |
| 14 | 14.1 Medium fixes | #13 0xE0 extended scancode handling | Done | Right Ctrl / Numpad Enter now distinguishable |
| 14 | 14.1 Medium fixes | #14 Spurious IRQ7/15 EOI handling | Done | Verified via synthetic pic_send_eoi(7)/(15) calls |
| 14 | 14.1 Medium fixes | #15 ATA drive-presence probe | Done | Pre-fix: 100,001 status reads; post-fix: 0 |
| 14 | 14.1 Medium fixes | #16 stress_test file-leak cleanup | Done | Cross-checked via mtools, no leaked file |
| 14 | 14.1 Medium fixes | #17 Check window_create() return value | Done | New taskbar_flash_message() mechanism, reused by #23 |
| 14 | 14.2 Low fixes | #18 Fix rapid-reopen unsaved-edit loss | Done | Live-verified: marker text survives rapid triple-click |
| 14 | 14.2 Low fixes | #19 Consistent PANIC logging | Done | 4 non-halting sites relabeled to ERROR |
| 14 | 14.2 Low fixes | #20 pmm_init bitmap-placement guard | Done | Permanent exclusion check + serial log every boot |
| 14 | 14.2 Low fixes | #21 Idempotent disk image Makefile target | Done | See [phase14.md](phase14.md) for a documented mid-test incident + recovery |
| 14 | 14.2 Low fixes | #22 fb_backbuffer_init re-entrancy guard | Done | PMM free-page count unchanged across repeat call |
| 14 | 14.2 Low fixes | #23 Dialog guard feedback | Done | Reuses #17's taskbar_flash_message() |
| 14 | 14.2 Low fixes | #24 Document/harden double-click identity | Done | Documentation-only, per audit's own latent-not-active assessment |
| 15 | 15.1 Cursor | Design + render arrow cursor | Done | 12x19 arrow w/ transparency; also fixed cursor-under-taskbar draw order — see [phase15.md](phase15.md) |
| 15 | 15.2 Wallpaper | Solid/gradient wallpaper layer | Done | Vertical blue→teal gradient (fallback layer for 15.3) |
| 15 | 15.3 Wallpaper (stretch) | BMP/raw loader + bundled image | Done | 24bpp BMP off FAT32; screendump pixel-matched source BMP (0/2000 mismatches); missing + corrupted-file fallbacks verified |
| 16 | 16.1 Window teardown | Full close cleanup (buttons, heap, callbacks) | Done | Already complete since Phase 13.6 (no heap-owned window fields exist); added `heap_free_bytes()` + a 20-cycle regression test to prove it — see [phase16.md](phase16.md) |
| 16 | 16.2 Minimize | Minimized flag + compositor skip | Done | Titlebar "_" button (user chose titlebar-button over keyboard-shortcut); unsaved editor text survives minimize/restore |
| 16 | 16.3 Taskbar | Taskbar render + restore/focus click handling | Done | Minimized entries dimmed; 3-window/2-minimized test confirms exact geometry + focus restored on click |
| 17 | 17.1 Maximize (optional) | Maximize/restore geometry toggle | Done | Titlebar toggle button (teal square, left of minimize); exact round-trip proven numerically + visually — see [phase17.md](phase17.md) |
| — | 14.0 README update | Update README (post-Track-B pass) | Done | Final v2 feature set (Phases 15-17) reflected — see this update |

---

## 7. Dependencies / Blockers

- **Phase 13 depends on Phase 12** — audit #11's stale-window fix (13.6) is scoped alongside `window_close`, whose behavior Phase 16 also modifies; doing it in Phase 13 first avoids Phase 16 having to fix the same function twice.
- **Phase 16.1 (window teardown) depends on Phase 12.3 (kfree double-free check)** — teardown now calls `kfree` on owned allocations (e.g. `textbox_buffer`); without 12.3's guard, a teardown bug here would silently corrupt the heap instead of failing loudly during development.
- **Phase 16.1/16.3 depend on Phase 13.6 (stale-window dispatch fix)** — minimize and taskbar-driven restore/focus both re-enter the same button-dispatch and window-state logic that 13.6 hardens; building on the unfixed version would let the same class of stale-pointer bug resurface in the taskbar's click handling.
- **Phase 15.3 (BMP wallpaper stretch) depends on Phase 12.1 and 12.4** — reading a bundled file off FAT32 at boot exercises the exact write/read-chain-walk paths those fixes target; attempting this before Track A would risk hitting the underflow or an unbounded chain hang while loading the wallpaper itself.
- **Phase 17 depends on Phase 16 landing cleanly** — maximize/restore reuses Phase 16's window geometry and taskbar-visible state; see Risk section below.
- **README.md (Milestone 14.0) is a living task**, not a one-time checkbox — touched once at end of Track A (Phase 14) and again at end of Track B (Phase 16, or 17 if attempted).

---

## 8. Risk / Scope-Creep Notes

- **Phase 16 (window close/minimize/taskbar state machine) is this plan's highest-risk phase**, the way v1 flagged memory management (Phase 3) and windowing (Phase 6). It touches three previously-fragile areas at once — window teardown, heap ownership, and click-dispatch — right after they were hardened in Track A. Budget slack here; if 16.1 (teardown) surfaces new heap or dispatch issues not caught by the Track A tests, resolve those before starting 16.3 (taskbar), don't build the taskbar on top of an uncertain teardown.
- **Phase 17 (maximize) is optional and should be the first thing cut if time runs short.** It's explicitly scoped as stretch-only, gated on Phase 16 landing cleanly with no lingering geometry/state bugs. If Phase 16 runs over its estimated 14–20 hours or its QEMU heap/state tests are flaky, skip Phase 17 entirely rather than layering more geometry state onto an unproven taskbar.
- **Phase 15.3 (BMP/raw image loader) is a stretch goal within Phase 15**, not a hard requirement — if it starts pulling in image-format complexity beyond a trivial raw/BMP reader, fall back to the solid-color/gradient wallpaper from 15.2 and move on to Phase 16.
- **Do not start Track B work early even if a Track A fix "looks small."** The priority rule (Section 1) exists specifically because Track B's riskiest phase (16) depends on Track A fixes that are easy to underestimate the blast radius of (kfree double-free, stale-window dispatch) — verify each Track A QEMU test actually passes before treating that finding as closed.
