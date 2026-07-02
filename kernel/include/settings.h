#ifndef GOS_SETTINGS_H
#define GOS_SETTINGS_H

#include <stdint.h>

/* Milestone 22.3: persists a small set of user preferences to GOS.CFG on
 * the FAT32 root - the wallpaper mode (gradient-forced or not) and the
 * File Manager window's last-known geometry, the two things Phase 21/15
 * actually made meaningful to remember (see phase22.md for why these two
 * specifically, and why "settings" doesn't cover more than this yet). */

#define SETTINGS_DEFAULT_FM_X 120
#define SETTINGS_DEFAULT_FM_Y 60
#define SETTINGS_DEFAULT_FM_W 420
#define SETTINGS_DEFAULT_FM_H 260

/* Loads GOS.CFG if present and applies it (wallpaper_set_gradient_forced()
 * is called directly; FM geometry is just cached for settings_fm_x() etc.
 * to return, since the FM window doesn't exist yet at boot time - it's
 * applied when desktop.c actually creates it). Falls back silently to the
 * compiled-in defaults above if the file is missing or malformed - this is
 * a preferences file, not something boot should ever halt over. Call once
 * at boot, after FAT32 is up. */
void settings_load(void);

/* Writes the CURRENT in-memory settings (wallpaper mode + whatever FM
 * geometry was last recorded via settings_record_fm_geometry()) to
 * GOS.CFG, creating the file if it doesn't exist yet. Called automatically
 * whenever a tracked setting actually changes (see keyboard.c's F2 handler
 * and desktop.c's per-frame geometry check) - there's no explicit "save"
 * user action, matching this project's choice to auto-save rather than
 * invent a fake "shutdown" trigger this OS doesn't otherwise have. */
void settings_save(void);

int64_t settings_fm_x(void);
int64_t settings_fm_y(void);
uint64_t settings_fm_w(void);
uint64_t settings_fm_h(void);

/* Called by desktop.c once per frame while the File Manager is open, to
 * notice a drag/resize and trigger settings_save(). No-op (and no save) if
 * the geometry hasn't actually changed since the last call. */
void settings_record_fm_geometry(int64_t x, int64_t y, uint64_t w, uint64_t h);

#endif
