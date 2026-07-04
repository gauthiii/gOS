# gOS — Project Plan v3

**Created:** 2026-07-02 (following [audit2.md](version2/audit2.md), a read-only review of everything built in v2's Tracks A–E — Phases 12–24 plus the wallpaper-picker patch)

---

## 1. Project Overview

gOS v2 is complete: a from-scratch x86_64 OS with UEFI/Limine boot, a FAT32 filesystem with VFAT long-filename support, a windowing compositor (resize/minimize/maximize/taskbar/Alt+Tab), ring-3 ELF processes under preemptive multitasking, a CMOS RTC-driven clock, `GOS.CFG` settings persistence, and five built-in apps (File Manager, Text Editor, a kernel-mode Terminal, Calculator, Image Viewer).

[audit2.md](version2/audit2.md) — a second read-only audit, scoped the same way [version1/audit.md](version1/audit.md) scoped itself against v1.0 — reviewed everything Tracks A–E added and produced **28 new findings** (5 Critical, 6 High, 10 Medium, 7 Low — see the note in Section 8 on the audit's own summary-line miscount) plus a clean regression check confirming none of the original 24 v1.0 findings were reintroduced. **None of these 28 findings have been fixed yet.**

### Strict track-priority rule

**Track A (audit 2 remediation) blocks every other track. No Track B/C/D work starts until Track A is fully complete and its QEMU tests verified passing.** Track B does not start until Track A is done; Track C does not start until Track B is done; Track D does not start until Track C is done. This is not new policy — v2's plan already had a version of this rule for its own Track A — but it bears repeating with teeth this time: **v1's original 24 audit findings sat unfixed for a full subsequent release (all of v2's Tracks B–E shipped on top of an unpatched kernel) before Track A of v2 finally closed them.** That is exactly the failure mode this rule exists to prevent, and it must not repeat for v3. Every phase in Tracks B–D below was deliberately designed to depend on a *specific* Track A fix landing first (e.g. Phase 28's new syscalls reuse Phase 25.1's pointer-validation discipline; Phase 29's real shell needs Phase 26.2's kill mechanism to be safe to build at all) — starting early doesn't just violate policy, it means building on top of the exact gaps the dependency was chosen to avoid.

### Tracks (priority order)

- **Track A — Audit 2 Remediation** (Phases 25–27.5): fix all 28 findings, then a regression pass confirming they (and the original 24) are still fixed.
- **Track B — Foundation gaps** (Phases 28–31): expand the syscall surface *using the validation discipline Track A establishes*, build a real userland toolchain and a genuine ring-3 shell, replace poll-style `waitpid` with real blocking, and harden memory protection (W^X, guard pages).
- **Track C — UI & app functionality** (Phases 32–35): a proper widget toolkit (fixing the menu bugs Track A patched by rebuilding the thing properly instead of patching twice), new apps that reuse Track A/B's hardened subsystems, and desktop polish.
- **Track D — Platform stretch** (Phases 36–37): APIC migration and optional sound — explicitly the first things cut if time runs short.
- **Phase 38 — Third audit pass**: closes the loop the same way Phase 27.5 does for Track A, scoped against everything Tracks A–D added this time.

### A note on `audit2.md`'s own finding count

`audit2.md`'s findings sections list **5 Critical, 6 High, 10 Medium, 7 Low** (28 total) when counted item-by-item; its own closing summary line says "7 High" — a one-off miscount in the audit doc itself, not a missing finding. This plan follows the itemized findings (6 High items, numbered #6–#11; the taskbar-right-click issue is Medium #12, not a 7th High item) since those are the ones with actual file:line evidence. Every one of the 28 itemized findings is scoped into Phase 25, 26, or 27 below.

---

## 2. Phases (dependency order)

| # | Phase | Track | Depends on |
|---|-------|-------|-----------|
| 25 | Audit 2: Critical Fixes | A | v2 (Phases 12–24 + patch) |
| 26 | Audit 2: High-Severity Fixes | A | 25 |
| 27 | Audit 2: Medium/Low Cleanup | A | 26 |
| 27.5 | Audit 2: Regression Pass | A | 27 |
| 28 | Syscall Surface Expansion | B | 27.5 (esp. 25.1's validation discipline) |
| 29 | Userland Toolchain & Real Shell | B | 28, 26.2 (kill mechanism) |
| 30 | Real Blocking Wait | B | 26.3 (waitpid ownership check), 29 |
| 31 | Memory Protection Hardening | B | 28–30 |
| 32 | UI Toolkit Upgrades | C | 27.5, 26.5/27.4's menu fixes (rebuilt, not patched twice) |
| 33 | New Apps, Wave 1 | C | 32, 25.2 (leak fix, for a trustworthy System Monitor), 27.3 (config checksum) |
| 34 | New Apps, Wave 2 | C | 32, 25.5 (hardened BMP codec), 27.3 (config format) |
| 35 | Desktop Polish | C | 32–34 |
| 36 | APIC Migration | D | 31 |
| 37 | Sound (optional) | D | 36 |
| 38 | Third Audit Pass | — | 25–37 (whatever of Track D actually lands) |

---

## 3–4. Milestones & Tasks per Phase

Every fix in Phases 25–27 follows the same standard, matching `audit2.md`'s own: **a QEMU-verified reproduction of the bug *before* the fix, and a passing test *after*** — a fix without a demonstrated repro is not confirmed closed, the same rule Phase 12–14 used for the original 24 findings.

### Phase 25 — Audit 2: Critical Fixes ✅ Complete — see [phase25.md](phase25.md)
**Estimated time: 16–24 hours (~2.5–3 weeks)**

**Milestone 25.1: Validate syscall pointers/lengths against the caller's mapped region**
- [x] In `kernel/src/syscall.c`, before dereferencing `frame->rdi`/copying via `frame->rsi` in `SYS_WRITE` and `SYS_SPAWN`, walk the calling process's own page tables (`pml4_phys`, stored per-process since Phase 20) to confirm every byte of the requested `[ptr, ptr+len)` range is actually mapped and user-accessible, rejecting the syscall (return an error code, not a partial success) if not
- [x] Reproduce first: a debug-only test ELF that calls `SYS_WRITE` with a pointer into an intentionally-unmapped region; confirm (pre-fix) this currently reaches `panic_screen()` and halts the machine — capture the panic screen via `screendump` as the "before" evidence
- [x] Test: same ELF, post-fix; confirm the syscall returns an error to the process (visible via its own follow-up `SYS_WRITE` of a "got error" marker, or via `process_get()->exit_code` if it exits with a distinct code) instead of faulting the kernel, and that the rest of the desktop remains fully responsive afterward (real mouse click on a window immediately after)

**Milestone 25.2: Real process teardown on exit**
- [x] On every transition to `PROC_ZOMBIE` (or at reap time in `SYS_WAITPID`), free every PT_LOAD/stack physical page via `pmm_free_page()`, free every page-table page the process's PML4 subtree owns (walking down from `pml4_phys`), and `kfree()` the process's kernel stack
- [x] Reproduce first: run `heap_free_bytes()`/a PMM free-page counter before and after 10 consecutive `run <name.elf>` cycles from the Terminal; confirm (pre-fix) free memory strictly decreases and never returns to baseline
- [x] Test: same 10-cycle loop, post-fix; confirm free heap bytes and free physical pages both return to their pre-loop baseline after the 10th process is reaped, not just "roughly stable" — an exact match, the same bar Phase 16.1's window-teardown leak test used

**Milestone 25.3: Fix `fat_rename`'s erase-after-write ordering**
- [x] Restructure `fat_rename` (`kernel/src/fat32.c`) so a failure in `erase_dirent_and_lfn` after a successful `write_named_entry` cannot leave two live entries pointing at the same cluster chain — either verify the erase succeeded before reporting the rename as done, or make the erase step itself resilient enough to retry/recover, and reflect any residual failure honestly (e.g. the new name exists, but say so, rather than a bare "failed" if the write half already committed)
- [x] Reproduce first: inject a forced single-sector write failure into the erase step (a debug-only fault-injection hook, following the same pattern the v1 audit used for ATA write-path testing) during a rename; confirm (pre-fix) the directory ends up with both the old and new name resolving to the same cluster, verified independently via `mdir`
- [x] Test: same fault-injection scenario, post-fix; confirm via `mdir` that exactly one name resolves to the file's data afterward, and that the API's return value accurately reflects what's actually on disk

**Milestone 25.4: Reorder `fat_delete_file`/`fat_delete_dir` to erase-then-free**
- [x] Swap the order in both functions (`kernel/src/fat32.c`) so `erase_dirent_and_lfn` runs *before* `fat_free_chain` — a failure now fails closed (entry gone, clusters merely leaked and recoverable by a future fsck-equivalent) instead of failing open (entry live, clusters already reusable elsewhere)
- [x] Reproduce first: same fault-injection technique as 25.3, this time on the erase step of a delete; confirm (pre-fix) the entry is still listed/resolvable while its clusters are already free and reused by a subsequent unrelated `fat_create_file`, verified via `xxd`/`mdir` showing two files sharing cluster data
- [x] Test: same scenario, post-fix; confirm the entry is gone from the listing (erase succeeded first) even though the chain-free step that would follow never got a chance to run cleanly, with no double-allocation possible

**Milestone 25.5: Bound BMP `w`/`h` before the row-stride/allocation arithmetic**
- [x] In `kernel/src/bmp.c`'s `bmp_decode()`, add an explicit sane maximum for `w`/`h` (e.g. bounded by a multiple of the framebuffer's own resolution, or a fixed generous cap like 8192×8192) checked *before* computing `row_stride` or the `kmalloc` size, closing the integer-overflow path
- [x] Reproduce first: hand-craft a malformed BMP with `w`/`h` values near the `int32_t`/product-overflow boundary (following the same "seed a scratch disk image via `mtools`" technique Phase 23 used for LFN testing), drop it on the disk image, open it via Image Viewer; confirm (pre-fix) this is at minimum a plausible overflow per the audit's arithmetic trace (may require intentionally crafting the exact overflow-triggering dimensions and confirming via a debug log of the computed sizes, since triggering the actual OOB write may depend on heap layout)
- [x] Test: same crafted file, post-fix; confirm `bmp_decode` rejects it (logged reason, returns 0) instead of allocating an undersized buffer, and that both the Image Viewer and the wallpaper picker (which share this decoder) handle the rejection gracefully (existing fallback-to-gradient / flash-message paths, unchanged)

**Phase 25 exit criterion:** ✅ all 5 Critical findings closed, each with a QEMU-verified reproduction-then-fix test passing. Found and fixed a real, previously-undiscovered bug along the way: `HELLO.ELF` was linked at the wrong virtual address for the multi-process spawn path (outside the process-private PML4 slot), silently leaking memory on every run since teardown could never reach it — a debug-only `BADPTR.ELF` test ELF proves 25.1 via a real Terminal `run` command, and `pmm_free_pages()` returns to the exact byte-identical baseline after every one of 10 spawn cycles. `make diagnostic` unaffected. Full writeup: [phase25.md](phase25.md).

---

### Phase 26 — Audit 2: High-Severity Fixes
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 26.1: Free partially-built address space on `process_spawn()` failure**
- [ ] In `kernel/src/process.c`, track every physical page and page-table page allocated so far during a `process_spawn()` attempt; on any failure path (a `map_and_fill_page` call failing, the kernel-stack `kmalloc` failing), free all of it — including `pml4_phys` itself — before returning -1
- [ ] Reproduce first: force a late-stage `map_and_fill_page` failure (e.g. a debug hook that fails the Nth call) during spawn; confirm (pre-fix) PMM free-page count doesn't return to baseline after the failed spawn
- [ ] Test: same forced-failure scenario, post-fix; confirm free-page count returns exactly to baseline after a failed spawn attempt

**Milestone 26.2: `SYS_KILL` + a way for Terminal's `run` to use it**
- [ ] Add a `SYS_KILL`-equivalent kernel-callable function (`process_kill(pid)`, tearing down via 25.2's new cleanup path) and a syscall wrapper for future ring-3 use
- [ ] Give the Terminal (`kernel/src/terminal.c`) either a hard timeout on `run` (e.g. a maximum tick count passed to a bounded variant of `scheduler_run_until_done()`) or a way to interrupt the blocking wait on a keypress (e.g. Ctrl+C checked between scheduler slices), calling `process_kill()` on timeout/interrupt instead of blocking forever
- [ ] Reproduce first: `run` a debug-only ELF with an intentional infinite loop (never calling `SYS_EXIT`); confirm (pre-fix) this freezes the entire desktop with no recovery
- [ ] Test: same infinite-loop ELF, post-fix; confirm the Terminal recovers (via timeout or Ctrl+C) within a bounded time, the process is actually killed (verified via `process_get()->state` and a PMM free-page count returning to baseline per 25.2/26.1's cleanup), and the desktop remains fully responsive throughout

**Milestone 26.3: `SYS_WAITPID` parent-pid ownership check**
- [ ] In `kernel/src/syscall.c`'s `SYS_WAITPID` handler, reject (return -1, "not your child") any `waitpid` call where `target->parent_pid != scheduler_current_pid()`
- [ ] Reproduce first: two sibling test ELFs where one calls `waitpid` on the other's pid (not its own child); confirm (pre-fix) the non-parent successfully reaps the zombie and steals its exit code, leaving the real parent's own later `waitpid` returning -1 forever
- [ ] Test: same two-sibling scenario, post-fix; confirm the non-parent's `waitpid` is rejected, and the real parent still successfully reaps its own child with the correct exit code afterward

**Milestone 26.4: Fix `write_named_entry`'s multi-slot LFN write partial-failure sentinel**
- [ ] Restructure the LFN-plus-SFN write loop (`kernel/src/fat32.c`) so a partial failure can't leave a `0x00` first-byte sentinel ahead of later still-valid entries in the same cluster — e.g. write in an order (or with a final commit step) such that any prefix of the write sequence that completes is itself a valid, fully-scannable state, or explicitly detect and repair a stray sentinel on the next directory scan
- [ ] Reproduce first: fault-inject a mid-sequence `write_dirent_at` failure during a long-filename create in a directory that already has valid entries positioned after the reused slot run; confirm (pre-fix) via `mdir`/`fat_list_dir` that those later entries become invisible
- [ ] Test: same scenario, post-fix; confirm all previously-existing entries remain listed/resolvable regardless of where the fault-injected failure lands in the write sequence

**Milestone 26.5: Fix desktop right-click menu z-order and hit-test correctness**
- [ ] Move the context menu's rendering to *after* `window_composite()` in the frame order (true top-layer overlay, matching how the mouse cursor is already drawn last), and make the open-menu guard check the menu's full post-clamp footprint against every open window's rect, not just the click pixel
- [ ] Reproduce first: right-click on empty desktop immediately beside an open window such that the (unclamped) menu position would overlap that window; confirm (pre-fix) via screendump that part of the menu renders invisibly underneath the window, and that a click in that invisible region still triggers `select_wallpaper()`
- [ ] Test: same scenario, post-fix; confirm via screendump the menu is always fully visible (either drawn on top of the window, or repositioned/suppressed to avoid the overlap), with no click-through to a hidden menu row

**Milestone 26.6: Terminal prompt-boundary guard**
- [ ] Add a tracked "prompt end offset" to the Terminal's window state, set every time a prompt is printed; make the textbox backspace handler (shared in `kernel/src/window.c`, or a Terminal-specific override via `custom_key`) refuse to delete past that offset
- [ ] Reproduce first: type `ls`, press Enter, then backspace repeatedly through the new prompt and into the previous line's output, then type a command; confirm (pre-fix) the resulting parsed command is corrupted/unintended per the audit's traced scenario
- [ ] Test: same backspace-through-prompt sequence, post-fix; confirm backspacing stops at the prompt boundary (or is simply a no-op past it), and the next command parses exactly as typed

**Phase 26 exit criterion:** all 6 High findings closed, each with a QEMU-verified reproduction-then-fix test passing.

---

### Phase 27 — Audit 2: Medium/Low Cleanup
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 27.1: Calculator sign handling + overflow guard**
- [ ] Fix `parse_int` (`kernel/src/calculator.c`) to recognize a leading `-` as a sign rather than an invalid "digit", so chaining an operation off a negative result (e.g. `3-5=` then `+4=`) computes correctly
- [ ] Add an overflow check around the four arithmetic operations (e.g. detect via division-based overflow checks before each operation, matching the existing "Error: div by 0" pattern) and display an "Error: overflow" message instead of a silently wrapped result
- [ ] Reproduce first: `3 - 5 =` then `+ 4 =`; confirm (pre-fix) the result is wrong. Separately, `999999999999 * 99999999 =`; confirm (pre-fix) a silently wrapped/nonsensical result with no error shown
- [ ] Test: same two sequences, post-fix; confirm the first produces the mathematically correct result (`2`) and the second shows a clear overflow error instead of a wrong number

**Milestone 27.2: Heap backward coalescing**
- [ ] Extend `kernel/src/heap.c`'s `kfree()` to also coalesce backward with the immediately preceding block when it's free (requires either a backward pointer or a footer-based lookup, whichever fits the existing block-header design with the least disruption)
- [ ] Reproduce first: a debug scenario allocating/freeing a repeating pattern of small and large (multi-MB, image-sized) blocks designed to strand small free gaps between larger freed regions that forward-only coalescing can't merge; confirm (pre-fix) `heap_grow()` is invoked more times than the total freed space should require
- [ ] Test: same pattern, post-fix; confirm adjacent free blocks (in both directions) merge into one, and the same workload triggers measurably fewer `heap_grow()` calls / lower peak heap usage

**Milestone 27.3: `GOS.CFG` checksum + File Manager geometry sanity check**
- [ ] Add a checksum (e.g. a simple additive or CRC-style checksum over the record) to `struct settings_record` in `kernel/src/settings.c`, verified on load alongside the existing magic/version/size checks
- [ ] Range/sanity-check loaded `fm_x`/`fm_y`/`fm_w`/`fm_h` against the screen dimensions before use in `desktop.c`'s `fm_create_window()` call, falling back to the compiled-in defaults for any field that's clearly invalid (negative, zero width/height, or entirely off-screen) rather than trusting the file unconditionally
- [ ] Reproduce first: hand-corrupt a few bytes of a valid `GOS.CFG`'s payload (via a host-side `mtools`/`xxd` edit) while leaving magic/version/size intact, and separately write an all-zero `fm_w`/`fm_h`; confirm (pre-fix) the corrupted checksum isn't detected, and the zero-geometry case launches an unusable File Manager window
- [ ] Test: same two corrupted files, post-fix; confirm the checksum mismatch is detected and defaults are used instead, and the invalid geometry is individually replaced with defaults rather than applied verbatim

**Milestone 27.4: Remaining Medium/Low findings**
- [ ] #12 Taskbar right-click wrongly opening the desktop wallpaper menu (`kernel/src/desktop.c`/`taskbar.c`) — exclude the taskbar's y-range from the desktop's right-click handler. Test: right-click on a taskbar entry, confirm no wallpaper menu appears
- [ ] #14 Redundant wallpaper reload on re-selecting the active option (`kernel/src/wallpaper.c`) — add an `idx == current_selection` short-circuit to `wallpaper_select()`. Test: re-click the currently-active wallpaper option, confirm via serial log that no `fat_read_file`/`bmp_decode` call happens
- [ ] #18 Short-alias-exhaustion generic error code (`kernel/src/fat32.c`) — give `generate_short_alias` exhaustion its own distinguishable return/log path from generic disk-full. Test: create 9 colliding-basename siblings, confirm the 10th's failure is logged distinctly from a real disk-full failure
- [ ] #19 `find_free_slot_n`'s cluster-boundary limitation (`kernel/src/fat32.c`) — either extend the run-search to carry `run_len` across a cluster boundary, or explicitly document it as an accepted simplification if the fix proves disproportionately invasive (a genuine scope call to make during implementation, not before)
- [ ] #20 `erase_dirent_and_lfn` silent degrade-to-short-name on partial failure (`kernel/src/fat32.c`) — surface a distinguishable log/error when a partial erase leaves a name degraded, rather than silently succeeding-looking. Test: fault-inject a partial erase, confirm a distinct log line appears instead of silent degrade
- [ ] #21 `cd` path truncation (`kernel/src/terminal.c`) — detect when the joined candidate path would exceed the buffer and reject with an error instead of silently truncating. Test: `cd` into deeply nested long-named directories until truncation would occur, confirm an explicit error instead of a silent wrong-directory `cd`
- [ ] #22 `run` with an embedded-space name (`kernel/src/terminal.c`) — detect a space in the parsed name and give a clear "invalid name" message instead of the generic spawn-failure message. Test: `run My File.ELF`, confirm a clear diagnostic
- [ ] #23 `on_close` callback reentrancy guard (`kernel/src/window.c`) — clear/guard against `window_close()` being re-entered for the same index from within its own `on_close` callback. Test: a debug-only `on_close` that calls `window_close()` on itself, confirm no double-free/double-invoke
- [ ] #24 Right-click menu not dismissed by a click on a window (`kernel/src/desktop.c`) — dismiss `menu_visible` on any left OR right click that lands on a window while the menu is open. Test: open the menu, click a window, confirm the menu is gone on the next screendump
- [ ] #26 RTC century handling (`kernel/src/rtc.c`) — read CMOS register 0x32 when present instead of hardcoding `+2000`. Test: boot with a QEMU `-rtc` date past 2099 (if QEMU's CMOS emulation supports it) or document as a known limitation if it doesn't, confirming the read path at least attempts the century register
- [ ] #27 Image Viewer memory/window budget (`kernel/src/imageviewer.c`) — add an explicit pre-check (e.g. estimated decode size against remaining heap) before attempting a large open, with a clear "image too large" message rather than relying on the existing graceful-failure paths alone. Test: attempt to open a very large image with a heap deliberately near-exhausted, confirm a clear message instead of a generic allocation-failure message

**Phase 27 exit criterion:** all 10 Medium and 7 Low findings closed or explicitly documented (per #19's allowed scope call), each with before/after evidence proportionate to its severity (full repro-then-fix for anything with a real failure mode; a straightforward before/after log line for pure UX/error-message improvements).

---

### Phase 27.5 — Audit 2: Regression Pass
**Estimated time: 4–6 hours (~0.5–1 week)**

**Milestone 27.5.1: Re-verify all 24 v1.0 findings, following `audit2.md`'s own spot-check methodology**
- [ ] For each of the original 24 findings, re-read the *current* file:line the finding pointed at (or wherever that logic now lives after Phases 25–27's edits) and confirm the fix is still intact — not by re-reading old phase docs, by reading the actual current code
- [ ] Document any regression found (expected: none, based on `audit2.md`'s own 5/24 spot-check already coming back clean, but all 24 get checked this time, not just 5)

**Milestone 27.5.2: Re-verify all 28 v2-audit findings against the Phase 25–27 code**
- [ ] For each of the 28 findings, confirm the corresponding Phase 25/26/27 fix is present and its documented test still passes against the current build (a full `make diagnostic` run plus each fix's own targeted QEMU test)
- [ ] This becomes a **standing practice**, not a one-time step — re-run this same pass (all 24 + all 28, i.e. 52 checks total) as part of Phase 38's third audit, and at the start of any future phase-25-scale remediation effort

**Phase 27.5 exit criterion:** all 24 v1.0 findings and all 28 v2-audit findings independently re-confirmed fixed against the current codebase (not stale docs), `make diagnostic` passing, **Track A fully closed — Track B may now begin.**

---

### Phase 28 — Syscall Surface Expansion
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 28.1: `read`/`open`/`close` syscalls**
- [ ] Add `SYS_OPEN` (path → a small per-process file-descriptor table entry), `SYS_READ` (fd + buffer + length → bytes read), `SYS_CLOSE` (fd → release the table entry) — every one of these validates its pointer/length arguments against the calling process's mapped region using **exactly** the discipline Phase 25.1 established, no exceptions
- [ ] Test: a debug-only ring-3 ELF that opens a known bundled file, reads it in chunks, and writes what it read back out via the existing `SYS_WRITE`; confirm (via serial log) the read content matches the file's real contents, cross-checked independently via `mtype`/`mdir` on the host

**Milestone 28.2: `list-dir`/`stat` syscalls**
- [ ] Add `SYS_LISTDIR` (path → an array of directory-entry records copied into a user-supplied buffer, length-validated per 25.1's discipline) and `SYS_STAT` (path → size/attributes)
- [ ] Test: a debug-only ELF that lists the root directory via the new syscall and compares the count/names against what `fat_list_dir` itself reports kernel-side (logged separately for comparison), plus an independent `mdir` cross-check

**Milestone 28.3: Fuzz/adversarial pointer testing across the whole syscall table**
- [ ] For every syscall (old and new), write a debug-only adversarial test ELF that deliberately passes out-of-range pointers/lengths to each one in turn, confirming every single one is rejected cleanly (no page fault reaching `panic_screen()`) rather than just the ones this phase added
- [ ] Test: run the full adversarial suite; confirm zero panics, and that each syscall's specific rejection is logged and distinguishable

**Phase 28 exit criterion:** a real, validated file-I/O and directory-listing syscall surface exists, every syscall (not just the new ones) provably rejects out-of-range user pointers without crashing the kernel, verified via an adversarial test suite covering the entire table.

---

### Phase 29 — Userland Toolchain & True User-Mode Shell
**Estimated time: 20–30 hours (~3–4 weeks) — this plan's highest-risk phase, see Section 8**

**Milestone 29.1: Minimal libc + build pipeline for C user programs**
- [ ] A tiny freestanding libc (syscall wrappers for write/exit/spawn/waitpid/kill/open/read/close/listdir/stat, minimal string/memory functions) and a Makefile target that cross-compiles a C source file into a bundleable, statically-linked ELF64 binary
- [ ] Test: port `tools/userland/hello.asm`'s behavior to a C source file, build it via the new pipeline, bundle and run it; confirm identical output to the original hand-assembled version, plus an independent `readelf` structural comparison between the old and new binary

**Milestone 29.2: Port the Terminal from kernel-mode to a genuine ring-3 shell**
- [ ] Rewrite the Terminal as a C user-mode program (using 29.1's libc and Phase 28's syscalls) with equivalent `ls`/`cd`/`run`/`help`/`clear` functionality, launched by the kernel at boot (or from the desktop icon) as a real spawned process rather than a kernel-mode window
- [ ] This is now safe to build specifically *because* Phase 26.2 gave the kernel a way to kill a hung child — the new shell's own `run` command inherits that same kill/timeout path rather than reintroducing Finding #7's freeze risk in a new form
- [ ] Test: the same command sequence Phase 24's kernel-mode Terminal was tested with (`ls`, `cd`, `run Child.Elf` → exact exit code 7) now run through the real ring-3 shell process, confirmed via serial log and screendump; additionally confirm a `run`-ed infinite-loop program can be killed without freezing the desktop, exercising 26.2's mechanism from this new caller

**Milestone 29.3: Retire or clearly demote the kernel-mode Terminal**
- [ ] Decide (a user-facing choice, not purely technical) whether the kernel-mode Terminal is removed entirely in favor of the new ring-3 shell, or kept as a documented fallback/diagnostic tool — either way, the desktop's Terminal icon should launch exactly one of them, not both, to avoid user confusion about which "Terminal" they're getting
- [ ] Test: whichever is chosen, confirm the desktop icon launches it correctly and the other is either fully removed or clearly unreachable from the UI

**Phase 29 exit criterion:** a real user-mode C toolchain exists and produces working ring-3 binaries; the Terminal is a genuine ring-3 process (or the kernel-mode version is deliberately retained with a clear, documented reason and no user-facing ambiguity); `run`-ing a hung program from the new shell doesn't freeze the desktop, proven via the same kill-mechanism test Phase 26.2 established.

---

### Phase 30 — Real Blocking Wait
**Estimated time: 10–14 hours (~1.5–2 weeks)**

**Milestone 30.1: Wait-queue-based `SYS_WAITPID`**
- [ ] Replace the poll-style "return -1 until zombie" `SYS_WAITPID` with a real blocking implementation: a waiting process is descheduled (removed from the ready queue) and only re-added when its specific child transitions to `PROC_ZOMBIE`, rather than being repeatedly scheduled just to immediately re-check and re-block
- [ ] Builds directly on Phase 26.3's ownership check — the wait queue only ever wakes the actual parent for its actual child, never a process that isn't entitled to reap that pid
- [ ] Test: a parent that calls the new blocking `waitpid` immediately after spawning a child that takes a deliberately long time to exit; confirm via serial log timestamps that the parent is genuinely descheduled (not burning scheduler slices in a busy poll loop) and wakes promptly when the child exits, plus confirm the desktop remains responsive throughout (no busy-wait stealing frame time)

**Milestone 30.2: Regression-test every existing `waitpid` caller against the new blocking semantics**
- [ ] Re-run Phase 20's original multi-process fairness test, Phase 24's Terminal `run` command (now via Phase 29's real shell), and any other existing `waitpid` call site against the new blocking implementation
- [ ] Test: confirm identical observable behavior (same exit codes round-tripped, same fairness properties) to the poll-style implementation it replaces — this phase should be invisible to every existing caller except in being more efficient

**Phase 30 exit criterion:** `waitpid` genuinely blocks (descheduling the caller) rather than polling, with no regression in any existing spawn/wait call site, verified by re-running their original tests unchanged.

---

### Phase 31 — Memory Protection Hardening
**Estimated time: 12–18 hours (~2–2.5 weeks)**

**Milestone 31.1: W^X page permissions**
- [ ] Ensure no page mapped into a user process is ever simultaneously writable and executable — code (PT_LOAD text) pages map read+execute, data/BSS/stack pages map read+write, never both, enforced in the ELF-loading path (`kernel/src/usermode.c`) and `vmm_map_page`'s callers
- [ ] Test: a debug-only ELF that deliberately attempts to execute code from its own stack (a classic exploit-primitive shape); confirm this now faults (page-protection violation) instead of succeeding, verified via a controlled QEMU test rather than by inspection alone

**Milestone 31.2: Guard pages for kernel and user stacks**
- [ ] Leave an unmapped guard page immediately beyond the end of every allocated kernel stack (`kmalloc`'d, per process) and every user-mode stack (`PROC_STACK_BASE`'s region), so a stack overflow faults immediately and visibly (routed to the IST-backed double-fault handler from the original v1.0 audit's Finding #5) instead of silently corrupting whatever memory happens to sit beyond the stack
- [ ] Test: a debug-only deliberately-deep-recursion ELF/kernel path that overflows its stack; confirm it now reliably hits the guard page and produces a clean panic/fault report rather than corrupting adjacent memory silently — compare against the pre-fix behavior (if reproducible) as the "before" evidence

**Phase 31 exit criterion:** no user-mapped page is ever both writable and executable, verified by a real controlled exploit-shaped test; every process stack (kernel and user) has a guard page that reliably converts an overflow into a clean fault instead of silent corruption. **Track B fully closed — Track C may now begin.**

---

### Phase 32 — UI Toolkit Upgrades
**Estimated time: 16–22 hours (~2.5–3 weeks)**

**Milestone 32.1: Scrollbars**
- [ ] A reusable scrollbar widget (vertical, at minimum) for content taller than its window — the File Manager's own "no scrolling in v1" limitation (documented in `fm_render`'s comments since Phase 9) is the first real consumer
- [ ] Test: a directory with more entries than fit in a default-sized File Manager window; confirm all entries are reachable via the scrollbar, screendump-verified at both scroll extremes

**Milestone 32.2: Reusable dropdown/context-menu widget**
- [ ] Generalize the wallpaper picker's ad hoc context-menu code (`kernel/src/desktop.c`) into a real `window.c`-level widget — a reusable menu type with N labeled rows, a callback per row, proper z-order (always drawn on top, fixing Finding #10/#26.5 as part of the *rebuild* rather than patching the bespoke implementation a second time), and correct occlusion/dismiss semantics (fixing Finding #24/#27.4 the same way)
- [ ] Migrate the desktop's wallpaper menu to use the new widget, confirming it's now a thin caller of general menu code rather than its own bespoke implementation
- [ ] Test: re-run the exact z-order/occlusion/dismiss tests from Phase 26.5 and Phase 27.4's #24 fix against the *new* generalized widget, confirming both bugs stay fixed through the rebuild, not just in the old bespoke code

**Milestone 32.3: Checkbox/radio widgets**
- [ ] Add checkbox and radio-button widget types to `window.c`, following the same `window_add_button`-style registration pattern
- [ ] Test: a debug-only test window with a few checkboxes/radio buttons; confirm click-to-toggle works and state is queryable, screendump-verified

**Milestone 32.4: Keyboard shortcuts framework**
- [ ] A generalized per-window keyboard-shortcut registration mechanism (beyond the ad hoc `custom_key` callbacks each app currently hand-rolls for e.g. Ctrl+S, Enter-to-submit), so new apps don't each reinvent shortcut handling
- [ ] Test: register a shortcut in a debug test window, confirm it fires correctly and doesn't interfere with normal typing in a textbox in the same window

**Milestone 32.5: Input validation checklist for future apps**
- [ ] Write a short, concrete checklist (living in this plan or a short `CONTRIBUTING`-style note) that Phase 33/34's new apps are explicitly checked against before being marked Done: numeric input has overflow/sign handling (the Calculator-class bug class from Finding #15/#16), file/path input is length-bounded and doesn't silently truncate into wrong-but-valid state (the `cd`-class bug from Finding #21), and any syscall/file-read path reuses Phase 25/28's validation discipline rather than a new ad hoc check
- [ ] Test: retroactively run the checklist against the *existing* Calculator and Terminal (post-Phase-27 fixes) as a dry run, confirming the checklist itself would have caught Findings #15/#16/#21 had it existed at the time

**Phase 32 exit criterion:** scrollbars, a reusable/generalized menu widget (with Finding #10/#24's fixes carried through the rebuild, verified by re-running their tests), checkboxes/radio buttons, a shared keyboard-shortcut framework, and a written input-validation checklist ready to gate Phase 33/34.

---

### Phase 33 — New Apps, Wave 1
**Estimated time: 14–20 hours (~2–2.5 weeks)**

**Milestone 33.1: Clock/Timer app**
- [ ] A window-based clock/timer/stopwatch app reusing the existing `rtc.c` driver (and its Phase 27.4-hardened century handling)
- [ ] Test: set a short countdown, confirm it fires correctly against wall-clock time via timed screendumps, the same precision-timing technique Phase 22.2's taskbar clock test used
- [ ] Checked against Phase 32.5's input-validation checklist before marking Done

**Milestone 33.2: Notes / sticky notes app**
- [ ] A simple text-notes app, persisted via the now-checksummed `GOS.CFG`-family config format from Phase 27.3 (either extending that same file or a sibling file using the identical checksummed-record pattern)
- [ ] Test: create a note, reboot (fresh QEMU process), confirm it's restored — and confirm a deliberately corrupted notes file is detected via the checksum and falls back safely, rather than displaying garbage
- [ ] Checked against Phase 32.5's checklist before marking Done

**Milestone 33.3: System Monitor app**
- [ ] A window showing live heap-free-bytes, the process table (pid/state/exit code), and uptime — meaningful and trustworthy specifically *because* Phase 25.2's leak fix means these numbers now reflect real system state rather than a permanently-draining counter
- [ ] Test: open System Monitor, run a few processes to completion via the shell, confirm the displayed process-table entries and heap-free-bytes both reflect reality (cross-checked against the same `heap_free_bytes()`/`process_get()` calls Phase 25.2's own test used) and return to baseline after each process is reaped
- [ ] Checked against Phase 32.5's checklist before marking Done

**Phase 33 exit criterion:** three new apps, each screendump/serial-log verified, each explicitly checked against Phase 32.5's input-validation checklist, `make diagnostic` unaffected.

---

### Phase 34 — New Apps, Wave 2
**Estimated time: 16–22 hours (~2.5–3 weeks)**

**Milestone 34.1: Paint (bitmap editor)**
- [ ] A basic bitmap-editing app (freehand draw, fill, save/load) reusing the now-hardened `bmp.c` codec from Phase 25.5 (bounded dimensions) rather than a new decoder
- [ ] Test: draw something, save as a `.BMP`, reopen it in both Paint and the existing Image Viewer, confirm pixel-identical round-trip via an independent Python comparison (the same technique Phase 15.3/24.3 used)
- [ ] Checked against Phase 32.5's checklist before marking Done

**Milestone 34.2: Snake or Minesweeper**
- [ ] One simple game (whichever proves the smaller lift once scoped in detail — a genuine implementation decision to make at the time, not pre-committed here), exercising the keyboard-shortcut framework (Snake) or the checkbox/grid-click patterns (Minesweeper) from Phase 32
- [ ] Test: play a full game to a win/loss condition via simulated input, screendump-verified at key states

**Milestone 34.3: Settings app (GUI front-end)**
- [ ] A real Settings window replacing the scattered right-click-menu/F2 mechanisms — wallpaper selection (using Phase 32.2's generalized menu/list widget), and any other config surfaced through Phase 33.2's notes-adjacent persistence work
- [ ] Test: change wallpaper via the new Settings app, confirm it persists identically to the old right-click menu's behavior (same `GOS.CFG` fields, same checksum verification from 27.3), then remove or clearly demote the old right-click menu once the GUI replacement is confirmed equivalent
- [ ] Checked against Phase 32.5's checklist before marking Done

**Phase 34 exit criterion:** Paint (pixel-verified round-trip via the hardened BMP codec), one working game, and a Settings app that fully replaces the ad hoc right-click/F2 mechanisms with no loss of functionality, each checked against Phase 32.5's checklist, `make diagnostic` unaffected.

---

### Phase 35 — Desktop Polish
**Estimated time: 10–14 hours (~1.5–2 weeks)**

**Milestone 35.1: Draggable/arrangeable desktop icons**
- [ ] Icons (Files/Terminal/Calc/any new Phase 33/34 app icons) become draggable, with positions persisted (via 27.3's hardened config format)
- [ ] Test: drag an icon to a new position, reboot, confirm it's restored at the dragged position

**Milestone 35.2: Taskbar app launcher**
- [ ] A launcher affordance in the taskbar (beyond the current open-window list) to start any installed app without needing its desktop icon visible/in view
- [ ] Test: launch each app via the new taskbar launcher, confirm identical behavior to launching via its desktop icon

**Milestone 35.3: Window-snap-to-edge tiling**
- [ ] Dragging a window against a screen edge snaps it to a half/full-screen tile, extending the existing drag/resize/maximize machinery rather than replacing it
- [ ] Test: drag a window to each screen edge, confirm correct snap geometry via screendump, and confirm un-snapping (drag away) restores the pre-snap geometry exactly, the same round-trip-geometry bar Phase 17.1's maximize/restore test used

**Phase 35 exit criterion:** icons are draggable and persist position, a taskbar launcher works for every installed app, and edge-snap tiling round-trips geometry exactly on un-snap. **Track C fully closed.**

---

### Phase 36 — APIC Migration
**Estimated time: 20–28 hours (~3–4 weeks) — high-risk, see Section 8; first cut if time runs short**

**Milestone 36.1: Local APIC + IO-APIC bring-up alongside the existing 8259 PIC**
- [ ] Detect and initialize the local APIC and IO-APIC, initially running in parallel with (not yet replacing) the legacy PIC, to validate the new interrupt path without risking the existing one
- [ ] Test: confirm timer/keyboard/mouse IRQs are deliverable via the new APIC path in a controlled test build, with the legacy PIC path still the default for normal boots

**Milestone 36.2: Full cutover from 8259 PIC to APIC**
- [ ] Disable the legacy PIC, route all existing IRQ sources through the IO-APIC, remap vectors as needed
- [ ] Test: re-run the *entire* existing `make diagnostic` regression suite (PMM/heap self-tests, FAT32 stress test, window stress test) against the APIC-only interrupt path, confirming zero regressions — this is the SMP prerequisite the plan flags as its biggest platform risk, and nothing in Track C should have to change because of it if this phase is done correctly

**Phase 36 exit criterion:** gOS boots and runs its full existing regression suite entirely on APIC interrupt delivery, with the legacy 8259 PIC path fully retired.

---

### Phase 37 — Sound (optional)
**Estimated time: 8–12 hours (~1–1.5 weeks) — cut first if Phase 36 runs over budget**

**Milestone 37.1: PC speaker beep, or a minimal AC'97 driver (scope decision at implementation time)**
- [ ] At minimum, a simple PC-speaker beep callable from any app (e.g. the new Timer app's alarm); a real AC'97 driver is a stretch-within-a-stretch, only attempted if Phase 36 left meaningful budget
- [ ] Test: trigger a beep from the Timer app's countdown completing, confirm audibly/via QEMU's audio backend logging that the expected tone plays

**Phase 37 exit criterion:** at least a working beep is callable from app code; a real audio driver is a bonus, not a requirement, for this phase to be considered done.

---

### Phase 38 — Third Audit Pass
**Estimated time: 10–16 hours (~1.5–2.5 weeks)**

**Milestone 38.1: Read-only audit of everything Tracks A–D added**
- [ ] Scoped identically to how `audit2.md` scoped itself against v1.0, and how this plan's Phase 27.5 scoped itself against v2: a full read-only review of Phases 25–37 (whatever of Track D actually landed), producing a ranked (Critical/High/Medium/Low) findings doc, `audit3.md`
- [ ] Particular attention to the phases this plan itself flagged as highest-risk: Phase 25.1/25.2 (syscall validation, process teardown — do they hold up under Phase 28's expanded syscall surface?), Phase 29 (the userland toolchain — a whole new C compilation pipeline is exactly the kind of thing that accumulates its own class of bugs), and Phase 36 (APIC migration — did the interrupt-routing cutover introduce anything the existing regression suite doesn't exercise?)

**Milestone 38.2: Regression check against every finding closed so far**
- [ ] Re-run Phase 27.5's full methodology (all 24 v1.0 findings + all 28 v2 findings) *plus* every finding Phases 25–37 themselves introduced fixes for, confirming none were reintroduced by later phases in this same plan
- [ ] Document the full running total (v1: 24, v2: 28, v3: however many `audit3.md` finds) in one place, so the next plan (v4, if one exists) has a single source of truth for "everything ever found and fixed" rather than needing to reconstruct it from three separate audit docs

**Phase 38 exit criterion:** `audit3.md` published with the same rigor as `audit2.md`/`audit.md`, a clean (or explicitly triaged) regression check against every finding from all three audits, and a consolidated running tally ready to hand off to whatever comes next.

---

## 5. Estimated Time Summary

Assuming the same ~7.5 hrs/week pace as v1/v2:

| Phase | Estimated hours | Estimated weeks |
|---|---|---|
| 25 — Audit 2 Critical Fixes | 16–24 | 2.5–3 |
| 26 — Audit 2 High-Severity Fixes | 14–20 | 2–2.5 |
| 27 — Audit 2 Medium/Low Cleanup | 14–20 | 2–2.5 |
| 27.5 — Audit 2 Regression Pass | 4–6 | 0.5–1 |
| **Track A total** | **48–70** | **~7–9** |
| 28 — Syscall Surface Expansion | 14–20 | 2–2.5 |
| 29 — Userland Toolchain & Real Shell | 20–30 | 3–4 |
| 30 — Real Blocking Wait | 10–14 | 1.5–2 |
| 31 — Memory Protection Hardening | 12–18 | 2–2.5 |
| **Track B total** | **56–82** | **~8.5–11** |
| 32 — UI Toolkit Upgrades | 16–22 | 2.5–3 |
| 33 — New Apps, Wave 1 | 14–20 | 2–2.5 |
| 34 — New Apps, Wave 2 | 16–22 | 2.5–3 |
| 35 — Desktop Polish | 10–14 | 1.5–2 |
| **Track C total** | **56–78** | **~8.5–10.5** |
| 36 — APIC Migration | 20–28 | 3–4 |
| 37 — Sound (optional) | 8–12 | 1–1.5 |
| **Track D total** | **28–40** | **~4–5.5** |
| 38 — Third Audit Pass | 10–16 | 1.5–2.5 |
| **v3 grand total (Tracks A–D + Phase 38)** | **198–286** | **~29.5–39** |

*(Figures are estimates, not commitments — Track A's estimates carry the most confidence since the findings are already fully specified in `audit2.md`; Track B/C/D will firm up the same way v2's did once each phase actually starts.)*

---

## 6. Status Tracker

| Phase | Milestone | Task | Status | Notes |
|---|---|---|---|---|
| 25 | 25.1 Syscall pointer validation | Validate rdi/rsi against mapped region in SYS_WRITE/SYS_SPAWN | Done | `vmm_range_mapped_user()`; verified via real Terminal `run Badptr.Elf` — see [phase25.md](phase25.md) |
| 25 | 25.1 Syscall pointer validation | QEMU test: unmapped-pointer repro fails cleanly post-fix | Done | BADPTR.ELF exit_code=42, desktop stayed responsive |
| 25 | 25.2 Process teardown | Free physical pages, page tables, kernel stack on exit | Done | `vmm_destroy_process_pml4()` + `process_free_resources()`; found+fixed a real HELLO.ELF linker-script bug along the way |
| 25 | 25.2 Process teardown | QEMU test: 10x run cycles, heap/PMM return to baseline | Done | pmm_free_pages() exact-match baseline after every cycle, not just at the end |
| 25 | 25.3 fat_rename ordering | Fix erase-after-write-failure duplicate-entry risk | Done | Bounded 3x retry + honest failure reporting |
| 25 | 25.3 fat_rename ordering | QEMU test: fault-injected erase failure, mdir cross-check | Done | Transient failure recovered via retry; persistent failure preserved original file |
| 25 | 25.4 fat_delete reordering | Erase entry before freeing cluster chain | Done | Fails closed instead of open |
| 25 | 25.4 fat_delete reordering | QEMU test: fault-injected erase failure, no double-alloc | Done | DELTEST.TXT still resolvable after failed delete |
| 25 | 25.5 BMP dimension bound | Bound w/h before row-stride/alloc arithmetic | Done | BMP_MAX_DIMENSION=4096, checked before any arithmetic |
| 25 | 25.5 BMP dimension bound | QEMU test: crafted-dimension BMP rejected cleanly | Done | 100000x100000-claiming BMP correctly rejected |
| 26 | 26.1 process_spawn cleanup | Free partial address space on spawn failure | Not Started | |
| 26 | 26.2 SYS_KILL + Terminal integration | Add kill mechanism + timeout/interrupt in run | Not Started | |
| 26 | 26.3 waitpid ownership | Reject non-parent waitpid calls | Not Started | |
| 26 | 26.4 LFN multi-slot write fix | Fix partial-failure 0x00 sentinel hiding entries | Not Started | |
| 26 | 26.5 Desktop menu z-order | Draw menu after windows, fix hit-test footprint | Not Started | |
| 26 | 26.6 Terminal prompt guard | Block backspace past prompt boundary | Not Started | |
| 27 | 27.1 Calculator fixes | Sign handling + overflow guard | Not Started | |
| 27 | 27.2 Heap backward coalescing | Extend kfree to merge backward | Not Started | |
| 27 | 27.3 Config checksum + geometry check | GOS.CFG checksum, FM geometry sanity check | Not Started | |
| 27 | 27.4 Remaining 11 findings | #12,14,18,19,20,21,22,23,24,26,27 | Not Started | |
| 27.5 | 27.5.1 Regression: v1.0 findings | Re-verify all 24 original findings against current code | Not Started | |
| 27.5 | 27.5.2 Regression: v2 findings | Re-verify all 28 audit2 findings against Phase 25-27 code | Not Started | |
| 28 | 28.1 read/open/close syscalls | Add with 25.1's validation discipline | Not Started | |
| 28 | 28.2 list-dir/stat syscalls | Add with same validation discipline | Not Started | |
| 28 | 28.3 Adversarial pointer fuzz test | Whole-table syscall pointer fuzz suite | Not Started | |
| 29 | 29.1 Minimal libc + toolchain | C build pipeline for user programs | Not Started | |
| 29 | 29.2 Ring-3 shell port | Port Terminal to real user-mode process | Not Started | |
| 29 | 29.3 Retire/demote kernel-mode Terminal | Decide and implement single-Terminal UX | Not Started | |
| 30 | 30.1 Blocking waitpid | Wait-queue based implementation | Not Started | |
| 30 | 30.2 Regression vs existing callers | Re-test Phase 20/24/29 wait call sites | Not Started | |
| 31 | 31.1 W^X page permissions | No page both writable and executable | Not Started | |
| 31 | 31.2 Stack guard pages | Kernel + user stack guard pages | Not Started | |
| 32 | 32.1 Scrollbars | Reusable vertical scrollbar widget | Not Started | |
| 32 | 32.2 Generalized menu widget | Rebuild wallpaper picker on reusable widget | Not Started | |
| 32 | 32.3 Checkbox/radio widgets | Add to window.c | Not Started | |
| 32 | 32.4 Keyboard shortcuts framework | Generalized shortcut registration | Not Started | |
| 32 | 32.5 Input validation checklist | Written + dry-run against Calculator/Terminal | Not Started | |
| 33 | 33.1 Clock/Timer app | Reuses RTC driver | Not Started | |
| 33 | 33.2 Notes app | Persisted via checksummed config format | Not Started | |
| 33 | 33.3 System Monitor app | Heap/process/uptime, trustworthy post-25.2 | Not Started | |
| 34 | 34.1 Paint app | Reuses hardened BMP codec | Not Started | |
| 34 | 34.2 Snake or Minesweeper | Scope decision at implementation time | Not Started | |
| 34 | 34.3 Settings app | GUI replacing right-click/F2 mechanisms | Not Started | |
| 35 | 35.1 Draggable desktop icons | Persisted positions | Not Started | |
| 35 | 35.2 Taskbar app launcher | Launch any installed app | Not Started | |
| 35 | 35.3 Window snap-to-edge tiling | Exact geometry round-trip on un-snap | Not Started | |
| 36 | 36.1 APIC bring-up alongside PIC | Parallel validation before cutover | Not Started | |
| 36 | 36.2 Full APIC cutover | Retire legacy PIC, full regression re-run | Not Started | |
| 37 | 37.1 PC speaker beep / AC'97 | Scope decision at implementation time | Not Started | |
| 38 | 38.1 Third audit pass | audit3.md against Phases 25-37 | Not Started | |
| 38 | 38.2 Full regression check | All v1 + v2 + v3 findings re-verified | Not Started | |

---

## 7. Dependencies / Blockers

- **Track A blocks Tracks B/C/D entirely** — see Section 1's strict-priority rule. This is the single most important rule in this plan.
- **Phase 26 depends on Phase 25** — several High findings (e.g. 26.1's `process_spawn` cleanup) build directly on 25.2's new process-teardown machinery; fixing them out of order would mean writing cleanup code twice.
- **Phase 27.5 depends on all of Phase 25–27 landing first** — it's a regression *check*, not something that can run concurrently with the fixes it's checking.
- **Phase 28 depends on Phase 25.1 specifically**, not just "Track A generally" — every new syscall must reuse the exact pointer-validation discipline 25.1 establishes; building Phase 28 before 25.1 exists would mean either blocking on it anyway or inventing a second, potentially inconsistent validation approach.
- **Phase 29 depends on Phase 28 (needs the new syscalls to build a real shell) and Phase 26.2 specifically** (needs the kill mechanism to be safe — this is called out explicitly because it's the plan's own stated reasoning for why Phase 29 wasn't attempted back in v2's Phase 24, and that reasoning is only satisfied once 26.2 actually exists, not just once Track A "in general" is done).
- **Phase 30 depends on Phase 26.3** (the ownership check) **and Phase 29** (a real shell process is the natural first caller of blocking wait semantics that matter).
- **Phase 32.2 depends on Phase 26.5/27.4's #24 fix already existing** — the point of rebuilding the menu as a general widget is to carry a *working* fix through the rebuild, re-verified by the same tests, not to fix the bug for the first time during the rebuild.
- **Phase 33.3 (System Monitor) depends on Phase 25.2** — a "free memory" display is actively misleading (and arguably worse than not having the app at all) if the process-exit leak it would be displaying against isn't fixed first.
- **Phase 34.1 (Paint) depends on Phase 25.5** — reusing an unbounded BMP decoder in a *second* app doubles the attack surface for the same integer-overflow bug; Paint must not exist before that decoder is hardened.
- **Phase 36 depends on Phase 31** — APIC migration touches the same low-level interrupt/memory-protection territory Phase 31 hardens; sequencing memory protection first means Phase 36 isn't laying new interrupt infrastructure on top of still-soft memory protections.
- **Phase 38 depends on Phases 25–37** (whatever of Track D actually landed) — same "don't audit against a moving target" reasoning that made `audit2.md` wait for all of v2's Tracks A–E to actually ship first.

---

## 8. Risk / Scope-Creep Notes

- **Phase 25.1 (syscall pointer validation) and Phase 25.2 (process teardown) are this plan's highest-risk *Track A* items**, on the scale of v1's memory management (Phase 3) or v2's window teardown (Phase 16). Both touch the exact ring-3/ring-0 boundary and page-table-walking code that's historically been this project's least-tested territory (Phase 19's original VMM bug, the `PAGE_USER` propagation issue, was found in exactly this neighborhood). Budget real slack; if either surfaces new instability beyond the audit's own predicted failure mode, resolve it fully before moving to Phase 25.3+, don't layer more Critical fixes on an uncertain foundation.
- **Phase 29 (userland toolchain) is this plan's single highest-risk phase overall**, on the scale of v2's Track C (Phases 19/20). Standing up a real C-to-ELF64 build pipeline from scratch is a substantially bigger undertaking than anything reused/extended elsewhere in this plan — it's new toolchain work, not new kernel code reusing existing patterns. If it runs significantly over budget, the fallback (explicitly allowed, matching v2's Phase 24 precedent) is to keep the kernel-mode Terminal as the shipped shell and defer the ring-3 port to a future plan, rather than let it block Phase 30/31 indefinitely — but note Phase 30 do genuinely benefit from a real shell existing, so deferring 29 has a real, not just cosmetic, cost to Track B's remaining phases.
- **Track D (Phases 36–37) is explicitly this plan's first-cut candidate if time runs short**, exactly like v2's Phase 17 was for Track B. Phase 36 (APIC) in particular is high-risk *and* has no direct payoff until a future SMP effort exists to use it — if Track A–C together run over their combined ~150-230 hour estimate, cut Track D entirely rather than rushing it; an incomplete APIC cutover that leaves some IRQ source only half-migrated is worse than not starting it.
- **Phase 27.4 bundles 11 small, independently-scoped fixes into one milestone** — this is a deliberate density choice (each fix is individually small), but if any single item in that list proves less trivial than expected during implementation (the audit flagged #19's cluster-boundary limitation as a case where "explicitly document as an accepted simplification" is a legitimate outcome, not a required fix), split it out rather than let one stubborn item block the other ten.
- **Do not start Track B/C/D work early even if a Track A fix "looks small,"** the same warning v2's plan gave for its own Track A/B boundary — the audit's own summary line (see Section 1) already shows how easy it is to miscount/misjudge scope even while writing the audit itself; verify each Track A QEMU test actually passes before treating that finding as closed, and don't let "it's basically done" substitute for the regression pass in Phase 27.5.
- **As with v1 and v2, Track B/C/D work should not start until the phase(s) it depends on (Section 7) are actually complete and tested**, not just "mostly done" — this has now bitten this project across two consecutive plans (v1's unfixed-audit-into-v2, and the reasoning this very plan opens with) and is worth repeating a third time until it stops needing repeating.

---

## 9. `README.md` Maintenance

Update `README.md`'s "Known limitations" section at the end of **every track**, not just once at the end of the whole plan — it's a living document, and Track A alone retires several currently-listed limitations (e.g. the poll-style-wait note becomes stale mid-plan at Phase 30, not at the very end; the row-index double-click-identity note may be retired or need updating depending on Phase 32/33's UI work). Specifically:
- **After Track A (Phase 27.5):** remove/update any "known limitation" language that referred to the 28 now-fixed findings; add a note that this is the second full audit-remediation pass, mirroring the existing note about the first.
- **After Track B (Phase 31):** update the `wait` is poll-style" limitation (retired at Phase 30) and note the new syscall surface, real shell, and hardened memory protections.
- **After Track C (Phase 35):** update the app list (five apps become up to ten), note the retired ad hoc settings mechanisms (Phase 34.3), and the row-index double-click-identity item if Phase 32's toolkit work changes how the File Manager tracks selection.
- **After Track D (Phase 37, if it lands) and Phase 38:** final pass noting APIC migration (if done) and pointing at `audit3.md` the same way the current README points at `audit.md`/`audit2.md`.
