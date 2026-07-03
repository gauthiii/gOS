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

### Tracks C / D / E (still v2)

**Track A + Track B (Phases 12–17) are complete.** Tracks C/D/E extend v2 with three more tracks, planned after a retrospective on what gOS is still missing as a "real" OS versus a windowed FAT32 file browser — these are additional v2 tracks, not a new major version:

- **Track C — OS internals**: the biggest remaining architectural gap is that gOS has no user mode, no syscalls, and no process concept at all — everything so far is one big kernel-mode loop. Phases 19–20 add ring-3 execution, a syscall interface, an ELF loader, and preemptive multitasking.
- **Track D — Desktop & storage depth**: window resize and Alt+Tab (the remaining windowing gaps after Phases 15–17's minimize/maximize), a real-time clock and taskbar clock, settings persistence, and FAT32 long filename support.
- **Track E — Apps**: a shell, calculator, and image viewer, exercising both Track C (the shell, if it lands, is gOS's first genuine user-mode program) and Track D (the image viewer reuses Phase 15.3's BMP decoder).

**Phase 18 (boot-time cleanup) comes first and blocks nothing** — it's an unrelated quick fix (gating the stress test/regression demos behind a debug flag so normal boots are fast again) that pays for itself immediately in faster QEMU test iteration for every phase after it.

Audio and networking were explicitly scoped **out** of this plan — both are large, multi-phase undertakings (PCI enumeration, real NIC/sound drivers, a network stack) with little payoff until Track C gives gOS something to actually run on top of them. Revisit in a future v3 once user-mode programs exist.

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
| 18 | Boot-Time Cleanup & Diagnostics Mode | — | 17 (Track A+B complete) |
| 19 | User Mode, Syscalls & ELF Loader | C | 14 (hardened VMM/GDT/IDT) |
| 20 | Preemptive Multitasking & Process Management | C | 19 |
| 21 | Window Resize & Alt+Tab | D | 14 |
| 22 | RTC Driver, Taskbar Clock & Settings Persistence | D | 15 (wallpaper), 21 (geometry to persist) |
| 23 | FAT32 Long Filename (VFAT) Support | D | 14 |
| 24 | Shell, Calculator & Image Viewer | E | 19, 20 (shell), 23 (LFN), 15.3 (BMP decoder) |

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

### Phase 18 — Boot-Time Cleanup & Diagnostics Mode ✅ Complete — see [phase18.md](phase18.md)
**Estimated time: 4–6 hours (~0.5–1 week)**

**Milestone 18.1: Gate regression demos/stress test behind a debug flag**
- [x] Wrap the Phase 6/7 window/mouse demo sequence and the Phase 8 boot-time stress test (`kernel/src/start.c`) behind a new compile-time flag (e.g. `GOS_DIAGNOSTIC_BOOT`), off by default, so a normal boot goes straight from hardware init to the desktop loop
- [x] Test: `make run` (default build, no debug flag) reaches `=== gOS boot checks complete ===` within a few seconds of kernel entry instead of ~75s — verify via the PIT tick count (`Timer tick: N`) logged at that point in serial output, comparing it against a pre-change baseline capture

**Milestone 18.2: Preserve full diagnostics for regression testing**
- [x] Add a `make diagnostic` Makefile target (or a documented `CFLAGS=... -DGOS_DIAGNOSTIC_BOOT` invocation) that rebuilds with the full regression suite enabled, matching today's boot sequence exactly
- [x] Test: boot the diagnostic build in QEMU and diff its serial log (excluding timestamps) against a saved pre-Phase-18 baseline log — confirm every existing regression check line (mouse test window, stress test PASS, demo window creation, etc.) is still present and unchanged, proving the gating didn't silently drop test coverage

**Phase 18 exit criterion:** ✅ default `make run` reaches the interactive desktop in ~1 second (PIT tick count = 100, down from ~7,000-8,000 pre-Phase-18), independently confirmed interactive via a real simulated click on the Files icon; every existing regression test still runs (and passes) via `make diagnostic`, with zero loss of coverage. Full writeup: [phase18.md](phase18.md).

---

### Phase 19 — User Mode, Syscalls & ELF Loader ✅ Complete — see [phase19.md](phase19.md)
**Estimated time: 16–24 hours (~2.5–3 weeks)**

**Milestone 19.1: Ring 3 segments + TSS extension**
- [x] Extend the GDT (`kernel/src/gdt.c`) with user-mode code/data segments (ring 3, DPL=3), and extend the existing TSS (already carrying `ist1` since Phase 12.5) with a kernel-mode stack pointer (`rsp0`) for ring 3→0 transitions
- [x] Test: a debug build constructs an `iretq` frame into a trivial ring-3 stub and jumps to it; confirm via serial log (reading `CS` before the stub does anything else) that the CPU is genuinely executing at CPL=3, then confirm it can transition back to ring 0 without a fault

**Milestone 19.2: Syscall entry point**
- [x] Implement a syscall gate (`int 0x80`, or the `syscall`/`sysret` instruction pair with the relevant MSRs) with a minimal syscall table covering at least `write` (to serial, proving the round-trip) and `exit`
- [x] Test: the ring-3 stub from 19.1 issues a `write` syscall with a distinctive test string; confirm the string appears in serial output, proving a full user→kernel→user round-trip through the new syscall path

**Milestone 19.3: Minimal ELF64 loader**
- [x] Parse a static, non-relocatable ELF64 executable's program headers and map its `PT_LOAD` segments into a fresh set of user-mode page tables (reusing `vmm_map_page`/`vmm_unmap_page` from the existing, Phase-13.5-hardened VMM), then transfer control to its entry point in ring 3
- [x] Bundle one trivial hand-assembled/compiled "hello from userland" ELF binary on the FAT32 disk image, via the same Makefile disk-seeding mechanism Phase 15.3 used for the wallpaper BMP
- [x] Test: in QEMU, load the bundled ELF via `fat_read_file` and execute it through the new loader; confirm its `write` syscall output appears on serial — this is the first genuinely independent, kernel-authored-but-not-kernel-linked code gOS has ever run

**Phase 19 exit criterion:** ✅ a real ELF binary — built and bundled separately from the kernel image, independently verified via `readelf` and a byte-for-byte `diff` against its on-disk copy — executes in ring 3 and makes syscalls back into the kernel, verified end-to-end in QEMU. Found and fixed a real VMM bug along the way (page-table `PAGE_USER` bit not propagating to already-existing intermediate table entries). Full writeup: [phase19.md](phase19.md).

---

### Phase 20 — Preemptive Multitasking & Process Management ✅ Complete — see [phase20.md](phase20.md)
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 20.1: Process table & context switching**
- [x] Add a process control block (PID, saved general-purpose register state, page-table root/CR3, state: ready/running/blocked/zombie) and a fixed-size process table
- [x] Implement a context-switch routine (save/restore GPRs + CR3) invoked from the existing PIT timer IRQ handler (`kernel/src/timer.c`), on a fixed time-slice
- [x] Test: in QEMU, launch two of the Phase 19 ELF binaries, each looping and calling `write` with a distinct marker string; confirm via serial log that output from both processes interleaves (not sequential run-to-completion), proving genuine preemption

**Milestone 20.2: Process lifecycle syscalls**
- [x] Add `exit` (already stubbed in 19.2 — give it real process-teardown semantics), a `wait`/`waitpid`-equivalent, and a `spawn`-style creation syscall (spawn-from-ELF-path is a simpler first pass than full `fork`+copy-on-write)
- [x] Test: a parent process spawns a child that exits with a specific status code; confirm the parent's `wait` call returns that exact code, verified via serial log

**Milestone 20.3: Scheduler fairness under load**
- [x] Test: spawn 5 concurrent processes, each incrementing a syscall-exposed shared counter in a loop for a fixed duration; confirm via serial log that every process made progress (no starvation), and confirm the desktop's own main loop (mouse/keyboard responsiveness, window compositing) remains live and interactive throughout — screendump the desktop mid-test to prove it

**Phase 20 exit criterion:** ✅ multiple independent processes — each with real, separate per-process page tables (a bigger scope than the plan's minimum, chosen with the user upfront) — run concurrently under genuine timer-driven preemption, verified via interleaved serial output, `wait`/`exit` code round-trip, 5-process fairness, and post-demo desktop responsiveness. Found and fixed a NASM label/register-name collision and an initial "workload too fast to actually preempt" test-design issue along the way. Full writeup: [phase20.md](phase20.md).

**Phase 20 exit criterion:** multiple independent user-mode processes run concurrently under preemptive scheduling without starving each other or the desktop's own responsiveness.

---

### Phase 21 — Window Resize & Alt+Tab ✅ Complete — see [phase21.md](phase21.md)
**Estimated time: 8–12 hours (~1–1.5 weeks)**

**Milestone 21.1: Drag-to-resize from window edges/corners**
- [x] Extend `window_system_update`'s hit-testing (`kernel/src/window.c`) to recognize a small margin along a window's right/bottom edge and bottom-right corner as resize handles, distinct from the existing titlebar-drag region and the Phase 16/17 minimize/maximize buttons
- [x] Update the window's `w`/`h` live during the drag, clamping to a sane minimum size and the screen bounds (reusing the audit-fixed clamping pattern from Finding #7 / Phase 13.2's drag clamp, extended to size as well as position)
- [x] Test: in QEMU, drag a window's bottom-right corner outward and inward; confirm via `screendump` that the titlebar/buttons/body all re-layout correctly at the new size with no visual corruption, and that dragging past the screen edge clamps instead of wrapping or crashing

**Milestone 21.2: Alt+Tab window switching**
- [x] Track Alt key state in `kernel/src/keyboard.c` (parallel to the existing Ctrl-tracking added for Ctrl+S), and on a Tab press while Alt is held, cycle window focus to the next window in z-order (skipping minimized ones per Phase 16.2), with no mouse click required
- [x] Test: in QEMU, open 3 windows, hold `sendkey alt` and repeat `sendkey tab`; confirm via a temporary serial debug print of the newly-focused window's title that focus cycles through all 3 in a stable, non-repeating order

**Phase 21 exit criterion:** ✅ windows can be resized by dragging an edge/corner (min-size and screen-edge clamping both verified via screendump), and Alt+Tab cycles focus through every open window in a stable, non-repeating rotation with no mouse involved. Found and fixed a real focus-cycling design bug (the original "raise the second-from-top window" approach only ever toggled between 2 windows) before the first boot, by hand-tracing the algorithm against the milestone's own test requirement. Full writeup: [phase21.md](phase21.md).

---

### Phase 22 — RTC Driver, Taskbar Clock & Settings Persistence ✅ Complete — see [phase22.md](phase22.md)
**Estimated time: 10–14 hours (~1.5–2 weeks)**

**Milestone 22.1: CMOS RTC driver**
- [x] Read the CMOS real-time clock registers (ports 0x70/0x71) for date/time, handling the BCD-vs-binary and 12/24-hour format quirks (checking Status Register B)
- [x] Test: in QEMU, boot with a known `-rtc base=...` value passed on the command line; confirm the driver's parsed date/time matches what QEMU was told to present, logged over serial

**Milestone 22.2: Taskbar clock widget**
- [x] Render a live HH:MM (or HH:MM:SS) clock in the taskbar (`kernel/src/taskbar.c`), updating at least once per displayed second
- [x] Test: in QEMU, `screendump` the taskbar clock at two points roughly 10 seconds apart (using QEMU's `-rtc` to control the simulated clock precisely) and confirm the displayed time advanced by the expected amount

**Milestone 22.3: Settings persistence**
- [x] Define a small config file format (e.g. `GOS.CFG` on the FAT32 root) storing at minimum the current wallpaper choice and each open window's last-known geometry (position, and size once Phase 21 lands)
- [x] Save on a graceful shutdown/reboot trigger and load at boot, applying saved geometry to whichever apps reopen
- [x] Test: in QEMU, move a window and/or change the wallpaper, trigger a save, then in a **separate, fresh QEMU process** against the same disk image, confirm the restored state matches — cross-check the config file's raw bytes independently via `mtype`/`xxd`, not just the OS's own read-back

**Phase 22 exit criterion:** ✅ a working taskbar clock reflecting real time (exact 10-second advance verified between precisely-timed screendumps), and user preferences (wallpaper mode + File Manager geometry) that survive a reboot, verified independently via `xxd` on the raw `GOS.CFG` bytes and confirmed end-to-end with a genuinely fresh QEMU process. Two underspecified design questions (wallpaper "choice" mechanism, save trigger, since gOS has no picker UI or real shutdown hook) were resolved with the user before implementation. Full writeup: [phase22.md](phase22.md).

**Phase 22 exit criterion:** a working taskbar clock reflecting real time, and user preferences that survive a reboot, verified independently via `mtools`.

---

### Phase 23 — FAT32 Long Filename (VFAT) Support ✅ Complete — see [phase23.md](phase23.md)
**Estimated time: 10–14 hours (~1.5–2 weeks)**

**Milestone 23.1: LFN read support**
- [x] Parse the long-name directory entries (attribute `0x0F`, currently explicitly skipped by `fat_list_dir` per its own doc comment in `kernel/include/fat32.h`) into full long filenames, associated with their trailing short-name entry
- [x] Test: seed a scratch disk image via host-side `mtools`/`mcopy` with a filename longer than 8.3 (e.g. `"a much longer file name.txt"`), boot gOS, list the directory in the File Manager, confirm the full name displays correctly — cross-check against `mdir`'s own long-name output on the same image

**Milestone 23.2: LFN write support**
- [x] Extend `fat_create_file`/`fat_create_dir` (`kernel/src/fat32.c`) to generate the LFN entry set (per-entry checksum, UTF-16LE name chunks) plus a legal, unique 8.3 alias when a long name is given — the new entry-writing logic must preserve Finding #13.4's rollback-on-failure behavior in `create_entry`/`find_free_slot`, not bypass it
- [x] Test: in QEMU, create a new file via the File Manager's New File dialog using a long name; in a **separate, fresh QEMU process** against the same disk image, confirm the name round-trips — independently verified via `mdir` on the host

**Phase 23 exit criterion:** ✅ long filenames read and write correctly (create, rename, and delete all tested, both via a debug hook and via real simulated File Manager UI interaction), verified independently via `mtools` (`mdir`/`mtype`) in both directions; existing 8.3-only files and the full pre-existing regression suite (`make diagnostic`) unaffected. Four underspecified design questions (max name length, character set, short-alias scheme, rename scope) were resolved with the user before implementation. Full writeup: [phase23.md](phase23.md).

---

### Phase 24 — Shell, Calculator & Image Viewer ✅ Complete — see [phase24.md](phase24.md)
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 24.1: Interactive shell**
- [x] If Phase 19/20 landed: a genuine user-mode shell process with a simple line-editing prompt, capable of listing/navigating the FAT32 filesystem and launching other user-mode ELF binaries via the Phase 20 spawn syscall
- [x] If Phase 19/20 did not land or were cut: a kernel-mode "Terminal" window (following the existing File Manager/Editor architecture in `kernel/src/fm.c`/`editor.c`) offering an equivalent command-line-style interface, as an explicit fallback that doesn't block this phase on Track C
- [x] Test: in QEMU, type a sequence of shell commands (list directory, change directory, and — if user-mode — launch the Phase 19 test ELF binary) via simulated keystrokes; confirm correct output at each step via serial log and `screendump`

**Milestone 24.2: Calculator app**
- [x] A window-based calculator (kernel-mode built-in, following the existing `window.c` button-widget patterns) supporting basic arithmetic entered via mouse-clicked buttons
- [x] Test: in QEMU, click a sequence of buttons (e.g. "1", "2", "+", "7", "="); confirm the displayed result is correct via `screendump`

**Milestone 24.3: Image viewer app**
- [x] Reuse the BMP decoder already written for Phase 15.3's wallpaper loader (`kernel/src/wallpaper.c`) to display an arbitrary bundled or user-created BMP file in its own window, launched from the File Manager (double-clicking a `.BMP` file opens the viewer instead of the text editor)
- [x] Test: in QEMU, double-click `WALLPAPR.BMP` in the File Manager; confirm the image viewer opens and renders the image correctly via `screendump`, cross-checked pixel-for-pixel against the source file the same way Phase 15.3 was verified (random-sample pixel comparison against the source BMP, 0 mismatches expected)

**Phase 24 exit criterion:** ✅ a kernel-mode Terminal (per the plan's own explicit fallback, user-confirmed since gOS has no ring-3 read/list/file syscalls or C userland toolchain yet) whose `run` command performs a genuine ring-3 spawn-and-wait through Phase 20's real scheduler (`run Child.Elf` → exact exit code 7 round-tripped); a working Calculator (`1,2,+,7,=` → `19`, matching the milestone's own example exactly); and an Image Viewer reusing Milestone 15.3's BMP decoder (extracted into a shared `kernel/src/bmp.c`), pixel-verified byte-for-byte against the source BMP via an independent Python decode. One real bug found and fixed (Terminal's parser was including its own prompt text in the command string); `make diagnostic`'s full regression suite unaffected. Full writeup: [phase24.md](phase24.md).

---

### Patch v2 — Desktop Wallpaper Picker, Taskbar Clock Margin & Wallpaper Mapping Fix ✅ Complete — see [phase-patchv2.md](phase-patchv2.md)

Four rounds of ad hoc, user-requested fixes/enhancements after Phase 24 shipped - not a numbered phase, since none of it was planned in advance, but documented and tested to the same standard.

- [x] **Round 1:** discoverable wallpaper control - a desktop right-click context menu (user's choice over a 4th icon or a text hint), replacing the previously hidden F2-only toggle; cleared a stale test-session `GOS.CFG` from the disk image
- [x] **Round 2:** multi-wallpaper support - 3 user-provided JPEGs converted to gOS's required bottom-up 24bpp BMP format (no JPEG decoder written - out of scope by the same reasoning Phase 15.3 used to reject PNG/JPEG originally); wallpaper selection extended from a boolean to a 5-option table; `GOS.CFG` format version bumped 1→2
- [x] **Round 3:** taskbar clock margin fix - the clock's display box was narrower than its own text, with too small a right margin, together making it look like it touched the screen edge
- [x] **Round 4:** removed the now-unused source JPEGs; fixed a real bug where the "Mac" and "Custom" menu labels pointed at each other's bundled image

**Patch v2 exit criterion:** ✅ a discoverable 5-option wallpaper picker (Gradient/Default/Custom/Mac/Windows), each option correctly labeled and pixel-confirmed via screendump; the taskbar clock no longer touches the screen edge; the context menu itself never renders off-screen (a real bug found and fixed during testing); no obsolete source assets remain; `make diagnostic` unaffected. Full writeup: [phase-patchv2.md](phase-patchv2.md).

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
| **v2 total (Track A + B)** | **60–89** | **~9–12** |
| 18 — Boot-Time Cleanup & Diagnostics Mode | 4–6 | 0.5–1 |
| 19 — User Mode, Syscalls & ELF Loader | 16–24 | 2.5–3 |
| 20 — Preemptive Multitasking & Process Management | 14–20 | 2–2.5 |
| **Track C total** | **34–50** | **~5–6.5** |
| 21 — Window Resize & Alt+Tab | 8–12 | 1–1.5 |
| 22 — RTC Driver, Taskbar Clock & Settings Persistence | 10–14 | 1.5–2 |
| 23 — FAT32 Long Filename (VFAT) Support | 10–14 | 1.5–2 |
| **Track D total** | **28–40** | **~4–5.5** |
| 24 — Shell, Calculator & Image Viewer | 14–20 | 2–2.5 |
| **Track E total** | **14–20** | **~2–2.5** |
| **Tracks C+D+E total (Phase 18 + Track C + D + E)** | **80–116** | **~11.5–16.5** |
| **v2 total (Track A + B + C + D + E)** | **140–205** | **~20.5–28.5** |
| **Project grand total (v1 est. + v2 est.)** | **278–417** | **~46.5–62.5** |

*(v1's own total — Phases 0–11 — was estimated at 138–212 hrs in [version1/PROJECT_PLAN.md](version1/PROJECT_PLAN.md); that plan doesn't record a rolled-up actual-hours total the way v2's phase docs do, so the grand total above combines v1's estimate with v2's largely-actual-plus-estimated figures. Treat it as a rough order of magnitude, not a commitment — Tracks C/D/E in particular are still mostly unbuilt and their estimates will firm up the same way Track A/B's did once each phase actually starts.)*

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
| 18 | 18.1 Boot speed | Gate regression demos/stress test behind debug flag | Done | Default boot: ~1s (tick=100), down from ~75-80s — see [phase18.md](phase18.md) |
| 18 | 18.2 Diagnostic build | Add `make diagnostic` target preserving full test coverage | Done | Diagnostic build reproduces every pre-Phase-18 regression line byte-for-byte |
| 19 | 19.1 Ring 3 + TSS | GDT user segments + TSS `rsp0` wiring | Done | GDT/TSS already existed since v1.0; built `enter_user_mode()`/resume trampoline to exercise it — found + fixed a real `PAGE_USER` propagation bug in `vmm.c` along the way — see [phase19.md](phase19.md) |
| 19 | 19.2 Syscall entry | Minimal syscall table (`write`, `exit`) | Done | `int 0x80`, DPL=3 gate; `write`+`exit` dispatch; caller CS RPL independently verified as 3 |
| 19 | 19.3 ELF loader | Load + execute a bundled user-mode ELF binary | Done | Real ELF64 (`HELLO.ELF`) built via nasm+ld, bundled on disk, loaded+run; cross-checked via host `readelf` + byte-for-byte `diff` against the disk copy |
| 20 | 20.1 Context switching | Process table + timer-driven context switch | Done | Full per-process PML4 isolation (bigger scope, user-confirmed); found+fixed a NASM `ch`/register-name collision and an under-loaded first test — see [phase20.md](phase20.md) |
| 20 | 20.2 Process lifecycle | `exit`/`wait`/`spawn` syscalls | Done | `wait` is poll-style (non-blocking), documented as a deliberate scope cut; exact exit code (7) round-tripped parent<->child |
| 20 | 20.3 Scheduler fairness | Multi-process no-starvation test | Done | 5 concurrent processes, heavy interleaving, all complete; desktop confirmed responsive via real click immediately after |
| 21 | 21.1 Resize | Drag-to-resize from edge/corner | Done | Corner/right/bottom handles, min-size + screen-edge clamping both screendump-verified — see [phase21.md](phase21.md) |
| 21 | 21.2 Alt+Tab | Keyboard window-switching | Done | Fixed a real "only toggles last 2 windows" design bug before first boot; full-ring rotation verified via serial log + screendump across 3 windows |
| 22 | 22.1 RTC driver | CMOS date/time read | Done | Exact match vs. QEMU `-rtc base=...`, up to expected boot-delay drift — see [phase22.md](phase22.md) |
| 22 | 22.2 Taskbar clock | Live clock widget | Done | Exact 10s advance verified between two precisely-timed screendumps |
| 22 | 22.3 Settings persistence | Config file save/load across reboot | Done | Wallpaper toggle (F2) + FM geometry auto-saved; `xxd`-verified raw bytes; restored on a genuinely fresh boot |
| 23 | 23.1 LFN read | Parse + display long filenames | Done | mtools-seeded long name reconstructed exactly, cross-checked vs `mdir` — see [phase23.md](phase23.md) |
| 23 | 23.2 LFN write | Create files/dirs with long names | Done | Create/rename/delete all tested via real File Manager UI + debug hook; `mdir`/`mtype`-verified; `make diagnostic` regression suite unaffected |
| 24 | 24.1 Shell | Interactive shell (user-mode, or kernel-mode fallback) | Done | Kernel-mode Terminal (user-confirmed fallback); `run` genuinely spawns ring-3 ELF via `process_spawn`+`scheduler_run_until_done`, exact exit code round-tripped — see [phase24.md](phase24.md) |
| 24 | 24.2 Calculator | Window-based calculator app | Done | 4x4 button grid; `1,2,+,7,=` → `19` verified via real click sequence, matching milestone's own example |
| 24 | 24.3 Image viewer | BMP viewer reusing Phase 15.3's decoder | Done | Decoder extracted to shared `kernel/src/bmp.c`; opened via real FM double-click; pixel-verified byte-for-byte vs source BMP via independent Python decode |
| Patch v2 | Round 1 | Discoverable desktop right-click wallpaper menu | Done | Replaces hidden F2-only toggle; stale test `GOS.CFG` cleared — see [phase-patchv2.md](phase-patchv2.md) |
| Patch v2 | Round 2 | Multi-wallpaper support (3 JPEGs converted to BMP, 5-option menu) | Done | `GOS.CFG` format v1→v2; no JPEG decoder written, converted on host instead |
| Patch v2 | Round 3 | Taskbar clock right-margin fix | Done | `CLOCK_WIDTH` 60→64, dedicated `CLOCK_RIGHT_MARGIN` 14px |
| Patch v2 | Round 4 | Removed obsolete JPEGs; fixed Mac/Custom label-to-file swap | Done | Both selections screendump-confirmed correct after swap |

---

## 7. Dependencies / Blockers

- **Phase 13 depends on Phase 12** — audit #11's stale-window fix (13.6) is scoped alongside `window_close`, whose behavior Phase 16 also modifies; doing it in Phase 13 first avoids Phase 16 having to fix the same function twice.
- **Phase 16.1 (window teardown) depends on Phase 12.3 (kfree double-free check)** — teardown now calls `kfree` on owned allocations (e.g. `textbox_buffer`); without 12.3's guard, a teardown bug here would silently corrupt the heap instead of failing loudly during development.
- **Phase 16.1/16.3 depend on Phase 13.6 (stale-window dispatch fix)** — minimize and taskbar-driven restore/focus both re-enter the same button-dispatch and window-state logic that 13.6 hardens; building on the unfixed version would let the same class of stale-pointer bug resurface in the taskbar's click handling.
- **Phase 15.3 (BMP wallpaper stretch) depends on Phase 12.1 and 12.4** — reading a bundled file off FAT32 at boot exercises the exact write/read-chain-walk paths those fixes target; attempting this before Track A would risk hitting the underflow or an unbounded chain hang while loading the wallpaper itself.
- **Phase 17 depends on Phase 16 landing cleanly** — maximize/restore reuses Phase 16's window geometry and taskbar-visible state; see Risk section below.
- **README.md (Milestone 14.0) is a living task**, not a one-time checkbox — touched once at end of Track A (Phase 14) and again at end of Track B (Phase 16, or 17 if attempted).
- **Phase 19 depends on Phase 14's hardened VMM/GDT/IDT**, not just v1.0's original versions — it extends `vmm_map_page`/`vmm_unmap_page` (hardened by 13.5) into user-space page tables and extends the GDT/TSS that 12.5's IST stack already lives in. Building ring-3 support on the pre-Track-A versions of that code would risk the same class of bug Track A fixed for kernel-space mappings, just in a new (user-space) context.
- **Phase 20 depends on Phase 19** — a scheduler with nothing but the kernel's own loop to schedule is untestable, and 20.2's process-lifecycle syscalls need real ELF-loaded processes to round-trip through.
- **Phase 22.3 (settings persistence) depends on Phase 15 (wallpaper choice to persist) and, for full geometry persistence, Phase 21 (resize)** — if 21 hasn't landed yet, 22.3 can still persist window *position* alone and defer size persistence.
- **Phase 24.1 (shell) depends on Phase 19/20 for a genuine user-mode implementation**, but is explicitly allowed to fall back to a kernel-mode Terminal window (matching the existing File Manager/Editor architecture) if Track C is cut or delayed — Phase 24 should not block on Track C's riskiest phases landing perfectly.
- **Phase 24.3 (image viewer) depends on Phase 15.3's BMP decoder**, reused directly rather than reimplemented.
- **Phase 18 is recommended first but blocks nothing technically** — every subsequent phase's QEMU test iteration is faster once boot time is fixed, so there's no reason to defer it, but Track C/D/E don't have a hard dependency on it.

---

## 8. Risk / Scope-Creep Notes

- **Phase 16 (window close/minimize/taskbar state machine) is this plan's highest-risk phase**, the way v1 flagged memory management (Phase 3) and windowing (Phase 6). It touches three previously-fragile areas at once — window teardown, heap ownership, and click-dispatch — right after they were hardened in Track A. Budget slack here; if 16.1 (teardown) surfaces new heap or dispatch issues not caught by the Track A tests, resolve those before starting 16.3 (taskbar), don't build the taskbar on top of an uncertain teardown.
- **Phase 17 (maximize) is optional and should be the first thing cut if time runs short.** It's explicitly scoped as stretch-only, gated on Phase 16 landing cleanly with no lingering geometry/state bugs. If Phase 16 runs over its estimated 14–20 hours or its QEMU heap/state tests are flaky, skip Phase 17 entirely rather than layering more geometry state onto an unproven taskbar.
- **Phase 15.3 (BMP/raw image loader) is a stretch goal within Phase 15**, not a hard requirement — if it starts pulling in image-format complexity beyond a trivial raw/BMP reader, fall back to the solid-color/gradient wallpaper from 15.2 and move on to Phase 16.
- **Do not start Track B work early even if a Track A fix "looks small."** The priority rule (Section 1) exists specifically because Track B's riskiest phase (16) depends on Track A fixes that are easy to underestimate the blast radius of (kfree double-free, stale-window dispatch) — verify each Track A QEMU test actually passes before treating that finding as closed.
- **Phases 19/20 (Track C: user mode + multitasking) are this plan's highest-risk pair since Track B**, on the scale of v1's memory management (Phase 3) or Track B's window teardown (Phase 16) — this is gOS's first-ever ring-3 code and first-ever preemptive scheduling, touching GDT/TSS/paging/interrupts all at once. Budget significant slack; if 19's ELF loader or ring-3 transition surfaces instability, resolve it fully before starting 20's scheduler — a flaky scheduler layered on an uncertain user-mode transition would be far harder to debug than either problem alone.
- **Phase 24's shell (24.1) is explicitly allowed to degrade to a kernel-mode fallback** if Track C isn't ready in time — don't block the whole apps phase on Track C landing perfectly; ship the calculator and image viewer regardless.
- **Phase 23 (LFN) touches `create_entry`/`find_free_slot`** — the same functions Finding #13.4 hardened with rollback-on-failure. New LFN entry-writing logic must preserve that behavior, not bypass it; if 23.2's write-support work starts eroding that guarantee, land 23.1 (read-only) alone and defer write support.
- **As with Track B, Track C/D/E work should not start until the phase(s) it depends on (Section 7) are actually complete and tested**, not just "mostly done" — this bit v1 and is worth repeating for every subsequent track.
