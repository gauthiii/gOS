# gOS — Gauthiii's Operating System — Project Plan

**Last updated:** 2026-07-01 (Phase 0 completed — see [phase0.md](phase0.md); Phase 1 completed — see [phase1.md](phase1.md); Phase 2 completed — see [phase2.md](phase2.md); Phase 3 completed — see [phase3.md](phase3.md); Phase 4 completed — see [phase4.md](phase4.md); Phase 5 completed — see [phase5.md](phase5.md); Phase 6 completed — see [phase6.md](phase6.md))

## 1. Project Overview

- **Name:** gOS (Gauthiii's Operating System)
- **Scope:** Path A — built completely from scratch. No existing kernel (no Linux/BSD/seL4 base), no existing C standard library (freestanding, will write our own minimal libc-equivalent as needed), no existing GUI toolkit. Everything from the bootloader handoff to the file manager's "New Folder" button is written by hand.
- **End goal:** A bootable x86_64 UEFI image that boots into a graphical desktop environment with:
  - A framebuffer-based GUI with a window manager (movable/draggable windows, compositor)
  - A file manager that lists folders/files from a real FAT32 partition
  - Full CRUD (Create, Read, Update, Delete) on files and folders via the UI
  - A basic text editor with keyboard input for creating/editing text files
- **Target architecture:** x86_64, booted via **UEFI** (no legacy BIOS support planned — reduces scope by half; no CSM/legacy boot paths to maintain)
- **Language:** C (freestanding, `-ffreestanding -nostdlib`) with x86_64 assembly (NASM) for boot stubs, context switches, interrupt entry trampolines, and other spots C cannot express (e.g., `iretq`, port I/O wrappers if not using inline asm)
- **Bootloader:** [Limine](https://github.com/limine-bootloader/limine) (Limine Boot Protocol) — provides UEFI boot, hands off a framebuffer address/pitch/bpp directly, memory map, and kernel file loading without you writing a UEFI bootloader yourself
- **Filesystem:** FAT32 — chosen for CRUD milestone because it's well-documented and the disk image can be mounted/inspected from macOS/Linux during development for debugging
- **Availability assumption:** 5–10 hours/week (part-time hobby pace). All estimates below assume the midpoint, ~7.5 hrs/week.
- **Toolchain:**
  - `x86_64-elf-gcc` / `x86_64-elf-binutils` cross-compiler (built via `crosstool-ng` or Homebrew `x86_64-elf-gcc` on macOS)
  - `nasm` for assembly
  - `qemu-system-x86_64` for testing (with `OVMF` UEFI firmware image)
  - `xorriso` + `limine` CLI tools to build a bootable ISO
  - `gdb` (with `x86_64-elf-gdb` or `gdb-multiarch`) for kernel debugging against QEMU's `-s -S` gdbstub
  - `make` for the build system (a single top-level Makefile; no need for CMake/meson at this scale)
  - A host Linux environment is strongly recommended for FAT32 image mounting/loopback tools; macOS works for building/QEMU but `mtools` (`mcopy`, `mdir`) will substitute for native loopback mounts of FAT32 images
- **Non-goals (explicitly out of scope for v1):** networking, multi-core/SMP, sound, USB (beyond what QEMU emulates as PS/2), a real package manager, POSIX compatibility, multi-user/permissions, a JIT/scripting layer. These can be "v2" ideas but are **not** part of this plan's finish line.

---

## 2. Phases (dependency order)

| # | Phase | Depends on |
|---|-------|-----------|
| 0 | Toolchain & Project Setup | — |
| 1 | Bootloader & Boot Process | 0 |
| 2 | Kernel Foundations (GDT/IDT/Interrupts) | 1 |
| 3 | Memory Management (Physical + Virtual) | 2 |
| 4 | Basic Drivers (Serial, PIT/Timer, Keyboard) | 2, 3 |
| 5 | Framebuffer Graphics | 1, 3 |
| 6 | Windowing / Compositor | 3, 4, 5 |
| 7 | Font Rendering & Text Input | 4, 5, 6 |
| 8 | Filesystem (FAT32) | 3, 4 |
| 9 | File Manager UI | 6, 7, 8 |
| 10 | CRUD Operations | 7, 8, 9 |
| 11 | Polish / Stability | all above |

---

## 3–4. Milestones & Tasks per Phase

### Phase 0 — Toolchain & Project Setup
**Estimated time: 6–10 hours (~1 week)**

**Milestone 0.1: Cross-compiler and emulator are installed and verified** — ✅ Done (see [phase0.md](phase0.md))
- [x] Install/build `x86_64-elf-gcc` and `x86_64-elf-binutils` cross toolchain
- [x] Install `nasm`, `qemu-system-x86_64`, `xorriso`, `mtools`
- [x] Download OVMF UEFI firmware (`OVMF_CODE.fd` / `OVMF_VARS.fd`) for QEMU — sourced from `rust-osdev/ovmf-prebuilt` (Homebrew does not bundle OVMF on macOS; see phase0.md)
- [x] Verify cross-gcc builds a trivial freestanding `.o` file (`x86_64-elf-gcc -ffreestanding -c test.c`)
- [x] Verify QEMU boots into the OVMF UEFI shell with no OS present (sanity check firmware works)

**Milestone 0.2: Repo and build skeleton exist** — ✅ Done (see [phase0.md](phase0.md))
- [x] Set up git repo structure: `kernel/`, `boot/`, `build/`, `third_party/limine/`, `tools/`
- [x] Write a top-level `Makefile` with targets: `build`, `iso`, `run`, `clean`, `debug`
- [x] Vendor Limine (git submodule) and build the Limine deployment binaries
- [x] Write a linker script (`linker.ld`) targeting a higher-half kernel at `0xffffffff80000000`

---

### Phase 1 — Bootloader & Boot Process
**Estimated time: 8–12 hours (~1–1.5 weeks)**

**Milestone 1.1: Kernel is loaded and entered by Limine** — ✅ Done (see [phase1.md](phase1.md))
- [x] Write `limine.conf` describing the kernel entry and protocol version (note: current Limine v9.x config file is named `limine.conf`, not `limine.cfg` — the plan's original filename was outdated terminology)
- [x] Write kernel entry point (`_start`) that receives the Limine boot info structure
- [x] Package kernel + Limine into a bootable ISO with `xorriso`
- [x] Boot the ISO in QEMU with OVMF and confirm the kernel entry point is reached (verified via QEMU monitor `info registers`: RIP parked inside the kernel's `hlt` loop at a higher-half address, HLT=1, no triple fault)

**Milestone 1.2: Kernel proves it's alive via serial output** — ✅ Done (see [phase1.md](phase1.md))
- [x] Write a minimal serial (COM1/UART 16550) driver: `outb`/`inb` wrappers in C (inline asm)
- [x] Implement `serial_write_char` / `serial_write_string`
- [x] Print a boot banner ("gOS booting...") over serial, visible via QEMU's `-serial stdio`
- [x] Confirm Limine memory map and framebuffer info structs are non-null and print their key fields (address, size, pitch, bpp) over serial

**Dependency note:** Nothing in Phase 2+ can be tested without Milestone 1.2's serial output — it is your only debugging channel until the framebuffer (Phase 5) is working. Do not skip it.

---

### Phase 2 — Kernel Foundations (GDT/IDT/Interrupts)
**Estimated time: 12–18 hours (~2 weeks)**

**Milestone 2.1: Custom GDT is loaded** — ✅ Done (see [phase2.md](phase2.md))
- [x] Define a GDT with null, kernel code, kernel data, user code, user data, and TSS descriptors
- [x] Write `gdt_load` in ASM (`lgdt` + far return to reload `cs`)
- [x] Load the new GDT at kernel init and confirm via serial log that segment registers hold expected selectors

**Milestone 2.2: IDT and exception handlers are wired up** — ✅ Done (see [phase2.md](phase2.md))
- [x] Define an IDT with 256 entries
- [x] Write ASM interrupt-entry trampolines (stub per vector, pushes error code/vector, jumps to common handler) for all 32 CPU exception vectors (plus 16 IRQ stubs for Milestone 2.3)
- [x] Write a C `isr_handler(struct interrupt_frame*)` that logs vector number, error code, and `rip` to serial
- [x] Deliberately trigger a divide-by-zero (`int 0/0`) and a page fault (write to unmapped address) and confirm both are caught and logged instead of triple-faulting

**Milestone 2.3: Hardware interrupts (PIC/APIC) are enabled** — ✅ Done (see [phase2.md](phase2.md))
- [x] Remap the legacy 8259 PIC (used legacy PIC per the plan's own recommendation — no APIC/SMP needed yet)
- [x] Write IRQ entry stubs for IRQ0 (timer) and IRQ1 (keyboard), mapped to IDT vectors 32+ (implemented generically for all 16 IRQs 32-47)
- [x] Enable interrupts (`sti`) and confirm a timer tick handler fires periodically (log every Nth tick to serial without flooding output)

**Dependency note:** Phase 4 keyboard driver requires IRQ1 (2.3) to already deliver interrupts. Phase 3's page fault handler requires 2.2's exception plumbing.

---

### Phase 3 — Memory Management (Physical + Virtual)
**Estimated time: 16–24 hours (~2.5–3 weeks)**

**Milestone 3.1: Physical memory allocator (PMM) works** — ✅ Done (see [phase3.md](phase3.md))
- [x] Parse the Limine memory map to find usable RAM regions
- [x] Implement a bitmap-based physical page allocator (`pmm_alloc_page` / `pmm_free_page`, 4 KiB pages)
- [x] Reserve pages already used by kernel image, Limine reclaimable regions, and framebuffer
- [x] Write a smoke test: allocate 100 pages, free them, allocate again, confirm addresses are reused and no double-allocation occurs (assert via serial log)

**Milestone 3.2: Virtual memory / paging is under kernel control** — ✅ Done (see [phase3.md](phase3.md))
- [x] Write page table structures (PML4/PDPT/PD/PT) and a `vmm_map_page(virt, phys, flags)` function
- [x] Identity-map or higher-half-map the kernel using Limine's provided base addresses
- [x] Load a new `CR3` with the kernel-built page tables (not Limine's) and confirm the kernel keeps running (serial log after switch = success)
- [x] Hook the page fault handler (from 2.2) to log faulting address (`CR2`) and access type

**Milestone 3.3: Kernel heap allocator exists** — ✅ Done (see [phase3.md](phase3.md))
- [x] Implement a simple heap (freelist allocator with header/footer guards) backed by `vmm_map_page` calls
- [x] Implement `kmalloc` / `kfree`
- [x] Stress test: allocate/free a mix of small and large blocks in a loop, verify no corruption via a canary/guard pattern

**Dependency note:** Everything from Phase 4 onward calls `kmalloc` — this phase blocks the entire rest of the project. Budget extra time here; it's the phase most OSDev projects stall on.

---

### Phase 4 — Basic Drivers (Serial, Timer, Keyboard)
**Estimated time: 8–12 hours (~1.5 weeks)**

*(Serial already exists from 1.2 — this phase extends it and adds new drivers.)*

**Milestone 4.1: PIT timer driver with tick counting** — ✅ Done (see [phase4.md](phase4.md))
- [x] Program the PIT (Programmable Interval Timer, port 0x40/0x43) to a fixed frequency (100 Hz)
- [x] Maintain a global tick counter incremented in the IRQ0 handler
- [x] Implement a `sleep_ms(ms)` busy-wait/tick-wait function and verify timing accuracy against a stopwatch (host `time` command used as the stopwatch, since QEMU is headless)

**Milestone 4.2: PS/2 keyboard driver** — ✅ Done (see [phase4.md](phase4.md))
- [x] Read scancodes from port 0x60 in the IRQ1 handler
- [x] Implement a scancode-set-1-to-ASCII translation table (handles shift and caps lock)
- [x] Buffer keypresses in a small ring buffer; expose `kb_getchar()` (blocking) to kernel code
- [x] Test: simulated keypresses via QEMU monitor `sendkey` (headless equivalent of typing on the QEMU window) correctly echoed over serial

**Dependency note:** Phase 7 (text input) directly consumes `kb_getchar()`/the keyboard ring buffer from 4.2.

---

### Phase 5 — Framebuffer Graphics
**Estimated time: 10–14 hours (~2 weeks)**

**Milestone 5.1: Raw pixel plotting works** — ✅ Done (see [phase5.md](phase5.md))
- [x] Extract framebuffer address, width, height, pitch, bpp from the Limine framebuffer response struct
- [x] Implement `fb_put_pixel(x, y, color)` respecting pitch (do not assume pitch == width*4)
- [x] Clear the entire screen to a solid color and confirm visually (QEMU `screendump` used in place of an interactive window — see phase5.md)

**Milestone 5.2: Primitive 2D drawing routines** — ✅ Done (see [phase5.md](phase5.md))
- [x] Implement `fb_draw_rect(x, y, w, h, color)` (filled)
- [x] Implement `fb_draw_rect_outline(x, y, w, h, color, thickness)`
- [x] Implement `fb_draw_line(x0, y0, x1, y1, color)` (Bresenham's algorithm)
- [x] Draw a test pattern (nested rectangles + diagonal lines) at boot to visually confirm all primitives work

**Milestone 5.3: Double buffering / flip to avoid tearing** — ✅ Done (see [phase5.md](phase5.md))
- [x] Allocate a back buffer in kernel heap matching framebuffer dimensions
- [x] Redirect all drawing routines to the back buffer
- [x] Implement `fb_flip()` (memcpy back buffer → real framebuffer)
- [x] Verify no visible tearing/flicker when redrawing rapidly (e.g., an animated bouncing rectangle test)

---

### Phase 6 — Windowing / Compositor
**Estimated time: 20–30 hours (~3.5–4 weeks)**

**Milestone 6.1: Mouse input works (PS/2 mouse)** — ✅ Done (see [phase6.md](phase6.md))
- [x] Enable the PS/2 auxiliary (mouse) port and IRQ12
- [x] Parse 3-byte PS/2 mouse packets (dx, dy, button state)
- [x] Track and clamp a global cursor `(x, y)` position; draw a simple cursor sprite/rect at that position each frame

**Milestone 6.2: Single window can be drawn and moved** — ✅ Done (see [phase6.md](phase6.md))
- [x] Define a `struct window { x, y, w, h, title, back_buffer, ... }`
- [x] Implement rendering: title bar rect + body rect + border, blitted into the framebuffer back buffer
- [x] Implement drag: on mouse-down inside title bar, track offset; on mouse-move while dragging, update window x/y; on mouse-up, stop dragging
- [x] Test: spawn one window, drag it around the screen with the mouse, confirm it tracks smoothly (simulated via QEMU monitor `mouse_move`/`mouse_button` — no interactive window in this environment)

**Milestone 6.3: Multiple windows with z-ordering** — ✅ Done (see [phase6.md](phase6.md))
- [x] Maintain a window list/array with a z-order (or linked list ordered front-to-back)
- [x] Implement compositing: redraw all windows back-to-front into the framebuffer back buffer every frame
- [x] Implement click-to-focus: clicking a window raises it to the top of the z-order
- [x] Test: spawn 3 overlapping windows, click each, confirm correct front/back ordering and drag-independence

**Milestone 6.4: Basic widgets — buttons and clickable regions** — ✅ Done (see [phase6.md](phase6.md))
- [x] Define a simple button widget (rect + label + click callback) — label rendering deferred to Phase 7 (no font renderer exists yet); button is a colored rect + callback for now
- [x] Implement hit-testing: mouse click coordinates → window → widget → callback dispatch
- [x] Test: a window with a button that changes color or logs to serial when clicked — logs to serial (`Button clicked! (click count=N)`), verified with two separate real clicks

**Dependency note:** This is the largest and riskiest phase. See §9 for scope-creep guidance — a tempting rabbit hole is animations/transparency/shadows. Cut those for v1.

---

### Phase 7 — Font Rendering & Text Input
**Estimated time: 10–16 hours (~2 weeks)**

**Milestone 7.1: Bitmap font rendering**
- [ ] Embed a monospace bitmap font (e.g., an 8x16 PSF1 font, or hand-embed a public-domain font as a C byte array)
- [ ] Implement `fb_draw_char(x, y, ch, fg, bg)` by blitting the glyph bitmap
- [ ] Implement `fb_draw_string(x, y, str, fg, bg)`
- [ ] Test: render "Hello, gOS!" at a fixed screen position at boot

**Milestone 7.2: Text rendering inside windows**
- [ ] Add a text rendering helper that clips to a window's content rect
- [ ] Render a window title using the font renderer instead of a placeholder rect
- [ ] Test: window title bars show real text ("File Manager", "Text Editor")

**Milestone 7.3: Keyboard input routed to focused window**
- [ ] Route `kb_getchar()` events (from 4.2) to whichever window currently has focus
- [ ] Implement a simple text buffer widget: appends printable characters, handles Backspace, Enter
- [ ] Implement a blinking text cursor drawn at the current insertion point
- [ ] Test: a window with a text box that you can click into and type text, with visible cursor and backspace working

**Dependency note:** 7.3 is a hard prerequisite for the text editor in Phase 9/10 — do not start the text editor UI before this milestone is solid.

---

### Phase 8 — Filesystem (FAT32)
**Estimated time: 16–22 hours (~3 weeks)**

**Milestone 8.1: Disk/block device access**
- [ ] Decide disk access path: ATA PIO driver (simplest, works in QEMU) — **recommended** over AHCI for v1 scope
- [ ] Implement `ata_read_sector(lba, buffer)` and `ata_write_sector(lba, buffer)` via port I/O
- [ ] Test: read sector 0 (MBR/GPT) of the disk image and log the first bytes/signature over serial to confirm correctness

**Milestone 8.2: FAT32 read support**
- [ ] Parse the FAT32 BIOS Parameter Block (BPB) from the boot sector
- [ ] Implement cluster-to-LBA translation and FAT table traversal (cluster chain following)
- [ ] Implement root directory listing: parse 32-byte directory entries, handle 8.3 names first (defer VFAT long filenames as a stretch goal)
- [ ] Implement `fat_read_file(path, buffer)` — walk directory entries by path, follow cluster chain, read data
- [ ] Test: create a disk image on the host with `mtools`/`mkfs.fat`, populate it with a known test file, boot gOS, and confirm it reads the exact same bytes back (verify via serial hex dump or checksum)

**Milestone 8.3: FAT32 write support**
- [ ] Implement free-cluster scanning (find unused clusters in the FAT)
- [ ] Implement `fat_create_file(path)` — allocate a directory entry + first cluster
- [ ] Implement `fat_write_file(path, buffer, size)` — write data, extend cluster chain as needed, update file size in directory entry
- [ ] Implement `fat_delete_file(path)` — mark directory entry deleted, free its cluster chain in the FAT
- [ ] Implement `fat_create_dir(path)` / `fat_delete_dir(path)`
- [ ] Test: from gOS, create a file, write text to it, reboot into QEMU, read it back and confirm persistence. Also mount the image on the host afterward to confirm it's a valid, uncorrupted FAT32 filesystem.

**Dependency note:** 8.3 write support is the single highest-risk milestone for silent data/filesystem corruption. Test against a disk image *copy*, not your only golden image, until write support is proven solid.

---

### Phase 9 — File Manager UI
**Estimated time: 12–18 hours (~2.5 weeks)**

**Milestone 9.1: File manager window shell**
- [ ] Create a "File Manager" window using the windowing system (Phase 6) with a toolbar area and a list area
- [ ] On open, call `fat_list_dir("/")` and render each entry as a row (icon placeholder + filename + type)
- [ ] Distinguish folders vs files visually (different icon color/shape is enough for v1 — no real icon graphics required)

**Milestone 9.2: Navigation**
- [ ] Implement click-to-open on a folder row: update current path, re-list, re-render
- [ ] Implement an "Up/Back" button that navigates to the parent directory
- [ ] Implement a path breadcrumb or text label showing current directory
- [ ] Test: navigate 3+ levels deep into a nested folder structure and back out correctly

**Milestone 9.3: Selection and context actions**
- [ ] Implement single-item selection (highlight on click)
- [ ] Add toolbar buttons: "New Folder", "New File", "Delete", "Rename" (wired to Phase 10 logic)
- [ ] Test: buttons are clickable and dispatch to (initially stubbed) handler functions logged over serial

---

### Phase 10 — CRUD Operations
**Estimated time: 10–16 hours (~2 weeks)**

**Milestone 10.1: Create**
- [ ] Wire "New Folder" button to `fat_create_dir()` + prompt for a name via a small text input dialog (reuses Phase 7.3 text box)
- [ ] Wire "New File" button to `fat_create_file()` similarly
- [ ] Test: create a folder and a file from the UI, confirm they appear in the listing immediately and persist after reboot

**Milestone 10.2: Read (text editor)**
- [ ] Build a minimal "Text Editor" window: double-clicking a `.txt` file in the file manager opens it
- [ ] Load file contents via `fat_read_file()` into the text box widget from 7.3
- [ ] Test: open an existing text file created on the host via `mtools`, confirm its exact contents render correctly

**Milestone 10.3: Update (save)**
- [ ] Add a "Save" button/keybind (e.g., Ctrl+S) in the text editor that calls `fat_write_file()` with the edited buffer
- [ ] Handle the case where edited content is larger/smaller than original (cluster chain grows/shrinks correctly — exercises 8.3's extend logic)
- [ ] Test: edit a file, save, reboot, reopen — confirm changes persisted

**Milestone 10.4: Delete and Rename**
- [ ] Wire "Delete" button to `fat_delete_file()`/`fat_delete_dir()` with a confirmation dialog (reuse window/button primitives)
- [ ] Implement rename: update the directory entry's filename bytes in place (no cluster changes needed)
- [ ] Test: delete a file, confirm it disappears from listing and from a host-side mount check; rename a file, confirm name change persists

**Dependency note:** This entire phase is a thin UI layer over Phase 8's FAT32 read/write logic — if Phase 8 is solid, this phase should be the fastest of the "hard" phases.

---

### Phase 11 — Polish / Stability
**Estimated time: 10–20 hours (open-ended — see scope note in §9)**

**Milestone 11.1: Crash resilience**
- [ ] Audit all pointer-returning functions (`kmalloc`, `fat_read_file`, etc.) for null-check handling at call sites
- [ ] Add a kernel panic screen (framebuffer red screen + message) instead of silent triple fault on unhandled exceptions
- [ ] Stress test: rapidly create/delete/rename files and open/close windows for several minutes without a crash

**Milestone 11.2: UX polish**
- [ ] Add window close buttons
- [ ] Add a simple taskbar/dock showing open windows
- [ ] Add a desktop background and a way to launch the File Manager (icon or menu) instead of it always auto-opening at boot

**Milestone 11.3: Documentation and demo**
- [ ] Write a `README.md` with build/run instructions
- [ ] Record a short screen capture demoing boot → file manager → create/edit/save/delete flow
- [ ] Tag a `v1.0` release commit

---

## 5. Estimated Time Summary (at 7.5 hrs/week)

| Phase | Hours | Weeks |
|---|---|---|
| 0. Toolchain & Setup | 6–10 | ~1 |
| 1. Bootloader & Boot Process | 8–12 | ~1.5 |
| 2. Kernel Foundations | 12–18 | ~2 |
| 3. Memory Management | 16–24 | ~2.5–3 |
| 4. Basic Drivers | 8–12 | ~1.5 |
| 5. Framebuffer Graphics | 10–14 | ~2 |
| 6. Windowing/Compositor | 20–30 | ~3.5–4 |
| 7. Font Rendering & Text Input | 10–16 | ~2 |
| 8. Filesystem (FAT32) | 16–22 | ~3 |
| 9. File Manager UI | 12–18 | ~2.5 |
| 10. CRUD Operations | 10–16 | ~2 |
| 11. Polish/Stability | 10–20 | ~2–3 |
| **Total** | **~138–212 hrs** | **~26–34 weeks (~6–8 months)** |

This assumes steady 5–10 hr/week pace with no major multi-week stalls. Phases 3, 6, and 8 are the most likely to run over estimate — pad your personal expectations accordingly.

---

## 6. Status Tracker

| Phase | Milestone | Task | Status | Notes |
|---|---|---|---|---|
| 0 | 0.1 Toolchain verified | Install x86_64-elf-gcc/binutils | Done | Installed via `brew install x86_64-elf-gcc x86_64-elf-gdb` (pulls in x86_64-elf-binutils). GCC 16.1.0, GDB 17.2. |
| 0 | 0.1 Toolchain verified | Install nasm, qemu, xorriso, mtools | Done | Already present via Homebrew: nasm 3.01, qemu 10.2.0, xorriso 1.5.6, mtools 4.0.49. |
| 0 | 0.1 Toolchain verified | Download OVMF firmware | Done | Homebrew qemu does NOT bundle OVMF on macOS (deviation from original assumption). Sourced from `rust-osdev/ovmf-prebuilt` release `edk2-stable202605-r1` instead. Stored at `third_party/ovmf/`. See phase0.md. |
| 0 | 0.1 Toolchain verified | Verify freestanding compile | Done | Compiled a throwaway `.c` file with `-ffreestanding`; produced valid ELF64 x86-64 object, disassembled successfully. |
| 0 | 0.1 Toolchain verified | Verify QEMU boots OVMF shell | Done | Headless QEMU run with OVMF_CODE.fd/OVMF_VARS.fd reached UEFI BDS phase ("No bootable option or device was found") — confirms firmware is valid. |
| 0 | 0.2 Repo skeleton | Set up repo directory structure | Done | Created `kernel/{src,include}`, `boot/`, `build/`, `third_party/`, `tools/`. Also ran `git init` (repo didn't exist before; user approved). |
| 0 | 0.2 Repo skeleton | Write top-level Makefile | Done | `Makefile` at repo root with `build`, `iso`, `run`, `debug`, `clean` targets. `iso`/`run` targets reference `boot/limine.cfg` and kernel sources that don't exist until Phase 1 — expected, not yet runnable end-to-end. |
| 0 | 0.2 Repo skeleton | Vendor Limine | Done | Added as git submodule `third_party/limine`, branch `v9.x-binary`, resolved to tag `v9.6.7-binary`. Host deploy tool built successfully (`make` inside submodule produced `limine` binary). |
| 0 | 0.2 Repo skeleton | Write linker script | Done | `kernel/linker.ld` — higher-half kernel linked at `0xffffffff80000000`, separate PT_LOAD segments for `.text`/`.rodata`/`.data+.bss`. |
| 1 | 1.1 Kernel loaded by Limine | Write limine.conf | Done | Filename corrected from planned `limine.cfg` to `limine.conf` (current Limine v9.x format); placed at `boot/limine.conf`, deployed to `/boot/limine.conf` on the ISO. |
| 1 | 1.1 Kernel loaded by Limine | Write kernel _start entry | Done | `kernel/src/start.c`; base revision 3 request + start/end markers in a dedicated `.requests` linker segment per Limine protocol. |
| 1 | 1.1 Kernel loaded by Limine | Build bootable ISO | Done | `make iso` produces `build/gos.iso` (hybrid BIOS+UEFI via xorriso + `limine bios-install`). |
| 1 | 1.1 Kernel loaded by Limine | Boot in QEMU, confirm entry reached | Done | Verified via QEMU monitor: RIP=0xffffffff80001005 (higher-half), HLT=1, no triple fault/reset. |
| 1 | 1.2 Serial output alive | Write serial (UART) driver | Done | `kernel/src/serial.c` — COM1 16550 UART via `outb`/`inb` inline asm, 38400 baud, FIFO enabled. |
| 1 | 1.2 Serial output alive | Implement serial_write_char/string | Done | Plus `serial_write_hex64`/`serial_write_uint` helpers added for memmap/framebuffer dumping. |
| 1 | 1.2 Serial output alive | Print boot banner | Done | "=== gOS booting... ===" visible over `-serial stdio` in QEMU. |
| 1 | 1.2 Serial output alive | Confirm memory map + framebuffer info | Done | Verified live: 36 memmap entries, 207 MiB usable RAM, framebuffer 1280x800x32bpp at 0xffff800080000000. |
| 2 | 2.1 Custom GDT loaded | Define GDT descriptors | Done | `kernel/src/gdt.c` — null, kernel code/data, user code/data, plus a 16-byte long-mode TSS descriptor with a static 16KiB rsp0 stack. |
| 2 | 2.1 Custom GDT loaded | Write gdt_load in ASM | Done | `kernel/src/gdt_flush.asm` — `lgdt`, reloads data segments directly, reloads CS via `retfq` trick (can't `mov` into CS), then `ltr` for the TSS. |
| 2 | 2.1 Custom GDT loaded | Load GDT, verify segment registers | Done | Verified live via serial: CS=0x08, DS=SS=0x10, TR=0x28 — exact match to the defined layout. |
| 2 | 2.2 IDT + exceptions wired | Define 256-entry IDT | Done | `kernel/src/idt.c` — all 256 entries; vectors 0-31 exceptions, 32-47 remapped IRQs, rest currently unused (default/blank). |
| 2 | 2.2 IDT + exceptions wired | Write ASM ISR trampolines (32 vectors) | Done | `kernel/src/isr.asm` — macro-generated stubs for all 32 exception vectors (correct error-code-vs-no-error-code push per vector) plus all 16 IRQ stubs (32-47), sharing one common register-save/dispatch/restore stub. |
| 2 | 2.2 IDT + exceptions wired | Write C isr_handler with logging | Done | `isr_handler()` in `idt.c` — logs vector, exception name, error code, RIP/CS/RFLAGS/RSP, plus CR2 specifically for page faults (vector 14). |
| 2 | 2.2 IDT + exceptions wired | Trigger div-by-zero + page fault, confirm caught | Done | Verified live via temporary `#ifdef` test triggers (build-time only, not in normal boot path): divide-by-zero correctly reported as Vector 0; page fault correctly reported as Vector 14 with CR2=0xdeadbeef000 matching the exact bad address written to. Both halted cleanly, no triple fault. |
| 2 | 2.3 Hardware interrupts enabled | Remap 8259 PIC | Done | `kernel/src/pic.c` — remaps IRQ0-7 to vectors 32-39 and IRQ8-15 to 40-47, preserves original interrupt masks across remap, provides `pic_send_eoi`/`pic_set_mask`/`pic_clear_mask`. |
| 2 | 2.3 Hardware interrupts enabled | Write IRQ0/IRQ1 entry stubs | Done | Covered generically by all 16 IRQ stubs added in the 2.2 ISR work; `idt_register_irq_handler()` added as a dispatch mechanism for individual IRQ handlers (used by the timer in this milestone, keyboard driver comes in Phase 4). |
| 2 | 2.3 Hardware interrupts enabled | Enable interrupts, confirm timer ticks | Done | `kernel/src/timer.c` registers an IRQ0 handler, unmasks IRQ0, `sti` executed in `_start`. Verified live: ticks increment and log roughly once per second (18 ticks) at the PIT's default ~18.2Hz rate, sustained over 13+ seconds with no crash. |
| 3 | 3.1 Physical memory allocator | Parse Limine memory map | Done | Bitmap sized against USABLE entries only, after a bug caused it to include a huge high-address PCI/MMIO RESERVED region (see phase3.md deviations). |
| 3 | 3.1 Physical memory allocator | Implement bitmap PMM alloc/free | Done | `kernel/src/pmm.c` — bitmap allocator with a search-hint cursor for fast reuse-after-free. |
| 3 | 3.1 Physical memory allocator | Reserve kernel/framebuffer regions | Done | Non-USABLE memmap types (kernel, framebuffer, ACPI, bootloader-reclaimable) default to reserved; physical page 0 also explicitly reserved (bug fix — see phase3.md). |
| 3 | 3.1 Physical memory allocator | Smoke test alloc/free reuse | Done | `pmm_self_test()` — 100 pages allocated (no duplicates), freed, re-allocated; verified live: "PASS (100/100 re-allocations reused a freed page)". |
| 3 | 3.2 Virtual memory/paging | Write page table structures | Done | `kernel/src/vmm.c` — 4-level (PML4/PDPT/PD/PT) walker with `vmm_map_page` (4KiB) and an internal `vmm_map_2mb` helper for efficient bulk identity/HHDM mapping. |
| 3 | 3.2 Virtual memory/paging | Map kernel higher-half | Done | Identity + HHDM map first 4GiB via 2MiB pages; kernel's own higher-half range mapped 4KiB via new `__kernel_virt_start`/`__kernel_virt_end` linker symbols and Limine's kernel address response. |
| 3 | 3.2 Virtual memory/paging | Load new CR3, confirm still running | Done | Verified live: "VMM: new CR3 loaded, kernel still running", plus timer interrupts continued firing for 10+ ticks after the switch — strong confirmation the new tables are fully correct, not just non-crashing. |
| 3 | 3.2 Virtual memory/paging | Hook page fault handler to log CR2 | Done | Extended Phase 2's handler to decode error-code bits into a human-readable access type; verified live: "Access type: not-present, write, supervisor-mode" matching the exact deliberate test fault. |
| 3 | 3.3 Kernel heap | Implement heap backed by vmm_map_page | Done | `kernel/src/heap.c` — freelist allocator (first-fit, forward coalescing), grows via `vmm_map_page`/`pmm_alloc_page` on demand, 64MiB virtual ceiling for v1. |
| 3 | 3.3 Kernel heap | Implement kmalloc/kfree | Done | Header+footer magic guards on every block for corruption detection. |
| 3 | 3.3 Kernel heap | Stress test alloc/free mix | Done | `heap_self_test()` — 300 randomized alloc/free cycles (small 8-64B and large 512-4096B blocks) with content-integrity checks, plus a deliberate buffer-overrun test proving the guard actually detects corruption. Verified live: "PASS (300 cycles clean, guard correctly detected deliberate overrun)". |
| 4 | 4.1 PIT timer driver | Program PIT to fixed frequency | Done | `kernel/src/timer.c` — PIT channel 0 programmed to 100Hz via divisor `1193182/100`; replaces Phase 2's unprogrammed default ~18.2Hz rate. |
| 4 | 4.1 PIT timer driver | Maintain tick counter in IRQ0 | Done | Existing IRQ0 handler from Phase 2.3 extended to log once/second at the new 100Hz rate (every 100th tick instead of every 18th). |
| 4 | 4.1 PIT timer driver | Implement sleep_ms, verify timing | Done | `sleep_ms()` tick-waits in a `hlt` loop. Verified live: `timer_self_test()` requested 2000ms, measured exactly 200 ticks at 100Hz; independently corroborated via host `time` wrapper showing ~13 real seconds of "Timer tick" logs matching wall clock. |
| 4 | 4.2 PS/2 keyboard driver | Read scancodes in IRQ1 handler | Done | `kernel/src/keyboard.c` — reads port 0x60 in the IRQ1 handler (stub already existed generically from Phase 2.3's 16-IRQ setup). |
| 4 | 4.2 PS/2 keyboard driver | Scancode-to-ASCII translation table | Done | US QWERTY scancode-set-1 tables (unshifted + shifted), shift key state tracking (make/break), caps lock toggle (inverts shift for letters only, per real keyboard behavior). |
| 4 | 4.2 PS/2 keyboard driver | Ring buffer + kb_getchar() | Done | 256-byte ring buffer, `kb_getchar()` blocks via `hlt` loop until a character is available; `kb_has_char()` added for future non-blocking use (Phase 7). |
| 4 | 4.2 PS/2 keyboard driver | Test typing, echo over serial | Done | Headless equivalent of "type on the QEMU window": used QEMU monitor `sendkey` to simulate keystrokes (g, shift-o, 1, ret, shift-a — then h, shift-i, 9, ret, z on a second run), verified live via serial log that every character was translated correctly (lowercase, shifted-uppercase, digit, enter, all exact matches). |
| 5 | 5.1 Raw pixel plotting | Extract framebuffer info from Limine | Done | Already retrieved in Phase 1 (`fb` struct); Phase 5 additionally passes channel shifts into `fb_init()` for correct color packing. |
| 5 | 5.1 Raw pixel plotting | Implement fb_put_pixel | Done | `kernel/src/fb.c` — respects real pitch (`y*pitch + x*bpp/8`), not an assumed `width*4`. |
| 5 | 5.1 Raw pixel plotting | Clear screen to solid color | Done | Verified via QEMU `screendump` (headless equivalent of visual confirmation — no interactive window in this environment): sampled pixels read back exactly `RGB(0,64,128)`, matching the requested color exactly, confirming correct channel packing. |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_rect (filled) | Done | Clipped to framebuffer bounds. |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_rect_outline | Done | Built from 4 calls to `fb_draw_rect` (top/bottom/left/right bands). |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_line (Bresenham) | Done | Handles all slope octants via the standard signed-error Bresenham formulation. |
| 5 | 5.2 Primitive 2D drawing | Draw test pattern | Done | Nested rects + 4 lines (horizontal, vertical, both diagonals) drawn at boot; screendump visually confirmed correct via direct image inspection — filled rect with outline, second outline, and an X-crossing of 4 correctly-colored, correctly-angled lines. |
| 5 | 5.3 Double buffering | Allocate back buffer | Done | `fb_backbuffer_init()` — 4000 KiB (`pitch * height`), allocated via Phase 3's `kmalloc`. |
| 5 | 5.3 Double buffering | Redirect drawing to back buffer | Done | All draw calls write through a `draw_target` pointer that switches from the real framebuffer to the back buffer once initialized — no call-site changes needed anywhere else in the kernel. |
| 5 | 5.3 Double buffering | Implement fb_flip | Done | Bulk 8-byte-word copy from back buffer to real framebuffer. |
| 5 | 5.3 Double buffering | Verify no tearing with animation test | Done | 40-frame bouncing-rectangle animation (`sleep_ms(50)` between frames); 3 screendumps taken at different points during the animation each show a single complete, uncorrupted frame with the rectangle at a different, correctly-interpolated position — no ghosting, no partial-frame artifacts. |
| 6 | 6.1 Mouse input | Enable PS/2 aux port + IRQ12 | Done | `kernel/src/mouse.c` — standard 0xA8/0x20+0x60/0xD4 handshake. Real bug found+fixed: the 0xF4 command's ACK byte (0xFA) leaked into the interrupt-driven stream and desynced packet framing since 0xFA's bit3 happens to pass the byte0 sanity check; fixed by explicitly rejecting 0xFA as a byte0 candidate. See phase6.md. |
| 6 | 6.1 Mouse input | Parse 3-byte mouse packets | Done | Sign-extended dx/dy, Y-axis inverted for screen coordinates, overflow bits checked and packet dropped if set. |
| 6 | 6.1 Mouse input | Track/draw cursor | Done | Verified live via QEMU monitor `mouse_move`/`mouse_button` (real PS/2 hardware path, same as `sendkey` for keyboard) + screendump: cursor visibly moved from center to the exact tracked `(840,500)` position after a `(200,100)` delta. |
| 6 | 6.2 Single window draggable | Define struct window | Done | `kernel/include/window.h` / `kernel/src/window.c` — designed the full window/button/z-order system upfront (used incrementally across 6.2-6.4), consistent with the same "build the shared mechanism once" approach used in Phase 2. |
| 6 | 6.2 Single window draggable | Render title bar + body + border | Done | Verified visually via screendump — colored title bar, body, black border, all correctly positioned. |
| 6 | 6.2 Single window draggable | Implement drag logic | Done | Edge-triggered mouse-down-in-titlebar detection, offset-preserving drag, release-to-stop. |
| 6 | 6.2 Single window draggable | Test dragging smoothly | Done | Verified live: window moved from (150,150) to (300,250), exactly matching the two (150,100) deltas sent via simulated mouse drag — confirmed visually via before/after screendumps. |
| 6 | 6.3 Multiple windows z-order | Maintain window list with z-order | Done | Fixed 8-slot window array + separate z_order index array (back-to-front). |
| 6 | 6.3 Multiple windows z-order | Implement compositing loop | Done | `window_composite()` draws all windows back-to-front then the cursor on top, called once per frame alongside `fb_flip()`. |
| 6 | 6.3 Multiple windows z-order | Implement click-to-focus | Done | `raise_to_front()` removes a window from its current z-order slot and appends it to the front. |
| 6 | 6.3 Multiple windows z-order | Test 3 overlapping windows | Done | 3 windows created (A back, B middle, C front, matching creation order). Verified visually: clicking backmost window A raised it above both B and C simultaneously — proves the general middle/back-of-array reordering path, not just a trivial 2-window swap. |
| 6 | 6.4 Basic widgets | Define button widget | Done | Rect + color + callback, positioned relative to its parent window's body (moves correctly with the window). |
| 6 | 6.4 Basic widgets | Implement hit-testing/dispatch | Done | Click coordinates → frontmost window under cursor → local body-relative coordinates → button rect test → callback invocation. |
| 6 | 6.4 Basic widgets | Test clickable button | Done | Verified live: two real simulated clicks on the button both dispatched correctly (`Button clicked! (click count=1)`, then `2`), with cursor visually confirmed positioned over the button in the triggering screendump. |
| 7 | 7.1 Bitmap font rendering | Embed bitmap font data | Not Started | |
| 7 | 7.1 Bitmap font rendering | Implement fb_draw_char | Not Started | |
| 7 | 7.1 Bitmap font rendering | Implement fb_draw_string | Not Started | |
| 7 | 7.1 Bitmap font rendering | Test render "Hello, gOS!" | Not Started | |
| 7 | 7.2 Text in windows | Add clipped text rendering helper | Not Started | |
| 7 | 7.2 Text in windows | Render real window titles | Not Started | |
| 7 | 7.3 Keyboard input to window | Route kb events to focused window | Not Started | |
| 7 | 7.3 Keyboard input to window | Implement text buffer widget | Not Started | |
| 7 | 7.3 Keyboard input to window | Implement blinking text cursor | Not Started | |
| 7 | 7.3 Keyboard input to window | Test typing into text box | Not Started | |
| 8 | 8.1 Disk/block access | Implement ATA PIO read_sector | Not Started | |
| 8 | 8.1 Disk/block access | Implement ATA PIO write_sector | Not Started | |
| 8 | 8.1 Disk/block access | Test read sector 0, log signature | Not Started | |
| 8 | 8.2 FAT32 read support | Parse FAT32 BPB | Not Started | |
| 8 | 8.2 FAT32 read support | Cluster-to-LBA + FAT chain traversal | Not Started | |
| 8 | 8.2 FAT32 read support | Root directory listing (8.3 names) | Not Started | |
| 8 | 8.2 FAT32 read support | Implement fat_read_file | Not Started | |
| 8 | 8.2 FAT32 read support | Test read known file, verify bytes | Not Started | |
| 8 | 8.3 FAT32 write support | Free-cluster scanning | Not Started | |
| 8 | 8.3 FAT32 write support | Implement fat_create_file | Not Started | |
| 8 | 8.3 FAT32 write support | Implement fat_write_file | Not Started | |
| 8 | 8.3 FAT32 write support | Implement fat_delete_file | Not Started | |
| 8 | 8.3 FAT32 write support | Implement fat_create_dir/fat_delete_dir | Not Started | |
| 8 | 8.3 FAT32 write support | Test create/write/reboot/read persistence | Not Started | |
| 9 | 9.1 File manager shell | Create File Manager window | Not Started | |
| 9 | 9.1 File manager shell | List root dir entries as rows | Not Started | |
| 9 | 9.1 File manager shell | Distinguish folders vs files visually | Not Started | |
| 9 | 9.2 Navigation | Click-to-open folder | Not Started | |
| 9 | 9.2 Navigation | Up/Back button | Not Started | |
| 9 | 9.2 Navigation | Path breadcrumb/label | Not Started | |
| 9 | 9.2 Navigation | Test deep nested navigation | Not Started | |
| 9 | 9.3 Selection and actions | Single-item selection highlight | Not Started | |
| 9 | 9.3 Selection and actions | Toolbar buttons (stubbed) | Not Started | |
| 9 | 9.3 Selection and actions | Test button dispatch logging | Not Started | |
| 10 | 10.1 Create | Wire New Folder button | Not Started | |
| 10 | 10.1 Create | Wire New File button | Not Started | |
| 10 | 10.1 Create | Test create persists after reboot | Not Started | |
| 10 | 10.2 Read (text editor) | Build Text Editor window | Not Started | |
| 10 | 10.2 Read (text editor) | Load file contents into text box | Not Started | |
| 10 | 10.2 Read (text editor) | Test open host-created file | Not Started | |
| 10 | 10.3 Update (save) | Add Save button/keybind | Not Started | |
| 10 | 10.3 Update (save) | Handle cluster chain grow/shrink | Not Started | |
| 10 | 10.3 Update (save) | Test edit/save/reboot/reopen | Not Started | |
| 10 | 10.4 Delete and Rename | Wire Delete button + confirmation | Not Started | |
| 10 | 10.4 Delete and Rename | Implement rename | Not Started | |
| 10 | 10.4 Delete and Rename | Test delete + rename persistence | Not Started | |
| 11 | 11.1 Crash resilience | Null-check audit | Not Started | |
| 11 | 11.1 Crash resilience | Kernel panic screen | Not Started | |
| 11 | 11.1 Crash resilience | Stress test create/delete/rename loop | Not Started | |
| 11 | 11.2 UX polish | Window close buttons | Not Started | |
| 11 | 11.2 UX polish | Taskbar/dock | Not Started | |
| 11 | 11.2 UX polish | Desktop background + launcher | Not Started | |
| 11 | 11.3 Docs and demo | Write README | Not Started | |
| 11 | 11.3 Docs and demo | Record demo capture | Not Started | |
| 11 | 11.3 Docs and demo | Tag v1.0 release | Not Started | |

---

## 7. Dependencies / Blockers Summary

- **Phase 1 blocks everything.** No milestone in Phase 2+ can be verified without serial output (1.2).
- **Phase 2.3 (hardware interrupts) blocks Phase 4** (both timer and keyboard drivers are IRQ-based).
- **Phase 3.3 (kernel heap / `kmalloc`) blocks Phase 4 onward.** Every driver, window, and FAT32 structure allocates memory. Do not attempt Phase 4+ with a stubbed/fake allocator — it will cause debugging pain later that's hard to distinguish from real bugs.
- **Phase 5 blocks Phase 6.** No windowing without a framebuffer to draw into.
- **Phase 4.2 (keyboard) blocks Phase 7.3 (text input).**
- **Phase 6 + 7 both block Phase 9** (file manager needs windows and text rendering).
- **Phase 8 blocks Phase 9 and 10 entirely.** The file manager and CRUD UI are empty shells without a working filesystem underneath.
- **Phase 8.3 (FAT32 write) specifically blocks Phase 10.1, 10.3, 10.4** (create, save, delete, rename all require write support; read-only Phase 10.2 only needs 8.2).
- **Phase 11 depends on all prior phases being functionally complete** — it is stabilization, not new capability.

---

## 8. Resources

### General / Getting Started
- [OSDev Wiki — Getting Started](https://wiki.osdev.org/Getting_Started)
- [OSDev Wiki — Beginner Mistakes](https://wiki.osdev.org/Beginner_Mistakes) (read this before Phase 1)
- [OSDev Wiki — Bare Bones](https://wiki.osdev.org/Bare_Bones)
- [xv6 (MIT teaching OS)](https://github.com/mit-pdos/xv6-public) — not x86_64/UEFI/Limine-based, but excellent reference for kernel structure, scheduler, and simple filesystem design decisions

### Phase 0 — Toolchain
- [OSDev Wiki — GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)
- [OSDev Wiki — Setting Up GCC for Cross Compiling](https://wiki.osdev.org/Cross-Compiler_Successful_Build)

### Phase 1 — Bootloader
- [Limine Boot Protocol spec](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md)
- [Limine Bare Bones tutorial (OSDev Wiki)](https://wiki.osdev.org/Limine_Bare_Bones)
- [UEFI Specification](https://uefi.org/specifications) (reference only — Limine abstracts most of this away)

### Phase 2 — GDT/IDT/Interrupts
- [OSDev Wiki — Global Descriptor Table](https://wiki.osdev.org/Global_Descriptor_Table)
- [OSDev Wiki — Interrupt Descriptor Table](https://wiki.osdev.org/Interrupt_Descriptor_Table)
- [OSDev Wiki — Exceptions](https://wiki.osdev.org/Exceptions)
- [OSDev Wiki — 8259 PIC](https://wiki.osdev.org/8259_PIC)

### Phase 3 — Memory Management
- [OSDev Wiki — Page Frame Allocation](https://wiki.osdev.org/Page_Frame_Allocation)
- [OSDev Wiki — Paging](https://wiki.osdev.org/Paging)
- [OSDev Wiki — Memory Map (x86)](https://wiki.osdev.org/Memory_Map_(x86))
- [OSDev Wiki — Writing a Memory Manager](https://wiki.osdev.org/Writing_a_memory_manager)

### Phase 4 — Drivers
- [OSDev Wiki — Serial Ports](https://wiki.osdev.org/Serial_Ports)
- [OSDev Wiki — Programmable Interval Timer](https://wiki.osdev.org/Programmable_Interval_Timer)
- [OSDev Wiki — PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard)

### Phase 5 — Framebuffer Graphics
- [OSDev Wiki — Drawing In Protected Mode](https://wiki.osdev.org/Drawing_In_Protected_Mode)
- [OSDev Wiki — GOP (Graphics Output Protocol)](https://wiki.osdev.org/GOP) (background context — Limine already gives you the framebuffer, but this explains where it comes from)
- Bresenham's line algorithm — any standard computer graphics reference

### Phase 6 — Windowing/Compositor
- [OSDev Wiki — PS/2 Mouse](https://wiki.osdev.org/PS/2_Mouse)
- [OSDev Wiki — User Interfaces](https://wiki.osdev.org/User_Interfaces) (survey of approaches other hobby OSes took)
- No single canonical tutorial exists for this phase — expect to design your own window/widget structs based on the primitives from Phase 5

### Phase 7 — Fonts & Text Input
- [OSDev Wiki — PC Screen Font](https://wiki.osdev.org/PC_Screen_Font) (PSF1/PSF2 format if you embed a real font file instead of hand-rolling glyphs)

### Phase 8 — FAT32
- [OSDev Wiki — FAT](https://wiki.osdev.org/FAT)
- [Microsoft FAT32 File System Specification (archived)](https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf)
- [OSDev Wiki — ATA PIO Mode](https://wiki.osdev.org/ATA_PIO_Mode)
- `mtools` documentation (for host-side FAT32 image creation/inspection during development)

### Phases 9–11 — File Manager, CRUD, Polish
- No dedicated OSDev Wiki pages — this is where you're building original application logic on top of Phases 1–8. Reuse the widget/window primitives from Phase 6–7 rather than searching for tutorials.

---

## 9. Risk / Scope-Creep Notes

**Where people typically get stuck:**
- **Phase 3 (Memory Management):** The #1 place hobby OS projects die. Paging bugs cause silent corruption or triple faults with no useful error message until Phase 2's exception handling is solid. *Minimum viable cutoff:* a bitmap PMM + basic 4-level paging + bump-allocator heap. Do not implement slab allocators, buddy allocators, or swap — those are v2 ideas.
- **Phase 6 (Windowing/Compositor):** The most open-ended phase with no single "correct" design, which invites endless redesign. *Minimum viable cutoff:* rectangular windows, solid colors, one z-order list, drag + click-to-focus + buttons. No transparency, shadows, animations, resizing, or minimize/maximize for v1.
- **Phase 8 (FAT32 write support):** Easy to silently corrupt a filesystem in ways that only show up later. *Minimum viable cutoff:* short (8.3) filenames only — skip VFAT long filename support entirely for v1; single-file-at-a-time operations (no need for open file handles/multiple concurrent writers).
- **General trap — "just one more driver":** USB, AHCI/NVMe, sound, and networking are common detours that feel like "real OS" milestones but contribute nothing toward the stated goal (a file manager with CRUD). If tempted, write the idea down as a "v2 backlog" item and keep moving.
- **General trap — rewriting the bootloader/build system repeatedly:** Once Phase 0–1 works, resist the urge to "perfect" the Makefile or switch build systems (e.g., to CMake or Meson) mid-project. It's pure yak-shaving relative to the stated goal.

**Suggested "definition of done" for v1 (the actual finish line):**
A QEMU-booted gOS image that: boots via Limine → shows a desktop with a taskbar → opens a File Manager window listing a FAT32 partition's contents → lets you create a folder, create a text file, open it in a text editor, type text, save it, and delete it — all without crashing, and the changes are verifiably persisted (visible after a reboot or when the disk image is mounted on the host). Everything beyond that (icons, themes, multiple font sizes, resizable windows, copy/paste, drag-and-drop between folders) is v2.
