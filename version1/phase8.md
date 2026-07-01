# Phase 8 — Filesystem (FAT32) — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all three milestones (8.1, 8.2, 8.3) done, all tasks verified.

---

## Summary

Phase 8 gave gOS a real, persistent filesystem — not a RAM disk or synthetic in-memory structure, but a genuine FAT32 volume on a virtual hard disk, readable and writable by both gOS itself and standard host tools (`mtools`), which is exactly what makes this phase's testing trustworthy: every claim below was cross-checked against an independent, trusted FAT32 implementation, not just gOS reading back its own writes. This is the plan's own second explicitly-flagged high-risk-for-silent-corruption phase (write support specifically), and it was treated with the corresponding care: all destructive testing happened against a disposable scratch copy of the disk image first, exactly as the plan itself recommends, before confirming the same behavior against the real disk image.

No screenshots were needed for this phase — all of Phase 8's output is textual (serial logs) and file-content verification (host-side `mtools`), consistent with how Phases 0-4 (also text-only phases) were documented.

---

## Milestone 8.1: Disk/block device access

### Task: Decide disk access path
- **What was done:** Used the plan's own explicit recommendation — ATA PIO over AHCI, for v1 simplicity. This required solving an environment problem first: QEMU's `q35` machine type (used by every prior phase for its modern chipset/AHCI-by-default setup) does **not** include a legacy IDE controller by default, which ATA PIO needs. Rather than switching the whole project to the older `pc`/`i440fx` machine type (which would have disrupted the UEFI/OVMF setup every previous phase depends on), added a legacy `piix3-ide` controller device explicitly alongside `q35`, with the disk attached to it (`-device piix3-ide,id=ide -drive id=gosdisk,file=...,if=none,format=raw -device ide-hd,drive=gosdisk,bus=ide.0`) — verified this combination boots cleanly with no device-conflict errors before writing any driver code.
- **Outcome:** A working legacy PATA/IDE disk attached at the classic ports (0x1F0-0x1F7, 0x3F6) exactly as OSDev Wiki's ATA PIO documentation assumes, without disrupting the existing UEFI boot path.
- **Issues:** None, once the machine-type/controller compatibility question was resolved upfront via a quick verification boot.

### Task: Implement `ata_read_sector`/`ata_write_sector`
- **What was done:** Wrote `kernel/include/ata.h` / `kernel/src/ata.c`. Standard 28-bit LBA PIO mode: select the drive/head with the LBA's top 4 bits, set sector count to 1, write the LBA's low/mid/high bytes, issue the READ SECTORS (0x20) or WRITE SECTORS (0x30) command, poll status for BSY-clear then DRQ-set, then `insw`/`outsw` 256 words (512 bytes) to/from the data port. Writes are followed by a CACHE FLUSH (0xE7) command and a final BSY-clear wait, ensuring data is actually committed before the function returns (not just handed to a write-behind cache that might not survive a subsequent reboot's memory reset — always true in a shared-nothing VM, but the right habit regardless).
- **Outcome:** Both functions work correctly, confirmed by the tests below and extensively by Milestone 8.3's write testing.
- **Issues:** None.

### Task: Test — read sector 0, log signature
- **What was done:** Added a boot-time test: `ata_init()` followed by `ata_read_sector(0, buffer)`, logging the first 16 bytes and the boot-signature bytes (offsets 510-511) to serial.
- **Outcome:** Verified live: boot signature reads exactly `0x55 0xAA` (the universal boot-sector-valid marker), and the first three bytes read `0xEB 0x58 0x90` — the classic x86 short-jump-plus-NOP instruction every FAT boot sector begins with, followed by readable ASCII bytes matching `mtools`' own OEM ID string. This is strong evidence the raw byte-level read is correct, not just "some bytes came back."
- **Issues:** None.

---

## Milestone 8.2: FAT32 read support

### Task: Parse the FAT32 BPB
- **What was done:** Wrote `kernel/include/fat32.h` / `kernel/src/fat32.c`. `fat32_init()` reads sector 0 (via Milestone 8.1's driver), validates the `0x55AA` signature and the `"FAT32   "` filesystem-type string at the documented offset, and extracts `bytes_per_sector`, `sectors_per_cluster`, `reserved_sector_count`, `num_fats`, `fat_size_32`, and `root_cluster` from their documented byte offsets in the BPB structure.
- **Outcome:** Verified live: all parsed values are sane and match what `mformat` (the tool that created the test disk) would be expected to produce for a 64MiB volume — `bytes/sector=512`, `sectors/cluster=1`, `num_fats=2`, `fat_size=1009`, `root_cluster=2`.
- **Issues:** None.

### Task: Cluster-to-LBA translation and FAT chain traversal
- **What was done:** `cluster_to_lba()` implements the standard `first_data_sector + (cluster - 2) * sectors_per_cluster` formula. `fat_get_next_cluster()` reads the correct FAT sector for a given cluster's 4-byte (28-bit, top 4 bits reserved/ignored) entry and returns the next cluster in the chain, or a value `>= 0x0FFFFFF8` signaling end-of-chain.
- **Outcome:** Correctness confirmed indirectly through every subsequent file read/write, which depends entirely on this translation being right.
- **Issues:** None.

### Task: Root directory listing (8.3 names)
- **What was done:** `fat_list_dir()` walks a directory's cluster chain, parsing 32-byte directory entries, skipping deleted entries (`0xE5`), VFAT long-name entries (`attr == 0x0F`, explicitly deferred per the plan's own allowance), and volume-label entries, and formatting valid 8.3 names (trimming trailing spaces from the 8-character name and 3-character extension, joining with a `.` only if an extension exists) into normal human-readable strings.
- **Outcome:** Verified live: gOS's listing of the root directory (`HOSTFILE.TXT size=53`, `TESTDIR <DIR>`) matches **exactly** the output of the host's own `mdir` command run against the same disk image — an independent cross-check, not just "gOS agrees with itself."
- **Issues:** None.

### Task: Implement `fat_read_file`
- **What was done:** `fat_resolve_path()` splits a `/`-separated path into components and walks them one directory level at a time (via `fat_list_dir` + case-insensitive name matching), supporting arbitrary nesting depth, not just single-level root files — a deliberate small scope extension beyond the plan's literal wording, since it costs almost nothing extra given `fat_list_dir` already exists, and directly benefits Phase 9's file manager (which will need to navigate into subfolders). `fat_read_file()` resolves the path, then reads the file's cluster chain into the caller's buffer.
- **Outcome:** Works correctly for both root-level and nested files (see the test below).
- **Issues:** None.

### Task: Test — read a known host-created file, verify bytes
- **What was done:** Created a FAT32 disk image on the host using `mtools` (`mformat` to create the filesystem, `mcopy`/`mmd` to add a root-level file `HOSTFILE.TXT` and a nested file `TESTDIR/NESTED.TXT`, both with known text content). Booted gOS against this disk and logged the results of reading both files.
- **Outcome:** Verified live: `HOSTFILE.TXT` (53 bytes) and `TESTDIR/NESTED.TXT` (21 bytes) both read back **exactly** the text originally written from the host side, byte-for-byte, including the trailing newline `echo` added — proving both flat-file reads and nested-directory-traversal reads are correct.
- **Issues:** None.

---

## Milestone 8.3: FAT32 write support

This is the plan's own explicitly-flagged highest-risk-for-silent-corruption milestone. Every test below was run first against a **disposable scratch copy** of the disk image (`gos_disk_scratch.img`, an exact copy of the golden `gos_disk.img`), per the plan's own explicit dependency-note guidance, before any conclusion was drawn.

### Task: Free-cluster scanning
- **What was done:** `fat_alloc_cluster()` scans the FAT linearly starting at cluster 2 (the first valid data cluster) for an entry with value `0` (free), marks it end-of-chain (`0x0FFFFFFF`) to claim it, and returns its number. `total_clusters` (needed as the scan's upper bound) is computed at `fat32_init()` time from the BPB's total-sector count.
- **Outcome:** Works correctly for every allocation performed during testing (single-cluster files, directory-extension allocations).
- **Issues:** None. **Scope note:** linear scanning is O(n) per allocation, which is fine for the small test volumes and file counts in this phase's scope; a production filesystem driver would typically cache the next-free-cluster hint, but that's an optimization, not a correctness requirement, and was deliberately not added to avoid scope creep this phase doesn't need.

### Task: Implement `fat_create_file`
- **What was done:** `create_entry()` (shared by `fat_create_file` and `fat_create_dir`) splits the path into parent-directory and filename, resolves the parent (must already exist), confirms the name doesn't already exist, allocates one initial data cluster, finds a free directory-entry slot (extending the parent directory with a new cluster if none exists — directories can grow, not just files), converts the name to 8.3 format (simple uppercase + 8+3 truncation, no long-name generation, consistent with the read side's VFAT-deferral), and writes the new 32-byte directory entry. `fat_create_file` additionally zeroes the newly allocated cluster, so a freshly-created, unwritten file reads back as empty rather than exposing whatever stale bytes happened to be on disk.
- **Outcome:** Verified live (see the combined test below).
- **Issues:** None.

### Task: Implement `fat_write_file`
- **What was done:** Resolves the file's existing directory entry and its exact on-disk location (sector + byte offset, needed to patch it later), walks its current cluster chain, and grows it (allocating and linking new clusters) or truncates it (freeing excess clusters, capping the chain with an end-of-chain marker) to fit the new size exactly. Writes the data cluster-by-cluster (zero-padding the last cluster's unused tail for cleanliness). Finally, re-reads the directory entry's sector fresh (rather than trusting a potentially-stale in-memory copy) and patches only the `first_cluster` and `file_size` fields before writing the sector back — a deliberately narrow, targeted patch rather than reconstructing the whole entry, minimizing the chance of accidentally clobbering unrelated fields (timestamps, attributes) that weren't meant to change.
- **Outcome:** Verified live and independently via host-side `mtools` (see below).
- **Issues:** **Bug caught during self-review before testing, not by a failed test.** An early draft of this function contained a leftover debug/placeholder line — `ata_read_sector(loc.sector_lba, (uint8_t *)&raw)` where `raw` was a 32-byte `struct fat_dir_entry_raw`, but `ata_read_sector` always writes a full 512-byte sector into whatever buffer it's given. This would have overflowed 480 bytes past the end of a 32-byte stack variable on every single file write - a serious stack-corruption bug. Caught while reviewing the code immediately after writing it (before the first build/test), and removed; the very next lines already correctly used a proper 512-byte sector buffer for the real read/patch/write-back logic, so the fix was simply deleting the stray leftover line, not redesigning anything.

### Task: Implement `fat_delete_file`
- **What was done:** Resolves the file's directory entry and location, refuses to proceed if it's actually a directory, frees its entire cluster chain (walking and zeroing each FAT entry), then marks the directory entry deleted by setting its first name byte to `0xE5` (the standard FAT deleted-entry marker).
- **Outcome:** Verified live and independently via `mtools` (deleted files no longer appear in `mdir` output).
- **Issues:** None.

### Task: Implement `fat_create_dir`/`fat_delete_dir`
- **What was done:** `fat_create_dir` reuses `create_entry()` (same as file creation) with the `DIRECTORY` attribute, then writes two synthetic entries into the new cluster: a `.` entry pointing at the new directory's own cluster, and a `..` entry pointing at its parent's cluster — matching what every real FAT32 directory contains. `fat_delete_dir` resolves the directory, lists its contents via the existing `fat_list_dir`, and explicitly checks that the only entries present are `.` and `..` (refusing to delete a non-empty directory, silently preventing an entire subtree's worth of orphaned clusters that would otherwise leak as unreachable-but-still-marked-used space) before freeing its cluster chain and marking it deleted the same way a file is.
- **Outcome:** Verified live and independently via `mtools` (the created directory's `.`/`..` entries are visible and correct in `mdir` output; after deletion, the directory and its cleaned-up contents are both gone).
- **Issues:** None.

### Task: Test — create, write, reboot, read back, confirm persistence + disk integrity
- **What was done:** This is the phase's central test, executed carefully in stages:
  1. Made a disposable copy of the disk image (`gos_disk_scratch.img`) before any write testing, per the plan's explicit guidance.
  2. **First boot** against the scratch disk: created `PERSIST.TXT` (a file deliberately never deleted, to test persistence), wrote 35 bytes of known text to it, read it back (matched exactly), then ran a full create→write→read→delete cycle on a separate temporary file (`TEMP.TXT`) and a temporary directory with a nested file (`TEMPDIR/INNER.TXT`), confirming every step's return value and content, and confirming `TEMP.TXT` was genuinely unresolvable (`fat_resolve_path` returned 0) immediately after deletion.
  3. **Independent host-side verification**, before any second boot: ran `mdir`/`mtype` directly against the scratch disk image. Confirmed `PERSIST.TXT` exists with exactly 35 bytes and the exact expected text content, `TESTDIR`/`NESTED.TXT`/`HOSTFILE.TXT` (never touched by any write operation) are completely unaffected, and there is no leftover trace of `TEMP.TXT` or `TEMPDIR` — proving the write/delete operations didn't corrupt anything, using a completely independent FAT32 implementation as the judge, not gOS checking its own homework.
  4. **Second boot**, same scratch disk image, no reformatting: `fat_create_file("PERSIST.TXT")` correctly returned failure (`0`, "already exists" — proving gOS itself recognizes the file survived), and the immediately-following read returned the **exact same 35-byte content** written during the first boot session — this is the actual persistence-across-reboot proof, since the content could only have come from disk, not from any in-memory state (a fresh QEMU process has no memory of the prior boot). The same `TEMP.TXT`/`TEMPDIR` create-delete cycle was repeated and produced identical, clean results the second time, proving the write/delete logic is correctly repeatable and doesn't accumulate any state that would corrupt a later run.
  5. Confirmed the original **golden** disk image (`gos_disk.img`, untouched throughout the scratch-disk testing above) still had its pristine original 2-entry listing and a different MD5 checksum than the scratch copy, proving the scratch-first testing methodology genuinely protected it from any risk during development.
- **Outcome:** All of the above passed cleanly, both from gOS's own perspective and from the independent host-tool perspective. This is the strongest verification standard applied anywhere in this project so far — not just "gOS says it worked," but "an unrelated, trusted program agrees the disk is valid and contains exactly the expected bytes."
- **Issues:** None beyond the pre-test code-review catch described under `fat_write_file` above.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| (implicit) QEMU disk attachment method unspecified | Added an explicit `piix3-ide` controller alongside the existing `q35` machine type, rather than switching machine types | `q35`'s default AHCI-only chipset has no legacy IDE controller ATA PIO needs; adding the controller explicitly avoided disrupting the UEFI/OVMF boot setup every prior phase depends on |
| `fat_read_file(path, buffer)` — root-level implied | Supports arbitrary path nesting depth (e.g. `TESTDIR/NESTED.TXT`) | `fat_list_dir` already existed as a building block; extending to multi-level paths cost little extra code and directly benefits Phase 9's file manager, which needs folder navigation |
| (implicit) directory-entry patching approach | Re-reads the entry's sector fresh and patches only the specific fields that changed (size, first cluster), rather than reconstructing/overwriting the whole 32-byte entry from a possibly-stale copy | Minimizes the risk of accidentally clobbering fields (timestamps, attributes) that should be left alone — a deliberate defensive choice given this is the highest-corruption-risk milestone |

None of these required revisiting later-phase plans. The one real bug found (the `fat_write_file` stack-overflow leftover) was caught during self-review before the first build, not during testing — worth noting as evidence that careful code review remains valuable even with a strong test-everything discipline.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 8.1 | ~4–6 hours | ~1 hour (including resolving the q35/IDE controller question) |
| Milestone 8.2 | ~6–8 hours | ~1.5 hours |
| Milestone 8.3 | ~6–8 hours | ~2 hours (including the thorough scratch-disk-first, host-tool-verified testing regimen) |
| **Phase 8 total** | **16–22 hours** | **~4.5 hours** |

**Why actual was faster than estimated:** FAT32's on-disk format is precisely and unambiguously documented (unlike, say, Phase 6's windowing system, which had genuine design freedom), so implementation was mostly "translate the spec's byte offsets into struct fields and follow the documented algorithms" rather than open-ended design work. The availability of `mtools` (already installed since Phase 0, anticipated exactly for this purpose in the original resources list) made both test-disk creation and, critically, independent verification of write correctness fast and reliable — without a trusted second implementation to cross-check against, verifying "is the disk still valid" would have required either trusting gOS's own read path (circular, and exactly the kind of self-verification the plan warns isn't sufficient for this milestone) or manually parsing raw sectors by hand.

**Revised estimate guidance for future phases:** Phase 9 (File Manager UI) is a comparatively low-risk phase — a UI layer over already-solid Phase 6 (windowing) and Phase 8 (filesystem) infrastructure, per the plan's own dependency note ("this entire phase is a thin UI layer... should be the fastest of the 'hard' phases"). Expect a similar speed pattern to this phase and Phase 7: fast, because the hard infrastructure underneath is already proven.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**Setup (once):** the Makefile's `disk` target creates a 64MiB FAT32 disk image via `mtools` if one doesn't already exist. Populate it with the test files these instructions expect:
```bash
make disk
echo "Hello from the host! This is a FAT32 read test file." > /tmp/hostfile.txt
mcopy -i disk_images/gos_disk.img /tmp/hostfile.txt ::/HOSTFILE.TXT
mmd -i disk_images/gos_disk.img ::/TESTDIR
echo "Nested file content." > /tmp/nested.txt
mcopy -i disk_images/gos_disk.img /tmp/nested.txt ::/TESTDIR/NESTED.TXT
mdir -i disk_images/gos_disk.img ::
```

### Milestone 8.1 — Disk/block device access

**Command to test (verify sector 0 read + boot signature):**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep "ATA:"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output:
```
ATA: primary master initialized (PIO, ports 0x1F0-0x1F7/0x3F6)
ATA: sector 0 read OK. First 16 bytes: 0x...eb 0x...58 0x...90 ...
ATA: boot signature (bytes 510-511): 0x...55 0x...aa (valid 0x55AA)
```
**What to check:** the boot signature must read `0x55` `0xaa` exactly, and the first byte must be `0xeb` (a short jump opcode) — any other value at byte 0 means either the disk isn't attached correctly or the sector-read logic has a bug.

### Milestone 8.2 — FAT32 read support

**Command to test (BPB parsing + directory listing + file reads):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -A6 "FAT32: bytes/sector"
rm -f /tmp/OVMF_VARS_test.fd
```
Expected output (adjust file names/dates if you populated the disk differently):
```
FAT32: bytes/sector=512 sectors/cluster=1 num_fats=2 fat_size=1009 root_cluster=2 fs_type=FAT32    (valid FAT32)
FAT32: root directory (2 entries):
  HOSTFILE.TXT size=53
  TESTDIR <DIR>
FAT32: read HOSTFILE.TXT (53 bytes): "Hello from the host! This is a FAT32 read test file.
"
FAT32: read TESTDIR/NESTED.TXT (21 bytes): "Nested file content.
```
**Command to see (independent cross-check against the host's own FAT32 tool):**
```bash
mdir -i disk_images/gos_disk.img ::
mtype -i disk_images/gos_disk.img ::/HOSTFILE.TXT
```
**What to check:** the file names, sizes, and byte contents gOS reports must match `mdir`/`mtype`'s output exactly. Any mismatch (wrong size, garbled text, missing entries) indicates a BPB parsing or cluster-chain-following bug.

### Milestone 8.3 — FAT32 write support

**⚠️ Always test against a disposable copy first**, per the plan's own explicit guidance for this highest-corruption-risk milestone:
```bash
cp disk_images/gos_disk.img disk_images/gos_disk_scratch.img
```

**Command to test (first boot — create/write/delete cycle):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_w1.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_w1.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk_scratch.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep "FAT32:"
rm -f /tmp/OVMF_VARS_w1.fd
```
Expected output includes:
```
FAT32: fat_create_file(PERSIST.TXT) = 1 (created new)
FAT32: fat_write_file(PERSIST.TXT) = 1 (OK)
FAT32: read PERSIST.TXT (35 bytes): "This file persists across reboots!"
FAT32: TEMP.TXT created+written+read: "temporary"
FAT32: fat_delete_file(TEMP.TXT) = 1 (OK)
FAT32: TEMP.TXT still resolvable after delete = 0 (correctly gone)
FAT32: fat_create_dir(TEMPDIR) = 1 (OK)
FAT32: TEMPDIR/INNER.TXT created+written+read: "nested write test"
FAT32: cleanup - delete INNER.TXT=1 delete TEMPDIR=1
```

**Command to see (independent host-side integrity check — the real proof):**
```bash
mdir -i disk_images/gos_disk_scratch.img ::
mtype -i disk_images/gos_disk_scratch.img ::/PERSIST.TXT
mdir -i disk_images/gos_disk_scratch.img ::/TESTDIR
mtype -i disk_images/gos_disk_scratch.img ::/HOSTFILE.TXT
```
**What to check:** `PERSIST.TXT` (35 bytes) must appear with exactly the text `This file persists across reboots!`; `TESTDIR` must still contain only `NESTED.TXT` unaffected; `HOSTFILE.TXT`'s content must be completely unchanged; there must be **no** `TEMP.TXT` or `TEMPDIR` remaining (both were deleted).

**Command to test (second boot — persistence across reboot, same scratch disk, do NOT reformat):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_w2.fd
timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_w2.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk_scratch.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep "FAT32:"
rm -f /tmp/OVMF_VARS_w2.fd
```
**What to check:** this time `fat_create_file(PERSIST.TXT)` must report `0 (already exists)` — **not** `1 (created new)` — and the very next line must still read back the exact same `"This file persists across reboots!"` content. This combination (creation correctly refused + content still correct) is the actual proof of on-disk persistence; if it instead shows `1 (created new)`, either the disk was accidentally reformatted between boots, or persistence is broken.

**Cleanup after testing:**
```bash
rm -f disk_images/gos_disk_scratch.img
```
The golden `disk_images/gos_disk.img` is untouched by any of the above (all destructive testing targeted the `_scratch` copy only).

### Full Phase 8 regression check — everything together in one boot
```bash
make clean && make iso && echo "BUILD OK"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|ATA:|FAT32:|boot checks complete"
rm -f /tmp/OVMF_VARS_full.fd
```
Expected: all PMM/heap `PASS` lines, valid ATA/FAT32 output as above, no `FAIL`/`PANIC`/`EXCEPTION` anywhere, ending with `=== gOS boot checks complete ===`. Note: running this against `gos_disk.img` (not a scratch copy) will add `PERSIST.TXT` to the golden disk permanently going forward — this is expected and fine for routine regression checks (the golden disk is gitignored and just a local dev artifact), but use a scratch copy if you specifically need to preserve a pristine 2-entry disk for a later comparison.

**To watch the whole OS live** (not through automated headless commands), use `-display cocoa` with the disk attached, exactly as established in Phase 5/6/7:
```bash
qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display cocoa -serial stdio
```
This shows the full boot sequence (graphics test pattern, bouncing rectangle, "Hello, gOS!", the three windows with real titles and a typeable text box) in a real window, while the serial log — including this phase's `ATA:`/`FAT32:` lines — streams in the same terminal.

---

## Not Yet Verified (intentionally deferred to Phase 9+)

- [ ] VFAT long filenames — only 8.3 short names are supported; a long-named file created by another OS would be invisible to gOS's directory listing (long-name entries are explicitly skipped, not misread)
- [ ] Multiple simultaneous open file handles / concurrent writers — each `fat_read_file`/`fat_write_file` call is a fully self-contained operation; there's no concept of a persistently "open" file across multiple kernel operations yet
- [ ] Files or directories larger than 256 clusters — `fat_write_file`'s cluster-chain-walking uses a fixed 256-entry stack array; at 512 bytes/cluster that's a 128KiB per-file ceiling on this test disk's geometry (ample for the text files this project's file manager/editor phases will handle, but a real-world limit worth knowing)
- [ ] Disk error recovery — a failed sector read/write currently just returns failure up the call stack; there's no retry logic or bad-sector handling
- [ ] FSInfo sector usage — FAT32's optional FSInfo sector (which caches the free-cluster count/next-free hint) is parsed by nothing here; `fat_alloc_cluster`'s linear scan doesn't need it, but a future optimization pass could use it to speed up allocation on larger, fuller disks

---

## Next Step

Proceed to **Phase 9 — File Manager UI** (Milestone 9.1: File manager window shell), per [PROJECT_PLAN.md](PROJECT_PLAN.md). Per the plan's own dependency note, this phase is "a thin UI layer over Phase 8's FAT32 read/write logic" — with that logic now solid and independently verified, Phase 9 should mainly involve wiring Phase 6's window/button widgets to the `fat_list_dir`/`fat_read_file`/etc. functions built in this phase.
