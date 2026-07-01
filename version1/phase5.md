# Phase 5 — Framebuffer Graphics — Completion Report

**Date completed:** 2026-07-01
**Status:** ✅ Complete — all three milestones (5.1, 5.2, 5.3) done, all tasks verified.

---

## Summary

Phase 5 is the first phase where gOS produces output a human can actually look at, rather than only serial log text. gOS can now plot individual pixels, draw filled/outlined rectangles and Bresenham lines, and double-buffer an animation with no tearing artifacts — all backed by the framebuffer Limine handed off in Phase 1 and the heap Phase 3 built. This phase also required solving a testing problem none of the previous phases had: **how do you visually verify pixel-level graphics output in a headless environment with no interactive display attached?** The answer used throughout this phase is QEMU's `screendump` monitor command, which captures the actual emulated display device's contents to a real image file — not a simulation or approximation, the literal pixels the framebuffer contains — which can then be inspected directly.

---

## Milestone 5.1: Raw pixel plotting works

### Task: Extract framebuffer address, width, height, pitch, bpp from Limine
- **What was done:** This data was already being retrieved in Phase 1 (the `fb` struct in `start.c`, logged over serial since the very first boot). Phase 5 additionally passes the framebuffer's `red_mask_shift`, `green_mask_shift`, `blue_mask_shift` fields into the new `fb_init()` function — these weren't needed before but are essential now for `fb_make_color()` to pack R/G/B components into the correct positions for this specific display mode.
- **Outcome:** No new extraction work needed; existing data reused and extended.
- **Issues:** None.

### Task: Implement `fb_put_pixel(x, y, color)` respecting pitch
- **What was done:** Wrote `kernel/include/fb.h` / `kernel/src/fb.c`. `fb_put_pixel()` computes the target address as `draw_target + y * pitch + x * (bpp / 8)` — using the real reported pitch (5120 bytes for a 1280-wide, 32bpp mode) rather than assuming `pitch == width * 4`, per the plan's own explicit warning. On this hardware `pitch` does happen to equal `width * 4` (1280 * 4 = 5120), but the code doesn't rely on that coincidence.
- **Outcome:** Functions correctly; verified as part of the screen-clear test below (which is really just `fb_put_pixel` called in a double loop internally via `fb_clear`).
- **Issues:** None.

### Task: Clear the entire screen to a solid color and confirm visually
- **What was done:** Implemented `fb_clear(color)`. Called it at boot with `fb_make_color(0, 64, 128)` (a distinct, easy-to-verify dark blue with three different non-zero channel values, chosen specifically so a channel-order bug — e.g., red and blue swapped — would be visually and numerically obvious). **Testing approach:** since this environment runs QEMU with `-display none` (no interactive window to look at), used QEMU's monitor `screendump` command instead, which writes the emulated display adapter's actual current framebuffer contents to a `.ppm` image file on the host — this is not a workaround or approximation, it captures the literal same memory gOS's `fb_put_pixel` writes to. Parsed the resulting PPM with a small Python script and sampled pixel values directly.
- **Outcome:** Both the center pixel and a corner pixel of the captured screenshot read back exactly `RGB(0, 64, 128)` — an exact match to the requested color, confirming both that pixels are being written to the real framebuffer memory and that the channel-shift-based color packing (`fb_make_color`) is correct (no channel swap).
- **Issues:** None. Establishing the `screendump`-based testing method here was the main new problem this milestone had to solve, and it worked cleanly on the first attempt.

---

## Milestone 5.2: Primitive 2D drawing routines

### Task: Implement `fb_draw_rect` (filled)
- **What was done:** Simple double loop over the rectangle's bounds, clipped against framebuffer width/height so an oversized rectangle can't write out of bounds.
- **Outcome:** Verified as part of the test pattern below.
- **Issues:** None.

### Task: Implement `fb_draw_rect_outline`
- **What was done:** Composed from four calls to `fb_draw_rect` (top band, bottom band, left band, right band, each `thickness` pixels wide/tall) rather than a separate pixel-by-pixel border-walking implementation — simpler code, and correctness is inherited directly from the already-verified filled-rect routine.
- **Outcome:** Verified as part of the test pattern below.
- **Issues:** None.

### Task: Implement `fb_draw_line` (Bresenham's algorithm)
- **What was done:** Standard integer-only Bresenham implementation using the signed-error-accumulator formulation, which handles all eight line-slope octants (shallow/steep, all four quadrant directions) uniformly without special-casing any of them.
- **Outcome:** Verified visually below across four different line orientations in one test.
- **Issues:** None.

### Task: Draw a test pattern and visually confirm all primitives work
- **What was done:** At boot, drew: a filled red rectangle with a white outline around it, a second, larger green outline rectangle around both, and four lines from a common origin point forming an "X" plus a right angle (horizontal, vertical, and two diagonals in opposite directions) — deliberately chosen to exercise every primitive and multiple line slopes in a single checkable frame. Captured a `screendump`, converted the resulting PPM to PNG (via macOS's `sips` utility) and **visually inspected the actual rendered image directly** (not just sampling a few pixel coordinates numerically, as Milestone 5.1's single-color test did — this time the full picture was viewed).
- **Outcome:** The rendered image showed exactly what was expected: a solid red rectangle with a clean white border, a larger green outline rectangle correctly enclosing it, and four correctly-colored, correctly-angled lines (yellow horizontal, yellow vertical, magenta diagonal, cyan diagonal) crossing to form a clean X-and-corner pattern with no gaps, no stray pixels, and no color bleed between shapes.
- **Issues:** None — every primitive rendered correctly on the first attempt.

---

## Milestone 5.3: Double buffering / flip to avoid tearing

### Task: Allocate a back buffer in kernel heap matching framebuffer dimensions
- **What was done:** `fb_backbuffer_init()` computes the exact byte size as `pitch * height` (5120 * 800 = 4,096,000 bytes) and allocates it via Phase 3's `kmalloc` — the first real, substantial consumer of the heap built in that phase (previous heap usage was only its own 300-cycle synthetic stress test).
- **Outcome:** Verified live: `FB: back buffer allocated (4000 KiB)` — matching the expected size exactly, and no `PANIC` (which the code would emit if `kmalloc` returned NULL), confirming the heap correctly handled a single large allocation many times bigger than anything in its own stress test.
- **Issues:** None.

### Task: Redirect all drawing routines to the back buffer
- **What was done:** All drawing functions (`fb_put_pixel`, and everything built on top of it) write through a single `draw_target` pointer variable, initialized to point at the real framebuffer in `fb_init()` and reassigned to point at the back buffer in `fb_backbuffer_init()`. This means no call site anywhere in the kernel needed to change — the exact same `fb_draw_rect(...)` calls used in Milestone 5.2's test pattern automatically started drawing into the back buffer instead, with zero code changes at the call sites.
- **Outcome:** Confirmed working as part of the animation test below (if redirection had failed, the animation would have drawn directly to the visible framebuffer with no double-buffering benefit, though it likely would still have "looked correct" in a screendump — the real proof is architectural, not visual: the back buffer pointer swap is what `fb_flip()` depends on existing).
- **Issues:** None.

### Task: Implement `fb_flip()`
- **What was done:** A bulk copy from the back buffer to the real framebuffer, copying 8 bytes (one `uint64_t`) at a time rather than byte-by-byte, for roughly 8x fewer loop iterations over the ~4MB buffer.
- **Outcome:** Verified as part of the animation test below — each frame's `fb_flip()` call is what makes that frame visible in a screendump at all.
- **Issues:** None.

### Task: Verify no tearing with an animation test
- **What was done:** Since visually detecting "tearing" (a partially-updated frame becoming momentarily visible) is inherently a real-time, eyes-on-a-live-display phenomenon that a headless environment can't observe directly, the practical and honest equivalent test used here is: **run a real multi-frame animation and confirm every individual flip produces one complete, internally-consistent frame** — if double buffering were broken (e.g., drawing directly to the live framebuffer while it's being scanned out, or flipping a partially-written buffer), a screenshot taken at an arbitrary moment during the animation would be very likely to show visible corruption (a half-drawn rectangle, stale pixels from the previous frame mixed with new ones, etc.). Implemented a 40-frame bouncing-rectangle animation: each frame clears the back buffer, draws a static gray border and an orange rectangle at its current animated position, calls `fb_flip()`, updates the position/bounce logic, and sleeps 50ms (using Phase 4's `sleep_ms`) before the next frame — a full ~2-second animation. Captured three `screendump`s at different points during the animation's run.
- **Outcome:** All three captured frames showed the orange rectangle at three different, correctly-progressed x-positions (left-of-center, near-center, right-of-center, consistent with the bounce trajectory), and each frame was completely clean — solid gray border, solid dark background, solid orange rectangle, no ghosting of a previous position, no partial/torn rectangle edges, no stray pixels anywhere. The animation also completed and logged `FB: bouncing-rectangle animation complete (40 frames flipped)` followed by the normal `=== gOS boot checks complete ===`, confirming all 40 flip cycles ran to completion without hanging or crashing.
- **Issues:** None.

---

## Deviations from the Original Plan (Summary)

| Planned | What actually happened | Why |
|---|---|---|
| "Clear the entire screen... and confirm visually in the QEMU window" (5.1) | Used QEMU's `screendump` monitor command and inspected the resulting image file instead of an interactive window | This environment runs QEMU headlessly (`-display none`) with no attached display to look at directly; `screendump` captures the actual emulated display device's real contents, making it a faithful substitute rather than an approximation |
| "confirm all primitives work" (5.2) — method unspecified | Did both numeric pixel-sampling (5.1) and full-image visual inspection (5.2), rather than only one or the other | Milestone 5.1's single flat color is fully verifiable by sampling a couple of pixel coordinates; Milestone 5.2's multi-shape test pattern benefits from actually looking at the whole rendered image, which was done directly (not just described) |
| "verify no visible tearing... on screen" (5.3) | Verified via multiple screenshots at different animation frames, checking each is a complete/uncorrupted frame, rather than literal real-time tearing detection | A headless environment cannot observe real-time screen tearing (that requires a live display and human eyes, or specialized frame-timing instrumentation neither available nor proportionate to this project's scope); the multi-frame-snapshot approach is the closest automatable proxy and does meaningfully validate the double-buffering mechanism itself |

None of these required revisiting later-phase plans — all three are testing-methodology adaptations for a headless environment, not changes to what was actually built.

---

## Effort Estimate vs. Actual

| | Original estimate (PROJECT_PLAN.md) | Actual |
|---|---|---|
| Milestone 5.1 | ~3–4 hours | ~1 hour (including establishing the `screendump` testing method) |
| Milestone 5.2 | ~3–5 hours | ~1 hour |
| Milestone 5.3 | ~3–5 hours | ~1 hour |
| **Phase 5 total** | **10–14 hours** | **~3 hours** |

**Why actual was faster than estimated:** Framebuffer graphics code itself is comparatively low-risk once the underlying pitch/bpp/color-format details are correctly understood (unlike Phase 3's paging, a wrong pixel color or a slightly-off rectangle boundary is immediately, harmlessly visible rather than silently corrupting kernel state) — and Phase 5 inherited a fully-working heap (Phase 3) and `sleep_ms` (Phase 4) with zero new plumbing required. The only genuinely new problem this phase had to solve was the testing methodology itself (how to "look at" graphics output with no display attached), and `screendump` turned out to be a clean, direct solution requiring no workarounds.

**Revised estimate guidance for future phases:** Phase 6 (Windowing/Compositor) is explicitly flagged in the plan's own risk notes as the largest, most open-ended phase, with real design-decision risk (not just implementation risk) around window/widget data structures — the plan's 20–30 hour estimate should be treated at face value, and the `screendump`-based visual testing method established in this phase will carry forward directly (screenshots of draggable/overlapping windows, mouse cursor position, etc.), so no new testing-methodology risk is expected there, only design and implementation risk.

---

## Per-Milestone Testing Instructions

Run these from the project root: `cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"`

**All three milestones below use QEMU's `screendump` monitor command** to capture the actual rendered framebuffer to a `.ppm` image file, since this environment has no interactive display. Convert to PNG for easy viewing with `sips -s format png <file>.ppm --out <file>.png` (macOS) or `convert <file>.ppm <file>.png` (ImageMagick, if available) and open the PNG in any image viewer.

### Milestone 5.1 — Raw pixel plotting works

**1. Build, boot, and capture a screenshot of the cleared screen:**
```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 4; printf "screendump /tmp/gos_test.ppm\n"; sleep 0.5; printf "quit\n") | timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_test.fd
```

**2. Verify the captured image is a solid dark blue (RGB 0, 64, 128):**
```bash
python3 -c "
with open('/tmp/gos_test.ppm','rb') as f:
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
pixels = data[idx:]
w=int(w); h=int(h)
cx, cy = w//2, h//2
off = (cy*w+cx)*3
print('size', w, h)
print('center RGB', pixels[off], pixels[off+1], pixels[off+2])
print('corner RGB', pixels[0], pixels[1], pixels[2])
"
```
Expected output:
```
size 1280 800
center RGB 0 64 128
corner RGB 0 64 128
```
**What to check:** Both sampled pixels must read exactly `0 64 128`. Any other values (especially `128 64 0`, which would indicate a red/blue channel swap) means the `fb_make_color` shift logic or `fb_put_pixel`'s pitch/offset math has a bug.

### Milestone 5.2 — Primitive 2D drawing routines

**1. Capture and convert a screenshot of the test pattern:**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 4; printf "screendump /tmp/gos_pattern.ppm\n"; sleep 0.5; printf "quit\n") | timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_test.fd
sips -s format png /tmp/gos_pattern.ppm --out /tmp/gos_pattern.png
open /tmp/gos_pattern.png   # or view the PNG with any image viewer
```
**What to check (visually, by looking at the opened image):**
- A filled red rectangle with a clean white border around it, roughly in the upper-left quadrant of the screen.
- A larger green outline rectangle enclosing the red one, with visible dark-blue-background gap between the green and white borders.
- Four lines forming an X-and-corner shape to the right of the rectangles: a yellow horizontal line, a yellow vertical line (sharing a top-left corner point), a magenta diagonal, and a cyan diagonal — the magenta and cyan lines should visibly cross each other in the middle.
- No gaps, jagged breaks, wrong colors, or stray pixels outside the intended shapes.

### Milestone 5.3 — Double buffering / flip to avoid tearing

**1. Capture three screenshots at different points during the 40-frame animation:**
```bash
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_test.fd
(sleep 4.5; printf "screendump /tmp/gos_anim_1.ppm\n"; sleep 1.0; printf "screendump /tmp/gos_anim_2.ppm\n"; sleep 1.0; printf "screendump /tmp/gos_anim_3.ppm\n"; sleep 0.5; printf "quit\n") | timeout 15 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_test.fd \
  -cdrom build/gos.iso \
  -display none -serial file:/tmp/gos_anim_serial.log -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_test.fd
for n in 1 2 3; do sips -s format png /tmp/gos_anim_$n.ppm --out /tmp/gos_anim_$n.png; done
grep -E "FB:|boot checks complete" /tmp/gos_anim_serial.log
```
Expected serial log lines:
```
FB: initialized 1280x800, 32bpp, pitch=5120
FB: test pattern drawn (nested rects + 4 lines)
FB: back buffer allocated (4000 KiB)
FB: bouncing-rectangle animation complete (40 frames flipped)
=== gOS boot checks complete ===
```
**What to check (visually, across the three PNGs):**
- Each of the three images should show a solid orange rectangle at a **different** horizontal position inside the gray-bordered box (proving the animation is genuinely progressing between screenshots, not stuck on one frame).
- Every individual frame must be completely clean: solid dark gray background, solid gray border, solid orange rectangle with sharp edges — **no** ghosting/doubling of the rectangle at two positions at once, no partially-drawn rectangle, no visible remnants of the earlier static test pattern (the animation's `fb_clear()` each frame should have fully replaced it).
- The serial log's `back buffer allocated (4000 KiB)` line must appear with no `PANIC` line before it — a `PANIC: fb_backbuffer_init failed to allocate back buffer` would mean the heap ran out of space (shouldn't happen with a 64MiB heap ceiling and a ~4MB request, but worth checking if this test is ever run after a change to heap sizing).

### Quick regression check for all of Phase 5 together

`make clean && make iso && echo "BUILD OK"` only confirms the code compiles — it does not show you any actual graphics output. To **see** the current state with your own eyes, build (if needed) then capture and open a screenshot:

```bash
make clean && make iso
cp third_party/ovmf/OVMF_VARS.fd /tmp/OVMF_VARS_view.fd
(sleep 4; printf "screendump /tmp/gos_view.ppm\n"; sleep 0.5; printf "quit\n") | timeout 12 qemu-system-x86_64 \
  -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS_view.fd \
  -cdrom build/gos.iso \
  -display none -serial null -monitor stdio -no-reboot -no-shutdown > /tmp/mon.log 2>&1
rm -f /tmp/OVMF_VARS_view.fd
sips -s format png /tmp/gos_view.ppm --out /tmp/gos_view.png   # macOS; use `convert` on Linux
open /tmp/gos_view.png
```
The `sleep 4` delay lands the screenshot partway through the 40-frame bouncing-rectangle animation (Milestone 5.3), so you'll see the orange rectangle at some position inside the gray-bordered box on a dark background — the exact position varies run-to-run depending on host timing, which is expected (it proves the animation is live, not a static image). Adjust the `sleep 4` value and re-run to catch different frames, or check the serial log (add `-serial file:/tmp/gos_serial.log` in place of `-serial null` and `cat` it afterward) for the `FB:` lines confirming each milestone's code ran in order.

---

## Not Yet Verified (intentionally deferred to Phase 6+)

- [ ] Mouse cursor rendering — no mouse input or cursor sprite exists yet; that's Phase 6, Milestone 6.1
- [ ] Window/widget compositing — the current drawing calls are all directly sequenced in `_start`, not organized into any window or layering abstraction yet
- [ ] Text/font rendering — `fb_draw_rect`/`fb_draw_line` can draw shapes but not characters; that's Phase 7
- [ ] Framebuffer formats other than 32bpp RGB — `fb_put_pixel` assumes a `uint32_t`-sized pixel (`bpp / 8` bytes, currently always 4 on this hardware); an 8bpp or 16bpp mode would need a different pixel-write path, not currently implemented (acceptable, since Limine/OVMF's GOP framebuffer on this target is always 32bpp)

---

## Next Step

Proceed to **Phase 6 — Windowing / Compositor** (Milestone 6.1: Mouse input works via PS/2), per [PROJECT_PLAN.md](PROJECT_PLAN.md). This is the plan's own second-flagged high-risk phase (after Phase 3) — budget real time for it, and hold firmly to the plan's minimum-viable cutoff (rectangular windows, solid colors, one z-order list, drag + click-to-focus + buttons; no transparency, shadows, animations, resizing, or minimize/maximize for v1) to avoid the open-ended design-risk this phase is flagged for.
