#ifndef GOS_FAT32_H
#define GOS_FAT32_H

#include <stdint.h>

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LONG_NAME 0x0F

#define FAT32_MAX_DIRENTS 64
/* 65 = up to 64 chars of (long or short) name + NUL. VFAT long-name entries
 * (attribute 0x0F) are parsed by fat_list_dir/fat_resolve_path and are
 * created/removed by fat_create_file/fat_create_dir/fat_rename/fat_delete_*
 * whenever a name doesn't fit the classic 8.3 short-name form; names longer
 * than 64 chars are rejected by the LFN-writing path. ASCII-only (gOS's
 * keyboard driver can't produce anything else); short aliases are generated
 * via a simple "BASENAM~1.EXT" scheme, trying ~1 through ~9 on collision. */
#define FAT32_NAME_MAX 65

struct fat_dirent {
    char name[FAT32_NAME_MAX];
    uint8_t attr;
    uint32_t first_cluster;
    uint32_t size;
};

/* Reads and parses the BPB from sector 0 (via the ATA driver). Returns 1 on
 * success (valid 0x55AA boot signature and "FAT32   " filesystem type), 0
 * on failure. */
int fat32_init(void);

uint32_t fat32_root_cluster(void);

/* Lists the entries of a directory (identified by its first cluster; pass
 * fat32_root_cluster() for the root). Reconstructs VFAT long names (attr
 * 0x0F entries checksummed against their trailing short-name entry) into
 * out[].name; falls back to the plain 8.3 name if no valid LFN entries
 * precede it. Skips volume-label entries. Returns the number of entries
 * written to `out` (up to `max`). */
int fat_list_dir(uint32_t dir_cluster, struct fat_dirent *out, int max);

/* Resolves a '/'-separated path (e.g. "TESTDIR/HOSTFILE.TXT") starting from
 * the root directory. Returns 1 and fills *out on success, 0 if any path
 * component isn't found. */
int fat_resolve_path(const char *path, struct fat_dirent *out);

/* Reads up to buffer_size bytes of a file's contents (resolved via
 * fat_resolve_path) into buffer. Returns the number of bytes actually read,
 * or -1 if the path doesn't resolve to a regular (non-directory) file. */
int64_t fat_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size);

/* Creates a new, empty file at `path` (parent directory must already
 * exist). Returns 1 on success, 0 on failure (parent not found, name
 * already exists, or disk full). */
int fat_create_file(const char *path);

/* Overwrites a file's entire contents (the file must already exist - call
 * fat_create_file first). Grows/shrinks the cluster chain as needed.
 * Returns 1 on success, 0 on failure. */
int fat_write_file(const char *path, const uint8_t *buffer, uint32_t size);

/* Deletes a file: frees its cluster chain and marks its directory entry
 * deleted. Returns 1 on success, 0 on failure (not found, or is a
 * directory). */
int fat_delete_file(const char *path);

/* Creates a new, empty directory (with "." and ".." entries) at `path`.
 * Returns 1 on success, 0 on failure. */
int fat_create_dir(const char *path);

/* Deletes an empty directory. Returns 1 on success, 0 on failure (not
 * found, not a directory, or not empty). */
int fat_delete_dir(const char *path);

/* Renames a file or directory in place: updates its 8.3 directory-entry
 * name bytes only (no cluster/data changes). Returns 1 on success, 0 on
 * failure (source not found, or a `new_name` entry already exists in the
 * same parent directory). */
int fat_rename(const char *path, const char *new_name);

#endif
