# gOS — Gauthiii's Operating System — Project Plan

**Last updated:** 2026-06-30 (Phase 0 completed — see [phase0.md](phase0.md))

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

**Milestone 1.1: Kernel is loaded and entered by Limine**
- [ ] Write `limine.cfg` describing the kernel entry and protocol version
- [ ] Write kernel entry point (`_start`) that receives the Limine boot info structure
- [ ] Package kernel + Limine into a bootable ISO with `xorriso`
- [ ] Boot the ISO in QEMU with OVMF and confirm the kernel entry point is reached (verify via QEMU's `-d int` trap log or a simple infinite `hlt` loop you can see isn't triple-faulting)

**Milestone 1.2: Kernel proves it's alive via serial output**
- [ ] Write a minimal serial (COM1/UART 16550) driver: `outb`/`inb` wrappers in C (inline asm) or ASM stubs
- [ ] Implement `serial_write_char` / `serial_write_string`
- [ ] Print a boot banner ("gOS booting...") over serial, visible via QEMU's `-serial stdio`
- [ ] Confirm Limine memory map and framebuffer info structs are non-null and print their key fields (address, size, pitch, bpp) over serial

**Dependency note:** Nothing in Phase 2+ can be tested without Milestone 1.2's serial output — it is your only debugging channel until the framebuffer (Phase 5) is working. Do not skip it.

---

### Phase 2 — Kernel Foundations (GDT/IDT/Interrupts)
**Estimated time: 12–18 hours (~2 weeks)**

**Milestone 2.1: Custom GDT is loaded**
- [ ] Define a GDT with null, kernel code, kernel data, user code, user data, and TSS descriptors
- [ ] Write `gdt_load` in ASM (`lgdt` + far jump to reload `cs`)
- [ ] Load the new GDT at kernel init and confirm via serial log that segment registers hold expected selectors

**Milestone 2.2: IDT and exception handlers are wired up**
- [ ] Define an IDT with 256 entries
- [ ] Write ASM interrupt-entry trampolines (stub per vector, pushes error code/vector, jumps to common handler) for at least the 32 CPU exception vectors
- [ ] Write a C `isr_handler(struct interrupt_frame*)` that logs vector number, error code, and `rip` to serial
- [ ] Deliberately trigger a divide-by-zero (`int 0/0`) and a page fault (write to unmapped address) and confirm both are caught and logged instead of triple-faulting

**Milestone 2.3: Hardware interrupts (PIC/APIC) are enabled**
- [ ] Remap the legacy 8259 PIC (or initialize the Local APIC + IOAPIC if going the modern route — **recommend legacy PIC first for simplicity**, revisit APIC only if SMP is ever added)
- [ ] Write IRQ entry stubs for IRQ0 (timer) and IRQ1 (keyboard), mapped to IDT vectors 32+
- [ ] Enable interrupts (`sti`) and confirm a timer tick handler fires periodically (log every Nth tick to serial without flooding output)

**Dependency note:** Phase 4 keyboard driver requires IRQ1 (2.3) to already deliver interrupts. Phase 3's page fault handler requires 2.2's exception plumbing.

---

### Phase 3 — Memory Management (Physical + Virtual)
**Estimated time: 16–24 hours (~2.5–3 weeks)**

**Milestone 3.1: Physical memory allocator (PMM) works**
- [ ] Parse the Limine memory map to find usable RAM regions
- [ ] Implement a bitmap-based physical page allocator (`pmm_alloc_page` / `pmm_free_page`, 4 KiB pages)
- [ ] Reserve pages already used by kernel image, Limine reclaimable regions, and framebuffer
- [ ] Write a smoke test: allocate 100 pages, free them, allocate again, confirm addresses are reused and no double-allocation occurs (assert via serial log)

**Milestone 3.2: Virtual memory / paging is under kernel control**
- [ ] Write page table structures (PML4/PDPT/PD/PT) and a `vmm_map_page(virt, phys, flags)` function
- [ ] Identity-map or higher-half-map the kernel using Limine's provided base addresses
- [ ] Load a new `CR3` with the kernel-built page tables (not Limine's) and confirm the kernel keeps running (serial log after switch = success)
- [ ] Hook the page fault handler (from 2.2) to log faulting address (`CR2`) and access type

**Milestone 3.3: Kernel heap allocator exists**
- [ ] Implement a simple heap (bump allocator or freelist) backed by `vmm_map_page` calls
- [ ] Implement `kmalloc` / `kfree`
- [ ] Stress test: allocate/free a mix of small and large blocks in a loop, verify no corruption via a canary/guard pattern

**Dependency note:** Everything from Phase 4 onward calls `kmalloc` — this phase blocks the entire rest of the project. Budget extra time here; it's the phase most OSDev projects stall on.

---

### Phase 4 — Basic Drivers (Serial, Timer, Keyboard)
**Estimated time: 8–12 hours (~1.5 weeks)**

*(Serial already exists from 1.2 — this phase extends it and adds new drivers.)*

**Milestone 4.1: PIT timer driver with tick counting**
- [ ] Program the PIT (Programmable Interval Timer, port 0x40/0x43) to a fixed frequency (e.g., 100 Hz)
- [ ] Maintain a global tick counter incremented in the IRQ0 handler
- [ ] Implement a `sleep_ms(ms)` busy-wait or tick-wait function and verify timing accuracy against a stopwatch

**Milestone 4.2: PS/2 keyboard driver**
- [ ] Read scancodes from port 0x60 in the IRQ1 handler
- [ ] Implement a scancode-set-1-to-ASCII translation table (handle shift, caps lock, at minimum)
- [ ] Buffer keypresses in a small ring buffer; expose `kb_getchar()` (blocking) to kernel code
- [ ] Test: type on the QEMU window and see characters echoed over serial

**Dependency note:** Phase 7 (text input) directly consumes `kb_getchar()`/the keyboard ring buffer from 4.2.

---

### Phase 5 — Framebuffer Graphics
**Estimated time: 10–14 hours (~2 weeks)**

**Milestone 5.1: Raw pixel plotting works**
- [ ] Extract framebuffer address, width, height, pitch, bpp from the Limine framebuffer response struct
- [ ] Implement `fb_put_pixel(x, y, color)` respecting pitch (do not assume pitch == width*4)
- [ ] Clear the entire screen to a solid color and confirm visually in the QEMU window

**Milestone 5.2: Primitive 2D drawing routines**
- [ ] Implement `fb_draw_rect(x, y, w, h, color)` (filled)
- [ ] Implement `fb_draw_rect_outline(x, y, w, h, color, thickness)`
- [ ] Implement `fb_draw_line(x0, y0, x1, y1, color)` (Bresenham's algorithm)
- [ ] Draw a test pattern (nested rectangles + diagonal lines) at boot to visually confirm all primitives work

**Milestone 5.3: Double buffering / flip to avoid tearing**
- [ ] Allocate a back buffer in kernel heap matching framebuffer dimensions
- [ ] Redirect all drawing routines to the back buffer
- [ ] Implement `fb_flip()` (memcpy back buffer → real framebuffer)
- [ ] Verify no visible tearing/flicker when redrawing rapidly (e.g., an animated bouncing rectangle test)

---

### Phase 6 — Windowing / Compositor
**Estimated time: 20–30 hours (~3.5–4 weeks)**

**Milestone 6.1: Mouse input works (PS/2 mouse)**
- [ ] Enable the PS/2 auxiliary (mouse) port and IRQ12
- [ ] Parse 3-byte PS/2 mouse packets (dx, dy, button state)
- [ ] Track and clamp a global cursor `(x, y)` position; draw a simple cursor sprite/rect at that position each frame

**Milestone 6.2: Single window can be drawn and moved**
- [ ] Define a `struct window { x, y, w, h, title, back_buffer, ... }`
- [ ] Implement rendering: title bar rect + body rect + border, blitted into the framebuffer back buffer
- [ ] Implement drag: on mouse-down inside title bar, track offset; on mouse-move while dragging, update window x/y; on mouse-up, stop dragging
- [ ] Test: spawn one window, drag it around the screen with the mouse, confirm it tracks smoothly

**Milestone 6.3: Multiple windows with z-ordering**
- [ ] Maintain a window list/array with a z-order (or linked list ordered front-to-back)
- [ ] Implement compositing: redraw all windows back-to-front into the framebuffer back buffer every frame
- [ ] Implement click-to-focus: clicking a window raises it to the top of the z-order
- [ ] Test: spawn 3 overlapping windows, click each, confirm correct front/back ordering and drag-independence

**Milestone 6.4: Basic widgets — buttons and clickable regions**
- [ ] Define a simple button widget (rect + label + click callback)
- [ ] Implement hit-testing: mouse click coordinates → window → widget → callback dispatch
- [ ] Test: a window with a button that changes color or logs to serial when clicked

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
| 1 | 1.1 Kernel loaded by Limine | Write limine.cfg | Not Started | |
| 1 | 1.1 Kernel loaded by Limine | Write kernel _start entry | Not Started | |
| 1 | 1.1 Kernel loaded by Limine | Build bootable ISO | Not Started | |
| 1 | 1.1 Kernel loaded by Limine | Boot in QEMU, confirm entry reached | Not Started | |
| 1 | 1.2 Serial output alive | Write serial (UART) driver | Not Started | |
| 1 | 1.2 Serial output alive | Implement serial_write_char/string | Not Started | |
| 1 | 1.2 Serial output alive | Print boot banner | Not Started | |
| 1 | 1.2 Serial output alive | Confirm memory map + framebuffer info | Not Started | |
| 2 | 2.1 Custom GDT loaded | Define GDT descriptors | Not Started | |
| 2 | 2.1 Custom GDT loaded | Write gdt_load in ASM | Not Started | |
| 2 | 2.1 Custom GDT loaded | Load GDT, verify segment registers | Not Started | |
| 2 | 2.2 IDT + exceptions wired | Define 256-entry IDT | Not Started | |
| 2 | 2.2 IDT + exceptions wired | Write ASM ISR trampolines (32 vectors) | Not Started | |
| 2 | 2.2 IDT + exceptions wired | Write C isr_handler with logging | Not Started | |
| 2 | 2.2 IDT + exceptions wired | Trigger div-by-zero + page fault, confirm caught | Not Started | |
| 2 | 2.3 Hardware interrupts enabled | Remap 8259 PIC | Not Started | |
| 2 | 2.3 Hardware interrupts enabled | Write IRQ0/IRQ1 entry stubs | Not Started | |
| 2 | 2.3 Hardware interrupts enabled | Enable interrupts, confirm timer ticks | Not Started | |
| 3 | 3.1 Physical memory allocator | Parse Limine memory map | Not Started | |
| 3 | 3.1 Physical memory allocator | Implement bitmap PMM alloc/free | Not Started | |
| 3 | 3.1 Physical memory allocator | Reserve kernel/framebuffer regions | Not Started | |
| 3 | 3.1 Physical memory allocator | Smoke test alloc/free reuse | Not Started | |
| 3 | 3.2 Virtual memory/paging | Write page table structures | Not Started | |
| 3 | 3.2 Virtual memory/paging | Map kernel higher-half | Not Started | |
| 3 | 3.2 Virtual memory/paging | Load new CR3, confirm still running | Not Started | |
| 3 | 3.2 Virtual memory/paging | Hook page fault handler to log CR2 | Not Started | |
| 3 | 3.3 Kernel heap | Implement heap backed by vmm_map_page | Not Started | |
| 3 | 3.3 Kernel heap | Implement kmalloc/kfree | Not Started | |
| 3 | 3.3 Kernel heap | Stress test alloc/free mix | Not Started | |
| 4 | 4.1 PIT timer driver | Program PIT to fixed frequency | Not Started | |
| 4 | 4.1 PIT timer driver | Maintain tick counter in IRQ0 | Not Started | |
| 4 | 4.1 PIT timer driver | Implement sleep_ms, verify timing | Not Started | |
| 4 | 4.2 PS/2 keyboard driver | Read scancodes in IRQ1 handler | Not Started | |
| 4 | 4.2 PS/2 keyboard driver | Scancode-to-ASCII translation table | Not Started | |
| 4 | 4.2 PS/2 keyboard driver | Ring buffer + kb_getchar() | Not Started | |
| 4 | 4.2 PS/2 keyboard driver | Test typing, echo over serial | Not Started | |
| 5 | 5.1 Raw pixel plotting | Extract framebuffer info from Limine | Not Started | |
| 5 | 5.1 Raw pixel plotting | Implement fb_put_pixel | Not Started | |
| 5 | 5.1 Raw pixel plotting | Clear screen to solid color | Not Started | |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_rect (filled) | Not Started | |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_rect_outline | Not Started | |
| 5 | 5.2 Primitive 2D drawing | Implement fb_draw_line (Bresenham) | Not Started | |
| 5 | 5.2 Primitive 2D drawing | Draw test pattern | Not Started | |
| 5 | 5.3 Double buffering | Allocate back buffer | Not Started | |
| 5 | 5.3 Double buffering | Redirect drawing to back buffer | Not Started | |
| 5 | 5.3 Double buffering | Implement fb_flip | Not Started | |
| 5 | 5.3 Double buffering | Verify no tearing with animation test | Not Started | |
| 6 | 6.1 Mouse input | Enable PS/2 aux port + IRQ12 | Not Started | |
| 6 | 6.1 Mouse input | Parse 3-byte mouse packets | Not Started | |
| 6 | 6.1 Mouse input | Track/draw cursor | Not Started | |
| 6 | 6.2 Single window draggable | Define struct window | Not Started | |
| 6 | 6.2 Single window draggable | Render title bar + body + border | Not Started | |
| 6 | 6.2 Single window draggable | Implement drag logic | Not Started | |
| 6 | 6.2 Single window draggable | Test dragging smoothly | Not Started | |
| 6 | 6.3 Multiple windows z-order | Maintain window list with z-order | Not Started | |
| 6 | 6.3 Multiple windows z-order | Implement compositing loop | Not Started | |
| 6 | 6.3 Multiple windows z-order | Implement click-to-focus | Not Started | |
| 6 | 6.3 Multiple windows z-order | Test 3 overlapping windows | Not Started | |
| 6 | 6.4 Basic widgets | Define button widget | Not Started | |
| 6 | 6.4 Basic widgets | Implement hit-testing/dispatch | Not Started | |
| 6 | 6.4 Basic widgets | Test clickable button | Not Started | |
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
