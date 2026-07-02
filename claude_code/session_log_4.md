# gOS Build Session 4 — Claude Code Conversation Log

This file continues from `session_log_3.md` (the v2 plan through Track A completion). It covers the fourth session: all of Track B (Phase 15 — Cursor & Wallpaper, Phase 16 — Window Close/Minimize/Taskbar, Phase 17 — Maximize), a live boot-hang debugging request in the middle of it, a full v3-roadmap brainstorm that was then reclassified back into v2 as Tracks C/D/E, and five more executed phases: Phase 18 (boot-time cleanup), Phase 19 (user mode/syscalls/ELF loader), Phase 20 (preemptive multitasking, done partly on a session-budget-constrained model), Phase 21 (window resize & Alt+Tab), and Phase 22 (RTC/taskbar clock/settings persistence).

**How to use this file:** Same as prior session logs — copy the "Suggested prompt for next time" version when repeating a similar request.

**Scoring scale:** 1–10, based on: clarity of scope, whether success criteria were explicit, whether format/output location was specified, and how much back-and-forth was needed before work could start.

---

## Prompt 1 — Phase 15 execution (Cursor & Wallpaper, Track B begins)

**Prompt:**
> Now do Phase 15 the same way and create phase15.md in the same format as the previous phase docs. [same standing template as Track A: command-to-test + command-to-see per milestone, screenshots, status tracker update, README update, always actually test/cross-check, document bugs found, ask clarifying questions]

**What happened:** First Track B phase — net-new feature work again, after three phases of audit remediation. Claude asked 2 clarifying questions (whether to attempt the 15.3 stretch goal given the budget note in the plan; gradient vs. solid color for the base wallpaper) — both answered with the recommended option. Delivered a 12x19 arrow cursor with transparency (replacing the old placeholder square, and moving the draw call to the true compositor top layer — after the taskbar, not before), a vertical gradient wallpaper, and the full 15.3 stretch goal: a hand-rolled 24bpp BMP loader with a generated `tools/make_wallpaper.py` script producing a bundled dusk-mountain wallpaper, seeded onto the disk image via the same Makefile mechanism as other bundled files. Verification went beyond gOS's own serial log: a host-side Python script independently decoded the source BMP and pixel-compared it against a QEMU screendump (0/2000 mismatches), and both "missing wallpaper file" and "corrupted wallpaper file" fallback paths were tested by hex-editing a scratch disk image directly. One real bug was found and fixed along the way: the cursor was being drawn before the taskbar in the compositor's draw order, so it visually disappeared under the taskbar strip near the bottom of the screen.

**Score: 9/10**
The standing template (four sessions old now) transferred cleanly to a fourth distinct category of work (graphics/UI feature construction after audit remediation), with no re-explanation needed. The clarifying questions were genuine judgment calls the prompt couldn't have pre-specified. Not a 10 only because — as with every phase using this exact reused template — the prompt is generic by design and can't anticipate phase-specific complications, though that's precisely what the "ask clarifying questions" step is for and it worked correctly here too.

**Suggested prompt for next time:** *(no change — the template continues to generalize; this session confirms it also works cleanly for graphics/compositor work, not just backend systems code)*

---

## Prompt 2 — Interrupt: diagnosing a reported boot hang

**Prompt:**
> can you test the os and see if it works I just checked it out, it hangs when the cursor loads

**What happened:** Claude rebuilt clean, booted headless with full serial logging, and confirmed the OS does **not** actually hang — it reaches `=== gOS boot checks complete ===` and remains fully responsive (proven by injecting a real simulated mouse click that successfully opened the File Manager at the ~85-second mark). The real explanation: boot takes ~75-80 seconds before reaching the interactive desktop, because a mouse-cursor test window and a 450-cycle file/window stress test both run unconditionally on every boot, with no visible screen change for most of that window — which looks exactly like a hang if you're watching the graphical window without knowing what's happening under the hood. Claude explained this clearly with supporting evidence (screendumps at different timestamps showing the actual boot-time animation frames) rather than just asserting "it's not a bug."

**Score: 9/10**
A short, informal, slightly imprecise report ("it hangs") that nonetheless gave Claude everything needed: a specific reproducible symptom and enough context (the cursor appears, then nothing) to investigate systematically rather than guess. The investigation itself — headless boot with full logging, then a live click-injection test to prove genuine interactivity rather than just "the log line printed" — is exactly the right level of rigor for a "is this actually broken" question. This later became the direct motivation for Phase 18 (boot-time cleanup), so the report had real downstream value beyond just this one exchange.

**Suggested prompt for next time:** *(no change needed for an informal bug report — the investigation quality was the same as it would have been for a more formally specified one; this is a good example of a low-effort prompt still getting a rigorous answer because the underlying working agreement, "always actually test," carries even without being restated)*

---

## Prompt 3 — Phase 16 execution (Window Close, Minimize & Taskbar)

**Prompt:**
> Now do Phase 16 the same way and create phase16.md in the same format as the previous phase docs. [same standing template]

**What happened:** Claude asked 1 clarifying question (minimize trigger: titlebar button vs. keyboard shortcut) — answered with the recommended titlebar-button option. Milestone 16.1 (window teardown) turned out to already be fully correct since Phase 13.6's earlier hardening — rather than re-implementing something already done, Claude verified this with a genuine measurement anyway: a new `heap_free_bytes()` introspection helper plus a 20-cycle open/close regression test proving zero heap growth, satisfying the milestone's actual test requirement even though the underlying fix was already in place. Added a minimize flag/button and taskbar restore/focus logic. During interactive taskbar testing, Claude found and diagnosed a **test-script bug, not a kernel bug**: assuming taskbar entry slot positions were fixed, when they're actually z-order-relative and shift on every focus change (correct, intentional behavior) — caught by comparing predicted vs. actual screendumps and correctly attributing the mismatch to its own math rather than the code. Separately, a real mistake occurred and was caught: two rounds of testing wrote a small edit to the shared `PERSIST.TXT` fixture file on the live disk image (once via a genuine Ctrl+S save used to debug a keyboard-timing false alarm, once via an imprecise manual restore that dropped a trailing byte) — both were caught via independent `mtools` reads showing an unexpected byte count, and fixed by writing the exact original bytes back, reconfirmed via a fresh boot.

**Score: 8/10**
Same reliable template. This phase is a good example of two different self-correction categories in one prompt cycle: recognizing "my test math was wrong, not the code" (a reasoning correction) versus "I actually mutated shared test data and need to restore it" (an execution mistake, caught and fixed). Not a 10 because of that second category — a genuinely avoidable slip (mutating a shared fixture file during testing without a scratch copy) that echoes the same pattern flagged as a lesson in session 3's Phase 14 — worth continuing to watch for.

**Suggested prompt for next time:** *(no change to the core template; the recurring "back up shared fixture files before any test that might write to them" lesson from session 3 is worth restating as a persistent standing instruction if it happens a third time)*

---

## Prompt 4 — Phase 17 execution (Maximize & Polish)

**Prompt:**
> Now do Phase 17 the same way and create phase17.md in the same format as the previous phase docs. [same standing template]

**What happened:** No clarifying questions needed — Milestone 17.1 (maximize/restore toggle) was fully specified by the plan and Phase 16's precedent (titlebar-button pattern already established). Delivered a third titlebar button (maximize/restore), verified two ways: a numeric round-trip test (a debug build logging exact x/y/w/h at three points — before maximize, after maximize, after restore — asserting exact equality) and a live visual test (screendump before/during/after). No bugs found. With this phase complete, Claude correctly recognized Track B (all of Phases 15-17) was now finished and updated the README's architecture description accordingly as the plan's own Milestone 14.0 called for ("re-touch at the end of Track B").

**Score: 9/10**
A clean, fast phase with no clarifying questions needed — everything required was already established by the two prior phases' precedent. Good example of the standing template compounding in efficiency as more of the windowing system gets built: less design space left to be ambiguous about with each subsequent phase.

**Suggested prompt for next time:** *(no change — this is the template working as well as it possibly can)*

---

## Prompt 5 — v3 roadmap brainstorm (new phases beyond the original plan)

**Prompt:**
> okay now go through the project plan 2
> Do you think there any additional phases we can add to this plan
> Since last time we had 12 phases.
> This time we have only 6 out of which 3 are audit fixed of the previous version.
> I wanna keep adding new features and imporvements to my OS
> Think, plan, analyze. Ask clarifying questions with recommended options for anything underspecified before proceeding.

**What happened:** Claude surveyed the current kernel source (~5,800 lines across 21 files) to ground the proposal in what actually exists, then asked 3 clarifying questions (direction: real-OS internals vs. desktop polish vs. filesystem depth vs. new apps — multi-select, all four were chosen; whether to scope in audio/networking, given they're the two largest remaining gaps — user chose to leave both out; roughly how many new phases, matching v1's 12-phase pace vs. v2's tighter 6-phase scope — user chose the tighter option). Proposed a 7-phase roadmap (Phases 18-24) organized into three new tracks — Track C (OS internals: user mode, syscalls, ELF loader, preemptive multitasking), Track D (desktop/storage depth: window resize, Alt+Tab, RTC/clock, settings, long filenames), Track E (apps: shell, calculator, image viewer) — plus Phase 18 as an independent, no-dependency quick fix directly addressing the boot-hang report from Prompt 2. Explicitly scoped audio/networking out with a stated rationale (large multi-phase undertakings with no payoff until user-mode programs exist to actually use them).

**Score: 9/10**
An exceptionally well-formed "help me think about this" request: it stated the actual motivation (wanting a bigger roadmap, noting the phase-count disparity vs. v1), explicitly invited push-back/analysis rather than just execution ("think, plan, analyze"), and asked for clarifying questions up front rather than assuming Claude should just guess at scope. This is close to ideal phrasing for an open-ended planning conversation — the only reason it's not a 10 is that the four-direction multi-select answer ("all of the above") meant Claude still had to make real prioritization/sequencing judgment calls about how to fit all four into a 6-8 phase budget, which is inherent to the nature of the request rather than something a differently-worded prompt could have avoided.

**Suggested prompt for next time:** *(no change — this is a strong template for "help me plan the next chunk of a long-running project": ground it in a request to survey current state first, invite analysis rather than immediate execution, and explicitly ask for clarifying questions before committing to a structure)*

---

## Prompt 6 — Confirm and commit the roadmap to the plan file

**Prompt:**
> yes write this to project-plan-2

**What happened:** Claude wrote the full 7-phase roadmap into `project-plan-2.md` in the exact same structural format as the existing Track A/B phases (milestones, tasks, tests, dependencies, risk notes, updated time-summary table with track subtotals and a grand total, updated Section 1 overview explaining the new tracks).

**Score: 9/10**
A minimal two-word confirmation that worked perfectly because the prior turn had already done all the substantive design work and explicitly asked "want me to write this into the file as-is, or adjust first?" — this is a good example of a properly-staged planning conversation (propose → confirm → commit) where the final commit step needs almost no additional instruction because everything was already agreed.

**Suggested prompt for next time:** *(no change — "yes write this" is exactly the right amount of instruction once a prior turn has already proposed a concrete plan and asked for confirmation)*

---

## Prompt 7 — Phase 18 execution (Boot-Time Cleanup & Diagnostics Mode)

**Prompt:**
> Now do Phase 18 the same way and create phase18.md in the same format as the previous phase docs. [same standing template]

**What happened:** No clarifying questions needed — the milestone wording was unambiguous. Gated the slow demo animations and the 450-cycle stress test behind a new `GOS_DIAGNOSTIC_BOOT` compile flag (default off), and added a `make diagnostic` target (forcing `clean` first, to avoid silently linking a mix of diagnostic/non-diagnostic object files — explicitly reasoned as "the same class of staleness hazard Finding #21 fixed for the disk image," reusing established project vocabulary). Measured the actual improvement precisely via the PIT tick counter: default boot dropped from ~75-80 seconds to ~1 second (`Timer tick: 100` exactly at the "boot checks complete" line). Verified the diagnostic build still reproduces every one of the original regression checks byte-for-byte, so no test coverage was silently lost by the gating — directly closing the loop on Prompt 2's hang report from earlier in the session.

**Score: 9/10**
Clean, fast execution with no back-and-forth needed. This phase is a good demonstration of how a mid-session bug report (Prompt 2) can be captured, roadmapped (Prompt 5), and then actually closed out (this prompt) within the same session without losing context or needing to re-explain the original symptom.

**Suggested prompt for next time:** *(no change — the template's momentum through a multi-phase session continues to hold)*

---

## Prompt 8 — Phase 19 execution (User Mode, Syscalls & ELF Loader)

**Prompt:**
> Now do Phase 19 the same way and create phase19.md in the same format as the previous phase docs. [same standing template]

**What happened:** The biggest architectural leap in the project so far — gOS's first-ever ring-3 code. Claude asked 1 clarifying question (syscall mechanism: `int 0x80` vs. `SYSCALL`/`SYSRET`) — answered with the recommended, simpler `int 0x80` option. Discovered that the GDT/TSS groundwork for ring 3 had actually already existed since v1.0 (a forward-looking comment literally said "used in later phases for privilege-level switches"), so the real new work was building the actual transition mechanism and a minimal ELF64 loader. Designed an elegant reuse of the existing interrupt-return path (`isr_common_stub`'s epilogue) for both the ring0→ring3 entry and the ring3→ring0 exit, rather than writing two separate mechanisms. Bundled a genuinely separate, independently-built-and-linked ELF64 test binary via a new `tools/userland/` directory with its own linker script — no libc, no shared code with the kernel. A real bug was found on the very first test: a page-fault immediately on entering ring 3, traced to a genuine flaw in `vmm_map_page`'s page-table-walking code (the `PAGE_USER` permission bit wasn't being retroactively applied to an already-existing intermediate page-table entry created earlier by the kernel's own boot-time identity map) — a latent bug that had existed since Phase 1 with zero observable effect until this was the first code path to ever need it. Verification went beyond gOS's own output: the bundled ELF was independently checked with the host's own `readelf` (confirming entry point, segment layout, and type match exactly what the kernel logged) and a `diff` confirmed the disk-image copy is byte-identical to the built file.

**Score: 9/10**
A single, well-targeted clarifying question for what could have been the riskiest phase of the whole project, followed by clean, well-reasoned execution — including catching and properly diagnosing a genuinely subtle, dormant one-year-old (in project-time) bug rather than working around it. The bug writeup itself (symptom → diagnosis → what was ruled out → the actual fix) is a strong example of the standing "document bugs found" instruction paying off with real technical substance, not just a changelog entry.

**Suggested prompt for next time:** *(no change — if anything, this phase demonstrates the template scales even to the highest-risk, most architecturally novel phase in the project without needing special-casing)*

---

## Prompt 9 — Phase 20 execution, part 1 (interrupted, Fable model, budget-constrained)

**Prompt:**
> Now do Phase 20 the same way and create phase20.md in the same format as the previous phase docs. [same standing template] ... Since you are using Fable do not use more than 20% of the session limit.

**What happened:** This prompt was sent while on the `claude-fable-5` model with an explicit session-budget cap. Claude read the Phase 20 plan section and the relevant window/keyboard source, but the user interrupted the session (via `/compact` cancellation and a manual model switch back to Sonnet) before substantive implementation began.

**Score: 7/10**
The instruction itself was clear and appropriately cautious (explicitly capping effort given a different, budget-constrained model), but the interruption means this particular prompt didn't get to run to completion — not a reflection on the prompt's quality, just noted for completeness since it's a distinct turn in the transcript. The budget-awareness itself ("do not use more than 20% of the session limit") is a good pattern worth reusing whenever operating under a different model's constraints.

**Suggested prompt for next time:** *(no change to the instruction itself; if switching models mid-task again, consider explicitly stating whether prior in-progress reads/context should be discarded or reused, to avoid ambiguity about what a fresh continuation should pick up)*

---

## Prompt 10 — Phase 20 execution, part 2 (continued on Sonnet, full scope)

**Prompt:**
> Now do Phase 20 the same way and create phase20.md in the same format as the previous phase docs. [same standing template, resumed on the default model]

**What happened:** Given the significantly larger scope of "real preemptive multitasking" versus prior phases, Claude proactively asked a clarifying question before diving in: whether to implement full per-process page-table isolation (separate `CR3` per process, as the plan's milestone wording implied) or a smaller cut (shared page tables, real preemption only) given budget concerns — the user chose the bigger, fully-isolated option. Built a process table, a genuine timer-driven context switch (elegantly reusing Phase 19's interrupt-frame-restore mechanism for both preemption and bootstrapping a brand-new process), and `spawn`/`exit`/`waitpid` syscalls (with `waitpid` explicitly scoped as poll-style rather than truly blocking, and that scope cut clearly documented rather than silently implemented). Two real bugs were found and fixed: (1) a NASM assembler quirk where a data label named `ch` silently collided with the 8-bit register `CH`, breaking an otherwise-correct `lea` instruction with a cryptic error — diagnosed by ruling out the more likely suspect (a `-D` command-line define) first, then correctly identifying the actual cause; (2) a genuine test-design flaw where the very first live test showed sequential, not interleaved, process output — correctly diagnosed as "the workload finishes too fast to ever get preempted" (confirmed via a zero-tick-elapsed check in the serial log) rather than a scheduler bug, then fixed by increasing the test workload and shortening the time slice, with the fix re-verified showing genuine interleaving.

**Score: 9/10**
A large, high-risk phase handled with the same rigor as every other phase in the session — proactively surfacing the isolation-scope tradeoff as a real decision point rather than silently picking one, and correctly distinguishing "my test is flawed" from "the code is flawed" twice in one phase (once for an assembler-level bug, once for a scheduler-timing bug), which requires genuinely understanding the underlying mechanism rather than pattern-matching on symptoms. The session resuming cleanly from Prompt 9's interrupted start (re-reading context rather than assuming stale state) is also a good sign for continuity across model switches.

**Suggested prompt for next time:** *(no change — this is the template holding up under its most demanding test yet: a phase requiring new architecture across the scheduler, VMM, GDT, and syscall layers simultaneously)*

---

## Prompt 11 — Phase 21 execution (Window Resize & Alt+Tab)

**Prompt:**
> Now do Phase 21 the same way and create phase21.md in the same format as the previous phase docs. [same standing template]

**What happened:** No clarifying questions needed — both milestones were fully specified by the plan and prior windowing-phase precedent. Added drag-to-resize handles (right edge, bottom edge, corner) to the existing hit-test priority chain, and Alt+Tab window cycling. A real design bug was caught **before ever booting the broken version**: the first `window_focus_next()` implementation picked "the window just behind the current frontmost" and reused the existing `raise_to_front()` — Claude hand-traced this against a 3-window example per the milestone's own "stable, non-repeating order" requirement and noticed it would only ever toggle between the last two windows, never surfacing a third. Fixed by rotating the entire z-order ring by one position per press instead, re-traced to confirm it visits all three exactly once per cycle. Separately, a test-script mistake (not a kernel bug) was caught and correctly diagnosed during resize testing: pressing exactly on a window's boundary pixel to start a second drag silently missed the hit-test, since `point_in_rect`'s strict less-than comparison treats the boundary pixel as just outside — confirmed by retrying 2-3 pixels further in, which worked immediately.

**Score: 9/10**
A strong example of catching a design flaw through reasoning alone, before any code ran — genuinely valuable given how easy it would have been to boot the broken version, see "focus changed," and declare victory without noticing it never reached every window. The test-script self-diagnosis (again correctly separating "my test coordinates were wrong" from "the code is wrong") continues a pattern established across several earlier phases this session.

**Suggested prompt for next time:** *(no change — the template continues to reliably surface both design bugs and test-script mistakes with correct attribution)*

---

## Prompt 12 — Reclassify Tracks C/D/E as part of v2, not a new v3

**Prompt:**
> just one change in the project plan and wherever seems fit.
> the additional tracks after b are also a a part of v2. this is not v3 yet. make that change too

**What happened:** Claude swept every file touched by the earlier v3 roadmap work (`project-plan-2.md`, `README.md`, `phase18.md`, `phase19.md`) for "v3" references, reclassifying Tracks C/D/E as additional v2 tracks rather than a new major version — including recomputing the time-summary table's roll-up math (v2 total now correctly includes Track C/D/E's estimate, not just Track A/B) and deliberately leaving the one genuinely forward-looking "a future v3, once user-mode programs exist" mention (for audio/networking) untouched, since that one actually does refer to a real future version.

**Score: 9/10**
A short, precise correction with an unusually well-chosen scope qualifier — "and wherever seems fit" correctly signaled "this isn't just one line, go find every place this classification appears" without needing to enumerate the files, while still being specific enough about *what* the change was (v3 → part of v2) that there was no ambiguity about the substance. The one nuance Claude had to get right on its own — recognizing that one particular "future v3" mention was a genuinely different, correct usage that shouldn't be touched — is exactly the kind of judgment call "wherever seems fit" appropriately delegates rather than a gap in the instruction.

**Suggested prompt for next time:** *(no change — "and wherever seems fit" is a nicely compact way to request a project-wide sweep without having to enumerate every file by name)*

---

## Prompt 13 — Phase 22 execution (RTC Driver, Taskbar Clock & Settings Persistence)

**Prompt:**
> Now do Phase 22 the same way and create phase22.md in the same format as the previous phase docs. [same standing template]

**What happened:** Claude identified that Milestone 22.3 ("settings persistence") was genuinely underspecified given gOS's current state — there's no multi-wallpaper picker UI and no real shutdown/reboot mechanism to "save on" — and asked 2 clarifying questions before implementing: what the persisted "wallpaper choice" should actually mean (a toggle between the bundled BMP and a plain gradient, since a real picker doesn't exist — chosen), and what should trigger a save (auto-save on every change vs. a manual hotkey, given no real shutdown hook exists — auto-save chosen). Built a CMOS RTC driver (handling the update-in-progress race, BCD conversion, and 12-hour/PM normalization), a live taskbar clock, and a `GOS.CFG` settings file. Verification was unusually thorough and fully independent at each layer: the RTC driver was checked against a QEMU-controlled `-rtc base=...` value (exact match on every field except seconds, which differed only by the real elapsed boot delay — correctly reasoned as expected, not a bug); the taskbar clock was proven to advance exactly 10 seconds between two precisely host-timed screendumps; and the settings file was checked three ways — gOS's own log, an independent `xxd` hex dump of the raw file (every byte matched by hand against the exact geometry dragged to), and a genuinely fresh, second QEMU process restoring both settings with zero interaction. No bugs were found this phase.

**Score: 9/10**
A great example of correctly identifying underspecification that isn't really about ambiguous wording but about a genuine capability gap in the system being built ("settings persistence" implies choices and triggers that don't fully exist yet) — rather than guessing at an interpretation or inventing fake mechanisms (a fake wallpaper picker, a fake shutdown hook) to technically satisfy the plan's literal wording. The three-layer independent verification of the settings file (own log, raw hex dump, fresh-process round-trip) is the most thorough cross-checking of any phase this session.

**Suggested prompt for next time:** *(no change — the template's built-in "ask clarifying questions for anything underspecified" step is doing real work here, correctly distinguishing "the prompt is ambiguous" from "the system doesn't have the capability this milestone assumes yet")*

---

## Prompt 14 — This meta-request (session log)

**Prompt:**
> save this entire session to calude_code folder as session_log_4.md just like the other sessions logs

**What happened:** This file.

**Score: 8/10**
Clear reference to the established pattern from three prior sessions, letting Claude infer the exact format, scoring rubric, and structure by reading `session_log_3.md` first — consistent with how each prior session's equivalent request has worked. The "just like the other session logs" phrasing carries a lot of information efficiently given the precedent already set three times over.

**Suggested prompt for next time:**
> Using claude_code/session_log_3.md as the exact format template, create claude_code/session_log_4.md covering this session (Phases 15-22, the boot-hang diagnosis, and the v3→v2 reclassification) — same structure (prompt, what happened, 1-10 score, suggested prompt for next time), ending with the same kind of session-wide observations section.

---

## Session-wide observations (for future prompting)

1. **The per-phase template now generalizes across five sessions and every kind of kernel work the project has needed**: graphics/UI construction (Phase 15-17, 21), boot-sequence/build-system optimization (Phase 18), ring-3/syscall/loader architecture (Phase 19), scheduler/multitasking (Phase 20), and hardware-driver-plus-persistence work (Phase 22). Zero re-explanation of the template's mechanics was needed at any point across 8 executed phases this session.
2. **A live, informal bug report mid-session ("it hangs when the cursor loads") was investigated with the same rigor as a formal request**, and its finding (boot silently takes 75-80 seconds due to unconditional demo/stress-test code) directly became a roadmapped, later-executed phase (18) within the same session — a good example of an informal aside not getting lost or under-investigated just because it wasn't phrased as a work ticket.
3. **Claude proactively surfaced scope-defining decisions rather than silently picking an interpretation, in at least three distinct phases this session**: Phase 20's page-table-isolation scope (shared vs. per-process), Phase 21 needed none (fully specified by precedent), and Phase 22's wallpaper-choice/save-trigger design (given gOS has no real picker UI or shutdown hook to literally satisfy the plan's wording). In each case, the underlying issue was correctly identified as "the plan's wording assumes a capability that doesn't fully exist yet," not just generic ambiguity — and the fix was to ask, not to invent a fake capability to technically comply.
4. **The "always actually test, cross-check independently" instruction kept catching real bugs *and* real test-script mistakes, with consistently correct attribution between the two** — Phase 19's page-table permission bug (real, one-year-dormant), Phase 20's NASM register-name collision (real) and its "workload too fast to preempt" issue (test-script), Phase 21's focus-cycling algorithm flaw (real, caught before ever booting it) and its boundary-pixel mishap (test-script), and Phase 16's taskbar-slot-position assumption (test-script). Distinguishing these two categories correctly, every time, is what let each phase's bug list stay accurate rather than either over- or under-reporting real defects.
5. **A large, open-ended planning conversation (Prompts 5-6, the v3 roadmap) worked best as two turns**: one to think/propose/ask clarifying questions, a second short one to confirm and commit. This mirrors a pattern worth reusing for any future "help me plan the next chunk of work" request — don't try to get commitment and execution in the same turn as the open-ended brainstorm.
6. **A same-session model switch (Fable → Sonnet, mid-Phase-20) was handled cleanly** — the second attempt re-read relevant context rather than assuming stale state from the interrupted first attempt, and picked up the same clarifying-question discipline (isolation-scope tradeoff) without needing to be reminded of the standing template.
7. **A short correction with a scope qualifier ("and wherever seems fit") efficiently triggered a project-wide, multi-file sweep** without needing every affected file enumerated by name — and correctly preserved the one place where the old terminology ("a future v3") was actually still accurate, rather than blanket-replacing every occurrence of the string.
