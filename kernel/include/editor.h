#ifndef GOS_EDITOR_H
#define GOS_EDITOR_H

/* Opens (or, if already open, refocuses and re-loads) the single Text
 * Editor window with the contents of the file at `path` (a FAT32 path
 * with no leading slash, e.g. "HOSTFILE.TXT" or "LEVEL1/LEVEL2/NOTE.TXT").
 * The file must already exist. Ctrl+S while the editor is focused saves
 * the current text box contents back to the same path via fat_write_file. */
void editor_open(const char *path);

#endif
