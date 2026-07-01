# Phase 1 — Bootloader & Boot Process — Completion Report

**Date completed:** 2026-06-30
**Status:** ✅ Complete — both milestones (1.1 and 1.2) done, all tasks verified.

---

## Summary

Phase 1 got gOS to the point of being a real, bootable (if silent-to-the-eye) operating system: Limine loads the kernel ELF, hands off control to `_start` in 64-bit long mode at the correct higher-half virtual address, and the kernel proves it's alive by talking to the serial port and dumping the boot-time memory map and framebuffer info it received from the bootloader. This is the foundation every later phase builds on — from here on, the kernel can report its own state instead of the developer having to guess from a black QEMU window.

---

## Milestone 1.1: Kernel is loaded and entered by Limine

### Task: Write the Limine config file
- **What was done:** Researched the current Limine v9.x config format directly from the source repo (`CONFIG.md` at tag `v9.6.7`, matching the vendored submodule version) rather than assuming the plan's originally-named `limine.cfg` was still correct.
- **Outcome:** Found the config file must be named `limine.conf` (Limine renamed it from `.cfg` in a past major version) and placed at one of several recognized paths. Wrote `boot/limine.conf`:
  ```
  timeout: 0
  verbose: yes

  /gOS
  protocol: limine
  path: boot():/boot/gos.elf
  ```
  Deployed to `/boot/limine.conf` on the ISO (one of Limine's recognized default search paths).
- **Issues / deviation from plan:** The plan's task text said "Write `limine.cfg`" — this is now outdated terminology for the Limine version we vendored in Phase 0. Corrected in PROJECT_PLAN.md.

### Task: Write the kernel entry point (`_start`)
- **What was done:** Wrote `kernel/src/start.c`. Declared a Limine base-revision request (revision 3) and the required start/end request markers, per the exact macro layout in our vendored `limine.h` (verified by reading the header directly rather than copying boilerplate from a possibly-mismatched newer Limine tutorial — the newer `limine-c-template` on GitHub uses a different macro calling convention for a later header revision, which would NOT have compiled against our v9.6.7 header).
- **Outcome:** `_start` checks `LIMINE_BASE_REVISION_SUPPORTED`, then (once Milestone 1.2 code was added) proceeds to serial-log boot state before halting.
- **Issues:** None once the correct header version's macro usage was confirmed.

### Task: Package kernel + Limine into a bootable ISO
- **What was done:** Extended the Phase 0 Makefile's `iso` target (previously untested — it referenced files that didn't exist yet) to copy the kernel ELF, `limine.conf`, Limine's BIOS/UEFI boot images, and `BOOTX64.EFI` into an ISO root, then run `xorriso -as mkisofs` (hybrid BIOS+UEFI, GPT+MBR hybrid) followed by `limine bios-install` to write BIOS boot stages.
- **Outcome:** `make iso` produces `build/gos.iso` cleanly, no errors from `xorriso` or the Limine installer.
- **Issues:** None.

### Task: Boot in QEMU and confirm the kernel entry point is reached
- **What was done:** Booted the ISO in QEMU with the Phase 0 OVMF firmware, headless, using the QEMU monitor (`info registers`) to inspect live CPU state a few seconds after boot — rather than only trusting "it didn't crash," which could also mean "it's still sitting in the UEFI shell."
- **Outcome:** `RIP=0xffffffff80001005`, `HLT=1` — the CPU was parked inside our kernel's `hcf()` halt loop, at a higher-half virtual address that only exists after Limine's page tables and kernel mapping took effect. This is unambiguous proof of successful boot: BIOS/UEFI → Limine → 64-bit long mode → higher-half kernel `_start` → clean halt, no triple fault or reset.
- **Issues:** First test attempt sampled registers only 2 seconds after QEMU launch and caught the CPU still in real-mode reset state (`EIP=0xfff0`, `CS=0xf000`) — the classic x86 reset vector, not a failure of the kernel. Increased the settle delay to 5 seconds before sampling, which reliably captures the post-boot state.

---

## Milestone 1.2: Kernel proves it's alive via serial output

### Task: Write a minimal serial (COM1/UART 16550) driver
- **What was done:** Wrote `kernel/src/serial.c` / `kernel/include/serial.h`. Programs COM1 (I/O port `0x3F8`) directly via `outb`/`inb` inline assembly: disables UART interrupts, sets the divisor for 38400 baud, configures 8N1 framing, enables the FIFO, and asserts RTS/DSR — the standard 16550 bring-up sequence used throughout OSDev tutorials.
- **Outcome:** Functional polling-mode UART driver (no interrupts needed yet — Phase 2/4 will wire up interrupt-driven I/O).
- **Issues:** None.

### Task: Implement `serial_write_char` / `serial_write_string`
- **What was done:** Implemented both, plus two extra helpers not explicitly in the original task list but needed immediately for the next task: `serial_write_hex64` (zero-padded 64-bit hex, for addresses) and `serial_write_uint` (decimal, for counts/sizes). `serial_write_string` also translates `\n` to `\r\n` since a raw serial terminal doesn't do that translation itself.
- **Outcome:** All four functions compile and produce correctly formatted output (verified in the combined test below).
- **Issues:** None. The two extra helpers are a reasonable, minimal addition — Milestone 1.2's next task explicitly requires printing addresses/sizes, which isn't possible with `write_string` alone.

### Task: Print a boot banner over serial
- **What was done:** `_start` calls `serial_init()` then immediately writes `"=== gOS booting... ===\n"`.
- **Outcome:** Banner visible as the first line of output over `-serial stdio` in QEMU.
- **Issues:** None.

### Task: Confirm Limine memory map and framebuffer info are non-null and print key fields
- **What was done:** Added a `limine_memmap_request` and `limine_framebuffer_request` to `start.c` (same `.requests` section as the base revision marker). After the banner, the kernel checks both responses are non-null, then iterates every memory map entry printing base/length/type (with a human-readable type name via a lookup function), sums usable memory, and prints the first framebuffer's address/width/height/pitch/bpp.
- **Outcome:** Live QEMU boot output confirmed:
  - 36 memory map entries enumerated correctly (mix of `USABLE`, `RESERVED`, `ACPI_NVS`, `ACPI_RECLAIMABLE`, `BOOTLOADER_RECLAIMABLE`, `KERNEL_AND_MODULES`, `FRAMEBUFFER` regions)
  - 207 MiB total usable RAM (matches the 256 MiB QEMU `-m` allocation minus firmware/reserved regions — sane)
  - Framebuffer: address `0xffff800080000000`, 1280×800, pitch 5120, 32bpp — a real, usable linear framebuffer ready for Phase 5.
- **Issues:** One compile error — the header's `LIMINE_MEMMAP_EXECUTABLE_AND_MODULES` constant is only defined when `LIMINE_API_REVISION >= 2`, which isn't set in our build (defaults to 0), so the older constant name `LIMINE_MEMMAP_KERNEL_AND_MODULES` applies instead. Caught immediately by the compiler and fixed by reading the header's `#if` guard rather than guessing.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| Config file named `limine.cfg` | Named `limine.conf` instead | Current Limine (v9.x, as vendored in Phase 0) renamed the config format/filename from older tutorials' `limine.cfg` |
| (implicit) copy boilerplate `_start`/request-struct code from a tutorial | Read the vendored `limine.h` directly and matched its exact macro calling convention | A newer official Limine template (targeting a later header revision) uses a different, incompatible macro style — copying it verbatim would not have compiled against our vendored v9.6.7 header |
| (implicit) `LIMINE_MEMMAP_EXECUTABLE_AND_MODULES` constant | Used `LIMINE_MEMMAP_KERNEL_AND_MODULES` instead | That constant name is conditional on an API revision macro not set in this build; caught at compile time |

None of these required any change to project scope or later-phase plans — all were naming/header-version corrections caught either through direct source research or the compiler, not left as silent assumptions.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 1.1 | ~4–6 hours | ~1.5 hours |
| Milestone 1.2 | ~4–6 hours | ~1 hour |
| **Phase 1 total** | **8–12 hours** | **~2.5 hours** |

**Why actual was faster than estimated:** The 8–12 hour estimate assumes a developer researching the Limine boot protocol and 16550 UART programming from scratch, including trial-and-error on config file syntax and linker script section placement. Because Phase 0 had already vendored the exact Limine version and its header, going straight to the authoritative source (`CONFIG.md` at the matching git tag, and the header itself) avoided the usual trial-and-error cycle of "try a tutorial's config syntax, watch it fail silently, debug why." The QEMU monitor register-inspection technique for confirming boot success (rather than needing serial output first) also avoided a chicken-and-egg debugging problem some tutorials run into.

**Revised estimate guidance for future phases:** Phase 2 (GDT/IDT/Interrupts) is qualitatively different work — it involves hand-written x86_64 assembly trampolines and deliberately triggering CPU exceptions, which has a much higher chance of subtle bugs (wrong IDT gate types, stack alignment issues, etc.) that won't be caught by the compiler the way Phase 1's header-mismatch issues were. Treat the plan's 12–18 hour estimate for Phase 2 as realistic, not as something likely to compress the way Phase 0/1 did.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

### Milestone 1.1 — Kernel is loaded and entered by Limine

**1. Build the kernel and inspect the ELF:**
```bash
make clean && make build
x86_64-elf-readelf -h build/gos.elf | grep -E "Entry|Type"
x86_64-elf-readelf -l build/gos.elf
```
Expected:
- `Type: EXEC (Executable file)`
- `Entry point address: 0xffffffff80001007` (or similar higher-half address — exact value may shift slightly if source changes, but must start with `0xffffffff8`)
- Program headers show a `.requests` segment mapped `R` (read-only) plus `.text`/`.rodata`/`.data` segments, all in the `0xffffffff8...` range

**2. Build the ISO:**
```bash
make iso
```
Expected: no errors from `xorriso` or `limine bios-install`; ends with `Limine BIOS stages installed successfully!`; `build/gos.iso` exists.

**3. Boot and verify via QEMU monitor (proves the kernel was actually entered, not just that QEMU didn't crash):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 5; printf "info registers\n"; sleep 1; printf "quit\n") | timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial none -monitor stdio -no-reboot -no-shutdown | grep -E "^RIP|HLT"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected: a line containing `RIP=ffffffff8000....` and `HLT=1`. The RIP value being in the `0xffffffff8...` range with `HLT=1` proves Limine loaded the kernel, jumped to `_start`, and the CPU is parked in our halt loop — not stuck in the UEFI shell (which would show a real-mode or low-address RIP) and not triple-faulted/reset (which would show the CPU back at the BIOS reset vector `EIP=0xfff0`, `CS=0xf000`).

### Milestone 1.2 — Kernel proves it's alive via serial output

**1. Rebuild (in case only Milestone 1.1 was tested so far):**
```bash
make clean && make iso
```

**2. Boot with serial output attached to your terminal:**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_serial.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_serial.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown
rm -f /tmp/OVMF_VARS_serial.fd
```

Expected output (exact addresses/memory sizes may vary slightly by host/QEMU version, but the structure must match):
```
=== gOS booting... ===
Limine base revision: supported
Memory map entries: <some number, e.g. 36>
  [0] base=0x... length=0x... type=USABLE
  [1] base=0x... length=0x... type=...
  ... (one line per entry) ...
Total usable memory: <N> MiB
Framebuffer address: 0x...
Framebuffer width: <e.g. 1280>
Framebuffer height: <e.g. 800>
Framebuffer pitch: <e.g. 5120>
Framebuffer bpp: 32
=== gOS boot checks complete ===
```

**What to check specifically:**
- `Limine base revision: supported` must appear — if instead you see `PANIC: Limine base revision not supported`, the Limine version and header version are mismatched (shouldn't happen here since both come from the same vendored submodule, but would indicate submodule corruption).
- `Memory map entries:` must be a number greater than 0, followed by that many `[N] base=... length=... type=...` lines with **no `UNKNOWN` types** (an `UNKNOWN` type means the `memmap_type_name()` lookup table is missing a case — not expected with the current code, but worth checking if you extend it).
- `Total usable memory:` should be a plausible fraction of the `-m 256M` passed to QEMU (expect roughly 180–230 MiB after firmware/reserved regions are subtracted — seeing something wildly different, like 0 MiB or more than 256 MiB, indicates a parsing bug).
- Framebuffer fields must all be non-zero. A framebuffer address of `0x0` or width/height of `0` would mean `PANIC: no framebuffer response from Limine` should have fired instead — if you see zeroed fields without that panic message, something is wrong with the response check.
- The final line `=== gOS boot checks complete ===` confirms the kernel reached the end of its boot-check sequence and is now (correctly, at this phase) sitting in the halt loop forever — QEMU will only exit here because of the `timeout 12` wrapper, not because the kernel shut down.

### Quick regression check for both milestones together
```bash
make clean && make iso && echo "BUILD OK"
```
If this prints `BUILD OK` with no errors, both milestones' build-time prerequisites are intact; then re-run either QEMU test above to confirm runtime behavior hasn't regressed.

---

## Not Yet Verified (intentionally deferred to Phase 2+)

- [ ] GDT/IDT — no custom descriptor tables exist yet; the kernel is still running on whatever GDT/IDT Limine left in place
- [ ] Exception handling — no interrupt handlers exist; a CPU exception right now would triple-fault with no diagnostic output
- [ ] Anything beyond a single halt loop — the kernel does not yet do anything other than report boot state and halt

---

## Next Step

Proceed to **Phase 2 — Kernel Foundations (GDT/IDT/Interrupts)** (Milestone 2.1: Custom GDT is loaded), per [PROJECT_PLAN.md](PROJECT_PLAN.md).
