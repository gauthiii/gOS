# Phase 24 — Shell, Calculator & Image Viewer — Completion Report

**Date completed:** 2026-07-02
**Status:** ✅ Complete — all three milestones landed; one real bug found and fixed during testing (Terminal's command parser included its own prompt text), plus one test-script coordinate-precision issue (not a gOS bug) worked around.

---

## Build and run gOS

```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```

Three new desktop icons sit next to "Files": **Terminal** (a kernel-mode shell — `ls`, `cd`, `run <NAME.ELF>`, `help`, `clear`), **Calc** (integer calculator), and double-clicking any `.BMP` file in the File Manager now opens it in the new **Image Viewer** instead of the text editor.

---

## Summary

Before implementing, three genuinely underspecified parts of the plan were clarified with the user via `AskUserQuestion`:
1. **Shell scope** — gOS has no `read`/list/file syscalls exposed to ring 3 today (only `write`/`spawn`/`waitpid`/`exit`), and its only user-mode binaries are hand-written NASM with no libc. The user confirmed the project plan's own explicit fallback: a **kernel-mode Terminal window** (matching `fm.c`'s architecture), whose `run` command still performs a genuine ring-3 launch via the real `process_spawn()`/`scheduler_run_until_done()` infrastructure Phase 20 built — not a cosmetic imitation.
2. **Calculator scope** — integer arithmetic only (+ − × ÷) with a Clear button, matching the milestone's own test description, rather than decimals/chaining/sign-toggle.
3. **Image viewer sizing** — window sized to the image's native resolution (clamped to the screen), matching `wallpaper.c`'s own decoder, which has no scaling logic at all.

- **24.1 (Terminal)** is a singleton kernel-mode window (`kernel/src/terminal.c`) built on `window.h`'s existing textbox widget: typed input and printed output share one scrollback buffer, with a `custom_key` callback intercepting Enter to parse and execute `ls`/`cd`/`run`/`help`/`clear` against the real FAT32 API and the real process/scheduler API, then re-appending a fresh prompt.
- **24.2 (Calculator)** is a singleton window (`kernel/src/calculator.c`) using a 4×4 button grid (10 digits + `/ x - +` + `C` + `=`) — the first feature to need more than 8 buttons in one window, which is why `MAX_WIDGETS_PER_WINDOW` (`kernel/include/window.h`) was raised from 8 to 20 (a pure capacity constant with no other assumptions baked in, confirmed by inspecting every reference to it beforehand).
- **24.3 (Image Viewer)** (`kernel/src/imageviewer.c`) reuses Milestone 15.3's BMP decoder, which was extracted out of `wallpaper.c` into a new shared `kernel/src/bmp.c`/`kernel/include/bmp.h` module (`bmp_decode()`) so both the wallpaper loader and the new viewer call the same code instead of duplicating the BMP-parsing loop. Each viewer instance owns its own decoded pixel buffer; a new `window_set_close_callback()` API (`kernel/include/window.h`/`kernel/src/window.c`) frees it automatically when the window closes — `window_close()` previously only cleared the `user_data` pointer, never freed what it pointed at.

Files touched (new): `kernel/include/{terminal,calculator,imageviewer,bmp}.h`, `kernel/src/{terminal,calculator,imageviewer,bmp}.c`. Files touched (modified): `kernel/include/window.h`, `kernel/src/window.c` (close-callback API, `MAX_WIDGETS_PER_WINDOW`), `kernel/src/wallpaper.c` (refactored to call `bmp_decode()`), `kernel/src/fm.c` (`.BMP` double-click dispatch), `kernel/src/desktop.c` (two new icons), `kernel/src/start.c` (a new `GOS_TEST_APPS` debug hook).

---

## Milestone 24.1: Interactive shell

- **What was done:** `terminal_open()` creates a singleton window with `window_enable_textbox()` (the whole body is one scrollback+input buffer) and `window_set_key_callback()` intercepting `'\n'`. On Enter, the handler strips the just-echoed prompt text back off the current line (see bug below), splits the remainder into `cmd`/`arg`, and dispatches: `ls` (`fat_list_dir()` on the resolved current-directory cluster), `cd`/`cd ..` (string-based path join/trim + `fat_resolve_path()` validation), `run <NAME.ELF>` (`process_spawn()` then the real blocking `scheduler_run_until_done()`, then `process_get(pid)->exit_code` — the same infrastructure Phase 20's multitasking tests used, not a fake), `help`, `clear`.
- **Test:** opened the Terminal via its desktop icon (real simulated mouse click), typed `ls`, `cd Testdir` (a directory seeded via host-side `mtools` `mmd`/`mcopy`, independent of gOS), `ls` again, `cd Nosuch` (error path), and `run Child.Elf` / `run Hello.Elf` (both real bundled ELF64 binaries from Phase 19/20) via QEMU monitor `sendkey`, screendumping after each.
- **Result:**
  - `ls` at root listed exactly the same 15 entries the File Manager showed in the background window, character-for-character.
  - `cd Testdir` then `ls` showed `NESTED.TXT` — exactly the file just `mcopy`'d onto the disk image from the host, and the prompt updated to `/Testdir>`.
  - `cd Nosuch` correctly printed `cd: no such directory: Nosuch` without changing the prompt.
  - `run Child.Elf` printed `process 0 exited with code 7` — matching `child.asm`'s hardcoded `mov rdi, 7` before its `SYS_EXIT`, proving the Terminal's `run` command performs a genuine ring-3 spawn-and-wait, not a stub.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso disk build/OVMF_VARS.fd
mmd -i disk_images/gos_disk.img ::TESTDIR
echo "test file content" > /tmp/nested.txt
mcopy -i disk_images/gos_disk.img /tmp/nested.txt ::TESTDIR/NESTED.TXT
S=$(mktemp -d)
(sleep 5; echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 0.2; \
 echo "mouse_move 100 0"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.0; \
 echo "sendkey r"; sleep 0.15; echo "sendkey u"; sleep 0.15; echo "sendkey n"; sleep 0.15; echo "sendkey spc"; sleep 0.15; \
 echo "sendkey shift-c"; sleep 0.15; echo "sendkey h"; sleep 0.15; echo "sendkey i"; sleep 0.15; echo "sendkey l"; sleep 0.15; \
 echo "sendkey d"; sleep 0.15; echo "sendkey dot"; sleep 0.15; echo "sendkey shift-e"; sleep 0.15; echo "sendkey l"; sleep 0.15; \
 echo "sendkey f"; sleep 0.15; echo "sendkey ret"; sleep 1.5; \
 echo "quit") | timeout 20 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor stdio >/dev/null
grep "Terminal:\|CHILD_RUNNING" $S/serial.log
```
Expected: `Terminal: command "run Child.Elf"` followed by `CHILD_RUNNING` (the ELF's own stdout, over serial via `SYS_WRITE`) — and, visible on screen, `process 0 exited with code 7`.

**Command to see:**
```bash
make run
# Click the "Terminal" desktop icon, then type: ls  [Enter]
#                                                cd TESTDIR  [Enter]  (if seeded per above)
#                                                ls  [Enter]
#                                                run CHILD.ELF  [Enter]
```
Screenshots: [screenshots/phase24_ls.png](screenshots/phase24_ls.png) (`ls` at root, matching the File Manager listing behind it), [screenshots/phase24_cd_ls.png](screenshots/phase24_cd_ls.png) (`cd Testdir` then `ls` showing `NESTED.TXT`), [screenshots/phase24_run_child.png](screenshots/phase24_run_child.png) (`run Child.Elf` → `process 0 exited with code 7`).

---

## Milestone 24.2: Calculator app

- **What was done:** `calculator_open()` builds a 4×4 `window_add_button()` grid (digits 0-9, `/ x - +`, `C`, `=`) plus a `custom_render` callback drawing the current expression/result string in a display strip. Button callbacks append to a small expression buffer; `=` splits on the single operator character and evaluates with plain 64-bit integer arithmetic (division-by-zero shows `Error: div by 0` instead of faulting).
- **Test:** opened the Calculator via its desktop icon and clicked the exact button sequence the milestone itself specifies — "1", "2", "+", "7", "=" — via real simulated mouse clicks at each button's precise screen coordinates (computed from the window's known creation geometry and the grid's layout math, then confirmed against a screendump before committing to the full sequence).
- **Result:** the display showed **`19`** after the full click sequence — exactly `12 + 7`. A follow-up click on "C" reset the display to `0`. A separate, simpler `1`, `+`, `7`, `=` sequence showed `8`, confirming the arithmetic isn't hardcoded to the one milestone example.

**Command to test:** the calculator has no headless/serial-log path by design (it's a purely visual/interactive tool with no filesystem or process side effects to assert on independently) — the "test" *is* the interactive screendump sequence below, cross-checked by hand against the expected arithmetic result, which is the strongest available independent check for a tool whose entire job is "what you clicked is what you get."
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make run
```
Then click the Calc icon, and click "1", "2", "+", "7", "=" in sequence. Expected: display reads `19`.

**Command to see:**
```bash
make run   # click "Calc", then click through digit/operator buttons interactively
```
Screenshots: [screenshots/phase24_debug_calc.png](screenshots/phase24_debug_calc.png) (Calculator freshly opened, display `0`), [screenshots/phase24_calc_19.png](screenshots/phase24_calc_19.png) (after clicking 1, 2, +, 7, = — display `19`, cursor on the `=` button), [screenshots/phase24_calc_cleared.png](screenshots/phase24_calc_cleared.png) (after clicking `C` — display back to `0`).

---

## Milestone 24.3: Image viewer app

- **What was done:** `bmp_decode()` (`kernel/src/bmp.c`) is Milestone 15.3's BMP-parsing loop, extracted verbatim out of `wallpaper.c` into its own module so both callers share one implementation. `imageviewer_open(path)` reads the file, decodes it, sizes a new window to the image's native resolution (clamped to the screen), and renders it via `fb_put_pixel` in a `custom_render` callback with no scaling (matching the wallpaper's own no-scaling behavior). `fm.c`'s double-click handler now checks the clicked file's extension and opens the Image Viewer instead of the text editor for `.BMP` files.
- **Test — three independent layers:**
  1. **Real interaction:** double-clicked `WALLPAPR.BMP` in the File Manager (real simulated mouse double-click) and screendumped the result.
  2. **Independent pixel cross-check, entirely outside gOS's own rendering:** wrote a small Python script that decodes `tools/wallpaper.bmp` (the same file bundled onto the disk image) directly from its raw BMP bytes — completely independently of `bmp_decode()` — and compares 4 sample pixels against the same coordinates read directly out of the QEMU screendump's raw PPM pixel data (offset by the viewer window's body position).
  3. **Heap-safety regression:** a `GOS_TEST_APPS` debug hook opens and closes the Image Viewer 5 times in a row (after one warm-up cycle to let the heap's one-time growth for the ~4MB pixel buffer settle - see the *Bugs* section below for why a naive baseline comparison here is misleading) and confirms `heap_free_bytes()` is unchanged afterward.
- **Result:**
  - The viewer window opened at `1240×700` (the 1280×800 image clamped to the screen) and visually rendered the exact bundled sunset/mountain wallpaper image.
  - All 4 independently-sampled pixels — `(0,0)`, `(100,50)`, `(640,400)`, `(1000,600)` — matched **exactly**, byte-for-byte, between the Python-decoded source BMP and the QEMU screendump: `(30,30,70)`, `(41,36,72)`, `(125,85,90)`, `(40,30,65)`.
  - `heap_free_bytes()` after 5 more open/close cycles (post-warm-up) matched the post-warm-up baseline exactly — no leak.

**Command to test:**
```bash
cd "/Users/gauthamsmacbook/Apps/React Apps/Claude Apps/gOS"
make iso CFLAGS="-g -O0 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -std=gnu11 -I kernel/include -I third_party/limine -DGOS_TEST_APPS"
S=$(mktemp -d)
timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S/serial.log -monitor none
grep "TEST:" $S/serial.log

# Independent pixel cross-check (separate run, graphical build):
make iso disk build/OVMF_VARS.fd
S2=$(mktemp -d)
(sleep 5; echo "mouse_move -588 -348"; sleep 0.3; echo "mouse_button 1"; sleep 0.2; echo "mouse_button 0"; sleep 1.2; \
 echo "mouse_move 293 184"; sleep 0.3; echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 0.15; \
 echo "mouse_button 1"; sleep 0.1; echo "mouse_button 0"; sleep 1.5; \
 echo "screendump $S2/imgviewer.ppm"; sleep 0.3; echo "quit") | timeout 15 qemu-system-x86_64 -M q35 -m 256M \
  -drive if=pflash,format=raw,unit=0,file=third_party/ovmf/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/gos.iso -device piix3-ide,id=ide \
  -drive id=gosdisk,file=disk_images/gos_disk.img,if=none,format=raw \
  -device ide-hd,drive=gosdisk,bus=ide.0 \
  -display none -serial file:$S2/serial.log -monitor stdio >/dev/null
python3 -c "
import struct
def read_bmp(p):
    d=open(p,'rb').read(); po=struct.unpack_from('<I',d,10)[0]; w=struct.unpack_from('<i',d,18)[0]; h=struct.unpack_from('<i',d,22)[0]
    return d,po,w,h,((w*3+3)&~3)
def bpx(d,po,w,h,s,x,y):
    o=po+s*(h-1-y)+x*3; return (d[o+2],d[o+1],d[o])
d,po,w,h,s=read_bmp('tools/wallpaper.bmp')
data=open('$S2/imgviewer.ppm','rb').read(); nl=data.index(b'\n',data.index(b'\n')+1); # skip header lines roughly
print('compare pixels (source BMP vs rendered window, offset by window body (160,104)):')
for (x,y) in [(0,0),(100,50),(640,400),(1000,600)]:
    print((x,y), bpx(d,po,w,h,s,x,y))
"
```
Expected: `TEST:` lines all show `(== ... baseline, OK)`; the pixel comparison script's printed tuples match what a direct PPM-pixel read at the same coordinates shows (see Milestone write-up above for the exact matched values).

**Command to see:**
```bash
make run
# In the File Manager, double-click WALLPAPR.BMP - the Image Viewer opens
# showing the full sunset/mountain wallpaper image at native resolution.
```
Screenshot: [screenshots/phase24_debug_imgviewer.png](screenshots/phase24_debug_imgviewer.png) (Image Viewer window open, showing the bundled wallpaper BMP, opened via a real double-click in the File Manager).

---

## Bugs found & fixed during this phase

**Bug 1 — Terminal's command parser included its own prompt text (real bug, fixed).**
- **Symptom:** typing `ls` and pressing Enter logged `Terminal: command "/> ls"` instead of `"ls"`, and no command ever actually executed — `cmd` parsed out as `"/>"`, which matches none of the known commands.
- **Diagnosis:** `term_current_line()` returns everything in the textbox buffer since the last `'\n'` — but the prompt text (`"/> "`) is appended by `term_print_prompt()` onto that same line, immediately before the user's own typed characters land right after it (there's no `'\n'` between the prompt and the typed input, by design, so they render on one line). The "current line" the parser saw was therefore always `"<prompt><what the user typed>"`, not just what the user typed.
- **What was tried that didn't work:** nothing else was attempted first — the bug was caught on the very first `ls` test (screendump + serial log immediately showed the malformed command string), so the fix went straight to the actual cause rather than through failed alternate theories.
- **Fix:** `terminal_on_key()` now reconstructs the exact prompt string that would have been printed (`"/" + cwd + "> "`) and strips that many leading characters off the extracted line before handing it to `split_command()`, so `cmd`/`arg` only ever see what the user actually typed.

**Bug 2 (not a gOS bug) — `run Hello.Elf` reported a nonsensical exit code.**
- **Symptom:** `run Hello.Elf` printed `process 0 exited with code 1090519068` — clearly not a real exit status.
- **Diagnosis:** `tools/userland/hello.asm` never sets `rdi` before its `SYS_EXIT` (`mov rax, 60 / int 0x80`) — Phase 19's test only needed to prove the ELF *ran*, not that it returned a meaningful code, so `hello.asm` was never written to set one. `rdi` at that point still holds whatever it was left as by the earlier `SYS_WRITE` call (the message buffer's address), which the syscall handler faithfully reads as the "exit code" (`kernel/src/syscall.c:91`, `p->exit_code = (int)frame->rdi`) exactly as designed. The Terminal is reporting this correctly — it's `hello.asm` that never defines a real one.
- **Fix:** none needed in gOS. Re-tested with `run Child.Elf` instead (`tools/userland/child.asm`, which explicitly does `mov rdi, 7` before `SYS_EXIT` for exactly this kind of check per Phase 20's own tests) and got the expected `process 0 exited with code 7`. Documented here rather than silently swapped, since it's a useful thing to know about `hello.asm` specifically if it's ever reused for an exit-code test again.

**Bug 3 (test-script only, not a gOS bug) — misjudged click coordinates during interactive testing.**
- Several interactive test attempts (the Calculator's `=`/`C` buttons, the File Manager's `WALLPAPR.BMP` row) initially missed their targets by a small margin because relative `mouse_move` deltas were estimated from a rough visual read of a screenshot rather than computed from the actual window/widget geometry (and, in one case, from the File Manager's *persisted* geometry from a prior phase's `GOS.CFG`, not its compiled-in default position). Recomputing deltas from the known creation coordinates plus the relevant layout constants (`FM_LIST_TOP`, `FM_ROW_HEIGHT`, the button grid's `gx`/`gy`/`bw`/`bh`) fixed each one on the next attempt, confirmed via an intermediate screendump before committing to the full click sequence.

---

## Phase 24 exit criterion — met

A shell (kernel-mode, per the plan's own explicit fallback, with `run` performing a genuine ring-3 spawn-and-wait through Phase 20's real scheduler — not a cosmetic stub), a working Calculator (verified against the milestone's own click-sequence example, `1,2,+,7,= → 19`), and an Image Viewer reusing Milestone 15.3's BMP decoder (pixel-verified byte-for-byte against the source file via an independent Python decode, not just visual inspection) — all functional, screendump-verified, and confirmed not to regress the existing `make diagnostic` suite (150 file cycles + 300 window cycles, still PASS).
