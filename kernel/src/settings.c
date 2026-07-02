#include <settings.h>
#include <wallpaper.h>
#include <fat32.h>
#include <serial.h>

#define SETTINGS_PATH "GOS.CFG"
#define SETTINGS_MAGIC 0x47534F47u /* 'GOSG' little-endian in the file */
#define SETTINGS_VERSION 1u

/* Fixed-size binary record - deliberately simple (no text parsing) since
 * this is a from-scratch OS with no existing config-file convention to
 * match. magic/version let a future format change be detected and ignored
 * (falling back to defaults) instead of misreading stale bytes. */
struct settings_record {
    uint32_t magic;
    uint32_t version;
    uint8_t gradient_forced;
    uint8_t reserved[7]; /* pad to 8-byte alignment for the fields below */
    int64_t fm_x, fm_y;
    uint64_t fm_w, fm_h;
} __attribute__((packed));

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

    wallpaper_set_gradient_forced(rec.gradient_forced);
    fm_x = rec.fm_x;
    fm_y = rec.fm_y;
    fm_w = rec.fm_w;
    fm_h = rec.fm_h;

    serial_write_string("Settings: loaded " SETTINGS_PATH " - gradient_forced=");
    serial_write_uint(rec.gradient_forced);
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
    rec.gradient_forced = (uint8_t)wallpaper_is_gradient_forced();
    rec.reserved[0] = rec.reserved[1] = rec.reserved[2] = rec.reserved[3] = 0;
    rec.reserved[4] = rec.reserved[5] = rec.reserved[6] = 0;
    rec.fm_x = fm_x;
    rec.fm_y = fm_y;
    rec.fm_w = fm_w;
    rec.fm_h = fm_h;

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
