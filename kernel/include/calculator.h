#ifndef GOS_CALCULATOR_H
#define GOS_CALCULATOR_H

/* Opens (or focuses, if already open) the Calculator window - Milestone
 * 24.2. Integer arithmetic only (+ - x /), entered via mouse-clicked
 * digit/operator buttons or the keyboard; '=' evaluates, 'C' clears. */
void calculator_open(void);

/* Returns 1 if the Calculator window is currently open, 0 otherwise - used
 * by the desktop for its "app is running" dock-dot indicator. */
int calculator_is_open(void);

/* Closes the Calculator window if open (no-op otherwise) - used by Phase
 * 24's open/close regression test. */
void calculator_close(void);

#endif
