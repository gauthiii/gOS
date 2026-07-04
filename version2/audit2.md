# gOS — Code Audit 2 (2026-07-02)

Read-only review of everything built since the original [audit.md](version1/audit.md) (v1.0, Phases 0–11): Track A/B/C/D/E's Phases 12–24 plus the post-24 wallpaper-picker patch. No code was changed as part of this audit — findings only. Ranked by severity within each tier, numbered continuously across the whole document.

Covers: `kernel/src/fat32.c` (VFAT long-filename read/write, rewritten in Phase 23), `kernel/src/process.c`/`syscall.c`/`usermode.c` (ring-3 user mode, Phase 19/20), `kernel/src/window.c`/`desktop.c` (widget-count increase, close-callback API, right-click context menu), `kernel/src/terminal.c`/`calculator.c`/`imageviewer.c`/`bmp.c` (Phase 24's new apps), `kernel/src/wallpaper.c`/`settings.c` (5-option wallpaper picker patch), `kernel/src/fm.c`/`editor.c`/`keyboard.c`/`taskbar.c`/`rtc.c`, and `kernel/src/heap.c`/`pmm.c`/`vmm.c`. A separate regression pass re-checked 5 of the original 24 v1.0 findings against the *current* code to confirm none were silently reverted or weakened by later phases (see §Regression Check).

---

## Critical — real crash/corruption risk

### 1. Any user-mode process can crash the entire kernel via an unvalidated pointer in `SYS_WRITE`/`SYS_SPAWN`
**`kernel/src/syscall.c:36-40`** (`SYS_WRITE`) and **`kernel/src/syscall.c:48-57`** (`SYS_SPAWN`)
```c
const char *buf = (const char *)frame->rdi;
uint64_t len = frame->rsi;
for (uint64_t i = 0; i < len; i++) {
    serial_write_char(buf[i]);
}
```
Neither syscall validates that `rdi`/`rsi` actually correspond to pages mapped into the *calling* process's small, explicitly-mapped address space (its ELF's PT_LOAD segments plus 4 stack pages — everything else in that process's private PML4 slot is unmapped). `SYS_SPAWN`'s 64-byte path copy (`syscall.c:48-57`) has the same root cause: it correctly caps the copy length against its own kernel-side buffer, but `len` itself is taken verbatim from the untrusted `rsi` register with no check against what's actually mapped in the caller. A user program that calls either syscall with a `len`/pointer reaching past its own mapped region causes the kernel to dereference unmapped memory *while running in ring 0 servicing the syscall*. Every page fault (`kernel/src/idt.c:110-139`) unconditionally calls `panic_screen()` and halts — there is no fault recovery. A single misbehaving (or simply buggy, not malicious) ring-3 ELF permanently freezes the whole OS — desktop, other windows, everything — which is a full, easily-reachable denial-of-service from unprivileged code.

### 2. Process teardown never frees anything — physical pages, page tables, and kernel stacks leak on every process exit
**`kernel/src/syscall.c:70-79`** (`SYS_WAITPID` reap path), **`kernel/src/process.c`** (whole file — no cleanup function exists anywhere)
```c
frame->rax = (uint64_t)(int64_t)target->exit_code;
target->state = PROC_UNUSED; /* reap */
```
"Reaping" only flips `state` back to `PROC_UNUSED` so `process_spawn()`'s free-slot scan can reuse the *table slot*. Nothing ever calls `pmm_free_page()` on the process's ELF/stack physical pages, nothing frees the PML4/PDPT/PD/PT page-table pages `vmm_create_process_pml4()` allocated, and the `kmalloc(PROC_KSTACK_SIZE)` kernel stack (`process.c:160`) is never `kfree()`'d. `vmm_unmap_page()` exists (added post-v1.0, fixing original Finding #10) but is never called from `process.c`/`syscall.c`. With `MAX_PROCESSES=8`, using the new Terminal's `run` command (`terminal.c:178-188`, which spawns-and-waits to completion every time) repeatedly leaks a full ELF's physical pages, page tables, and 8KiB kernel stack on **every single run that completes**, not just under obscure conditions — this is the *only* code path that exists for process exit, on any process, ever.

### 3. `fat_rename` can leave two directory entries pointing at the same data if the old entry's erase fails after the new entry's write succeeds
**`kernel/src/fat32.c:1338-1346`**
```c
struct dirent_location new_loc;
if (!write_named_entry(parent_cluster, new_name, entry.attr, entry.first_cluster, entry.size, &new_loc)) {
    return 0;
}
return erase_dirent_and_lfn(&loc, &span);
```
The comment above this reasons carefully about avoiding data loss on a *write* failure (write the new entry before erasing the old one), but never considers erase-after-write failing. If `write_named_entry` succeeds and the subsequent `erase_dirent_and_lfn` then fails (a single `ata_write_sector` failure — the same class of fault the v1 audit's Finding #6 already flagged as undetected), `fat_rename` returns 0 ("failed") while the directory now durably contains **two live entries** — old and new name — both pointing at the same `first_cluster`. Deleting either one calls `fat_free_chain()` on that shared cluster chain while the other name's entry still references it; the freed clusters can then be handed to an unrelated new file by `fat_alloc_cluster()`, and reading the surviving stale name returns (or corrupts) someone else's data.

### 4. `fat_delete_file`/`fat_delete_dir` free the cluster chain before erasing the directory entry, so an erase failure leaves a live entry pointing at already-freed (and reusable) clusters
**`kernel/src/fat32.c:1271-1272`** and **`:1305-1306`**
```c
fat_free_chain(entry.first_cluster);
return erase_dirent_and_lfn(&loc, &span);
```
Both delete paths mark the whole cluster chain free in the FAT *before* attempting to erase the directory entry. If `erase_dirent_and_lfn` then fails partway (same single-sector-write-failure class as #3), the function returns 0 ("delete failed") but the entry is still fully visible via `fat_list_dir`/`fat_resolve_path` — with its `first_cluster` chain already marked free. The very next `fat_create_file`/`fat_write_file`/`fat_create_dir` anywhere on the volume can allocate those same clusters for an unrelated file while the "deleted-but-not-erased" entry still resolves to and reads/writes the same physical storage — two files silently sharing clusters. Reordering (erase first, free after) would fail closed (entry gone, clusters merely leaked) instead of failing open (entry live, clusters actively reused elsewhere).

### 5. BMP decoder's dimension/row-stride arithmetic can integer-overflow, undersizing the `kmalloc` relative to the decode loop's actual iteration range
**`kernel/src/bmp.c:34-58`**
```c
if (header_size < 40 || planes != 1 || bpp != 24 || compression != 0 || w <= 0 || h <= 0) { ... return 0; }
uint64_t row_stride = (((uint64_t)w * 3) + 3) & ~3ULL;
if ((uint64_t)pixel_offset + row_stride * (uint64_t)h > len) { ... return 0; }
uint32_t *pixels = (uint32_t *)kmalloc((uint64_t)w * (uint64_t)h * sizeof(uint32_t));
```
`w`/`h` are only checked for `> 0`, not bounded relative to `len` except via the truncation check above — and that check, plus the `kmalloc` size on the next line, are both 64-bit products of attacker/file-controlled 32-bit values that can themselves wrap for sufficiently large `w`/`h` (near `2^31`), making the truncation check and the allocation size both pass while the decode loops (driven by `int32_t w`/`h` directly, not the wrapped product) still iterate the full, huge, un-wrapped range — a classic crafted-input heap overflow. **Not currently triggered**: the four bundled BMPs (`WALLPAPR.BMP`/`CUSTOM.BMP`/`MAC.BMP`/`WINDOWS.BMP`) are well-formed 1280×800 files. But both `wallpaper.c`'s `load_option()` and Phase 24's `imageviewer.c`'s `imageviewer_open()` feed **arbitrary file content the user navigates to and opens** into this exact function with no additional sanity check of their own — a malformed or deliberately crafted `.BMP` dropped anywhere on the FAT volume and opened via the File Manager reaches this path directly.

---

## High — real, but narrower trigger conditions

### 6. `process_spawn()`'s own failure paths leak the partially-built address space
**`kernel/src/process.c:146-149, 154-158, 160-164`**
```c
if (!map_and_fill_page(pml4_phys, vaddr, src, copy_len)) {
    kfree(buf);
    return -1;
}
...
for (uint64_t i = 0; i < PROC_STACK_PAGES; i++) {
    if (!map_and_fill_page(pml4_phys, PROC_STACK_BASE + i * PAGE_SIZE, 0, 0)) {
        return -1;
    }
}
```
`pml4_phys` (`vmm_create_process_pml4()`) is allocated before any PT_LOAD/stack mapping runs. If a mapping call fails partway (PMM exhaustion) on any page, or the kernel-stack `kmalloc` fails afterward, `process_spawn()` returns -1 without ever freeing `pml4_phys` or any already-mapped physical/page-table pages — compounding Finding #2 by making even a *failed* spawn leak memory. Reachable directly from ring 3 via `SYS_SPAWN`, this is an attacker-triggerable resource-exhaustion primitive layered on top of the unconditional per-exit leak.

### 7. `run` freezes the entire desktop (input, cursor, every other window) for as long as the spawned process runs, with no way to abort a hung program
**`kernel/src/terminal.c:178-188`** blocking on **`kernel/src/process.c:235-250`** (`scheduler_run_until_done`) / **`kernel/src/scheduler_entry.asm`**
```c
int pid = process_spawn(arg);
...
scheduler_run_until_done();  // doesn't return until every spawned process is PROC_ZOMBIE
```
`scheduler_run_until_done()` never returns until the process reaches `PROC_ZOMBIE`; the desktop's single-threaded `for(;;)` main loop (`start.c`'s frame loop: `window_system_update()` → `desktop_render()` → `window_composite()` → `fb_flip()`) is entirely suspended for the whole duration — no frame renders, the cursor freezes, keystrokes/clicks merely queue rather than being processed. There is no `SYS_KILL`, no watchdog, and no way to interrupt a running process. Typing `run` on any bundled or future ELF that never reaches its `SYS_EXIT` call (an infinite loop, or a genuine bug) hangs the entire GUI permanently — a single terminal command is a full, unrecoverable OS hang short of a hard reset.

### 8. `SYS_WAITPID` has no ownership check — any process can reap any other process's zombie
**`kernel/src/syscall.c:70-79`**
```c
int target_pid = (int)frame->rdi;
struct process *target = process_get(target_pid);
if (!target || target->state != PROC_ZOMBIE) {
```
`process_get()` returns a pointer to any slot with no check that `target->parent_pid` matches the caller's own pid. Any process — not just the real parent — can `waitpid` on any other zombie's pid, steal its exit code, and reap it out from under its actual parent, which then gets `-1` ("not exited") forever since the slot has already flipped to `PROC_UNUSED`. With multiple concurrent children a normal usage pattern (`start.c`'s 5-process fairness demo), this is trivially reachable by two sibling processes racing to `waitpid` the same pid.

### 9. `write_named_entry`'s multi-slot LFN write, if it fails partway, can leave a `0x00` sentinel that hides every later entry in that directory cluster
**`kernel/src/fat32.c:972-1003`**, interacting with the `0x00`-terminates-the-whole-scan behavior in `fat_list_dir`/`find_dirent` (`fat32.c:361-363`, `:636-638`)
The LFN-plus-SFN write loop commits entries low-to-high offset within a freshly-zeroed or reused slot run. If any `write_dirent_at` call fails partway (a single sector write error), the function returns 0 immediately, but earlier records in that same run are already committed (valid non-zero-first-byte LFN entries) while later records (including the SFN) are still `0x00`. Both `fat_list_dir` and `find_dirent` treat the *first* `0x00` first-byte as **end of the entire directory**, not "this slot is free, keep scanning" — so if this partially-written run sits before other, later, still-valid entries in the same cluster (plausible if it reused a hole from an earlier delete), every entry after it becomes permanently invisible to listing/lookup, though still physically intact on disk. No fsck/repair path exists in this codebase to recover it.

### 10. Desktop right-click menu can render entirely hidden behind an already-open window, while its hit-test rectangle stays live
**`kernel/src/desktop.c:98-121`** (menu drawn in `desktop_render`, which runs *before* `window_composite()` in the frame order) vs. the click-open guard at **`desktop.c:154`**
The right-click-open guard only checks `window_point_hits_any()` at the exact click *pixel*, not against the menu's full footprint after the edge-clamping at `desktop.c:165-171` potentially slides its final position elsewhere — and since the menu is drawn on the *bottom* compositing layer (before windows), any part of it that ends up under an open window's body is rendered invisible underneath it. Right-clicking an empty strip of desktop immediately beside an open window can open a menu whose rows are drawn under that window; a subsequent left click the user believes is landing on the window above (which `window_system_update()` — running first — handles completely normally) can *also* fall inside the invisible menu's rectangle, causing `desktop_update()` to silently call `select_wallpaper()`/`settings_save()` as an unintended side effect of an ordinary-looking window click.

### 11. Backspacing past the Terminal's freshly-printed prompt corrupts the next command's parsing
**`kernel/src/window.c:578-582`** (generic textbox backspace, no prompt-boundary concept) + **`kernel/src/terminal.c:196-231`** (prompt-strip-by-string-comparison logic)
The shared textbox backspace handler only checks `textbox_length > 0` — nothing stops a user from backspacing through the just-printed `/cwd> ` prompt and into prior scrollback. `terminal_on_key`'s prompt-strip logic does a byte-for-byte compare of the current line against a freshly reconstructed `"/cwd> "` string; once the prompt has been partially erased, that comparison fails to match at the front, and the raw (now-mangled, possibly containing leftover fragments of previous output) line is fed straight into `split_command`/`run_run` as if it were a fresh command — including as a `run <name>` invocation the user never intended to type. Not a memory-safety bug (all buffers stay in-bounds), but a real, easily reproduced command-confusion bug.

---

## Medium

### 12. Right-clicking the taskbar strip opens the desktop's wallpaper menu instead of doing nothing taskbar-specific
**`kernel/src/desktop.c:150-174`** vs. **`kernel/src/taskbar.c:70-103`**
`taskbar.c`'s own click handling only checks `MOUSE_LEFT_BUTTON` and has no right-click logic at all; nothing in `desktop_update()`'s right-click branch excludes the taskbar's y-range (`fb_height() - TASKBAR_HEIGHT .. fb_height()`) — `window_point_hits_any()` only tests real window rects, not the taskbar strip. Right-clicking a taskbar window-list entry (e.g. testing whether the taskbar has its own menu, or simply missing a window's title bar by a few pixels) instead pops the wallpaper-selection menu, clamped to sit just above the taskbar directly under the cursor. A reflexive second click to dismiss it can land on a wallpaper row and silently change (and persist) the wallpaper.

### 13. Heap has no backward coalescing — mixed-size Phase 24 image workloads can fragment the fixed 64MiB ceiling over a long session
**`kernel/src/heap.c:170-180`** (forward-coalesce only, explicitly documented as a deliberate v1 limitation) and **`heap.c:8`** (`HEAP_MAX_SIZE = 64MiB`)
Not a confirmed crash — a real structural gap. A freed multi-MB block (Image Viewer's/wallpaper's ~3-4MB pixel buffers) can never merge backward with an earlier-freed adjacent block. Repeated open/close of differently-sized BMPs interleaved with smaller, shorter-lived allocations (window state, FAT read buffers, terminal buffers) over a long uptime can, in principle, leave many free blocks individually too small for the next large request even though their sum would suffice, forcing `heap_grow()` to keep permanently consuming fresh space until the 64MiB ceiling is hit and `kmalloc` starts failing for otherwise-satisfiable requests. Compounds with Finding #2/#6's leaks consuming the same heap for kernel stacks.

### 14. `wallpaper_select()` unconditionally reloads from disk even when re-selecting the already-active wallpaper
**`kernel/src/wallpaper.c:73-97`**
No `if (idx == current_selection) return;` short-circuit exists anywhere (unlike `settings_record_fm_geometry`'s explicit unchanged-value check). Every call — including re-clicking the *currently selected* option in the desktop menu, or `settings_load()` at boot re-selecting what `wallpaper_init()` already loaded — performs a full `kmalloc` + `fat_read_file` + `bmp_decode` (another multi-MB `kmalloc`) + `kfree` of the old buffer, purely to end up in the same state. On this OS's synchronous ATA/FAT32 stack, that's a several-hundred-millisecond-to-multi-second stall per redundant click for zero visible effect.

### 15. Calculator's `parse_int` has no sign handling — chaining an operation off a negative result silently produces the wrong answer
**`kernel/src/calculator.c:49-55, 106-113`**
`append_result_digits` can write a leading `-` into `cal_expr` for a negative result (e.g. `3-5` → `-2`); pressing an operator afterward chains directly off that shown text (e.g. producing `-2+5`) with no re-validation. `parse_int` treats `'-' - '0'` as an ordinary "digit" instead of a sign, so parsing `-2` does not yield `-2` — a genuine, easily reproduced wrong-answer bug for the common "compute a negative result, then keep going" flow, not a crash.

### 16. Calculator arithmetic has no overflow guard
**`kernel/src/calculator.c:106-111`**
`left + right` / `left * right` etc. run with no bounds check comparable to the existing "Error: div by 0" path. `CAL_EXPR_MAX` (24 chars) comfortably permits operand pairs whose product overflows `int64_t` (UB in C, wraps in practice on this target) with no detection or error message — e.g. `999999999999 * 99999999` silently displays a wrong, wrapped number.

### 17. `GOS.CFG` has no checksum — a torn/partial write could apply garbage window geometry with no detection
**`kernel/src/settings.c:33-63`**
The only integrity checks on load are magic/version/size — no CRC over the payload. If a write is interrupted mid-record (plausible given the v1 audit's still-open Finding #6, ATA write errors not fully detected) such that magic+version (the first 8 bytes, likely landing in one contiguous write) survive intact while `fm_x`/`fm_y`/`fm_w`/`fm_h` are garbage, those values are applied uncritically into `fm_create_window()` with no clamping — an unvalidated, arbitrary window geometry. Plausible, not confirmed with a concrete reproduction.

### 18. Short-alias exhaustion (`~1`..`~9` all colliding) collapses into the same generic failure code as every other create/rename error
**`kernel/src/fat32.c:906-923, 958-960`**
Correctly handled from a rollback-completeness standpoint (fails before any slot is touched, so only the pre-allocated data cluster needs freeing, and it is). But `fat_create_file`/`fat_create_dir`/`fat_rename` all collapse this specific "9 same-stripped-basename siblings already exist" case into the same plain `return 0` as disk-full, missing-parent, or name-too-long — a user hitting this sees an unhelpfully generic "create failed" with no way to distinguish the cause.

### 19. `find_free_slot_n` cannot find (or use) a contiguous free run that spans a cluster boundary
**`kernel/src/fat32.c:761-779`**
`run_len` is reset to 0 at the top of every cluster's scan and never carried across the `cluster = fat_get_next_cluster(cluster)` step — an intentional simplification, explicitly documented at `loc_advance`'s own comment ("assuming those records stay within a single cluster... true for every caller here"). A multi-entry LFN write needing slots that could only be satisfied by combining a run split across exactly such a boundary is reported "not found," causing the directory to grow an entirely new cluster even though enough total free space already existed elsewhere in the chain — a real, if minor, functional/efficiency gap rather than a memory-safety bug.

### 20. `erase_dirent_and_lfn` partial failure silently degrades a long name back to its short alias instead of fully failing or fully succeeding
**`kernel/src/fat32.c:804-820`**
Erases each LFN span entry then the SFN, returning 0 on the first read/write failure. If an early LFN entry is erased successfully but a later one then fails, the directory ends up with a broken LFN chain; the next `fat_list_dir`/`find_dirent` pass fails the checksum-continuity match and gracefully falls back to `format_83_name` — the file survives, silently renamed-in-appearance to its short alias (e.g. `AMUCHL~1.TXT`) rather than vanishing or crashing. No data loss, but an unannounced, unexpected name change from a single failed sector write mid-erase.

---

## Low / minor

### 21. `cd`'s path-joining silently truncates instead of rejecting overly-long candidate paths
**`kernel/src/terminal.c:112-125`**
No overflow (bounds are correctly enforced) — but no truncation detection either. If `term_cwd` plus a new component would exceed 199 characters (reachable via many nested `cd`s over a session), the joined path is silently cut off mid-component; if the truncated string coincidentally still resolves to *some* existing directory, `run_cd` silently lands the user in an unrelated, unintended directory with no warning. Narrow (requires deep nesting to trigger), real silent-wrong-behavior class of bug rather than a crash.

### 22. Terminal `run <name>` with an embedded space produces a generic rather than clear usage error
**`kernel/src/terminal.c:49-61, 173-190`**
`split_command` only splits on the *first* space; `run My File.ELF` passes the literal `"My File.ELF"` (with its internal space) straight to `process_spawn`, which fails FAT lookup (8.3 names can't contain that construct) and falls into the same generic `"run: could not spawn "` message as any other not-found name, rather than a clearer "invalid name" diagnostic. Minor UX gap only — confirmed this cannot be used to inject or corrupt anything, just a confusing failure message.

### 23. `window_close()`'s new `on_close` callback has no reentrancy guard
**`kernel/src/window.c:235-241`**
`on_close` is invoked while `in_use` is still `1`, with no guard against it (or anything it triggers) re-entering `window_close()` on the same index. Currently safe only by callback discipline: all three registered `on_close` implementations (`terminal.c`, `calculator.c`, `imageviewer.c`) merely reset a static index or `kfree()` unrelated memory, none call `window_close()` again. A future callback that did (e.g. if `imageviewer_on_close` were ever changed to close a companion window) would recurse and could double-free. Same class of "safe only by convention, not by structure" latent risk the original audit flagged elsewhere — not a regression of the original Finding #11 (that one, in the button-dispatch loop, is still correctly guarded — see §Regression Check).

### 24. Right-clicking a window while the desktop context menu is already open doesn't dismiss it
**`kernel/src/desktop.c:153-174`**
The right-click handler's early `return` (when the click hits a window) never touches `menu_visible` — an already-open menu stays open, unchanged, still fully functional and dismissible by the next left click. `menu_visible` can't diverge from what's actually drawn (draw and hit-test share the same flag/coordinates), so this is a missed UX affordance, not a state-corruption bug.

### 25. `GOS.CFG`-loaded File Manager geometry is never range/sanity-checked before use
**`kernel/src/settings.c:33-63`**, consumed at **`kernel/src/desktop.c:206`**
Unlike the window-drag/resize clamps added elsewhere in this same set of phases, no comparable clamp exists for settings-loaded `fm_x`/`fm_y`/`fm_w`/`fm_h` before they're passed to `fm_create_window()`. A corrupted or hand-edited `GOS.CFG` (no different in kind from the "legitimate on-disk state" reasoning behind the original audit's Finding #1) with e.g. `fm_w = 0` or an off-screen `fm_x` would launch File Manager unusably placed/sized, with no fallback to the compiled-in defaults for individually-invalid fields (only for a wrong magic/version/size).

### 26. RTC year hardcodes `+2000` with no century-register read
**`kernel/src/rtc.c:81`**
`rtc_read()` never reads CMOS register `0x32` (century, when present) and unconditionally adds `2000` to the raw 2-digit BCD year. Harmless today and not currently surfaced anywhere visible (the taskbar clock only displays H:M:S), but would silently misreport `.year` past 2099 or on any BIOS/VM that seeds CMOS differently. Documentation-only risk given nothing currently consumes the field.

### 27. Image Viewer has no explicit memory/window budget cap for repeated large opens
**`kernel/src/imageviewer.c:49-123`**
Every open `kmalloc`s a full `w*h*4`-byte pixel buffer with no cap beyond the generic `MAX_WINDOWS` limit inherited from `window_create()`. Every failure path I(the audit) traced — file-buffer `kmalloc` failure, decode failure, `window_create` returning -1, state-struct `kmalloc` failure — is handled gracefully with a `taskbar_flash_message()` and clean rollback of whatever was already allocated, so this is not a leak or crash risk, just the absence of a proactive "don't even try" policy before hitting one of those already-graceful failure paths.

---

## Regression Check (5 of the original 24 v1.0 findings, re-verified against current code)

| # | Original finding | Status |
|---|---|---|
| 1 | `fat_write_file` unsigned underflow on empty cluster chain | **Still fixed.** Current location `fat32.c:1186-1200` — the explicit `existing_count == 0` branch (fresh-cluster allocation instead of `clusters[-1]`) is present and reached first; Phase 23's LFN rewrite touched name/entry lookup and rename/delete, not this write-side growth logic, and did not reintroduce the underflow. |
| 3 | No double-free detection in `kfree` | **Still fixed.** Current location `heap.c:157-166` — the `is_free` check runs before any re-marking/coalescing, exactly as required. Now also protects Phase 24's new `kfree()` call sites (`imageviewer.c:43-45`) that didn't exist at the time of the original audit. |
| 11 | Stale window processed after a button callback closes it mid-dispatch | **Still fixed** (button-dispatch loop, `window.c:466-478`, explicit `if (!win->in_use) break;` plus a re-check before the `custom_click` fallback) — this is a real strengthening since v1.0 and remains correct. **However**, Phase 24 added a structurally analogous **new** gap in the unrelated `on_close` callback path (see Finding #23 above) — not a regression of #11 itself (the code #11 originally pointed at is unambiguously still fixed), but a related risk class the audit should track going forward. |
| 13 | 0xE0 extended-scancode prefix not handled | **Still fixed.** Current location `keyboard.c:101-179` — `pending_extended` tracking, a distinct `right_ctrl_held` variable, and explicit Numpad-Enter recognition are all present and correct; Ctrl+S honors either Ctrl key. |
| 17 | `window_create()`'s `-1` return never checked at any call site | **Still fixed, and correctly extended to every new call site.** Checked `terminal.c:280-286`, `calculator.c:172-178`, `imageviewer.c:87-94` (plus the original `fm.c`/`editor.c` sites) — all five guard the failure return and surface `taskbar_flash_message()`, a strict improvement over the original "fails completely silently" finding. |

---

## Explicitly checked and ruled out (not flaws)

- **LFN parse-state persistence across cluster boundaries** (`fat_list_dir`/`find_dirent`) — `struct lfn_parse_state` is declared outside the cluster-walking loop in both functions and correctly tracks checksum/sequence continuity across a boundary.
- **`lfn_parse_step`'s state machine** — correctly resets to inactive on bad sequence range, sequence discontinuity, checksum mismatch, or entry-count overflow; no OOB write into its fixed-size buffers found.
- **`find_free_slot_n`'s directory-growth rollback (`grown_this_iter`)** — correctly unlinks/frees a just-allocated growth cluster on every failure branch checked; no leaked, permanently-linked-but-unusable cluster found.
- **Wallpaper `load_option()`/`wallpaper_select()` leak on partial failure** — traced every path; `buf` is always freed exactly once, and `bmp_decode()` never writes to `*out_pixels` unless fully successful, so there's nothing to leak on a decode failure. Contradicts an initial hypothesis that this needed checking.
- **PMM accounting after large wallpaper reads** — `wallpaper_select()` is only called from menu clicks and once at boot, not per-frame; `wallpaper_render()` (the per-frame call) touches no FAT32/heap/PMM calls at all.
- **Image Viewer's own leak-on-close** — already correctly fixed via `imageviewer_on_close()`, which frees both the pixel buffer and its state struct; `window_set_close_callback` wiring confirmed present.
- **`scheduler_run_until_done()` reentrancy** — confirmed only ever called synchronously from the main desktop loop's own call stack (via the Terminal), never from an IRQ handler directly; no path exists for a second call to be issued while a prior one hasn't unwound (the real, confirmed problem is the opposite — Finding #7: the main loop is fully blocked, not re-entered).
- **Terminal's fixed buffers** (`line[200]`, `cmd[32]`, `arg[160]`, the prompt-reconstruction `prompt[210]`, the `cd` `candidate[200]`) — all correctly bounds-checked for any input length, including at `term_cwd`'s maximum possible length; extra characters are silently dropped, never overflowed.
- **`cal_append_char`'s bounds check** — correctly prevents all overflow of `cal_expr[24]`; a full expression with no operator correctly falls through `cal_press_equals`'s guard with no misfire. The specific INT64_MIN/-1 divide-trap scenario is not reachable through the actual button UI (no way to enter a literal negative second operand).
- **`imageviewer_render()`'s pixel loop** — correctly bounded by `min(img_w/h, win->w/h)`; no OOB read possible except via Finding #5's upstream decode-time overflow.
- **Taskbar clock/flash-message buffers** — correctly bounds-checked; the once-per-second RTC re-read gating has no rollover hazard.
- **`fm_has_bmp_suffix()`** (Phase 24) — the `len < 4` guard prevents underflow on short/empty names; all 4 suffix characters are individually uppercased, so any case combination (including a name ending in `..bmp`) matches correctly.
- **Row-index double-click identity** (original Finding #24) — re-verified after the Phase 23 LFN rewrite: every mutation path (including `fat_rename`'s now-more-complex write-then-erase sequence) still calls `fm_refresh()` before the user's next possible click, so LFN entries potentially reordering directory-scan position does not make this latent risk more reachable than originally assessed.
- **`MAX_WIDGETS_PER_WINDOW`'s raise (8→20)** — every reference (init loop, add-button search, dispatch loop, draw loop) uses the macro, not a stale literal `8`; no hardcoded-8 assumption found anywhere in the actual widget consumers (`fm.c`, `editor.c`, `calculator.c`, `terminal.c`).
- **Desktop menu row-select math and edge-clamping** — checked at exact boundary values; no off-by-one, no out-of-range `select_wallpaper()` index possible.
- **Three-icon hit-testing** (Files/Terminal/Calc) — independently guarded, non-overlapping rects; no double-fire between adjacent icons.

---

## Summary

28 findings total (5 Critical, 7 High, 9 Medium, 7 Low), plus a clean 5/5 regression check on previously-fixed v1.0 findings (one related-but-distinct new gap noted, not a reversion). The most load-bearing pattern across this pass: **Phase 19/20's ring-3/process infrastructure has no resource-cleanup story at all** (Findings #2, #6) and **no way to bound or recover from a hung process** (Finding #7) — three Critical/High findings from one root design gap, all directly reachable via the very `run` command Phase 24 just added a user-facing entry point for. The second pattern: **FAT32's write-path error handling assumes single-entry atomicity that multi-entry LFN writes/renames/deletes don't actually have** (Findings #3, #4, #9, #20) — each individually narrow (requires a disk write to fail at a specific point), but the underlying gap (no journaling, no two-phase commit, no fsck) is structural and applies to every multi-sector directory mutation Phase 23 introduced.
