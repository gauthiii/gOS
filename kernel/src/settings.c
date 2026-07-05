#include <settings.h>
#include <wallpaper.h>
#include <fat32.h>
#include <serial.h>
#include <stddef.h>
#include <fb.h>

#define SETTINGS_PATH "GOS.CFG"
#define SETTINGS_MAGIC 0x47534F47u /* 'GOSG' little-endian in the file */
/* Bumped 1 -> 2: the single gradient_forced boolean became a wallpaper
 * option index (Gradient/Default/Custom/Mac/Windows). The version check
 * below means a v1 file is treated as "wrong version" and ignored rather
 * than misread - falling back to defaults (wallpaper option 1) is harmless
 * and simpler than writing a v1->v2 migration for one byte.
 * Bumped 2 -> 3: added a checksum field (audit2.md #17) so a hand-corrupted
 * or torn write with intact magic/version/size is still caught. */
#define SETTINGS_VERSION 3u

/* Fixed-size binary record - deliberately simple (no text parsing) since
 * this is a from-scratch OS with no existing config-file convention to
 * match. magic/version let a future format change be detected and ignored
 * (falling back to defaults) instead of misreading stale bytes. */
struct settings_record {
    uint32_t magic;
    uint32_t version;
    uint8_t wallpaper_selection;
    uint8_t reserved[7]; /* pad to 8-byte alignment for the fields below */
    int64_t fm_x, fm_y;
    uint64_t fm_w, fm_h;
    uint32_t checksum; /* additive checksum over every byte above, computed last */
    uint8_t checksum_pad[4];
} __attribute__((packed));

/* Simple additive checksum: sum every byte of the record up to (not
 * including) the checksum field itself. Not cryptographic - just enough to
 * catch the accidental-corruption/torn-write scenario the audit describes,
 * matching this kernel's existing lightweight validation style. */
static uint32_t settings_checksum(const struct settings_record *rec) {
    const uint8_t *bytes = (const uint8_t *)rec;
    uint64_t len = offsetof(struct settings_record, checksum);
    uint32_t sum = 0;
    for (uint64_t i = 0; i < len; i++) {
        sum += bytes[i];
    }
    return sum;
}

static int64_t fm_x = SETTINGS_DEFAULT_FM_X;
static int64_t fm_y = SETTINGS_DEFAULT_FM_Y;
static uint64_t fm_w = SETTINGS_DEFAULT_FM_W;
static uint64_t fm_h = SETTINGS_DEFAULT_FM_H;

void settings_load(void) {
    struct fat_dirent ent;
    if (!fat_resolve_path(SETTINGS_PATH, &ent) || (ent.attr & FAT32_ATTR_DIRECTORY)) {
        serial_write_string("Settings: " SETTINGS_PATH " not found - using defaults\n");
        return;
    }
    struct settings_record rec;
    int64_t n = fat_read_file(SETTINGS_PATH, (uint8_t *)&rec, sizeof(rec));
    if (n != (int64_t)sizeof(rec) || rec.magic != SETTINGS_MAGIC || rec.version != SETTINGS_VERSION) {
        serial_write_string("Settings: " SETTINGS_PATH " malformed or wrong version - using defaults\n");
        return;
    }
    if (rec.checksum != settings_checksum(&rec)) {
        serial_write_string("Settings: " SETTINGS_PATH " checksum mismatch - corrupted, using defaults\n");
        return;
    }

    /* Sanity-check loaded File Manager geometry against the framebuffer
     * before trusting it: a corrupted or hand-edited zero/negative/off-screen
     * value used to be applied verbatim, producing an unusable window. */
    uint64_t screen_w = fb_width();
    uint64_t screen_h = fb_height();
    int fm_geometry_ok = rec.fm_w > 0 && rec.fm_h > 0 &&
                          rec.fm_w <= screen_w && rec.fm_h <= screen_h &&
                          rec.fm_x >= 0 && rec.fm_y >= 0 &&
                          (uint64_t)rec.fm_x + rec.fm_w <= screen_w &&
                          (uint64_t)rec.fm_y + rec.fm_h <= screen_h;

    wallpaper_select(rec.wallpaper_selection);
    if (fm_geometry_ok) {
        fm_x = rec.fm_x;
        fm_y = rec.fm_y;
        fm_w = rec.fm_w;
        fm_h = rec.fm_h;
    } else {
        serial_write_string("Settings: " SETTINGS_PATH " File Manager geometry invalid - using defaults\n");
    }

    serial_write_string("Settings: loaded " SETTINGS_PATH " - wallpaper_selection=");
    serial_write_uint(rec.wallpaper_selection);
    serial_write_string(" fm=(");
    serial_write_uint((uint64_t)fm_x);
    serial_write_string(",");
    serial_write_uint((uint64_t)fm_y);
    serial_write_string(",");
    serial_write_uint(fm_w);
    serial_write_string("x");
    serial_write_uint(fm_h);
    serial_write_string(")\n");
}

void settings_save(void) {
    struct settings_record rec;
    rec.magic = SETTINGS_MAGIC;
    rec.version = SETTINGS_VERSION;
    rec.wallpaper_selection = (uint8_t)wallpaper_current_selection();
    rec.reserved[0] = rec.reserved[1] = rec.reserved[2] = rec.reserved[3] = 0;
    rec.reserved[4] = rec.reserved[5] = rec.reserved[6] = 0;
    rec.fm_x = fm_x;
    rec.fm_y = fm_y;
    rec.fm_w = fm_w;
    rec.fm_h = fm_h;
    rec.checksum = settings_checksum(&rec);
    rec.checksum_pad[0] = rec.checksum_pad[1] = rec.checksum_pad[2] = rec.checksum_pad[3] = 0;

    struct fat_dirent ent;
    if (!fat_resolve_path(SETTINGS_PATH, &ent)) {
        if (!fat_create_file(SETTINGS_PATH)) {
            serial_write_string("Settings: fat_create_file(" SETTINGS_PATH ") failed\n");
            return;
        }
    }
    if (!fat_write_file(SETTINGS_PATH, (const uint8_t *)&rec, sizeof(rec))) {
        serial_write_string("Settings: fat_write_file(" SETTINGS_PATH ") failed\n");
        return;
    }
    serial_write_string("Settings: saved " SETTINGS_PATH "\n");
}

int64_t settings_fm_x(void) { return fm_x; }
int64_t settings_fm_y(void) { return fm_y; }
uint64_t settings_fm_w(void) { return fm_w; }
uint64_t settings_fm_h(void) { return fm_h; }

void settings_record_fm_geometry(int64_t x, int64_t y, uint64_t w, uint64_t h) {
    if (x == fm_x && y == fm_y && w == fm_w && h == fm_h) {
        return; /* unchanged - don't hit the FAT32 write path every frame for nothing */
    }
    fm_x = x;
    fm_y = y;
    fm_w = w;
    fm_h = h;
    settings_save();
}
