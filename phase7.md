# Phase 7 — Font Rendering & Text Input — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all three milestones (7.1, 7.2, 7.3) done, all tasks verified.

---

## Summary

Phase 7 gave gOS its first text rendering capability, and connected Phase 4's keyboard driver to Phase 6's window system for the first time — windows now show real titles instead of blank colored bars, and a window can accept and display live-typed text with a blinking cursor, backspace, and multi-line input. This is the last piece of general-purpose UI infrastructure before Phase 8 (filesystem) and the file manager/text editor phases that depend directly on this session's text box widget.

All screenshots captured during this phase's testing are saved in the new `screenshots/` folder at the project root, as requested.

---

## Milestone 7.1: Bitmap font rendering

### Task: Embed a monospace bitmap font
- **What was done:** Rather than converting a `.psf`/`.ttf` file into a C array by hand, downloaded a ready-made, public-domain 8x8 bitmap font directly as a C header: `font8x8_basic.h` from the `dhepper/font8x8` project (itself tracing back to Marcel Sondaar's public-domain IBM VGA ROM font work). Saved as `kernel/src/font8x8_basic.h`, covering all 128 basic-Latin code points (`U+0000`-`U+007F`) as a `char font8x8_basic[128][8]` array.
- **Outcome:** A complete, correctly-licensed (public domain) font ready to use with zero manual glyph-drawing work.
- **Issues:** None. The plan explicitly allows either an 8x16 PSF1 font or a hand-embedded public-domain C array — 8x8 was chosen over 8x16 for simplicity (smaller glyph table, simpler bit-packing, one byte per row instead of needing to handle a taller glyph), since the plan does not mandate 8x16 specifically.

### Task: Implement `fb_draw_char`
- **What was done:** Wrote `kernel/include/font.h` / `kernel/src/font.c`. `fb_draw_char_ex()` (with `fb_draw_char()` as a background-filling convenience wrapper) reads the 8-byte glyph for a character, and for each of the 8 rows, tests each of the 8 bits (bit 0 = leftmost pixel, verified empirically to be correct — see the test result below, since getting this backwards would render every character horizontally mirrored) and calls `fb_put_pixel` with either the foreground or background color.
- **Outcome:** Correct, legible character rendering confirmed visually (see below).
- **Issues:** None.

### Task: Implement `fb_draw_string`
- **What was done:** Loops over a null-terminated C string, calling `fb_draw_char` for each character and advancing the x cursor by `FONT_WIDTH` (8px); a `\n` character resets x and advances y by `FONT_HEIGHT` (8px), giving basic multi-line support for free (this detail becomes directly useful in Milestone 7.3's text box, which relies on exactly this behavior for wrapping typed Enter presses into new lines).
- **Outcome:** Works correctly as part of the "Hello, gOS!" test below.
- **Issues:** None.

### Task: Test — render "Hello, gOS!" at boot
- **What was done:** Added a dedicated frame in `start.c`, right after Phase 5's bouncing-rectangle animation completes: clears the screen, draws `"Hello, gOS!"` at a fixed position via `fb_draw_string`, flips the buffer, and holds that frame for 2 seconds (`sleep_ms(2000)`) so it can be reliably captured with a screendump before the Phase 6 mouse/window demos take over the screen.
- **Outcome:** Verified visually — the text renders crisply and is fully legible, confirming the bit-order assumption (bit 0 = leftmost) was correct on the first attempt (a wrong bit order would have produced mirrored, unreadable text, which did not happen).
- **Issues:** None. **Testing note:** finding the correct screendump timing for this frame took several attempts — the cumulative boot time from all six prior phases' self-tests (PMM, heap, timer, animations) pushed this frame's actual appearance later in wall-clock time than initially estimated; the exact timing used in the final test commands (below) was determined empirically via multiple test screendumps at different `sleep` offsets, not assumed.

**Screenshot:** `screenshots/phase7_7.1_hello_gos.png`

---

## Milestone 7.2: Text rendering inside windows

### Task: Add a text rendering helper that clips to a window's content rect
- **What was done:** Added `fb_draw_string_clipped()` to `font.c`, which behaves like `fb_draw_string` but additionally checks every glyph pixel's coordinates against a caller-supplied clip rectangle before drawing, skipping anything outside it. This guarantees a window's title (or, later, its text box contents) can never visually spill outside the window's own bounds, regardless of string length.
- **Outcome:** Used by both the window title bar (this milestone) and the text box widget (Milestone 7.3).
- **Issues:** None.

### Task: Render a window title using the font renderer
- **What was done:** Added a `title[WINDOW_TITLE_MAX]` field to `struct window` (`kernel/include/window.h`), populated via a new `title` parameter on `window_create()` (updated at all three call sites in `start.c`, renamed the previously-anonymous test windows to `"Window A"`, `"Window B"`, and `"Text Editor"` — the latter chosen deliberately to double as the window used for Milestone 7.3's text-input testing). `draw_window()` now calls `fb_draw_string_clipped()` to render the title, clipped to the title bar's exact rectangle.
- **Outcome:** Verified visually — all three windows show correct, readable titles in their title bars with no overflow past the window edges.
- **Issues:** None.

### Task: Test — window title bars show real text
- **What was done:** Captured a screendump of all three windows during their static initial-position state (before any dragging), zoomed into each title bar visually.
- **Outcome:** `"Window A"`, `"Window B"`, and `"Text Editor"` all render legibly in white text against their respective title bar colors (blue, red, green), correctly positioned and clipped.
- **Issues:** None.

**Screenshot:** `screenshots/phase7_7.2_window_titles.png`

---

## Milestone 7.3: Keyboard input routed to focused window

### Task: Route `kb_getchar()` events to whichever window has focus
- **What was done:** Defined "focused window" as whichever window is currently frontmost in the z-order (`z_order[window_count - 1]`) — this is a deliberate, zero-extra-state design choice: Phase 6's click-to-focus already maintains exactly this invariant (clicking a window raises it to the front), so "frontmost" and "focused" are the same concept with no separate focus-tracking variable needed. In `window_system_update()`, after the existing mouse-handling logic, added a check: if the focused window has a text box (see below), drain all currently-available characters from the keyboard ring buffer (`while (kb_has_char()) { char c = kb_getchar(); ... }` — using the non-blocking `kb_has_char()` check first, so this never stalls the main loop waiting for a keystroke that hasn't happened).
- **Outcome:** Verified working correctly — characters typed while a given window is frontmost land in that window's buffer only.
- **Issues:** None.

### Task: Implement a text buffer widget (printable chars, Backspace, Enter)
- **What was done:** Added `has_textbox`, `textbox_buffer[TEXTBOX_BUFFER_SIZE]` (512 bytes), and `textbox_length` fields to `struct window`, opted into via a new `window_enable_textbox(win_index)` call (used on the "Text Editor" window in `start.c`). Backspace (`'\b'`, matching the keyboard driver's existing scancode-to-ASCII mapping from Phase 4) decrements the length and null-terminates; any other character appends to the buffer if there's room. Enter is not special-cased at all — Phase 4's keyboard driver already maps the Enter scancode to `'\n'`, and `fb_draw_string_clipped` (via the same newline-handling logic as plain `fb_draw_string`) already treats `\n` as a line break, so multi-line text "just works" from composing two already-correct pieces rather than needing new line-tracking logic.
- **Outcome:** Verified correct for both appending and backspacing (see the pixel-level test below).
- **Issues:** None.

### Task: Implement a blinking text cursor at the current insertion point
- **What was done:** Rather than tracking the cursor's pixel position separately (which would require re-implementing `fb_draw_string_clipped`'s own line-wrapping math a second time just to know where the last character landed), the cursor is drawn as a literal `_` character conditionally appended to a **local copy** of the buffer right before rendering — when appended, it naturally lands exactly where the next typed character would go, using the exact same rendering path as the real text. Blink timing uses Phase 4's timer tick counter: `(timer_get_ticks() / 50) % 2 == 0`, giving a 0.5-second on/off cycle at the 100Hz tick rate. The cursor is only appended when the window is focused (determined by comparing the window's own index, computed via pointer arithmetic against the static `windows[]` array, to `z_order[window_count-1]`), so unfocused windows with a text box never show a cursor.
- **Outcome:** Cursor visible in both of the typing test screenshots below, correctly positioned immediately after the typed text (and after a newline, correctly relocated to the start of the new line).
- **Issues:** None.

### Task: Test — a window with a text box you can type into, with visible cursor and backspace working
- **What was done:** Since there's no interactive keyboard in this headless environment, used QEMU's monitor `sendkey` command (established in Phase 4 as the real-hardware-path equivalent of physical typing) to send a sequence of keystrokes to the already-focused "Text Editor" window (it's frontmost by creation order, so no click was needed first to focus it). Two separate test runs:
  1. Typed `h`, `e`, `l`, `l`, `o` and captured a screendump.
  2. Typed `g`, `o`, `s`, `x`, `backspace`, `ret` (Enter), `shift-2` and captured a second screendump, then additionally parsed the **raw PPM pixel data directly** (not just a visual glance at the rendered PNG) to render an ASCII-art dump of the exact glyph shapes in the text region, definitively confirming the exact characters drawn.
- **Outcome:**
  - First run: `"hello_"` renders correctly in the Text Editor window's body, cursor visible immediately after the final `o`.
  - Second run: the pixel-level ASCII-art dump conclusively shows line 1 reading `"gos"` (confirming the `x` was correctly removed by backspace before ever being "committed" to the visible buffer) and line 2 showing a single `@` glyph (confirming Enter correctly created a new line, and `shift-2` correctly produced `@` per the Phase 4 keyboard driver's shifted-scancode table) — both exactly as expected from the input sequence.
- **Issues:** None. This is the most rigorously-verified test in this phase — going beyond "does it look right" to "is this the exact bitmap pattern the font renderer would produce for these exact characters," which rules out any ambiguity a compressed/resized screenshot glance might introduce.

**Screenshots:** `screenshots/phase7_7.3_typed_hello.png`, `screenshots/phase7_7.3_backspace_enter.png`

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| "8x16 PSF1 font, or hand-embed a public-domain font" (7.1) | Used an 8x8 public-domain C array (`font8x8_basic.h`) instead | The plan explicitly permits either approach; 8x8 is simpler to integrate (one byte per row, no need to handle a taller glyph or PSF file parsing) and was sufficient for all of this phase's rendering needs |
| (implicit) cursor position tracked as explicit (x,y) state | Implemented as a conditionally-appended `_` character rendered through the exact same string-drawing path as real text | Avoids duplicating `fb_draw_string_clipped`'s line-wrap logic in a second, parallel cursor-position calculation that could drift out of sync with the real renderer's behavior |
| (implicit) Enter/newline handling in the text box | No new code needed — composed for free from Phase 4's existing `'\n'` scancode mapping and the font renderer's pre-existing newline support | Both pieces were already correct in isolation; Milestone 7.3 only needed to *not* special-case Enter, rather than to write new line-break logic |

None of these required revisiting later-phase plans.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 7.1 | ~3–4 hours | ~1 hour (including finding a usable public-domain font) |
| Milestone 7.2 | ~3–4 hours | ~1 hour |
| Milestone 7.3 | ~4–8 hours | ~1.5 hours |
| **Phase 7 total** | **10–16 hours** | **~3.5 hours** |

**Why actual was faster than estimated:** Font rendering is a well-defined, singular technique (bit-test a glyph bitmap, plot pixels) with no design ambiguity, unlike Phase 6's windowing system which had genuine architectural choices to make. Using a ready-made public-domain font array eliminated the otherwise-tedious work of hand-transcribing glyph bitmaps or writing a PSF file parser. Milestone 7.3's keyboard-routing and cursor logic were both able to reuse existing, already-correct pieces (Phase 6's z-order-as-focus invariant, Phase 4's scancode-to-ASCII table, the font renderer's own newline handling) rather than building new mechanisms from scratch, which is the direct payoff of front-loading slightly-more-general infrastructure in earlier phases (e.g., Phase 6's z-order design already had a natural "frontmost = focused" concept that Phase 7 could adopt with zero new state).

**Revised estimate guidance for future phases:** Phase 8 (Filesystem/FAT32) is flagged in the plan as a phase where "silent corruption" risk is high, particularly for write support (Milestone 8.3) — treat the plan's 16–22 hour estimate at face value, and expect this phase's speed pattern (reusing prior infrastructure, low design ambiguity) not to repeat, since filesystem correctness bugs are categorically closer to Phase 3's memory-management risk profile than to Phase 5/7's rendering risk profile.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

All screenshots from these commands should be saved into the `screenshots/` folder (created at the project root specifically for this purpose). Commands below save directly there.

**Timing note:** as of this phase, the boot sequence's approximate timeline is: ~4-6s for the Phase 5 animation to complete and the new "Hello, gOS!" frame to appear (holds for 2s), ~13-16s until the Phase 6/7 window+text demo loop begins (running for up to ~10 more seconds). These were determined empirically for this specific build — if you add new boot-time work in a future session, re-derive timing with a multi-screendump probe (as shown in the "finding timing" example below) rather than assuming these values still hold.

### Milestone 7.1 — Bitmap font rendering

**Command to test + see ("Hello, gOS!" render):**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 5.5; printf "screendump /tmp/gos_hello.ppm\n"; sleep 0.5; printf "quit\n") | timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_test.fd
sips -s format png /tmp/gos_hello.ppm --out screenshots/phase7_7.1_hello_gos.png
open screenshots/phase7_7.1_hello_gos.png
```
**If the timing lands wrong** (shows the bouncing-orange-rectangle animation instead of text), probe for the right moment:
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_probe.fd
(sleep 5; printf "screendump /tmp/p5.ppm\n"; sleep 1; printf "screendump /tmp/p6.ppm\n"; sleep 1; printf "screendump /tmp/p7.ppm\n"; sleep 1; printf "screendump /tmp/p8.ppm\n"; sleep 0.5; printf "quit\n") | timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_probe.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_probe.fd
for t in 5 6 7 8; do sips -s format png /tmp/p$t.ppm --out /tmp/p$t.png; done
open /tmp/p5.png /tmp/p6.png /tmp/p7.png /tmp/p8.png
```
**What to check:** the text `"Hello, gOS!"` should be clearly legible, white text on a dark background, near the top-left of the screen. Every letter must be correctly formed (not mirrored, not garbled) — a bit-order bug in `fb_draw_char` would produce horizontally-flipped or scrambled glyphs.

### Milestone 7.2 — Text rendering inside windows

**Command to test + see (window titles):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_titles.fd
(sleep 15; printf "screendump /tmp/gos_titles.ppm\n"; sleep 0.5; printf "quit\n") | timeout 22 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_titles.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_titles.fd
sips -s format png /tmp/gos_titles.ppm --out screenshots/phase7_7.2_window_titles.png
open screenshots/phase7_7.2_window_titles.png
```
**What to check:** three windows visible, each with legible white title text in its title bar reading exactly `"Window A"` (blue), `"Window B"` (red), and `"Text Editor"` (green) — no text overflowing past its window's edges, no missing/garbled characters.

### Milestone 7.3 — Keyboard input routed to focused window

**Command to test + see (typing "hello" into the focused Text Editor window):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_type.fd
(sleep 16; \
 printf "sendkey h\n"; sleep 0.2; \
 printf "sendkey e\n"; sleep 0.2; \
 printf "sendkey l\n"; sleep 0.2; \
 printf "sendkey l\n"; sleep 0.2; \
 printf "sendkey o\n"; sleep 0.2; \
 printf "screendump /tmp/gos_typed.ppm\n"; sleep 0.5; \
 printf "quit\n") | timeout 25 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_type.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_type.fd
sips -s format png /tmp/gos_typed.ppm --out screenshots/phase7_7.3_typed_hello.png
open screenshots/phase7_7.3_typed_hello.png
```
**What to check:** the Text Editor window's body should show `"hello_"` (the trailing `_` is the blinking cursor — if the screenshot happens to land during the cursor's "off" phase of its blink cycle, you'll see `"hello"` with no trailing underscore, which is also correct; re-run if you specifically want to see the cursor visible).

**Command to test + see (backspace, Enter/newline, shifted character):**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_bksp.fd
(sleep 16; \
 printf "sendkey g\n"; sleep 0.2; \
 printf "sendkey o\n"; sleep 0.2; \
 printf "sendkey s\n"; sleep 0.2; \
 printf "sendkey x\n"; sleep 0.2; \
 printf "sendkey backspace\n"; sleep 0.2; \
 printf "sendkey ret\n"; sleep 0.2; \
 printf "sendkey shift-2\n"; sleep 0.2; \
 printf "screendump /tmp/gos_bksp.ppm\n"; sleep 0.5; \
 printf "quit\n") | timeout 25 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_bksp.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_bksp.fd
sips -s format png /tmp/gos_bksp.ppm --out screenshots/phase7_7.3_backspace_enter.png
open screenshots/phase7_7.3_backspace_enter.png
```
**What to check visually:** line 1 of the Text Editor body should read `"gos"` (not `"gosx"` — proving backspace removed the `x`); line 2 should show a single `@` character (proving Enter created a new line and `shift-2` produced the correct shifted symbol).

**For definitive pixel-level verification** (rather than eyeballing a small screenshot), extract the exact glyph pattern from the raw PPM:
```bash
python3 -c "
with open('/tmp/gos_bksp.ppm','rb') as f:
    data = f.read()
idx=0
def read_token(data, idx):
    while data[idx:idx+1].isspace(): idx+=1
    start=idx
    while not data[idx:idx+1].isspace(): idx+=1
    return data[start:idx], idx
magic, idx = read_token(data, idx)
w, idx = read_token(data, idx)
h, idx = read_token(data, idx)
maxv, idx = read_token(data, idx)
idx+=1
w=int(w); h=int(h)
pixels = data[idx:]
for y in range(375, 400):
    row = ''
    for x in range(283, 450):
        off = (y*w+x)*3
        r,g,b = pixels[off], pixels[off+1], pixels[off+2]
        row += '#' if (r>150 and g>150 and b>150) else '.'
    print(row)
"
```
**What to check:** the printed ASCII-art should show the letter shapes for `g`, `o`, `s` on the first non-blank row, and a single `@`-shaped glyph on the next — this is the ground-truth pixel data, not a compressed/resized approximation.

### Full Phase 7 regression check — see everything together in one boot
```bash
make clean && make iso && echo "BUILD OK"
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_full.fd
timeout 30 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_full.fd \
  -cdrom build/gos.iso \
  -display none -serial stdio -no-reboot -no-shutdown 2>&1 | grep -E "PASS|FAIL|PANIC|EXCEPTION|Hello, gOS|Window system|boot checks complete"
rm -f /tmp/OVMF_VARS_full.fd
```
Expected: `PMM self-test: PASS`, `Heap self-test: PASS`, `FB: "Hello, gOS!" rendered via bitmap font`, `Window system initialized: window_a=0 window_b=1 window_c=2...`, `Window system test loop complete`, `=== gOS boot checks complete ===`, no `FAIL`/`PANIC`/`EXCEPTION` anywhere.

**To watch it live and type yourself** (not through automated monitor commands), use `-display cocoa` and your real keyboard:
```bash
qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=third_party/ovmf/OVMF_VARS.fd \
  -cdrom build/gos.iso \
  -display cocoa -serial stdio
```
Wait for the window demo to appear (~15 seconds in), then click the "Text Editor" window's title bar to make sure it's focused, and type on your real keyboard — characters should appear in its body with a blinking cursor.

---

## Not Yet Verified (intentionally deferred to Phase 8+)

- [ ] Text box scrolling — the buffer can hold more text than fits visibly in the window body; there's no scroll mechanism yet, so text simply renders past the visible/clipped area and becomes invisible (clipped, not corrupted — safe failure mode, but not yet usable for long documents)
- [ ] Cursor movement (arrow keys, click-to-position-cursor-mid-text) — the cursor is always at the end of the buffer; inserting/editing in the middle of existing text isn't supported yet
- [ ] Extended/special keys in the text box — Phase 4's keyboard driver doesn't parse scancode set 1's extended (`0xE0`-prefixed) sequences, so arrow keys, Delete, Home/End don't produce any text box behavior yet
- [ ] Multiple text boxes per window — the current design supports exactly one text box per window (a `has_textbox` flag plus one buffer), sufficient for the file manager/text editor phases' current scope but not more complex forms
- [ ] Word wrap — long lines simply run past the clip boundary and get cut off pixel-by-pixel rather than wrapping to the next line

---

## Next Step

Proceed to **Phase 8 — Filesystem (FAT32)** (Milestone 8.1: Disk/block device access), per [PROJECT_PLAN.md](PROJECT_PLAN.md). This is flagged in the plan's own risk notes as high-risk for silent corruption, particularly the write-support milestone (8.3) — test against a disposable copy of any disk image, not the only copy, until write support is proven solid.
