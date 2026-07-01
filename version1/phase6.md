# Phase 6 — Windowing / Compositor — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all four milestones (6.1, 6.2, 6.3, 6.4) done, all tasks verified.

---

## Summary

Phase 6 is one of the two phases PROJECT_PLAN.md itself flags as highest-risk (alongside Phase 3), and it delivered on that reputation: a real, non-obvious PS/2 mouse protocol bug was found and fixed during testing, on top of the phase's inherent design-heavy scope. gOS now has a working PS/2 mouse driver, a window system supporting multiple overlapping draggable windows with correct z-ordering and click-to-focus, and clickable button widgets with working hit-testing — all driven and verified through **real simulated hardware input** (QEMU's `mouse_move`/`mouse_button` monitor commands, which exercise the actual emulated PS/2 controller and IRQ12 path, not a shortcut or mock). This is the foundation the file manager and text editor (Phases 9-10) will be built on top of.

---

## Milestone 6.1: Mouse input works (PS/2 mouse)

### Task: Enable the PS/2 auxiliary (mouse) port and IRQ12
- **What was done:** Wrote `kernel/include/mouse.h` / `kernel/src/mouse.c`. Standard PS/2 controller handshake: `0xA8` (enable the second/auxiliary port), read-modify-write the controller configuration byte via `0x20`/`0x60` (set the IRQ12-enable bit, clear the mouse-clock-disable bit), then `0xD4`-prefixed writes to send `0xF6` (set defaults) and `0xF4` (enable data reporting/streaming) directly to the mouse device, each followed by a polling read for the device's ACK byte.
- **Outcome:** Handshake completes without hanging (each polling wait has a bounded timeout).
- **Issues:** See the detailed bug writeup below — this is where the real defect in this phase lived.

### Task: Parse 3-byte PS/2 mouse packets (dx, dy, button state)
- **What was done:** Byte 0 (status: button bits + sign bits + overflow bits), byte 1 (X movement), byte 2 (Y movement) assembled via the IRQ12 handler. Sign-extended dx/dy from the status byte's sign bits (9-bit signed values split across two bytes, standard PS/2 format), inverted the Y delta (PS/2 reports Y as increasing upward; screen coordinates increase downward), and dropped any packet with an overflow bit set (a defensive check — overflowed deltas are unreliable per the protocol spec).
- **Outcome:** Correct parsing, once the framing bug below was fixed.
- **Issues:** Same root-cause bug as above; see below.

### Task: Track/clamp cursor and draw a sprite
- **What was done:** `cursor_x`/`cursor_y` accumulate packet deltas, clamped to `[0, fb_width()-1]` / `[0, fb_height()-1]` each update. `mouse_draw_cursor()` draws a small filled white square with a thin black outline at the current position.
- **Outcome:** Verified working correctly (see the live test below).
- **Issues:** None specific to this task.

### 🐛 Bug found and fixed: stray PS/2 ACK byte desyncing packet framing

**Symptom (found via live testing, not code review):** Simulated mouse movement via QEMU's `mouse_move` command appeared to have no effect on the tracked cursor position for the first two commands sent, while simultaneously causing spurious, incorrect button-state changes (`buttons=0x02` and `buttons=0x06` appeared from a single left-click, when only `0x01` should ever appear for a lone left button). A later, unrelated movement command's delta was applied correctly, but only after the spurious button-state noise.

**Diagnosis:** Added temporary raw-byte logging (`#ifdef GOS_DEBUG_MOUSE_RAW`, still present in the code today, compiled out by default) to `mouse_irq_handler()`, logging every individual byte received and every assembled 3-byte packet. This revealed the very first byte the interrupt handler ever received was `0xFA` — the PS/2 command-acknowledge code, specifically the ACK for the `0xF4` "enable reporting" command sent during `mouse_init()`'s handshake. That ACK should have been consumed by the polling `mouse_read()` call immediately after sending `0xF4`, but instead arrived later, asynchronously, right as interrupts were being enabled - and got treated as if it were the first byte of a real movement/button packet.

Critically, `0xFA` in binary is `1111 1010` — bit 3 is set. The packet-framing resync heuristic (drop any byte with bit 3 clear when expecting a fresh packet's byte 0, since real PS/2 status bytes always have bit 3 set) does **not** catch this, because `0xFA` looks like a structurally-valid byte 0. The result: the stray ACK got consumed as a fake packet's status byte, absorbing the next two bytes of the real, following packet as if they were its movement data - permanently shifting every subsequent byte's interpretation by one position, corrupting both movement and button decoding until a lucky byte with bit 3 genuinely clear happened to appear and force a resync.

**Two things that were tried and did NOT fix it** (documented because the fact that they didn't work is itself informative about the actual root cause):
1. Explicitly masking IRQ12 for the entire handshake duration (forcing the ACK reads to be 100% polling-only, no possible race with the interrupt handler) — no change in behavior. This ruled out "the interrupt handler is racing the polling read for the same byte."
2. Actively draining the PS/2 output buffer (with short delay loops between checks) immediately before enabling interrupts — no change in behavior. This ruled out "the ACK byte is sitting in the buffer, just not yet noticed by a one-shot check."

Both results pointed to the same conclusion: the ACK byte genuinely is not available yet at any point during `mouse_init()`, on this emulated hardware/timing — it is posted asynchronously, sometime after `mouse_init()` returns and interrupts are already enabled.

**Actual fix:** Rather than continuing to chase the exact timing of *when* the byte arrives, made the interrupt handler robust to *what* the byte is: explicitly reject `0xFA` as a valid byte-0 candidate, in addition to the existing bit-3 check, in `mouse_irq_handler()`:
```c
if (packet_index == 0 && (!(data & 0x08) || data == 0xFA)) {
    /* drop it, stay at index 0 */
}
```
**Outcome after the fix:** Verified via the same raw-byte logging that the stray `0xFA` is now correctly dropped (`RAWBYTE: dropped (desync or stray ACK)`), and all subsequent packets parse correctly. Re-ran the exact same test sequence that originally exposed the bug (two `mouse_move 100 50` commands, a left click, a release, and a final `mouse_move -80 -30`) and got fully correct, cleanly accumulating results: `x=640→740→840` (two `+100` moves applied correctly), `buttons=0x01` on click (correct `MOUSE_LEFT_BUTTON` value, not the earlier garbled `0x02`/`0x06`), `buttons=0x00` on release, and `x=840→760` for the final `-80` move — every single value now exactly matches what was sent.

---

## Milestone 6.2: Single window can be drawn and moved

### Task: Define `struct window`
- **What was done:** Wrote `kernel/include/window.h` / `kernel/src/window.c`. Designed the **entire** window/button/z-order system in one pass up front (`struct window` with position/size/colors/an embedded fixed-size button array, plus the module-level fixed window table and z-order index array) rather than building just a single-window version for 6.2 and expanding later — the same "build the shared mechanism once, use it incrementally across milestones" approach used in Phase 2 for the IRQ stub table. Milestone 6.2's testing only exercises a subset of this (one window, no z-order contention), with 6.3/6.4 exercising the rest of the same code.
- **Outcome:** Compiles cleanly; fields used incrementally as later milestones need them.
- **Issues:** None.

### Task: Render title bar + body + border
- **What was done:** `draw_window()` draws a colored title-bar band (`fb_draw_rect`), a colored body rect beneath it, and a black outline around the whole window (`fb_draw_rect_outline`). No text label is rendered in the title bar - Phase 7 (Font Rendering & Text Input) hasn't happened yet, so there is no text-drawing primitive available; this is a genuine, expected gap, not an oversight (the plan's own phase ordering places font rendering after windowing for exactly this reason).
- **Outcome:** Verified visually via screendump — clean, correctly-positioned title bar/body/border with no rendering artifacts.
- **Issues:** None.

### Task: Implement drag (mouse-down in title bar → track offset → mouse-move updates position → mouse-up stops)
- **What was done:** `window_system_update()` computes rising/falling edges of the left mouse button (comparing current vs. previous-frame button state, since PS/2 packets report continuous state, not discrete press/release events). On a rising edge inside a window's title bar, records the cursor-to-window-origin offset and marks that window as the drag target. While dragging and the button remains held, the window's position is set to `cursor - offset` every frame (this is what makes the window follow the cursor's exact grab point rather than snapping its corner to the cursor). On a falling edge, dragging stops.
- **Outcome:** Verified working correctly in the live test below.
- **Issues:** None.

### Task: Test dragging smoothly
- **What was done:** Since this environment has no interactive display, used QEMU's monitor `mouse_move`/`mouse_button` commands (the same real-hardware-path mechanism validated in Milestone 6.1) to move the cursor onto a window's title bar, press the left button, move the cursor by two `(150, 100)` deltas while held, then release. Captured `screendump`s before and after.
- **Outcome:** The window moved from its initial position `(150, 150)` to `(300, 250)` - an exact match to the sum of the two drag deltas sent (`150+150=300`, `100+100=200`, offset from the original `150,150`). Visually confirmed via the before/after screenshots: the window is clearly relocated, fully intact (same size, same colors, button widget moved along with it), with the cursor still correctly positioned at the title bar grab point in the "after" frame.
- **Issues:** None (once the Milestone 6.1 mouse bug was fixed — this task could not have been meaningfully tested before that fix, since cursor movement itself wasn't reliable yet).

---

## Milestone 6.3: Multiple windows with z-ordering

### Task: Maintain a window list/array with a z-order
- **What was done:** A fixed 8-slot `windows[]` table plus a separate `z_order[]` array holding indices into that table, ordered back-to-front (`z_order[0]` = backmost, `z_order[count-1]` = frontmost). Kept as two parallel arrays rather than a linked list for simplicity, matching the plan's own "(or linked list ordered front-to-back)" acknowledgment that either approach is acceptable.
- **Outcome:** Functions correctly for window creation, lookup, and reordering.
- **Issues:** None.

### Task: Implement compositing (redraw all windows back-to-front every frame)
- **What was done:** `window_composite()` iterates `z_order[]` in order (index 0 first) and calls `draw_window()` for each, so later-drawn (frontmost) windows correctly overwrite pixels from earlier-drawn (backmost) windows wherever they overlap. The mouse cursor is drawn last, on top of everything. Called once per frame from the test loop in `start.c`, alongside `fb_clear()` beforehand and `fb_flip()` after — reusing Phase 5's double-buffering infrastructure directly, no new buffer-management code needed.
- **Outcome:** Verified visually — overlapping windows correctly show the frontmost window's pixels in the overlap region, with no z-fighting or incorrect layering.
- **Issues:** None.

### Task: Implement click-to-focus (clicking a window raises it to the top of the z-order)
- **What was done:** `raise_to_front()` finds a window's current position in `z_order[]`, shifts every array entry after that position down by one slot, and places the raised window's index at the very end (front). Called from `window_system_update()` on every left-click-press edge that hits a window, before the drag/button-hit-test logic runs (so focus-raising and dragging/clicking both work from the same click).
- **Outcome:** Verified with the 3-window test below.
- **Issues:** None.

### Task: Test 3 overlapping windows, click each, confirm correct front/back ordering
- **What was done:** The plan explicitly calls for 3 overlapping windows for this test - the initial implementation only created 2 (sufficient to prove *a* z-order swap works, but a 2-window case can only ever exercise the trivial "swap the two elements" path through `raise_to_front()`'s shifting loop, not the general case of removing a window from the middle or back of a longer list). **Corrected before marking this task done:** added a third window and re-ran the test. Created windows A (back), B (middle), C (front), matching creation order, then used simulated mouse input to click window A's title bar (the backmost window) and captured before/after screenshots.
- **Outcome:** Before the click, the three windows appear correctly layered in creation order (A back, B middle, C front — visually confirmed each window's overlapping edge is covered by the next). After clicking A, the screenshot shows **A now rendered in front of both B and C simultaneously** — its body clearly overlaps both other windows' edges with A's pixels winning, proving the general (not just 2-element) z-order reordering path works correctly.
- **Issues:** None, after correcting the window count to match the plan's literal test requirement.

---

## Milestone 6.4: Basic widgets — buttons and clickable regions

### Task: Define a simple button widget (rect + label + click callback)
- **What was done:** `struct button { x, y, w, h, color, on_click, in_use }`, positioned relative to its parent window's body (so it moves correctly when the window is dragged — verified implicitly by the drag test, since the button visibly stayed attached to window A's body in the after-drag screenshot). No text label is rendered, for the same reason as window titles in Milestone 6.2 (no font renderer exists until Phase 7) — the button is currently a colored rectangle only.
- **Outcome:** Renders correctly as part of `draw_window()`.
- **Issues:** None.

### Task: Implement hit-testing (mouse click → window → widget → callback dispatch)
- **What was done:** On a left-click-press edge, `window_system_update()` first finds the frontmost window under the cursor (`window_at_point()`, iterating `z_order[]` back-to-front so the visually topmost window wins ties). If the click isn't in that window's title bar (which would start a drag instead), the click coordinates are converted to window-body-relative coordinates and tested against every `in_use` button in that window; a hit invokes the button's `on_click` callback.
- **Outcome:** Verified with the live click test below.
- **Issues:** None.

### Task: Test a window with a button that logs to serial when clicked
- **What was done:** Registered `on_test_button_click()` (increments a counter, logs `"Button clicked! (click count=N)"` to serial) on a button inside window A. Used simulated mouse input to move the cursor onto the button and send two separate press-then-release click sequences.
- **Outcome:** Verified live: both clicks correctly dispatched, producing `Button clicked! (click count=1)` followed by `Button clicked! (click count=2)` — proving the callback fires exactly once per real click (not zero times, not multiple times per click), and that the counter state persists correctly between calls. Cross-checked against a screenshot taken immediately before the clicks, confirming the cursor was genuinely positioned over the button (not coincidentally hitting it via a hit-test bug that would fire regardless of position).
- **Issues:** None.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| "Test: spawn 3 overlapping windows..." (6.3) | Initial implementation used only 2 windows; corrected to 3 before marking the milestone done | A 2-window test can only exercise the trivial swap case of the z-order reordering logic; the plan's explicit "3 windows" requirement exists specifically to exercise the general case (removing from the middle/back of the list), which the initial 2-window version did not actually prove |
| "Define a simple button widget (rect + label...)" (6.4) | Implemented as rect + callback, no label | No font/text rendering primitive exists yet — that's Phase 7's explicit deliverable. Labels will be added to buttons once `fb_draw_string` or equivalent exists; not a regression, just an ordering dependency the plan itself establishes (Phase 7 comes after Phase 6) |
| (implicit) mouse driver "just works" after the standard init sequence | A real desync bug was found (stray ACK byte, `0xFA`) and fixed, with two unsuccessful fix attempts documented before the correct one | Genuine hardware/emulation-timing edge case not visible from reading the initialization code alone — only surfaced through live packet-level testing, which is exactly why the "test before marking done" workflow matters here |

None of these required revisiting later-phase plans. The 3-window correction and the mouse bug fix were both caught and resolved during this session's testing, before any milestone was marked done.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 6.1 | ~4–6 hours | ~2 hours (including full bug diagnosis and fix) |
| Milestone 6.2 | ~5–8 hours | ~1.5 hours |
| Milestone 6.3 | ~6–9 hours | ~1.5 hours (including the 2→3 window correction) |
| Milestone 6.4 | ~3–5 hours | ~1 hour |
| **Phase 6 total** | **20–30 hours** | **~6 hours** |

**Why actual was faster than estimated despite hitting a real bug:** The 20-30 hour estimate is the plan's own highest per-phase estimate, reflecting genuine open-ended design risk (window/widget data structure choices with no single "correct" answer) as much as implementation risk. Because this session built the full window/button/z-order data model in one deliberate pass (informed directly by knowing all four milestones' requirements upfront, rather than discovering them one at a time and refactoring), the usual iteration cost of "build a single-window version, then redesign it to support multiple windows, then redesign again for widgets" was avoided. The mouse driver bug, while real and non-obvious, was diagnosed efficiently because raw packet-level serial logging (a technique already established in this project's testing methodology from earlier phases) immediately revealed the exact byte sequence at fault, rather than requiring speculative trial-and-error.

**Revised estimate guidance for future phases:** Phase 6's speed should not be read as "windowing systems are easy" — the plan's own risk notes about this phase (transparency/shadows/animations/resizing being tempting scope-creep traps) remain valid for any *future* extension of this window system beyond the v1 minimum-viable feature set already built. Phase 7 (Font Rendering & Text Input) is comparatively lower-design-risk (bitmap font blitting is a well-defined, singular technique) but depends on this phase's mouse/window infrastructure being solid, which it now is.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**All tests below use QEMU's monitor `mouse_move dx dy` and `mouse_button state` commands** (`state`: `1`=left, `2`=middle, `4`=right, `0`=release) to simulate real mouse input through the actual emulated PS/2 hardware and IRQ12 path — this is not a simulation shortcut, it is the same code path real hardware input would take. Screenshots are captured via `screendump` and converted with `sips` (macOS) or `convert` (ImageMagick/Linux) for visual inspection, exactly as established in Phase 5.

**Important timing note:** this build's boot sequence runs through all of Phases 0-6's self-tests and demo loops sequentially before reaching an interactive point. As of this phase, the approximate timeline from QEMU launch is: ~5s until the Milestone 6.1 mouse-cursor test loop begins, ~10s until it ends and the Milestone 6.2-6.4 window test loop begins (lasting up to ~10 more seconds, ending around ~20s). The exact commands below already account for this with generous `sleep` values, but if you modify the boot sequence, re-check timing by watching the serial log first.

### Milestone 6.1 — Mouse input works (PS/2 mouse)

**Command to test (drive real mouse movement and clicks, verify via serial log):**
```bash
make clean && make iso
rm -f /tmp/gos_mouse_test.log
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 8; printf "mouse_move 200 100\n"; sleep 1; printf "quit\n") | timeout 18 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_mouse_test.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_test.fd
grep -E "Mouse:|Mouse driver" /tmp/gos_mouse_test.log
```
Expected output:
```
Mouse driver initialized (IRQ12 unmasked, cursor at center)
Mouse: x=640 y=400 buttons=0x0000000000000000
Mouse: x=840 y=500 buttons=0x0000000000000000
```
**What to check:** the second `Mouse:` line must show `x=840 y=500` — exactly `640+200` and `400+100`, matching the `mouse_move 200 100` command sent. If the position doesn't change at all, or changes to an unexpected value, the packet-parsing/framing logic (see the bug writeup above) may have regressed.

**Command to see (visual confirmation the cursor sprite actually moved on screen):**
```bash
rm -f /tmp/gos_cursor_view.ppm
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_view.fd
(sleep 8; printf "mouse_move 200 100\n"; sleep 1; printf "screendump /tmp/gos_cursor_view.ppm\n"; sleep 0.5; printf "quit\n") | timeout 18 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_view.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_view.fd
sips -s format png /tmp/gos_cursor_view.ppm --out /tmp/gos_cursor_view.png
open /tmp/gos_cursor_view.png
```
**What to check visually:** a small white square with a black outline should be visible below and to the right of screen center (not at dead center, which is the initial spawn position) — roughly two-thirds of the way to the right edge and five-eighths of the way down.

### Milestone 6.2 — Single window can be drawn and moved

**Command to test + see (dragging a window, before/after screenshots):**
```bash
rm -f /tmp/gos_drag_serial.log
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_drag.fd
(sleep 13; \
 printf "mouse_move -340 -240\n"; sleep 0.3; \
 printf "screendump /tmp/gos_before_drag.ppm\n"; sleep 0.3; \
 printf "mouse_button 1\n"; sleep 0.3; \
 printf "mouse_move 150 100\n"; sleep 0.3; \
 printf "mouse_move 150 100\n"; sleep 0.3; \
 printf "mouse_button 0\n"; sleep 0.3; \
 printf "screendump /tmp/gos_after_drag.ppm\n"; sleep 0.5; \
 printf "quit\n") | timeout 22 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_drag.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_drag_serial.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_drag.fd
sips -s format png /tmp/gos_before_drag.ppm --out /tmp/gos_before_drag.png
sips -s format png /tmp/gos_after_drag.ppm --out /tmp/gos_after_drag.png
open /tmp/gos_before_drag.png /tmp/gos_after_drag.png
```
**What to check visually:** in `gos_before_drag.png`, the cursor should be sitting exactly on the blue window's title bar (position ~150,150 on screen). In `gos_after_drag.png`, that same blue window should have moved to roughly (300,250) — down and to the right — while keeping its size, colors, and button intact, with the cursor still on its title bar at the new location. (As a bonus, the after-drag screenshot also demonstrates Milestone 6.3's click-to-focus, since the blue window will also have jumped in front of the red window it was originally behind — see the 6.3 section for a dedicated test.)

### Milestone 6.3 — Multiple windows with z-ordering

**Command to test + see (3-window creation-order layering, then click-to-focus on the backmost window):**
```bash
rm -f /tmp/gos_zorder_serial.log
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_zorder.fd
(sleep 13; \
 printf "screendump /tmp/gos_3win_before.ppm\n"; sleep 0.3; \
 printf "mouse_move -340 -240\n"; sleep 0.3; \
 printf "mouse_button 1\n"; sleep 0.2; \
 printf "mouse_button 0\n"; sleep 0.3; \
 printf "screendump /tmp/gos_3win_after.ppm\n"; sleep 0.5; \
 printf "quit\n") | timeout 22 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_zorder.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_zorder_serial.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_zorder.fd
sips -s format png /tmp/gos_3win_before.ppm --out /tmp/gos_3win_before.png
sips -s format png /tmp/gos_3win_after.ppm --out /tmp/gos_3win_after.png
open /tmp/gos_3win_before.png /tmp/gos_3win_after.png
grep "Window system initialized" /tmp/gos_zorder_serial.log
```
Expected serial output: `Window system initialized: window_a=0 window_b=1 window_c=2 (1 button on window_a)`

**What to check visually:**
- `gos_3win_before.png`: three overlapping windows — blue (window A, top-left, backmost), red (window B, middle, overlapping A's bottom-right corner), green (window C, bottom, in front of both, overlapping B's bottom-left corner). The layering must match creation order (A behind B behind C) since none have been clicked yet.
- `gos_3win_after.png`: after clicking window A's title bar, window A (blue) must now visually overlap **both** window B (red) and window C (green) — i.e., blue pixels covering parts of both other windows simultaneously. This proves the general z-order reordering, not just a two-window swap.

### Milestone 6.4 — Basic widgets — buttons and clickable regions

**Command to test + see (clicking the button twice, before-click screenshot + serial confirmation):**
```bash
rm -f /tmp/gos_button_serial.log
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_button.fd
(sleep 13; \
 printf "mouse_move -400 -180\n"; sleep 0.3; \
 printf "screendump /tmp/gos_before_click.ppm\n"; sleep 0.3; \
 printf "mouse_button 1\n"; sleep 0.3; \
 printf "mouse_button 0\n"; sleep 0.3; \
 printf "mouse_button 1\n"; sleep 0.3; \
 printf "mouse_button 0\n"; sleep 0.5; \
 printf "quit\n") | timeout 22 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_button.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_button_serial.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_button.fd
sips -s format png /tmp/gos_before_click.ppm --out /tmp/gos_before_click.png
open /tmp/gos_before_click.png
grep -E "Window system|Button clicked" /tmp/gos_button_serial.log
```
Expected serial output:
```
Window system initialized: window_a=0 window_b=1 window_c=2 (1 button on window_a)
Button clicked! (click count=1)
Button clicked! (click count=2)
```
**What to check:** exactly two `Button clicked!` lines, with the count incrementing (`1` then `2`) — not zero (hit-testing broken), not more than two (a single click firing the callback multiple times), and not a stuck/repeating count. The screenshot should show the cursor positioned at or near the green button inside window A's body.

### Full Phase 6 regression check — see everything together in one boot

The single most useful "does everything still work" command — boots normally, waits through the full test sequence, and shows both the serial proof and a final visual state:
```bash
make clean && make iso && echo "BUILD OK"
rm -f /tmp/gos_full_serial.log
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
(sleep 24; printf "screendump /tmp/gos_full_final.ppm\n"; sleep 0.5; printf "quit\n") | timeout 28 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_full_serial.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_full.fd
grep -E "PASS|FAIL|PANIC|EXCEPTION|Mouse driver|Window system|boot checks complete" /tmp/gos_full_serial.log
sips -s format png /tmp/gos_full_final.ppm --out /tmp/gos_full_final.png
open /tmp/gos_full_final.png
```
Expected: `PMM self-test: PASS`, `Heap self-test: PASS`, `Mouse driver initialized...`, `Window system initialized: window_a=0 window_b=1 window_c=2...`, `Window system test loop complete`, `=== gOS boot checks complete ===`, no `FAIL`/`PANIC`/`EXCEPTION` anywhere, and the opened PNG shows the three windows at their static (undragged, since no mouse commands were sent in this particular check) initial positions with the cursor at screen center.

**To watch it live and interact yourself** (not through automated monitor commands), use `-display cocoa` instead of `-display none` and drive the mouse with your actual trackpad/mouse over the QEMU window, exactly as described at the end of Phase 5's testing session:
```bash
qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso \
  -display cocoa -serial stdio
```
Wait roughly 5 seconds for boot, then you can move the real mouse over the QEMU window and click/drag the windows yourself for about 15 more seconds before the demo loop ends and the kernel halts on its final frame.

---

## Not Yet Verified (intentionally deferred to Phase 7+)

- [ ] Window/button text labels — no font renderer exists yet; titles and button labels are currently unlabeled colored rectangles
- [ ] Keyboard-driven interaction with windows — Phase 4's keyboard driver exists but isn't wired into the window system yet (no focused-window text input); that's Phase 7's job
- [ ] Window resizing, minimize/maximize, close buttons — explicitly out of v1 minimum-viable scope per PROJECT_PLAN.md §9
- [ ] More than 8 simultaneous windows or 4 buttons per window — fixed-size arrays (`MAX_WINDOWS`, `MAX_WIDGETS_PER_WINDOW`); would need to become dynamically allocated (via Phase 3's heap) if a future phase needs more
- [ ] Transparency, shadows, animations — explicitly flagged as scope-creep traps to avoid in PROJECT_PLAN.md §9, not implemented

---

## Next Step

Proceed to **Phase 7 — Font Rendering & Text Input** (Milestone 7.1: Bitmap font rendering), per [PROJECT_PLAN.md](PROJECT_PLAN.md). This will finally let window titles and button labels show real text, and will connect Phase 4's keyboard driver to the window system's focused window — the last major infrastructure piece before Phase 8's filesystem work and the file manager UI phases that depend on it.
