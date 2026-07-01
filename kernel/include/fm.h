#ifndef GOS_FM_H
#define GOS_FM_H

#include <window.h>

/* Creates the File Manager window (title bar + toolbar buttons + listing
 * area) and wires it up to the FAT32 driver. Returns the window index, or
 * -1 on failure (window table full or the filesystem couldn't be listed). */
int fm_create_window(int64_t x, int64_t y, uint64_t w, uint64_t h);

#endif
