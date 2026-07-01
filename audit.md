# gOS — Code Audit (2026-07-01)

Read-only review of the kernel at the current `v1.0` state (Phases 0–11 complete). No code was changed as part of this audit — findings only. Ranked by severity within each tier.

---

## Critical — real crash/corruption risk

### 1. `fat_write_file` unsigned underflow on an empty cluster chain
**`kernel/src/fat32.c:709-710`**
```c
if (existing_count < clusters_needed) {
    uint32_t last = clusters[existing_count - 1];
```
If a file's `first_cluster` is 0 (a zero-length file with no allocated cluster — a legitimate, common on-disk state, not just disk corruption; e.g. a zero-byte file created by another OS/tool), `existing_count` is 0 and `existing_count - 1` wraps to `0xFFFFFFFF`, reading `clusters[0xFFFFFFFF]` (wild stack read) and feeding that garbage into `fat_set_next_cluster()`, writing FAT-table data at an arbitrary sector. Trivially reachable by editing/saving any zero-byte file not created via this OS's own `fat_create_file`.

### 2. A "PANIC" log that doesn't actually halt, feeding into further FAT32 calls on an invalid filesystem
**`kernel/src/start.c:406`** vs. **`:502`**
`fat32_init()` failing just logs `"FAT32: PANIC - not a valid FAT32 filesystem"` and falls through — it never calls `hcf()`. Execution continues all the way to `stress_test()` (unconditional, not inside the `if (fat32_init())` block), which then calls `fat_create_file`/`fat_write_file`/`fat_rename`/`fat_delete_file` against BPB globals that were never populated.

### 3. No double-free detection in `kfree`
**`kernel/src/heap.c:124-154`**
`kfree` checks header/footer magic but never checks `is_free` before re-marking a block free and coalescing. Calling `kfree()` twice on the same pointer passes both magic checks and can merge with a block that's since been reallocated and is live, silently corrupting the allocator for whoever now owns that memory.

### 4. No cycle detection when walking FAT cluster chains
**`kernel/src/fat32.c`** — `fat_list_dir:178`, `fat_read_file:297`, `find_dirent:431`, `fat_free_chain:381`, and especially `find_free_slot:479-514` (an unbounded `for(;;)` with no cap at all)
A cyclic/corrupted FAT chain hangs the kernel permanently — no watchdog, no visited-cluster tracking anywhere.

### 5. No IST configured for double-fault/NMI
**`kernel/src/idt.c`** (`idt_set_gate` always called with `ist=0`), **`kernel/src/gdt.c:41-47`** (TSS `ist1..7` fields declared but never initialized/wired up)
A stack overflow that trips a double fault reuses the same already-overflowed stack instead of a dedicated safe one, risking a triple fault/reset instead of the panic screen actually running.

---

## High — real, but narrower trigger conditions

### 6. ATA write path never checks ERR/DF after cache-flush
**`kernel/src/ata.c`** (`ata_write_sector`)
Only `BSY` clearing is checked after `ATA_CMD_CACHE_FLUSH`; a drive-reported write/flush failure is silently treated as success. `fat_write_file`/directory-entry writes trust this, so a real write error becomes invisible data loss.

### 7. Window dragged to negative coordinates fails to render instead of clipping
**`kernel/src/window.c:281-282`** (no clamping on drag) → **`kernel/src/fb.c`** `fb_draw_rect` (unsigned `uint64_t` params)
Dragging a window off the top/left edge produces a negative `int64_t` window x/y with no clamp; passed into `fb_draw_rect`'s `uint64_t` parameters it wraps to a huge value, making the draw loop's start already past its end — the window silently fails to draw at all rather than clipping at the screen edge. Not a memory-safety issue (every path still funnels through `fb_put_pixel`'s own bounds check, so no OOB write occurs) — a real visual bug only.

### 8. `heap_grow`'s "extend last block" path doesn't check the block is actually free
**`kernel/src/heap.c:112-121`**
If the highest-address block happens to be in-use (not free) when the heap needs to grow, the newly-mapped pages get silently absorbed into that live allocation's `size` instead of becoming a new free block — the memory becomes stuck/unusable until that specific block is freed, and `kmalloc` can spuriously report OOM despite genuinely free address space existing.

### 9. `create_entry` rollback is incomplete on partial failure
**`kernel/src/fat32.c:591-600`**
If the file's data-cluster allocation succeeds but growing the parent directory fails partway (inside `find_free_slot`), only the data cluster is freed — a directory-growth cluster that was already allocated/FAT-linked leaks permanently (marked used, unreachable, no fsck to reclaim it).

### 10. No `vmm_unmap_page()` / no TLB invalidation anywhere
**`kernel/src/vmm.c`** (whole file)
Any future remap of an already-mapped virtual address to a different physical page leaves stale TLB entries with no `invlpg`, risking a silent read/write to the wrong physical page. Not currently triggered by existing call sites, but there's no mechanism to safely do this if ever needed.

### 11. Stale window continues being processed after a button callback closes it, within the same click-dispatch pass
**`kernel/src/window.c:260-268`** (button-dispatch loop) + `window_close` (**`window.c:175-196`**, only clears `in_use`, leaves `buttons[]`/`custom_click`/`custom_render`/`textbox_buffer` untouched)
`fm.c`'s modal dialog Confirm/Cancel buttons close their own window from inside their own `on_click()` callback, called from a loop that still holds a `struct window *win` pointer into that now-"closed" (but not cleared) slot and keeps iterating its other button rects afterward. Currently harmless only because Confirm/Cancel rects don't overlap — a future dialog with adjacent/overlapping buttons could double-fire a stale callback post-close.

---

## Medium

### 12. Demo windows permanently consume window slots
**`kernel/src/start.c:503-505`**, `MAX_WINDOWS=8`
"Window A"/"Window B"/the Phase 6/7 demo "Text Editor" auto-open at boot and are never auto-closed, permanently eating 3 of 8 total window slots before the user opens anything real. (Manually closable via Phase 11's close button, but on by default.)

### 13. 0xE0 extended-scancode prefix isn't handled
**`kernel/src/keyboard.c`** (IRQ handler)
The prefix byte is silently dropped (≥128, out of the ASCII table range), but the *following* byte is processed as an ordinary scancode. Concretely: Right Ctrl (`0xE0 0x1D`/`0x9D`) is indistinguishable from Left Ctrl, and Numpad Enter (`0xE0 0x1C`) is silently accepted as a normal Enter — real misattribution, not just "arrow keys unsupported."

### 14. Spurious IRQ7/IRQ15 not detected
**`kernel/src/pic.c`** (`pic_send_eoi`)
No ISR-register check before sending EOI; a spurious slave IRQ15 gets its EOI forwarded to both PICs rather than master-only per the standard protocol, risking the PIC losing track of pending priority.

### 15. No ATA drive-presence probe
**`kernel/src/ata.c`** (`ata_init`)
If no drive were attached, every I/O call burns the full ~100,000-iteration busy-wait before failing (no `IDENTIFY`/fast-fail check) — repeated, compounding stalls rather than a clean "no disk" failure.

### 16. `stress_test()` leaks a stray file on partial failure
**`kernel/src/start.c:87-93`**
The create→write→rename→delete loop `break`s on any failed step with no cleanup, leaving `STRESS.TXT`/`STRESSR.TXT` on disk permanently if it ever fails partway (currently doesn't fail in practice, but there's no corrective path if it did).

### 17. `window_create()`'s `-1` return is never checked at any call site
`fm.c`'s dialog open, `editor.c`'s editor open, `start.c`'s Window A/B/C creation
Every callee it feeds into (`window_add_button`, `window_enable_textbox`, etc.) guards internally, so this never crashes, but it fails completely silently: hit `MAX_WINDOWS=8` and clicking "New Folder" (or opening a second editor) produces no dialog, no window, and no error — the app just appears to do nothing.

### 18. Reopening an already-open file via a third rapid click silently discards unsaved edits
`fm.c`'s double-click handler + `editor_open()`
The double-click handler re-arms itself on every double-click (not just single clicks), so a third rapid click within the timeout re-triggers `editor_open()`, which unconditionally reloads from disk into the text box — overwriting any typed-but-unsaved changes in the currently-open editor with no warning.

---

## Low / minor

### 19. Inconsistent "PANIC" logging that doesn't always halt
Several other paths print literal `"PANIC"` to serial but don't actually `hcf()` (e.g. ATA sector-0 read failure, a few FAT32 read failures in the boot-time demo code) — makes grepping logs for "PANIC" an unreliable signal of whether the kernel actually halted.

### 20. `pmm_init` bitmap-placement ordering hazard (unconfirmed)
The bitmap-placement search doesn't explicitly guard against overlapping the physical backing of Limine's own `memmap->entries[]` array, which is read again later in the same function — a plausible but not confirmed-triggered ordering hazard.

### 21. Disk image Makefile target isn't versioned/idempotent
If the seed recipe ever changes, existing checkouts silently keep a stale image with no warning.

### 22. `fb_backbuffer_init()` has no re-entrancy guard
Fine today since called exactly once; would leak a multi-MB allocation if ever called twice.

### 23. `fm_open_dialog`'s "one dialog at a time" guard silently drops a second request
Clicking a second toolbar button (e.g. Rename) while a dialog is already open does nothing, with no feedback that the click was ignored.

### 24. Double-click identity in `fm.c` is row-index-based, not filename-based
Currently safe only because every mutation path happens to call `fm_refresh()` (which resets the click-tracking state) before the list could change under a user's fingers; a latent fragility if that invariant is ever broken by future code, not an active bug today.

---

## Explicitly checked and ruled out (not flaws)

- **Panic-screen interrupt safety** — all 48 IDT entries use interrupt-gate type `0x8E`, which hardware-clears `IF` on entry before any C code runs; neither `isr.asm` nor `panic.c` ever re-enables interrupts or returns. No IRQ can fire mid-panic. Safe, though implicitly so (no explicit defensive `cli` in `panic_screen` itself) — worth a comment if a trap gate or stray `sti` is ever introduced later.
- **Framebuffer bounds checks** — every draw path (`fb_draw_rect`, `fb_draw_rect_outline`, `fb_draw_line`, `font.c`'s glyph blitting, the mouse cursor) funnels through `fb_put_pixel`'s own unsigned bounds check. No out-of-bounds writes found anywhere.
- **Keyboard/mouse ring buffers** — correctly bounded (drop-on-full, no wraparound corruption); single-producer(IRQ)/single-consumer(main loop) model is safe without locking.
- **PS/2 mouse packet framing** — the 0xFA-rejection/desync-recovery logic (Phase 6) is deliberate and sound.
- **LBA28 math and ATA register bit masking** — correct; the sector-count-0-means-256 hardware edge case doesn't arise since the driver hardcodes 1 sector per call.
- **Boot ordering** (`sti` before PMM/VMM/heap init) — currently safe because only the timer IRQ is unmasked at that point and its handler touches no heap state; flagged only as fragile for future changes, not an active bug (see Low #19's neighbor concerns, not separately numbered).
- **GDT/TSS iomap_base placement** — one-byte convention question, but per the Intel SDM `iomap_base >= limit` alone is sufficient to signal "no I/O bitmap," so this is harmless in practice.

---

## Methodology

Performed via four parallel focused code reviews (memory/interrupts; drivers/FAT32; windowing/UI/rendering; boot sequence/build config) plus direct manual verification of the highest-severity claims against the actual source before inclusion here. Every finding above was independently confirmed by reading the cited file/line, not merely reported by a single automated pass.
