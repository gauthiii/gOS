#ifndef GOS_WALLPAPER_H
#define GOS_WALLPAPER_H

/* Milestone 15.2/15.3: desktop wallpaper layer, drawn by the compositor
 * before any windows. Tries to load WALLPAPR.BMP (24-bit uncompressed BMP)
 * from the FAT32 root at init; falls back to a built-in vertical gradient
 * if the file is missing or malformed. */

/* Call once at boot, after heap + FAT32 are up. Safe to call even if
 * fat32_init() failed earlier in principle, but in practice boot halts on
 * FAT32 failure, so this only runs against a mounted filesystem. */
void wallpaper_init(void);

/* Returns 1 if the BMP wallpaper loaded successfully, 0 if the gradient
 * fallback is in use. */
int wallpaper_image_loaded(void);

/* Draws the wallpaper across the whole screen (image if loaded, gradient
 * otherwise). Must be the first thing drawn each frame. */
void wallpaper_render(void);

#endif
