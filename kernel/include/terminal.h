#ifndef GOS_TERMINAL_H
#define GOS_TERMINAL_H

/* Opens (or focuses, if already open) the kernel-mode Terminal window -
 * Milestone 24.1's shell, in the form the project plan explicitly allows
 * as a fallback when no ring-3 read/list/file syscalls exist yet (see
 * phase24.md). Supports `ls`, `cd`, `run <NAME.ELF>` (a genuine ring-3
 * spawn via process_spawn()+scheduler_run_until_done()), `help`, `clear`. */
void terminal_open(void);

/* Returns 1 if the Terminal window is currently open, 0 otherwise - used by
 * the desktop for its "app is running" dock-dot indicator. */
int terminal_is_open(void);

/* Closes the Terminal window if open (no-op otherwise) - used by Phase 24's
 * open/close regression test. */
void terminal_close(void);

#endif
