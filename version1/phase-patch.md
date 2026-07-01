# Patch — Post-v1.0 Stability & UX Fixes — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — both fixes verified, plus a supporting read-only audit.

---

## Summary

This isn't a numbered phase from `PROJECT_PLAN.md` — it's a patch on top of the completed v1.0 (Phases 0–11), covering everything in commit `f961a86` ("audit conducted, buttons with texts and os halt time removed"). Three things happened, in order: (1) a full read-only flaw audit of the entire kernel (`audit.md`, 24 ranked findings, no code changed), (2) a real production bug fix reported by the user directly ("after some time the OS is hanging") that turned out to be unrelated to any audit finding, and (3) a UI polish fix (buttons rendered as bare colored rectangles with no visible label). Documented here in the same format as `phase0.md` through `phase11.md` so it has the same test-and-verify record.

---

## Fix 1: Desktop hang after ~25 seconds

### Task: Diagnose and fix "the OS is hanging" after some time of interactive use
- **What was done:** The user reported the desktop becoming unresponsive after a while, specifically while dragging/opening/closing windows, using the real interactive `qemu-system-x86_64 ... -display cocoa -serial stdio` command (not a headless test script). Two clarifying questions narrowed this down immediately (what were you doing; how are you running it) before any code was touched.
- **Outcome:** Root cause found and fixed — see "Bugs Found and Fixed" below. Verified live via screendump ~48 seconds into a real run (well past the old cutoff), confirming the desktop stays fully responsive.
- **Issues:** None remaining after the fix.

---

## Fix 2: Buttons with no visible label text

### Task: Add text labels to every button that was rendered as a bare colored rectangle
- **What was done:** `struct button` (in `kernel/include/window.h`) had no label field at all — every button in the OS (the File Manager's 5 toolbar buttons, the modal dialog's 2 buttons, and the Phase 6 demo button on Window A) had only ever been a plain colored rect with an outline, since the day each was introduced. Added a `char label[BUTTON_LABEL_MAX]` field to `struct button`, extended `window_add_button()`'s signature to take a `const char *label` parameter (bounded-copy, always null-terminated, same pattern as `window_set_title`), and added label rendering to `draw_window()`'s button-drawing loop: `fb_draw_string_clipped()` draws the label in black, vertically centered, clipped to the button's own rect so a label can never spill outside it even if too long for the button's width.
- **Outcome:** Updated all 8 existing call sites: File Manager toolbar → "Up", "New Folder", "New File", "Delete", "Rename" (`kernel/src/fm.c`); the reusable modal dialog → "OK", "Cancel" (`kernel/src/fm.c`); Window A's Phase 6 demo button → "Click Me" (`kernel/src/start.c`). Verified visually via screendump — every button is now clearly legible.
- **Issues:** None — every existing button width was already generous enough to fit its new label without needing to resize anything (the clipping guarantees safety regardless).

---

## Bugs Found and Fixed

### Bug: Desktop hangs after ~25 seconds of interactive use

- **Symptom:** Reported directly by the user: after some time of dragging/opening/closing windows via the real interactive `-display cocoa` QEMU session, the desktop stopped responding to the mouse and keyboard entirely — no crash, no error, no panic screen, just frozen.
- **Diagnosis:** The main desktop loop in `kernel/src/start.c` was `for (int frame = 0; frame < 500; frame++) { ...; sleep_ms(50); }` — exactly 500 × 50ms = **25 seconds**, after which it fell straight into `hcf()` (`for (;;) { hlt; }`, an intentional, permanent halt). This loop bound was a leftover from Phases 6 through 10, where it existed *only* so headless automated screendump/monitor-command test scripts had a finite window to run in before the QEMU process's own `timeout` wrapper killed it. Nobody had converted it to a real, infinite event loop once the OS became something meant for actual interactive use (Phase 11's desktop/taskbar/launcher). To anyone running it interactively, the intentional halt after 25 seconds is indistinguishable from a genuine hang.
- **What was tried that didn't work:** Nothing else was tried — once the two clarifying questions ruled out "some specific window action triggers it" (the user said it happens "after some time," not tied to a specific click), the fixed 25-second window was the obvious next thing to check, and grepping `start.c` for the loop bound and the following `hcf()` call confirmed it immediately.
- **Actual fix:** Changed the loop to `for (;;) { ...; sleep_ms(50); }` (runs forever, as a real OS's desktop event loop should). This created a secondary problem: every phase doc's "full regression check" headless test script greps serial output for the literal line `=== gOS boot checks complete ===`, which used to print *after* the bounded loop finished — with an infinite loop, that line would now never print within any script's `timeout` window, breaking every documented regression check. Fixed by moving that print to *before* the now-infinite loop instead of after it (all the meaningful self-tests — PMM, heap, the Phase 11 stress test — already run earlier in boot, so the line's meaning, "boot reached the desktop without crashing," is unchanged; it just now prints at the same point in boot for a different structural reason). Verified by screendumping a live session ~48 seconds in (`screenshots/phase-patch_still_alive_48s.png`) and confirming the desktop was still fully interactive (a Files-icon click launched the File Manager successfully, with the timer tick counter having incremented far past where the old loop would have halted).

### Bug: none found in the button-label fix (feature gap, not a regression)

- **Symptom:** N/A — this was a reported UX gap ("some of the buttons dont have a text... no idea what it is"), not a broken behavior.
- **Diagnosis:** `struct button` simply never had a label field designed into it from Phase 6 onward; every subsequent phase (9, 10, 11) added more buttons using the same label-less struct without anyone noticing the toolbar/dialog buttons were illegible colored rectangles, since their *positions* and *colors* were enough for the developer (who already knew what each one did) to test them successfully throughout Phases 9–11.
- **What was tried that didn't work:** N/A.
- **Actual fix:** See "Fix 2" above.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| N/A — not a `PROJECT_PLAN.md` phase | This patch exists outside the 12-phase roadmap entirely | Both fixes were discovered after v1.0 was already tagged: one via a proactive read-only audit request, one via a live user bug report during real interactive use — neither was a planned deliverable |
| `audit.md`'s methodology explicitly made no code changes | This patch *does* change code, in a separate, later request | The user's audit request was explicitly "don't do anything, just list flaws"; the hang and button-label fixes were separate, later requests that explicitly asked for a fix, not just a diagnosis |

---

## Effort Estimate vs. Actual

| | Estimate | Actual |
|---|---|---|
| Read-only kernel audit (`audit.md`) | N/A (not a planned phase) | ~35 minutes (4 parallel review passes + direct verification of the highest-severity claims) |
| Hang diagnosis + fix | N/A | ~15 minutes (2 clarifying questions, direct `grep`, one-line-scope fix, live verification) |
| Button-label fix | N/A | ~15 minutes (struct change + 8 call-site updates + visual verification) |
| **Total** | — | **~1 hour** |

**Why this was fast:** Both bugs were narrow, well-isolated, single-file-family fixes once located — the hang was a single loop-bound change plus moving one log line, and the button labels were an additive struct field with no behavior changes to anything existing. Neither required touching the FAT32 driver, memory management, or any of the higher-risk subsystems the plan itself flags as the places most likely to need real rework.

---

## Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_patch.fd
```

### Fix 1 — Desktop no longer hangs after 25 seconds

**Command to test (confirm the kernel is still alive and responsive well past the old 25-second cutoff):**
```bash
(sleep 16; \
 sleep 32; \
 printf "mouse_move -588 -348\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "quit\n") | timeout 60 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_patch.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:/tmp/gos_patch_test.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
grep -E "Desktop:|Timer tick" /tmp/gos_patch_test.log | tail -6
```
Expected: a click sent ~48 seconds into the run still logs `Desktop: Files icon clicked - launching File Manager`, and the `Timer tick:` counter has incremented well past 2500 (the old 25-second/500-frame cutoff) — proof the kernel never halted on its own.

**Command to see (screendump proving the desktop is still interactive past the old cutoff):**
```bash
(sleep 16; sleep 32; \
 printf "mouse_move -588 -348\n"; sleep 0.3; printf "mouse_button 1\n"; sleep 0.2; printf "mouse_button 0\n"; sleep 0.6; \
 printf "screendump /tmp/still_alive.ppm\n"; sleep 0.3; printf "quit\n") | timeout 60 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_patch.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/still_alive.ppm --out screenshots/phase-patch_check_alive.png && open screenshots/phase-patch_check_alive.png
```
**What to check:** the File Manager window is open (proving the click 48 seconds in was processed), the taskbar reflects it, and nothing looks frozen mid-frame.

### Fix 2 — Buttons show readable labels

**Command to test (headless boot completes cleanly with the new button-label code, no regressions):**
```bash
timeout 60 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_patch.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|boot checks complete"
```
Expected: `PMM self-test: PASS`, `Heap self-test: PASS`, `Stress test: PASS`, `=== gOS boot checks complete ===`, no `FAIL`/`PANIC`/`EXCEPTION`.

**Command to see (screendump of labeled buttons on the demo window and the File Manager toolbar):**
```bash
(sleep 16; printf "screendump /tmp/labels.ppm\n"; sleep 0.3; printf "quit\n") | timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_patch.fd \
  -cdrom build/gos.iso \
  -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
sips -s format png /tmp/labels.ppm --out screenshots/phase-patch_check_labels.png && open screenshots/phase-patch_check_labels.png
```
**What to check:** Window A's green button reads "Click Me"; if you also open the File Manager (click the desktop's Files icon first), its toolbar reads "Up", "New Folder", "New File", "Delete", "Rename" and any opened dialog's buttons read "OK"/"Cancel" — all in black text, fully legible, none spilling outside their button's rect.

```bash
rm -f /tmp/OVMF_VARS_patch.fd
```

**To watch it live and interact yourself** (the real test for "does it still hang" — leave it running far longer than 25 seconds):
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

---

## Not Yet Verified / Related Follow-ups

- [ ] The 24 findings in `audit.md` are documented but intentionally **not** fixed as part of this patch — that was an explicit read-only audit request. Any of those (e.g. the `fat_write_file` unsigned-underflow bug, `kfree`'s missing double-free check, no IST for double-fault/NMI) remain open for a future, separate fix pass.
- [ ] No automated regression test currently asserts the desktop loop runs indefinitely (the fix is verified manually per the commands above, not via a self-test that would fail loudly if a future change reintroduces a bounded loop).

---

## Next Step

With v1.0 tagged and this patch's two fixes verified, the natural next step (if desired) is working through `audit.md`'s findings — starting with the five items marked Critical, all of which are real crash/corruption/hang risks rather than cosmetic issues.
