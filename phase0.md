# Phase 0 — Toolchain & Project Setup — Completion Report

**Date completed:** 2026-06-30
**Status:** ✅ Complete — both milestones (0.1 and 0.2) done, all tasks verified.

---

## Summary

Phase 0 set up the development environment and repository skeleton needed to start Phase 1 (Bootloader & Boot Process). All tools were installed/verified, the OVMF UEFI firmware was sourced from an alternate location after the originally planned source turned out not to exist on macOS, and the repo skeleton (directory structure, git, Makefile, linker script, vendored Limine) is in place.

---

## Milestone 0.1: Cross-compiler and emulator installed and verified

### Task: Install `x86_64-elf-gcc` / `x86_64-elf-binutils`
- **What was done:** Ran `brew install x86_64-elf-gcc x86_64-elf-gdb`. This pulled in `x86_64-elf-binutils` and `libmpc` as dependencies automatically, plus `x86_64-elf-gdb` (installed proactively since Phase 2 debugging will need it, and it shares dependency setup).
- **Outcome:** GCC 16.1.0, GDB 17.2, binutils 2.46.1 installed successfully via prebuilt Homebrew bottles (no source compilation needed — installation took under 2 minutes).
- **Issues:** None.

### Task: Install `nasm`, `qemu-system-x86_64`, `xorriso`, `mtools`
- **What was done:** Checked existing Homebrew packages first.
- **Outcome:** All four were **already installed** on this machine prior to this session: nasm 3.01, qemu 10.2.0, xorriso 1.5.6.pl02, mtools 4.0.49. No action needed.
- **Issues:** None.

### Task: Download OVMF UEFI firmware
- **What was done:** The original plan assumed Homebrew's `qemu` formula bundles OVMF firmware on macOS. This assumption was **verified false** — Homebrew's `qemu` package only ships network option ROMs (`efi-e1000.rom`, etc.), not `OVMF_CODE.fd`/`OVMF_VARS.fd`. Searched Homebrew for `ovmf` and `edk2` formulas/taps — none exist in the default tap set on this machine.
  - Flagged this to the user via a clarifying question; user selected the fallback path (download prebuilt OVMF from a trusted release source).
  - Downloaded from `rust-osdev/ovmf-prebuilt`, release `edk2-stable202605-r1` (tracks official Tianocore edk2 stable release `edk2-stable202605`). This is a well-known, actively maintained source commonly used in the Rust OSDev community and referenced by Phil Opperman's "Writing an OS in Rust" blog series' tooling.
  - Extracted the `x64/` firmware set and placed clean copies at:
    - `third_party/ovmf/OVMF_CODE.fd`
    - `third_party/ovmf/OVMF_VARS.fd`
    - `third_party/ovmf/OVMF_SHELL.efi` (UEFI shell binary, useful for manual boot debugging later)
- **Outcome:** Working OVMF_CODE.fd (3.65 MB) and OVMF_VARS.fd (540 KB) in place.
- **Issues / Deviation from plan:** This is the one place Phase 0 didn't go as originally scoped in PROJECT_PLAN.md. The plan's §1 Tools section says "OVMF for QEMU" without specifying a source, and the AskUserQuestion at kickoff offered "via Homebrew qemu package" as the recommended option — that option doesn't actually exist on macOS. This has been corrected in PROJECT_PLAN.md's status tracker notes. **Action for future readers:** if re-provisioning this project on a new machine, go straight to the `rust-osdev/ovmf-prebuilt` GitHub releases page rather than trying Homebrew first.

### Task: Verify freestanding compile
- **What was done:** Wrote a throwaway `test_freestanding.c` with a function using no libc calls, compiled with `x86_64-elf-gcc -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -c`, inspected the output with `objdump -d`, then deleted the test file (not part of the permanent repo).
- **Outcome:** Produced a valid `ELF 64-bit LSB relocatable, x86-64` object file with correct disassembly (function prologue, `hlt` loop). Confirms the cross-compiler works correctly for freestanding kernel code.
- **Issues:** None.

### Task: Verify QEMU boots OVMF shell
- **What was done:** Ran QEMU headlessly (`-display none -serial stdio`) with the two newly downloaded OVMF pflash images and no boot media attached, with a 15-second timeout.
- **Outcome:** QEMU successfully initialized UEFI firmware and reached the Boot Device Selection (BDS) phase, printing `BdsDxe: No bootable option or device was found.` This is the **expected and correct** result — it proves the firmware itself is valid and functional; a "bootable option found" message would be surprising since no disk/kernel exists yet (that's Phase 1).
- **Issues:** None.

---

## Milestone 0.2: Repo and build skeleton exist

### Task: Set up repo directory structure
- **What was done:** Created `kernel/src`, `kernel/include`, `boot/`, `build/`, `third_party/`, `tools/`.
- **Outcome:** Directory skeleton matches PROJECT_PLAN.md's described layout.
- **Issues:** None.

### Task: Initialize git
- **What was done:** This wasn't an explicit Phase 0 task in the original plan text, but was required to vendor Limine "properly" (git submodule) as described in the plan. The project directory had no `.git` at session start. Asked the user for confirmation before running `git init` (a project-wide, moderately consequential action) — user approved. Initialized on branch `main`. Added a `.gitignore` (ignoring `build/`, `*.o`, `*.elf`, `*.iso`).
- **Outcome:** Git repository initialized, ready for commits as work progresses. No commits made yet in this session (user has not asked for a commit).
- **Issues:** None.

### Task: Write top-level Makefile
- **What was done:** Wrote `Makefile` at repo root with `build`, `iso`, `run`, `debug`, `clean` targets. Compiler flags set for a freestanding, no-red-zone, `mcmodel=kernel` build appropriate for a higher-half x86_64 kernel. `iso` target assembles a hybrid BIOS+UEFI ISO using `xorriso` and Limine's `limine-bios-cd.bin`/`limine-uefi-cd.bin`; `run`/`debug` targets launch QEMU with the vendored OVMF firmware.
- **Outcome:** Makefile exists and is syntactically structured correctly. **Not yet fully runnable end-to-end** — `make iso`/`make build` will currently fail because:
  1. No kernel C sources exist yet in `kernel/src/` (zero objects to link) — this is Phase 1/2 work.
  2. `boot/limine.cfg` doesn't exist yet — also Phase 1 work (explicitly listed as a Milestone 1.1 task in the plan).
  This is expected and by design — Phase 0's job was to prepare the build system, not to produce a bootable kernel (that's Phase 1's job). Flagging clearly here so it isn't mistaken for a bug.
- **Issues:** None beyond the expected incompleteness noted above.

### Task: Vendor Limine
- **What was done:** Added Limine as a git submodule (`third_party/limine`) tracking branch `v9.x-binary`. Resolved to tag `v9.6.7-binary`. Built the host-side `limine` deploy tool via `make` inside the submodule (compiles `limine.c`, a small C99 host utility used to install BIOS boot sectors onto disk images).
- **Outcome:** Submodule present with prebuilt `BOOTX64.EFI`, `BOOTAA64.EFI`, `limine-bios-cd.bin`, `limine-uefi-cd.bin`, `limine.h` (the boot protocol header the kernel will `#include`), and a working compiled `limine` host binary.
- **Issues:** First attempt (`git submodule add -b v9.x-binary --depth=1 ...`) failed with `fatal: 'origin/v9.x-binary' is not a commit`, despite that branch existing upstream. Root cause appears to be an interaction between `--depth=1` shallow-clone and branch resolution in this git version (2.52.0). **Resolution:** retried without `--depth=1`; succeeded immediately. No data integrity concern — just a slower (full-history) clone of a small binary-branch repo.

### Task: Write linker script
- **What was done:** Wrote `kernel/linker.ld` — higher-half kernel linked at `0xffffffff80000000`, with three separate `PT_LOAD` program headers (text: R+X, rodata: R only, data+bss: R+W), each page-aligned, `.eh_frame`/`.note`/`.comment` sections discarded.
- **Outcome:** Script matches the standard Limine higher-half kernel convention referenced in the OSDev Wiki's "Limine Bare Bones" tutorial. Not yet exercised by an actual link (no kernel objects exist yet) — will be validated for real in Phase 1, Milestone 1.1.
- **Issues:** None.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| OVMF sourced via Homebrew qemu package | Sourced from `rust-osdev/ovmf-prebuilt` GitHub release instead | Homebrew's macOS `qemu` formula does not bundle OVMF firmware files — verified by direct inspection, not an assumption |
| Vendor Limine via submodule (implied, not fully specified) | Required `git init` first since no git repo existed; user explicitly approved before proceeding | Repo wasn't initialized at session start |
| `git submodule add ... --depth=1` | Dropped `--depth=1` after it failed with a branch-resolution error | Shallow clone + specific branch combination failed on git 2.52.0; full clone worked without issue |

None of these deviations required any change to project scope, architecture, or later-phase plans — they were tooling/environment corrections only.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 0.1 | ~3–5 hours | ~1 hour (most tools were already installed; OVMF detour added ~15–20 min of investigation) |
| Milestone 0.2 | ~3–5 hours | ~1 hour |
| **Phase 0 total** | **6–10 hours** | **~2 hours** |

**Why actual was faster than estimated:** The original 6–10 hour estimate assumed installing every tool from scratch, including a from-scratch cross-compiler build if the Homebrew tap route failed. In practice: nasm/qemu/xorriso/mtools were pre-installed, the Homebrew `x86_64-elf-gcc` formula installed cleanly from prebuilt bottles (no source build needed), and Limine's binary branch meant no compilation of Limine itself beyond the small host tool. The only unplanned time cost was diagnosing the missing OVMF firmware and finding a trustworthy alternate source.

**Revised estimate guidance for future phases:** Phase 1 (Bootloader & Boot Process) estimates in PROJECT_PLAN.md (8–12 hours) remain realistic and unchanged — Phase 0's speed was a function of favorable pre-existing tool state on this machine, not a signal that later phases (which involve actual kernel code, not just environment setup) will be similarly fast.

---

## Verification Checklist (all confirmed working)

- [x] `x86_64-elf-gcc --version` → GCC 16.1.0
- [x] `x86_64-elf-gdb --version` → GDB 17.2
- [x] `nasm -v` → NASM 3.01
- [x] `qemu-system-x86_64 --version` → QEMU 10.2.0
- [x] `xorriso --version` → 1.5.6.pl02
- [x] `mtools --version` → 4.0.49
- [x] Freestanding `.c` file compiles to valid ELF64 object
- [x] QEMU boots OVMF firmware to BDS phase headlessly
- [x] `third_party/ovmf/{OVMF_CODE.fd,OVMF_VARS.fd,OVMF_SHELL.efi}` present
- [x] `third_party/limine/` submodule present, `v9.6.7-binary`, host tool builds
- [x] `kernel/linker.ld` present
- [x] `Makefile` present with `build`/`iso`/`run`/`debug`/`clean` targets
- [x] Git repository initialized on `main` with `.gitignore`

## Not Yet Verified (intentionally deferred to Phase 1)

- [ ] `make iso` — requires `boot/limine.cfg` and kernel sources (Phase 1, Milestone 1.1)
- [ ] `make run` — same dependency
- [ ] Linker script producing a real higher-half binary — requires actual kernel object files to link

---

## Next Step

Proceed to **Phase 1 — Bootloader & Boot Process** (Milestone 1.1: Kernel is loaded and entered by Limine), per [PROJECT_PLAN.md](PROJECT_PLAN.md).

---

## How to Test This Yourself

Run these from the project root:
`cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

### 1. Confirm tool versions

```bash
x86_64-elf-gcc --version | head -1     # expect: GCC 16.1.0 (or later)
x86_64-elf-gdb --version | head -1     # expect: GNU gdb 17.2 (or later)
nasm -v                                # expect: NASM version 3.01 (or later)
qemu-system-x86_64 --version | head -1 # expect: QEMU emulator version 10.2.0 (or later)
xorriso --version | head -1            # expect: GNU xorriso 1.5.6...
mtools --version | head -1             # expect: mtools (GNU mtools) 4.0.49 (or later)
```

### 2. Confirm the cross-compiler produces a valid freestanding binary

```bash
cat > /tmp/gos_test.c << 'EOF'
void _test_entry(void) {
    volatile int x = 1 + 1;
    (void)x;
    for (;;) { __asm__ volatile ("hlt"); }
}
EOF
x86_64-elf-gcc -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
    -c /tmp/gos_test.c -o /tmp/gos_test.o
file /tmp/gos_test.o          # expect: ELF 64-bit LSB relocatable, x86-64
x86_64-elf-objdump -d /tmp/gos_test.o   # expect: disassembly showing hlt loop
rm /tmp/gos_test.c /tmp/gos_test.o
```

### 3. Confirm OVMF firmware files exist and are the right shape

```bash
ls -la third_party/ovmf/
# expect: OVMF_CODE.fd (~3.6MB), OVMF_VARS.fd (~540KB), OVMF_SHELL.efi (~900KB)
file third_party/ovmf/OVMF_CODE.fd   # expect: "data" (raw flash image, no standard magic)
```

### 4. Confirm QEMU actually boots the OVMF firmware (headless, ~15s)

```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 15 qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=third_party/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_test.fd \
  -display none -serial stdio -monitor none
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output includes: `BdsDxe: No bootable option or device was found.`
This is the **correct** result at this stage — it proves UEFI firmware initialized properly. It should NOT say anything about finding a kernel yet (no kernel/ISO exists until Phase 1).

### 5. Confirm repo structure and version control

```bash
ls -la                         # expect: Makefile, PROJECT_PLAN.md, phase0.md, .gitignore, kernel/, boot/, build/, third_party/, tools/
git status                     # expect: on branch main, tracked/untracked files as expected
git submodule status           # expect: third_party/limine at v9.6.7-binary
```

### 6. Confirm Limine vendored correctly and its host tool builds

```bash
ls third_party/limine/ | grep -E "BOOTX64.EFI|limine.h|limine-bios-cd.bin|limine-uefi-cd.bin"
# expect: all four filenames listed

test -x third_party/limine/limine && echo "limine host tool is built and executable"
# if this prints nothing, rebuild it:
#   (cd third_party/limine && make)
```

### 7. Confirm the linker script and Makefile are syntactically valid

```bash
# Linker script parses without error (an empty link will still fail due to
# missing kernel objects/entry symbol — that's expected until Phase 1;
# this just checks the script itself isn't malformed):
x86_64-elf-ld --verbose -T kernel/linker.ld 2>&1 | grep -i "error\|parse" || echo "linker script parses cleanly"

# Makefile targets are recognized (won't fully succeed yet — no kernel
# sources or boot/limine.cfg exist until Phase 1):
make -n build   # dry-run: shows the commands it WOULD run
```

### What "success" looks like right now

Everything in steps 1–6 should complete with no errors and match the expected output shown. Step 7's `make -n build` dry-run should print commands without erroring, but an actual `make build`/`make iso`/`make run` is **expected to fail** until Phase 1 adds kernel source files and `boot/limine.cfg` — that failure is not a Phase 0 regression, it's the correctly-drawn boundary between phases.
