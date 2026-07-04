# Phase 12 — Critical Audit Fixes — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all five Critical findings from [version1/audit.md](version1/audit.md) fixed, all with a live QEMU reproduction of the original bug followed by a verified fix.

---

## Summary

Phase 12 is Track A, Phase 1 of [project-plan-2.md](project-plan-2.md): the five Critical-severity findings from the post-v1.0 kernel audit. Unlike the audit itself (explicitly read-only, no code changed), every fix here was verified two ways: (1) a live QEMU reproduction of the *original* bug — proving it was real, not theoretical — captured before the fix landed, and (2) confirmation that the fix closes it, cross-checked against an independent tool (`mtools`, raw sector diffing, or direct register/CR2 inspection) rather than trusting the kernel's own self-report alone. Every finding also got a permanent, code-level guard (not just a one-off patch) so the class of bug can't silently regress.

---

## Build and run gOS (normal, non-test build)

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```
Builds the kernel, packages it with Limine into a bootable ISO, creates/reuses `disk_images/gos_disk.img`, and launches a real graphical QEMU window (mouse and keyboard work normally). This is the one command to sanity-check the whole kernel, including all five Phase 12 fixes, together in the normal boot path (no `GOS_TEST_*` flags — those are opt-in, one-off repro builds, covered per-milestone below).

Other useful targets, same as documented in [README.md](README.md):
```bash
make build   # compile the kernel only, no ISO/QEMU
make iso     # build the bootable ISO without launching it
make debug   # like `make run`, but pauses at boot and opens a GDB stub (-s -S)
make clean   # remove build artifacts (kernel, ISO — NOT the disk image)
```

---

## Milestone 12.1: `fat_write_file` unsigned underflow fixed

### Task: Fix the underflow when `existing_count == 0`
- **What was done:** `fat_write_file` (`kernel/src/fat32.c`) walked a file's cluster chain starting from `entry.first_cluster` without checking for `cluster == 0` first. `is_end_of_chain()` only recognizes `0x0FFFFFFF8+` as end-of-chain, so a legitimate on-disk zero-length file with `first_cluster == 0` (created by another OS/tool via `mtools`, not this kernel's own `fat_create_file`, which always allocates a real cluster) would have its walk enter the loop with `cluster == 0`, treat cluster 0 as if it were real data, and — depending on downstream chain-growth logic — either read garbage stack memory (`clusters[existing_count - 1]` with `existing_count == 0`, the audit's literal underflow) or, as the live repro below showed, write real file data directly into cluster 0's computed disk location.
- **The actual fix:** Two changes, applied together: (1) the chain-walk now explicitly skips walking when `cluster == 0` (`if (cluster != 0) { while (...) ... }`), so `existing_count` stays a clean `0` instead of picking up a bogus "cluster 0" entry; (2) the cluster-growth branch now special-cases `existing_count == 0` by allocating a fresh first cluster via `fat_alloc_cluster()`, instead of unconditionally computing `clusters[existing_count - 1]`.
- **Outcome:** Zero-byte files with `first_cluster == 0` can now be written to safely.

### Test: reproduce the original corruption, then confirm the fix
- **Repro setup:** Built a scratch disk image (`mtools`, `mcopy` of an empty host file) containing `ZEROLEN.TXT` with a genuine on-disk `first_cluster == 0` — confirmed via direct BPB/dirent parsing in Python, not just visual inspection.
- **Pre-fix reproduction:** Temporarily reverted the fix, added a debug print of the underflowed `last` cluster value, and diffed the disk image's sectors before/after calling `fat_write_file("ZEROLEN.TXT", "hi", 2)`. Result: **sector 2048 — inside the mirror FAT table (FAT2 spans sectors 1041–2049) — was overwritten with the literal file bytes `"hi"`**, confirmed byte-for-byte via a raw Python sector read. This is a real, concrete instance of "writing FAT-table data at an arbitrary sector," not just a theoretical wild pointer.
- **Post-fix verification:** Same test, fixed code: `fat_write_file` returns success, `fat_read_file` reads back `"hi"` correctly, and the sector diff shows only the expected legitimate sectors changed (`32`, `1041` — both FAT copies' first sector; `2050` — the directory entry; `2061–2063` — the real newly-allocated data cluster). **Sector 2048 was untouched.**
- **Independent cross-check:** Ran `mtools`' `mdir`/`mtype` against the fixed image from the host (outside the kernel entirely) — confirmed `ZEROLEN.TXT` shows size 2, content `"hi"`, and `PERSIST.TXT` (an unrelated existing file) is byte-for-byte unchanged.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean && make build   # confirm clean compile with the fix in place
```
To re-run the live repro end-to-end (regenerates its own scratch image, no state left in `disk_images/gos_disk.img`):
```bash
cp disk_images/gos_disk.img /tmp/gos_disk_zerolen.img
: > /tmp/ZEROLEN.TXT
mcopy -i /tmp/gos_disk_zerolen.img /tmp/ZEROLEN.TXT ::/ZEROLEN.TXT
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_ZEROLEN_WRITE"
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_zerolen.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep "TEST:"
```
Expected:
```
TEST: fat_write_file(ZEROLEN.TXT) - first_cluster==0 repro...
TEST: fat_write_file(ZEROLEN.TXT) = 1 (OK)
TEST: read back ZEROLEN.TXT (2 bytes): "hi"
```
Then independently verify via `mtools`: `mdir -i /tmp/gos_disk_zerolen.img | grep ZEROLEN` should show size `2`, and `mtype -i /tmp/gos_disk_zerolen.img ::/ZEROLEN.TXT` should print `hi`.

### Command to see
```bash
make debug   # -s -S GDB stub + real graphical QEMU window
```
Or, non-interactively, capture a screendump of the desktop after boot completes — see [screenshots/phase12_12.1_zerolen_write_boot.png](screenshots/phase12_12.1_zerolen_write_boot.png), taken after the `GOS_TEST_ZEROLEN_WRITE` repro build ran to completion (full desktop renders normally — proof the fix doesn't destabilize anything downstream).

---

## Milestone 12.2: `fat32_init()` failure now actually halts

### Task: Make a failed FAT32 mount call `hcf()` instead of falling through
- **What was done:** `_start()` in `kernel/src/start.c` logged `"FAT32: PANIC - not a valid FAT32 filesystem"` on `fat32_init()` failure but never called `hcf()` — execution fell straight through to `fb_init()`, `stress_test()`, and every other FAT32 call, all operating on BPB globals from a filesystem the kernel itself had just declared invalid.
- **The actual fix:** Added `hcf();` immediately after the PANIC log in the `else` branch of `if (fat32_init())`.

### Test: reproduce the original silent-continue bug, then confirm the fix
- **Repro setup:** Built a scratch disk image with a valid `0x55AA` boot signature but a zeroed-out `"FAT32   "` filesystem-type string at BPB offset 82 (a realistic "wrong/corrupted FS type" scenario — geometry fields intact, signature string wiped).
- **Pre-fix reproduction:** Temporarily removed the `hcf()` call. Booted against the corrupted image. The kernel printed the PANIC line, then **continued straight into `fb_init()`, the mouse driver, and `stress_test()` — which reported `"Stress test: PASS (150 file cycles, 300 window cycles, no crash)"` on a filesystem it had just called invalid.** This is exactly the audit's concern: false confidence from a self-test that never should have run.
- **Post-fix verification:** Same corrupted image, fixed code. Serial log shows the PANIC line and **nothing else** — `grep -c "Stress test\|TEMP.TXT\|PERSIST.TXT\|TEMPDIR"` on the log returns `0`. A screendump 8 seconds into boot shows a solid black screen — proof `fb_init()`/`fb_clear()` never ran, confirming the halt happens before any further subsystem executes.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
cp disk_images/gos_disk.img /tmp/gos_disk_corrupt_bpb.img
python3 -c "
with open('/tmp/gos_disk_corrupt_bpb.img','r+b') as f:
    f.seek(82); f.write(b'\x00'*8)
"
make iso
timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_corrupt_bpb.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | tail -20
```
Expected: the last FAT32-related line is `FAT32: PANIC - not a valid FAT32 filesystem`, followed only by `Timer tick:` lines (the timer IRQ still fires since `hcf()` is a `hlt` loop with interrupts enabled, not a `cli`) — no `Stress test:`, no further `FAT32:` lines, no framebuffer output.

### Command to see
Non-interactive screendump (works headless, no host display needed):
```bash
# boot with -monitor unix:/tmp/qmon.sock,server,nowait -display none, then:
python3 -c "
import socket, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/tmp/qmon.sock'); time.sleep(0.5); s.recv(4096)
s.sendall(b'screendump /tmp/panic_halt.ppm\n'); time.sleep(1)
"
```
See [screenshots/phase12_12.2_fat32init_halt.png](screenshots/phase12_12.2_fat32init_halt.png) — solid black, confirming the kernel halted before `fb_init()` ever ran. For live interactive confirmation instead: `make debug` against a similarly-corrupted `disk_images/gos_disk.img` copy.

---

## Milestone 12.3: `kfree` double-free detection

### Task: Add an `is_free` check before re-marking a block free
- **What was done:** `kfree()` (`kernel/src/heap.c`) validated the header/footer magic values but never checked whether the block was already free before setting `is_free = 1` and attempting forward coalescing. A double-free would pass both magic checks (the block is otherwise intact) and silently succeed.
- **The actual fix:** Added a check for `b->is_free` immediately after the footer-magic check, logging `"HEAP CORRUPTION: double free detected at kfree() - block already free"` and returning early (matching the existing style of the header/footer magic checks) instead of falling through to the free/coalesce logic.
- **Also added:** A permanent deliberate double-free self-test inside `heap_self_test()` (already called unconditionally at every boot), directly alongside the existing deliberate-overrun self-test — both now run on every boot as a standing regression guard, the same pattern the codebase already used for the overrun guard.

### Test: reproduce the original aliasing bug, then confirm the fix
- **Pre-fix reproduction:** Temporarily removed the `is_free` check and added a debug scenario (`p_a = kmalloc(16)`; write `0xAA`; `kfree(p_a)`; `kfree(p_a)` again (the bug); `p_b = kmalloc(16)`; write `0xBB` through `p_b`). Result: **`p_a == p_b` (aliased — the allocator handed the same block out twice), and `p_a[0]` read back as `0xBB` instead of the original `0xAA`** — a real, live-data corruption caused purely by the missing double-free check, exactly matching the audit's stated risk ("merge with a block that's since been reallocated and is live, silently corrupting the allocator for whoever now owns that memory"). The existing deliberate-double-free self-test assertion (added as part of this fix) correctly reported `"FAIL (double free was NOT detected)"` on this pre-fix build, confirming the test itself is sound.
- **Post-fix verification:** Same scenario with the fix in place: the second `kfree(p_a)` call is caught and logged (`"HEAP CORRUPTION: double free detected..."`), `heap_corruption_count()` increments by exactly 1, and the permanent self-test reports `"Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun and double-free)"`.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make clean && make run 2>&1 | grep -A3 "Heap self-test: deliberately double-freeing"
```
Expected:
```
Heap self-test: deliberately double-freeing a block to verify double-free detection...
HEAP CORRUPTION: double free detected at kfree() - block already free
Heap self-test: PASS (300 cycles clean, guard correctly detected deliberate overrun and double-free)
```
This runs on **every** normal boot now (no `#ifdef` needed) — it's a permanent regression guard, not a one-off repro.

### Command to see
This milestone is non-graphical by nature (heap internals, no visual surface) — matching the project's existing convention (Phase 2/3's exception and PMM self-tests were also serial-only). Confirm via:
```bash
make debug   # watch the "Heap self-test: ..." lines scroll in the -serial stdio output live, alongside the graphical QEMU window
```

---

## Milestone 12.4: FAT chain-walk cycle detection

### Task: Add a cycle/iteration bound to all unbounded chain-walk sites
- **What was done:** Five functions in `kernel/src/fat32.c` walked FAT cluster chains via `while (!is_end_of_chain(cluster))` or an outright `for (;;)` with no cap: `fat_list_dir`, `fat_read_file`, `fat_free_chain`, `find_dirent`, and `find_free_slot`. A cyclic or corrupted FAT chain (a cluster's FAT entry pointing back to an earlier cluster in its own chain) would hang any of these forever.
- **The actual fix:** Added a shared helper, `chain_step_limit_exceeded(uint32_t steps)`, bounded by `total_clusters` (the maximum number of distinct clusters a well-formed chain can visit before legitimately reaching end-of-chain). Every one of the five unbounded walks now checks this at the top of each loop iteration and bails out (logging `"FAT32: cyclic/corrupted cluster chain detected - aborting walk"`) instead of looping forever.

### Test: reproduce the original hang, then confirm the fix
- **Repro setup:** This took two iterations to get right. The first attempt (self-looping `LEVEL1` directory cluster) didn't actually exercise the bug — `fat_list_dir` hit a `0x00` end-of-directory marker within the first cluster and returned before ever needing a second cluster, regardless of the corrupted FAT entry. The working repro: filled `TESTDIR`'s single directory cluster entirely with 16 fake `ATTR_VOLUME_ID` entries (skipped/uncounted by `fat_list_dir`, so `count < max` never becomes false) and set `FAT[TESTDIR's cluster]` to point back to itself — forcing a genuine, unterminated cluster-to-cluster walk.
- **Pre-fix reproduction:** Temporarily stripped the five `chain_step_limit_exceeded` guards. Booted against the corrupted image with a 35-second wall-clock budget via the QEMU monitor. **Result: only `Timer tick:` lines kept incrementing — the kernel never progressed past the `TESTDIR` listing, confirmed via `info registers` showing the CPU still executing (not halted) at the same instruction pointer repeatedly.** A genuine, unrecoverable hang.
- **Post-fix verification:** Same corrupted image, fixed code. The cycle-detection message fired **twice** — once incidentally, via the pre-existing `TESTDIR/NESTED.TXT` boot-time read (which also walks `TESTDIR`'s chain and correctly failed with `"FAT32: PANIC - failed to read TESTDIR/NESTED.TXT"` instead of hanging), and once via the explicit `fat_list_dir(TESTDIR)` test call. **The kernel then proceeded through the entire rest of boot** — heap self-test, timer self-test, ATA/FAT32 init, ending in `"Stress test: PASS"` and a fully rendered desktop.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
python3 - <<'PYEOF'
import struct
path = "/tmp/gos_disk_cyclic.img"
import shutil; shutil.copy("disk_images/gos_disk.img", path)
with open(path, "rb") as f:
    boot = f.read(512)
bps = struct.unpack_from("<H", boot, 11)[0]; spc = boot[13]
res = struct.unpack_from("<H", boot, 14)[0]; nf = boot[16]
fsz = struct.unpack_from("<I", boot, 36)[0]; rc = struct.unpack_from("<I", boot, 44)[0]
fds = res + nf*fsz; fat_lba = res
root_lba = fds + (rc-2)*spc
with open(path, "rb") as f:
    f.seek(root_lba*bps); data = f.read(bps*spc)
tc = None
for i in range(0, len(data), 32):
    if data[i] == 0: break
    if data[i:i+11] == b'TESTDIR    ':
        tc = (struct.unpack_from("<H", data, i+20)[0] << 16) | struct.unpack_from("<H", data, i+26)[0]
td_lba = fds + (tc-2)*spc
buf = bytearray(bps*spc)
for e in range(bps*spc//32):
    off = e*32
    buf[off:off+8] = ("F%07d" % e).encode()[:8].ljust(8, b' ')
    buf[off+8:off+11] = b'TXT'; buf[off+11] = 0x08
with open(path, "r+b") as f:
    f.seek(td_lba*bps); f.write(bytes(buf))
    fo = tc*4; fs = fat_lba + fo//bps; ofs = fo % bps
    f.seek(fs*bps); sec = bytearray(f.read(bps))
    struct.pack_into("<I", sec, ofs, tc)
    f.seek(fs*bps); f.write(sec)
    f.seek((fs+fsz)*bps); f.write(sec)
print("cyclic scratch image ready at", path)
PYEOF
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_CYCLIC_CHAIN"
timeout 30 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=/tmp/gos_disk_cyclic.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | grep -A2 "cyclic\|TEST:\|Stress test"
```
Expected: `FAT32: cyclic/corrupted cluster chain detected - aborting walk` (twice), `TEST: fat_list_dir(TESTDIR) returned (0 entries) - did not hang`, and eventually `Stress test: PASS (150 file cycles, 300 window cycles, no crash)`.

### Command to see
```bash
make debug   # interactive; watch the desktop finish loading instead of hanging
```
Or capture a screendump after the full boot sequence completes against the cyclic image — a fully rendered desktop (not a frozen black/last-good frame) is the visual confirmation the hang was avoided.

---

## Milestone 12.5: IST for double-fault/NMI

### Task: Wire up TSS `ist1` and route vectors 2/8 through it
- **What was done:** `struct tss` in `kernel/src/gdt.c` already declared `ist1..7` but never initialized them; `idt_set_gate()` calls for every vector (`kernel/src/idt.c`) always passed `ist=0`. Added a dedicated static 16KiB `ist1_stack`, set `tss.ist1` to point at its top, and changed the IDT gate calls for vector 2 (NMI) and vector 8 (Double Fault) to use `ist=1`.

### Test: reproduce the original triple fault, then confirm the fix
- **Repro setup and a real diagnostic detour:** The first repro attempt (unbounded recursive stack growth) didn't trigger a fault at all within a practical time budget — `vmm_init()` identity-maps the *entire* first 4GiB, so there's no nearby unmapped guard page below the boot stack for naive recursion to hit; after 8 seconds and ~116,000 recursive calls, the kernel was still running, un-faulted. Switched to a deterministic approach: force `RSP` to a low address (`0x1000`) via inline assembly immediately before the same recursive function, so a handful of calls underflow past address 0 into genuinely unmapped/non-canonical memory.
  - A second false start: the test trigger was initially placed early in `_start()` (matching the existing `GOS_TEST_DIVIDE_BY_ZERO`/`GOS_TEST_PAGE_FAULT` pattern), *before* `fb_init()` ran. This caused the double-fault handler itself to appear to fail (no serial output, CPU frozen) — but debug prints proved `tss.ist1` and `idt[8].ist` were both correctly set. The real cause: `panic_screen()`, called from the exception handler for *any* unhandled exception including vector 8, needs the framebuffer initialized to draw into. Relocating the trigger to run *after* `fb_init()`/`fb_clear()` resolved it — this is a genuine ordering dependency worth documenting for any future exception-handling test.
- **Pre-fix reproduction:** Temporarily reverted both `idt_set_gate` calls back to `ist=0`. Ran with `-d int` interrupt tracing. **Result: vector 8 (double fault) was raised, but pushing its own exception frame immediately faulted again (`check_exception old: 0x8 new 0xe`) — a genuine triple fault.** No `"!!! CPU EXCEPTION !!!"` line, no panic screen, ever appeared in the serial log; `info registers` showed the CPU frozen (via `-no-reboot -no-shutdown`) at the exact faulting instruction, `RSP` still pointing at the same exhausted/invalid stack (`0xffffffffffffff60`) both before and during the failed double-fault delivery.
- **Post-fix verification:** Same repro, fixed code. Serial log shows a clean `"!!! CPU EXCEPTION !!! Vector: 8 (Double Fault)"` report followed by `"!!! KERNEL PANIC - drawing panic screen !!!"`. `info registers` after the fault shows `HLT=1` (cleanly halted in the panic screen's own `hlt` loop, not frozen mid-fault) and, critically, **`RSP=0xffffffff80016770` — inside `ist1_stack`'s bounds** (`top - 16384` to `top`, where `top = 0xffffffff80016950`) — direct proof the handler ran on the dedicated IST1 stack, not the exhausted one. A screendump confirms the red panic screen renders correctly: `"Exception: Double Fault, Vector: 0x8"`.

### Command to test
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_STACK_OVERFLOW"
timeout 15 qemu-system-x86_64 -M q35 -m 256M -no-reboot -no-shutdown \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 -serial stdio -display none | tail -12
```
Expected:
```
TEST: deliberately overflowing the stack to trigger a double fault...

!!! CPU EXCEPTION !!!
Vector: 8 (Double Fault)
Error code: 0x0000000000000000
RIP: 0x...
CS: 0x0000000000000008
RFLAGS: 0x...
RSP: 0x...
!!! System halted !!!

!!! KERNEL PANIC - drawing panic screen !!!
```
**What to check:** the `Vector: 8 (Double Fault)` block must appear at all — its *absence* (silence after the `TEST:` line, kernel just frozen) is exactly what the pre-fix build produces, since `-no-reboot` prevents QEMU from resetting on a triple fault but nothing is printed if the panic handler itself never runs.

### Command to see
```bash
make debug   # real graphical QEMU window; the red panic screen renders live
```
Non-interactive screendump: see [screenshots/phase12_12.5_doublefault_ist_panic.png](screenshots/phase12_12.5_doublefault_ist_panic.png) — shows `*** KERNEL PANIC ***`, `Exception: Double Fault`, `Vector: 0x8`.

**Restore the normal build afterward** (as with all `GOS_TEST_*` flags): `make clean && make iso`.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| 12.1 test: "create a zero-byte file... write data to it" | Also had to prove the *specific* corruption mechanism (cluster 0 walked as real data) via a raw before/after sector diff, not just confirm the write "succeeds" | The audit's literal underflow scenario (`existing_count == 0` reached via the while loop's exit condition) doesn't reproduce as written against the current code — `is_end_of_chain(0)` is `false`, so cluster 0 gets walked, not skipped. Needed to trace the actual path to design a fix that closes the real bug, not just the literally-described one |
| 12.4 test: single LEVEL1 self-loop repro | First attempt didn't exercise the bug at all (early-exit on a `0x00` directory marker); had to redesign the corrupted directory cluster (all-`VOLUME_ID` entries, no terminator) to force genuine cross-cluster traversal | `fat_list_dir`'s early-return-on-null-byte behavior meant a directory with few real entries never needs the corrupted second cluster regardless of the FAT corruption present |
| 12.5 test: recursive stack overflow, triggered early in boot (matching `GOS_TEST_DIVIDE_BY_ZERO`/`GOS_TEST_PAGE_FAULT` placement) | Relocated to after `fb_init()`; recursion approach also replaced with a direct RSP-underflow trampoline | `vmm_init()`'s blanket 4GiB identity mapping means naive recursion never hits an unmapped page in practical time; separately, `panic_screen()` (invoked by *any* unhandled exception) needs the framebuffer ready, so the test can't run before `fb_init()` the way the simpler exception tests can |

None of these changed the actual fixes delivered — all three Critical findings' root causes were confirmed and closed as originally scoped. The deviations were entirely in *how to reliably reproduce* each bug live, which is itself useful diagnostic information preserved here for anyone re-running these tests later.

---

## Effort Estimate vs. Actual

| | Original estimate (project-plan-2.md) | Actual |
|---|---|---|
| Milestone 12.1 | included in Phase 12's 10–15h | ~2 hours (incl. root-causing the real corruption path) |
| Milestone 12.2 | included in Phase 12's 10–15h | ~45 minutes |
| Milestone 12.3 | included in Phase 12's 10–15h | ~1 hour |
| Milestone 12.4 | included in Phase 12's 10–15h | ~2.5 hours (two repro redesigns) |
| Milestone 12.5 | included in Phase 12's 10–15h | ~3 hours (two repro redesigns + one real ordering bug found in the test harness itself) |
| **Phase 12 total** | **10–15 hours** | **~9.5 hours** |

**Why the repro work took longer than the fixes themselves:** For every Critical finding, writing the *fix* was straightforward (a few lines each); the time went into designing a live QEMU reproduction that actually exercises the described failure mode against the current code, rather than the literal scenario as written in the audit. Two of five milestones needed a redesigned repro after the first attempt silently failed to trigger the bug, and one (12.5) surfaced a genuine ordering dependency (`panic_screen()` needs `fb_init()`) that would otherwise have gone unnoticed. This is consistent with the plan's own instruction to verify against "a QEMU-based check that exercises the specific failure condition where feasible" rather than accepting "code compiles" as sufficient.

---

## Per-Milestone Testing Instructions

See each milestone's **Command to test** / **Command to see** sections above — they're self-contained and copy-pasteable from the project root (`cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`).

### Quick regression check for all of Phase 12 together
```bash
make clean && make run 2>&1 | grep -E "PANIC|EXCEPTION|Stress test:|Desktop ready"
```
Expected — a clean, normal boot with none of the Phase 12 test triggers defined:
```
Stress test: PASS (150 file cycles, 300 window cycles, no crash)
Desktop ready - click the "Files" icon to launch the File Manager
```
No `PANIC` or `EXCEPTION` lines should appear. All five `GOS_TEST_*` macros added/used in this phase (`GOS_TEST_ZEROLEN_WRITE`, `GOS_TEST_CYCLIC_CHAIN`, `GOS_TEST_STACK_OVERFLOW`) are `#ifdef`-gated and compile out entirely in a normal build — this confirms none of the test scaffolding leaked into the default build path.

---

## Not Yet Verified (intentionally deferred to Phase 13+)

- [ ] The 6 High-severity findings from `audit.md` (ATA write ERR/DF checking, window drag clamping, `heap_grow` free-block check, `create_entry` rollback, `vmm_unmap_page`, stale-window dispatch) — Phase 13
- [ ] The 12 Medium/Low findings — Phase 14
- [ ] README.md update reflecting Track A progress — first task of Phase 14 per project-plan-2.md, a living task re-touched at the end of Track A and again at the end of Track B

---

## Next Step

Proceed to **Phase 13 — High-Severity Audit Fixes**, per [project-plan-2.md](project-plan-2.md). Two of Phase 13's fixes are explicitly scoped alongside functions Phase 12 already touched or that Phase 16 (Track B) will build on: Milestone 13.6 (stale-window-after-close dispatch fix) shares `window_close()` with Phase 16's window-teardown work, and both this phase's `kfree` double-free fix (12.3) and Phase 13's dispatch fix are named as explicit Track-B blockers in project-plan-2.md's dependency section.
