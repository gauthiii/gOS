# Phase 23 — FAT32 Long Filename (VFAT) Support — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — both milestones landed with no bugs found in testing (design decisions were clarified with the user upfront, before implementation, rather than discovered as bugs during testing).

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Open the File Manager ("Files" desktop icon). Long filenames (up to 63 characters, ASCII) now read, display, create, rename, and delete correctly — both files seeded from the host via `mtools` and files created directly through the File Manager's New File/Rename dialogs.

---

## Summary

Before implementing, four genuinely underspecified parts of the plan were clarified with the user via `AskUserQuestion`:
1. **Max long-filename length** — 64 characters (comfortably covers realistic test names, needs only 5 LFN directory entries per file).
2. **Character set** — ASCII only, encoded as UTF-16LE with a zero high byte (valid VFAT; gOS's keyboard driver can't produce non-ASCII input anyway).
3. **Short-alias generation** — the common-case `~1`..`~9` incrementing-suffix scheme, not the full spec's rare hash-based fallback for more than 9 colliding basenames.
4. **Rename scope** — the Rename dialog also gains long-name support (reuses the same LFN-writing path as create), not left as an 8.3-only gap.

- **23.1 (LFN read)** — `fat_list_dir`/`find_dirent` in `kernel/src/fat32.c` now track a running "LFN parse state" while scanning raw 32-byte directory entries: consecutive attribute-`0x0F` entries (VFAT long-name entries) are accumulated in on-disk order (which is *reverse* name order — the entry holding the tail of the name, marked with the `0x40` "last" bit, appears first), and reconstructed into a full name the moment a short-name entry is reached whose checksum matches the accumulated LFN run. If the checksum doesn't match (orphaned/corrupt LFN entries), it falls back to the plain 8.3 name exactly as before.
- **23.2 (LFN write)** — `write_named_entry()` decides per-name whether the 8.3 form round-trips losslessly (`name_needs_lfn()`: no lowercase, no spaces, at most one dot, base ≤8 chars, extension ≤3 chars). If not, `generate_short_alias()` produces a unique `BASENAM~N.EXT` alias (checked against existing directory entries via the now-LFN-aware `find_dirent`), and the long name is packed into the necessary UTF-16LE LFN entries (`lfn_pack_chars`), written contiguously before the short-name entry via a generalized `find_free_slot_n()` that can locate (or grow a fresh cluster for) a run of N contiguous free slots instead of just one.
- **Delete** (`fat_delete_file`/`fat_delete_dir`) now erases every LFN entry in the matched name's span, not just the short-name entry, via `erase_dirent_and_lfn()` — preventing orphaned LFN entries with stale checksums from confusing a later listing.
- **Rename** (`fat_rename`) writes the new named entry *before* erasing the old one, so a failure partway (disk full, all of `~1`..`~9` colliding) leaves the original entry completely untouched instead of destroying it and then failing to write its replacement.

Files touched (modified only — no new files): `kernel/include/fat32.h` (`FAT32_NAME_MAX` 13→65), `kernel/src/fat32.c` (~350 new/changed lines: LFN parsing, alias generation, generalized free-slot search, rewritten create/delete/rename), `kernel/src/start.c` (two new `GOS_TEST_LFN`/`GOS_TEST_LFN_WRITE` debug hooks, following the existing `GOS_TEST_RTC` convention). `kernel/src/fm.c` needed **zero** changes — every filename buffer in the File Manager (row display, rename/delete target, path breadcrumb stack) is already sized off `FAT32_NAME_MAX`, so raising that one constant grew the whole UI path automatically.

---

## Milestone 23.1: LFN read support

- **What was done:** `fat_list_dir` and `find_dirent` share a new `struct lfn_parse_state` (`kernel/src/fat32.c`) that tracks an in-progress run of long-name entries while scanning a directory. Each `0x0F`-attribute entry is checksum- and sequence-validated against the ones before it (`lfn_parse_step`); when the following short-name entry's own 11-byte-name checksum (`lfn_checksum`) matches, `lfn_parse_finish` returns the reconstructed name instead of the classic 8.3 form.
- **Test:** seeded a scratch copy of the disk image via host-side `mcopy` with a filename longer than 8.3 (`"a much longer file name.txt"`), booted gOS with a temporary `GOS_TEST_LFN` debug hook that calls `fat_list_dir` on the root and logs every entry's name to serial, and separately opened the File Manager in a normal (non-debug) build to see it rendered on screen — cross-checked against `mdir`'s own long-name output on the same image.
- **Result:** serial log showed `TEST: LFN entry [a much longer file name.txt]` alongside all 11 pre-existing 8.3-named files, exactly matching `mdir`'s output (`AMUCHL~1 TXT ... a much longer file name.txt`) — mtools' own independently-generated short alias (`AMUCHL~1`) also happens to match gOS's own alias-generation scheme byte-for-byte when gOS later creates files, confirming the two implementations agree on the same VFAT convention. The File Manager screenshot (below) shows the long name rendered correctly in the live UI, not just in a debug log.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
echo "hello lfn test file" > /tmp/lfntest.txt
mcopy -i disk_images/gos_disk.img /tmp/lfntest.txt "::a much longer file name.txt"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_LFN"
S=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST: LFN" $S/serial.log
# Independent host-side cross-check:
mdir -i disk_images/gos_disk.img ::
```
Expected: `TEST: LFN entry [a much longer file name.txt]` in the serial log, and `mdir`'s output shows the same long name against alias `AMUCHL~1.TXT`.

**Command to see:**
```bash
make iso disk build/OVMF_VARS.fd   # normal (non-test) build
make run
# In the desktop, click the "Files" icon - the long-named file(s) appear
# in the listing exactly as typed, not truncated to 8.3.
```
Screenshot: [screenshots/phase23_lfn_read.png](screenshots/phase23_lfn_read.png) — File Manager listing showing `a much longer file name.txt` and other long-named files rendered in full alongside the classic 8.3-named files, all from a single real (non-debug) boot.

---

## Milestone 23.2: LFN write support

- **What was done:** `write_named_entry()` is the new shared entry point for both create and rename. `name_needs_lfn()` decides if the plain 8.3 path suffices; if not, `generate_short_alias()` strips spaces/dots, uppercases, and tries `BASENAM~1.EXT` through `~9` (checked for collision via the LFN-aware `find_dirent`), and `find_free_slot_n()` locates (or grows a fresh, guaranteed-free cluster for) a contiguous run of slots sized to fit the LFN entries plus the short-name entry. `fat_delete_file`/`fat_delete_dir` erase the whole span (`erase_dirent_and_lfn`) instead of just the short-name entry. `fat_rename` writes the new entry *before* erasing the old one, preserving a safe failure ordering.
- **Test — four independent layers, matching the milestone's own "create via File Manager, verify via `mdir` in a separate check" requirement, extended to also cover rename and delete:**
  1. **Programmatic round-trip** (`GOS_TEST_LFN_WRITE` debug hook): `fat_create_file` → `fat_write_file` → `fat_rename`, all with long names, in one boot.
  2. **Independent host-side check, outside gOS entirely:** `mdir`/`mtype` on the disk image immediately after that boot exited.
  3. **Real File Manager UI, driven by simulated keystrokes/mouse clicks** (not the debug hook): opened the New File dialog, typed `"my interactive test.txt"` character-by-character via QEMU monitor `sendkey`, clicked OK; then selected that file, clicked Rename, backspaced the prefilled name, typed `"my renamed interactive test.txt"`, clicked OK; then selected it again and clicked Delete → confirmed OK.
  4. **`mdir` re-run after every UI step**, cross-checking on-disk state independently of gOS's own serial log or on-screen rendering.
- **Result:**
  - Debug-hook round-trip: `fat_create_file(long) = OK`, `fat_write_file(long) = OK`, `fat_rename(long->long) = OK`; `mdir` immediately after showed exactly the renamed file (`ARENAM~1.TXT` → `a renamed long filename.txt`) with **no** orphaned entry for the original creation name — confirming rename's write-then-erase ordering left no residue.
  - Real UI round-trip: serial log shows `FM: fat_create_file("my interactive test.txt") = ... Timer tick: 1100`, then the File Manager's own listing refreshed live to show the new file with **no reboot involved**. `mdir` independently showed `MYINTE~1 TXT ... my interactive test.txt` immediately after.
  - UI rename: `FM: fat_rename("my interactive test.txt", "my renamed interactive test.txt") = 1 (OK)`; the File Manager's listing updated in place to the new name; `mdir` confirmed exactly 15 files total (no leaked old-name entry, no leaked LFN remnants).
  - UI delete: `FM: fat_delete_file("my renamed interactive test.txt") = 1 (OK)`; `mdir` confirmed exactly 14 files afterward — the file and **all** of its LFN entries were gone, not just its short-name entry.
  - `make diagnostic`'s full regression suite was re-run after these changes (150 file create/write/rename/delete cycles + 300 window cycles, all using classic 8.3 names) and still reports `Stress test: PASS` — the LFN rewrite of `create_entry`/`find_dirent`/`find_free_slot` didn't regress the plain 8.3 path Finding #13.4's rollback behavior depends on.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_LFN_WRITE"
S=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST:" $S/serial.log
# Independent host-side check, in a completely separate process:
mdir -i disk_images/gos_disk.img ::
mtype -i disk_images/gos_disk.img "::a renamed long filename.txt"
```
Expected: `TEST: fat_create_file(long) = OK`, `TEST: fat_write_file(long) = OK`, `TEST: fat_rename(long->long) = OK`; `mdir` shows `ARENAM~1.TXT` mapped to `a renamed long filename.txt` (and **not** the pre-rename name); `mtype` prints `written by gOS LFN write test`.

**Command to see:**
```bash
make iso disk build/OVMF_VARS.fd   # normal (non-test) build
make run
# In the File Manager: click "New File", type a long name (e.g. "my new
# long file.txt"), click OK - it appears in the listing immediately.
# Select it, click "Rename", change the name, click OK - listing updates
# in place. Select it again, click "Delete", click OK - it disappears,
# and (verify independently) `mdir` off the disk image shows no leftover
# short- or long-name entries for it.
```
Screenshots: [screenshots/phase23_typed.png](screenshots/phase23_typed.png) (New File dialog with a long name typed in, before confirming), [screenshots/phase23_created_via_ui.png](screenshots/phase23_created_via_ui.png) (File Manager listing showing the new long-named file immediately after clicking OK — no reboot), [screenshots/phase23_rename_done.png](screenshots/phase23_rename_done.png) (same file renamed to a different long name, in place), [screenshots/phase23_delete_done.png](screenshots/phase23_delete_done.png) (the file deleted — gone from the listing, confirmed via `mdir` to have zero remaining entries, LFN or short-name).

---

## Bugs found & fixed during this phase

**None in the implementation itself** — `make diagnostic`'s full pre-existing regression suite passed unchanged after the LFN rewrite, and every LFN read/write/rename/delete test passed on its first real attempt. The four design questions (max name length, character set, alias scheme, rename scope) were resolved with the user *before* writing any code, not discovered as ambiguities during testing.

One **test-script-only** issue was hit and fixed during interactive testing (not a gOS bug): the first two attempts to click the New File dialog's "OK" button after typing a long name used an incorrectly-computed relative mouse-move offset (guessed from a rough visual read of a screenshot rather than the dialog's actual on-screen coordinates), landing the click on the Cancel button once and on the File Manager's window behind the dialog once. Recomputing the offset from the dialog's known screen position (`fm_win.x + 40 + 10`, `fm_win.y + 60 + 24 + 60` for the OK button's top-left, per `fm_open_dialog`'s and `window_add_button`'s actual local-to-screen math in `kernel/src/fm.c`/`window.c`) fixed it on the next attempt — confirmed by the serial log showing `FM: fat_create_file("my interactive test.txt") = ... (OK)` instead of `FM: dialog cancelled` or no dialog-related log line at all.

---

## Phase 23 exit criterion — met

Long filenames read and write correctly, verified independently via `mtools` in both directions (host-seeded names read correctly by gOS; gOS-created/renamed/deleted names read correctly by `mdir`/`mtype` afterward), with existing 8.3-only files and the full pre-existing regression suite (`make diagnostic`, 150 file cycles + 300 window cycles) unaffected.
