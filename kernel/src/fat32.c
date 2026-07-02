#include <fat32.h>
#include <ata.h>
#include <serial.h>

struct fat32_bpb {
    uint8_t  jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended BPB */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed));

struct fat_dir_entry_raw {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed));

/* VFAT long-name directory entry (attribute 0x0F). Up to 13 UTF-16LE code
 * units per entry; gOS only ever produces/consumes the ASCII subset (no
 * keyboard input can produce anything else), so each unit's low byte is
 * treated as the character and the high byte is always 0. */
struct fat_lfn_entry_raw {
    uint8_t  order;      /* bit 0x40 = last (first-on-disk) entry of the name; bits 0-4 = sequence number, 1-based */
    uint16_t name1[5];
    uint8_t  attr;       /* always FAT32_ATTR_LONG_NAME */
    uint8_t  type;       /* always 0 */
    uint8_t  checksum;   /* checksum of the associated short-name entry */
    uint16_t name2[6];
    uint16_t first_cluster; /* always 0 */
    uint16_t name3[2];
} __attribute__((packed));

#define FAT32_LFN_CHARS_PER_ENTRY 13
#define FAT32_LFN_MAX_ENTRIES 5 /* ceil(64 / 13) */

struct dirent_location {
    uint32_t sector_lba;
    uint32_t offset_in_sector;
};

static uint32_t bytes_per_sector;
static uint32_t sectors_per_cluster;
static uint32_t reserved_sector_count;
static uint32_t num_fats;
static uint32_t fat_size;
static uint32_t root_cluster;
static uint32_t first_data_sector;
static uint32_t fat_start_lba;
static uint32_t total_clusters;

static uint16_t read16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fat32_init(void) {
    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(0, sector)) {
        serial_write_string("FAT32: failed to read boot sector\n");
        return 0;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        serial_write_string("FAT32: invalid boot signature\n");
        return 0;
    }

    bytes_per_sector = read16(&sector[11]);
    sectors_per_cluster = sector[13];
    reserved_sector_count = read16(&sector[14]);
    num_fats = sector[16];
    uint32_t total_sectors_32 = read32(&sector[32]);
    fat_size = read32(&sector[36]);
    root_cluster = read32(&sector[44]);

    fat_start_lba = reserved_sector_count;
    first_data_sector = reserved_sector_count + (num_fats * fat_size);
    uint32_t data_sectors = total_sectors_32 - first_data_sector;
    total_clusters = (data_sectors / sectors_per_cluster) + 2;

    int is_fat32 = (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T' &&
                     sector[85] == '3' && sector[86] == '2');

    serial_write_string("FAT32: bytes/sector=");
    serial_write_uint(bytes_per_sector);
    serial_write_string(" sectors/cluster=");
    serial_write_uint(sectors_per_cluster);
    serial_write_string(" num_fats=");
    serial_write_uint(num_fats);
    serial_write_string(" fat_size=");
    serial_write_uint(fat_size);
    serial_write_string(" root_cluster=");
    serial_write_uint(root_cluster);
    serial_write_string(" fs_type=");
    for (int i = 0; i < 8; i++) {
        serial_write_char((char)sector[82 + i]);
    }
    serial_write_string(is_fat32 ? " (valid FAT32)\n" : " (NOT FAT32)\n");

    return is_fat32;
}

uint32_t fat32_root_cluster(void) {
    return root_cluster;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return first_data_sector + (cluster - 2) * sectors_per_cluster;
}

static uint32_t fat_get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % bytes_per_sector;

    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(fat_sector, sector)) {
        return 0x0FFFFFFF; /* treat read failure as end-of-chain */
    }
    uint32_t val = read32(&sector[offset_in_sector]) & 0x0FFFFFFF;
    return val;
}

static int is_end_of_chain(uint32_t cluster) {
    return cluster >= 0x0FFFFFF8;
}

/* A well-formed chain can visit at most total_clusters distinct clusters
 * before hitting end-of-chain; a corrupted/cyclic FAT chain can loop
 * forever instead. Every unbounded chain-walk below is capped at this
 * many steps and logs+bails instead of hanging. total_clusters is 0
 * before fat32_init() runs; guard against that too (treat as no bound
 * met, i.e. always "exceeded" -> caller bails immediately). */
static int chain_step_limit_exceeded(uint32_t steps) {
    if (steps > total_clusters) {
        serial_write_string("FAT32: cyclic/corrupted cluster chain detected - aborting walk\n");
        return 1;
    }
    return 0;
}

/* Reads one full cluster (sectors_per_cluster sectors) into buffer, which
 * must be at least sectors_per_cluster * bytes_per_sector bytes. */
static int fat_read_cluster(uint32_t cluster, uint8_t *buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        if (!ata_read_sector(lba + i, buffer + i * bytes_per_sector)) {
            return 0;
        }
    }
    return 1;
}

static void format_83_name(const uint8_t *raw_name, char *out) {
    int pos = 0;
    int name_len = 8;
    while (name_len > 0 && raw_name[name_len - 1] == ' ') {
        name_len--;
    }
    for (int i = 0; i < name_len; i++) {
        out[pos++] = (char)raw_name[i];
    }
    int ext_len = 3;
    while (ext_len > 0 && raw_name[8 + ext_len - 1] == ' ') {
        ext_len--;
    }
    if (ext_len > 0) {
        out[pos++] = '.';
        for (int i = 0; i < ext_len; i++) {
            out[pos++] = (char)raw_name[8 + i];
        }
    }
    out[pos] = '\0';
}

/* Per the VFAT spec: checksum of the 11 raw short-name bytes, used to bind
 * a run of long-name entries to the short-name entry immediately following
 * them (guards against a partially-overwritten/corrupt LFN run being
 * misread as belonging to an unrelated short name). */
static uint8_t lfn_checksum(const uint8_t sfn[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i]);
    }
    return sum;
}

/* Extracts this entry's up to 13 characters into buf[dest_offset..], ASCII
 * only (high byte of each UTF-16LE unit is ignored). Stops at a 0x0000
 * terminator or 0xFFFF pad unit without writing further characters (the
 * terminator itself is written as the string's NUL only if it falls within
 * buf_size). Returns 1 if a terminator (0x0000) was seen in this entry, 0
 * if all 13 units were real characters (the name continues in the entry
 * with the next-lower sequence number). */
static int lfn_extract_chars(struct fat_lfn_entry_raw *e, char *buf, int buf_size, int dest_offset) {
    uint16_t units[FAT32_LFN_CHARS_PER_ENTRY];
    for (int i = 0; i < 5; i++) units[i] = e->name1[i];
    for (int i = 0; i < 6; i++) units[5 + i] = e->name2[i];
    for (int i = 0; i < 2; i++) units[11 + i] = e->name3[i];

    for (int i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        if (units[i] == 0x0000) {
            if (dest_offset + i < buf_size) {
                buf[dest_offset + i] = '\0';
            }
            return 1;
        }
        if (units[i] == 0xFFFF) {
            return 0; /* padding after a terminator already handled elsewhere */
        }
        if (dest_offset + i < buf_size - 1) {
            buf[dest_offset + i] = (char)(units[i] & 0xFF);
        }
    }
    return 0;
}

/* Packs up to 13 characters of `name` (starting at src_offset) into an LFN
 * entry's three UTF-16LE character fields, NUL-terminating and 0xFFFF-
 * padding the final entry per spec. */
static void lfn_pack_chars(const char *name, int name_len, int src_offset, struct fat_lfn_entry_raw *e) {
    uint16_t units[FAT32_LFN_CHARS_PER_ENTRY];
    int terminated = 0;
    for (int i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        int src_i = src_offset + i;
        if (terminated) {
            units[i] = 0xFFFF;
        } else if (src_i < name_len) {
            units[i] = (uint16_t)(uint8_t)name[src_i];
        } else {
            units[i] = 0x0000;
            terminated = 1;
        }
    }
    for (int i = 0; i < 5; i++) e->name1[i] = units[i];
    for (int i = 0; i < 6; i++) e->name2[i] = units[5 + i];
    for (int i = 0; i < 2; i++) e->name3[i] = units[11 + i];
}

/* Tracks an in-progress run of long-name entries while scanning a
 * directory's raw entries in on-disk order (which is reverse name order:
 * the entry holding the tail of the name, marked with the 0x40 "last" bit,
 * appears first). Shared by fat_list_dir and find_dirent so both reconstruct
 * long names identically. */
struct lfn_parse_state {
    int active;
    uint8_t checksum;
    int expected_seq; /* next sequence number we should see, counting down */
    char buf[FAT32_NAME_MAX];
    int locs_count;
    struct dirent_location locs[FAT32_LFN_MAX_ENTRIES];
};

static void lfn_parse_reset(struct lfn_parse_state *st) {
    st->active = 0;
    st->locs_count = 0;
}

static void lfn_parse_step(struct lfn_parse_state *st, struct fat_lfn_entry_raw *e, struct dirent_location loc) {
    uint8_t order = e->order;
    int seq = order & 0x1F;
    int is_last = (order & 0x40) != 0;
    if (seq == 0 || seq > FAT32_LFN_MAX_ENTRIES) {
        lfn_parse_reset(st);
        return;
    }
    if (is_last) {
        lfn_parse_reset(st);
        st->active = 1;
        st->checksum = e->checksum;
        st->expected_seq = seq;
        for (int i = 0; i < FAT32_NAME_MAX; i++) st->buf[i] = '\0';
        lfn_extract_chars(e, st->buf, FAT32_NAME_MAX, (seq - 1) * FAT32_LFN_CHARS_PER_ENTRY);
        st->locs[st->locs_count++] = loc;
    } else if (st->active && seq == st->expected_seq - 1 && e->checksum == st->checksum &&
               st->locs_count < FAT32_LFN_MAX_ENTRIES) {
        st->expected_seq = seq;
        lfn_extract_chars(e, st->buf, FAT32_NAME_MAX, (seq - 1) * FAT32_LFN_CHARS_PER_ENTRY);
        st->locs[st->locs_count++] = loc;
    } else {
        lfn_parse_reset(st);
    }
}

/* Called when a short-name entry is reached. If a fully-matched LFN run
 * (checksum + sequence-1 terminal) precedes it, writes the reconstructed
 * long name into `out_name` and returns 1; otherwise returns 0 (caller
 * should fall back to format_83_name). Resets `st` either way. */
static int lfn_parse_finish(struct lfn_parse_state *st, const uint8_t sfn[11], char *out_name) {
    int matched = st->active && st->expected_seq == 1 && lfn_checksum(sfn) == st->checksum;
    if (matched) {
        int i = 0;
        for (; st->buf[i] && i < FAT32_NAME_MAX - 1; i++) {
            out_name[i] = st->buf[i];
        }
        out_name[i] = '\0';
    }
    lfn_parse_reset(st);
    return matched;
}

int fat_list_dir(uint32_t dir_cluster, struct fat_dirent *out, int max) {
    int count = 0;
    static uint8_t cluster_buf[64 * 1024 / 8]; /* generously sized; actual size checked below */
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    struct lfn_parse_state lfn_state;
    lfn_parse_reset(&lfn_state);

    uint32_t cluster = dir_cluster;
    uint32_t steps = 0;
    while (!is_end_of_chain(cluster) && count < max) {
        if (chain_step_limit_exceeded(steps++)) {
            break;
        }
        if (cluster_bytes > sizeof(cluster_buf)) {
            serial_write_string("FAT32: cluster size exceeds internal buffer\n");
            return count;
        }
        if (!fat_read_cluster(cluster, cluster_buf)) {
            break;
        }

        uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dir_entry_raw);
        struct fat_dir_entry_raw *entries = (struct fat_dir_entry_raw *)cluster_buf;

        for (uint32_t i = 0; i < entries_per_cluster && count < max; i++) {
            uint8_t first_byte = entries[i].name[0];
            if (first_byte == 0x00) {
                return count; /* end of directory */
            }
            if (first_byte == 0xE5) {
                lfn_parse_reset(&lfn_state);
                continue; /* deleted entry */
            }
            if (entries[i].attr == FAT32_ATTR_LONG_NAME) {
                struct dirent_location loc = {0, 0}; /* location unused by fat_list_dir's callers */
                lfn_parse_step(&lfn_state, (struct fat_lfn_entry_raw *)&entries[i], loc);
                continue;
            }
            if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_parse_reset(&lfn_state);
                continue;
            }

            if (!lfn_parse_finish(&lfn_state, entries[i].name, out[count].name)) {
                format_83_name(entries[i].name, out[count].name);
            }
            out[count].attr = entries[i].attr;
            out[count].first_cluster = ((uint32_t)entries[i].fst_clus_hi << 16) | entries[i].fst_clus_lo;
            out[count].size = entries[i].file_size;
            count++;
        }

        cluster = fat_get_next_cluster(cluster);
    }
    return count;
}

static int name_matches(const char *dirent_name, const char *component) {
    int i = 0;
    for (; dirent_name[i] && component[i]; i++) {
        char a = dirent_name[i];
        char b = component[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) {
            return 0;
        }
    }
    return dirent_name[i] == '\0' && component[i] == '\0';
}

int fat_resolve_path(const char *path, struct fat_dirent *out) {
    uint32_t current_cluster = root_cluster;

    /* Special-case root itself. */
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        out->name[0] = '\0';
        out->attr = FAT32_ATTR_DIRECTORY;
        out->first_cluster = root_cluster;
        out->size = 0;
        return 1;
    }

    static struct fat_dirent entries[FAT32_MAX_DIRENTS];
    char component[FAT32_NAME_MAX];

    const char *p = path;
    if (*p == '/') {
        p++;
    }

    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < FAT32_NAME_MAX - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        if (*p == '/') {
            p++;
        }

        int count = fat_list_dir(current_cluster, entries, FAT32_MAX_DIRENTS);
        int found = 0;
        for (int j = 0; j < count; j++) {
            if (name_matches(entries[j].name, component)) {
                *out = entries[j];
                current_cluster = entries[j].first_cluster;
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

int64_t fat_read_file(const char *path, uint8_t *buffer, uint32_t buffer_size) {
    struct fat_dirent entry;
    if (!fat_resolve_path(path, &entry)) {
        return -1;
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return -1;
    }

    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t cluster_buf[64 * 1024 / 8];
    if (cluster_bytes > sizeof(cluster_buf)) {
        return -1;
    }

    uint32_t to_read = entry.size < buffer_size ? entry.size : buffer_size;
    uint32_t bytes_read = 0;
    uint32_t cluster = entry.first_cluster;
    uint32_t steps = 0;

    while (bytes_read < to_read && !is_end_of_chain(cluster)) {
        if (chain_step_limit_exceeded(steps++)) {
            break;
        }
        if (!fat_read_cluster(cluster, cluster_buf)) {
            break;
        }
        uint32_t chunk = to_read - bytes_read;
        if (chunk > cluster_bytes) {
            chunk = cluster_bytes;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            buffer[bytes_read + i] = cluster_buf[i];
        }
        bytes_read += chunk;
        cluster = fat_get_next_cluster(cluster);
    }

    return (int64_t)bytes_read;
}

/* ---- Write support (Milestone 8.3) ---- */

static void write32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int fat_set_next_cluster(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % bytes_per_sector;

    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(fat_sector, sector)) {
        return 0;
    }
    uint32_t old = read32(&sector[offset_in_sector]);
    uint32_t newval = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    write32(&sector[offset_in_sector], newval);
    if (!ata_write_sector(fat_sector, sector)) {
        return 0;
    }

    /* Mirror the write to every additional FAT copy, so the two copies
     * never disagree (a corrupt/mismatched second FAT is a classic source
     * of "works until you run chkdsk/fsck" bugs). */
    for (uint32_t f = 1; f < num_fats; f++) {
        uint32_t mirror_sector = fat_sector + f * fat_size;
        if (!ata_write_sector(mirror_sector, sector)) {
            return 0;
        }
    }
    return 1;
}

static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = 2; c < total_clusters; c++) {
        if (fat_get_next_cluster(c) == 0) {
            if (!fat_set_next_cluster(c, 0x0FFFFFFF)) {
                return 0;
            }
            return c;
        }
    }
    return 0; /* disk full */
}

static int fat_write_cluster(uint32_t cluster, const uint8_t *buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        if (!ata_write_sector(lba + i, buffer + i * bytes_per_sector)) {
            return 0;
        }
    }
    return 1;
}

static void fat_free_chain(uint32_t start) {
    uint32_t cluster = start;
    uint32_t steps = 0;
    while (cluster != 0 && !is_end_of_chain(cluster)) {
        if (chain_step_limit_exceeded(steps++)) {
            break;
        }
        uint32_t next = fat_get_next_cluster(cluster);
        fat_set_next_cluster(cluster, 0);
        cluster = next;
    }
}

#define FAT32_CLUSTER_BUF_MAX 8192

static int to_83_name(const char *input, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    int i = 0, pos = 0;
    while (input[i] && input[i] != '.' && pos < 8) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[pos++] = (uint8_t)c;
        i++;
    }
    while (input[i] && input[i] != '.') {
        i++; /* truncate an overlong base name rather than fail */
    }
    if (input[i] == '.') {
        i++;
        pos = 8;
        int ext_count = 0;
        while (input[i] && ext_count < 3) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[pos++] = (uint8_t)c;
            i++;
            ext_count++;
        }
    }
    return 1;
}

/* A matched entry's associated run of long-name entries (empty if the
 * matched name was a plain 8.3 short name with no LFN entries preceding
 * it), so callers that need to erase or rewrite a whole entry (delete,
 * rename) can find every 32-byte record involved, not just the SFN. */
struct lfn_span {
    int count;
    struct dirent_location locs[FAT32_LFN_MAX_ENTRIES];
};

/* Same traversal as fat_list_dir, but stops at the first name match and
 * additionally reports exactly where on disk that 32-byte entry lives, so
 * callers can patch it in place (used by write/delete). `lfn_out` may be
 * NULL if the caller doesn't need the associated long-name entry span. */
static int find_dirent(uint32_t dir_cluster, const char *component,
                        struct fat_dirent *out, struct dirent_location *loc,
                        struct lfn_span *lfn_out) {
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t buf[FAT32_CLUSTER_BUF_MAX];
    if (cluster_bytes > sizeof(buf)) {
        return 0;
    }

    struct lfn_parse_state lfn_state;
    lfn_parse_reset(&lfn_state);

    uint32_t cluster = dir_cluster;
    uint32_t steps = 0;
    while (!is_end_of_chain(cluster)) {
        if (chain_step_limit_exceeded(steps++)) {
            return 0;
        }
        if (!fat_read_cluster(cluster, buf)) {
            return 0;
        }
        uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dir_entry_raw);
        struct fat_dir_entry_raw *entries = (struct fat_dir_entry_raw *)buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = entries[i].name[0];
            if (first_byte == 0x00) {
                return 0;
            }
            uint32_t byte_offset = i * sizeof(struct fat_dir_entry_raw);
            struct dirent_location entry_loc = {
                cluster_to_lba(cluster) + byte_offset / bytes_per_sector,
                byte_offset % bytes_per_sector
            };
            if (first_byte == 0xE5) {
                lfn_parse_reset(&lfn_state);
                continue;
            }
            if (entries[i].attr == FAT32_ATTR_LONG_NAME) {
                lfn_parse_step(&lfn_state, (struct fat_lfn_entry_raw *)&entries[i], entry_loc);
                continue;
            }
            if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_parse_reset(&lfn_state);
                continue;
            }

            char name[FAT32_NAME_MAX];
            struct lfn_parse_state pre_finish_state = lfn_state; /* lfn_parse_finish resets lfn_state */
            int had_lfn = lfn_parse_finish(&lfn_state, entries[i].name, name);
            if (!had_lfn) {
                format_83_name(entries[i].name, name);
            }
            if (name_matches(name, component)) {
                int k = 0;
                for (; name[k]; k++) {
                    out->name[k] = name[k];
                }
                out->name[k] = '\0';
                out->attr = entries[i].attr;
                out->first_cluster = ((uint32_t)entries[i].fst_clus_hi << 16) | entries[i].fst_clus_lo;
                out->size = entries[i].file_size;
                *loc = entry_loc;
                if (lfn_out) {
                    if (had_lfn) {
                        lfn_out->count = pre_finish_state.locs_count;
                        for (int j = 0; j < pre_finish_state.locs_count; j++) {
                            lfn_out->locs[j] = pre_finish_state.locs[j];
                        }
                    } else {
                        lfn_out->count = 0;
                    }
                }
                return 1;
            }
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return 0;
}

/* Advances a directory-entry location forward by n 32-byte records,
 * assuming those records stay within a single cluster (true for every
 * caller here, since find_free_slot_n only ever reports a contiguous run
 * found within one cluster's own entries array). */
static void loc_advance(struct dirent_location loc, int n, struct dirent_location *out) {
    uint32_t total = loc.offset_in_sector + (uint32_t)n * sizeof(struct fat_dir_entry_raw);
    out->sector_lba = loc.sector_lba + total / bytes_per_sector;
    out->offset_in_sector = total % bytes_per_sector;
}

/* Finds `need_count` contiguous free (0x00 or 0xE5) directory-entry slots
 * within a single cluster of dir_cluster's chain, growing the directory
 * with a fresh (fully-zeroed, hence fully-free) cluster if no existing
 * cluster has a long enough run. Reports only the first slot's location;
 * the remaining need_count-1 slots are the following contiguous records
 * (see loc_advance). */
static int find_free_slot_n(uint32_t dir_cluster, int need_count, struct dirent_location *loc) {
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t buf[FAT32_CLUSTER_BUF_MAX];
    if (cluster_bytes > sizeof(buf)) {
        return 0;
    }

    uint32_t cluster = dir_cluster;
    uint32_t prev_cluster = 0;
    uint32_t steps = 0;
    for (;;) {
        if (chain_step_limit_exceeded(steps++)) {
            return 0;
        }
        /* Tracks whether `cluster` was just allocated+linked as a
         * directory-growth cluster THIS iteration, so a subsequent
         * failure on the same iteration can roll it back instead of
         * leaving it permanently linked into the parent directory's
         * chain (marked used, contributing nothing) despite the overall
         * create having failed. */
        int grown_this_iter = 0;
        if (is_end_of_chain(cluster)) {
            uint32_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0) {
                return 0; /* disk full - nothing allocated yet to roll back */
            }
            for (uint32_t i = 0; i < cluster_bytes; i++) {
                buf[i] = 0;
            }
            if (!fat_write_cluster(new_cluster, buf)) {
                fat_free_chain(new_cluster); /* allocated but not yet linked - just free it */
                return 0;
            }
            if (!fat_set_next_cluster(prev_cluster, new_cluster)) {
                fat_free_chain(new_cluster); /* link write failed - not linked, just free it */
                return 0;
            }
            cluster = new_cluster;
            grown_this_iter = 1;
        }

        if (!fat_read_cluster(cluster, buf)) {
            if (grown_this_iter) {
                /* cluster is the growth cluster we just linked above -
                 * unlink it from the parent chain and free it, rather
                 * than leaving a permanently-linked-but-unusable cluster
                 * behind after this create ultimately fails. */
                fat_set_next_cluster(prev_cluster, 0x0FFFFFFF);
                fat_free_chain(cluster);
            }
            return 0;
        }
        uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dir_entry_raw);
        struct fat_dir_entry_raw *entries = (struct fat_dir_entry_raw *)buf;
        uint32_t run_start = 0;
        uint32_t run_len = 0;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = entries[i].name[0];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                if (run_len == 0) {
                    run_start = i;
                }
                run_len++;
                if (run_len >= (uint32_t)need_count) {
                    uint32_t byte_offset = run_start * sizeof(struct fat_dir_entry_raw);
                    loc->sector_lba = cluster_to_lba(cluster) + byte_offset / bytes_per_sector;
                    loc->offset_in_sector = byte_offset % bytes_per_sector;
                    return 1;
                }
            } else {
                run_len = 0;
            }
        }

        prev_cluster = cluster;
        cluster = fat_get_next_cluster(cluster);
    }
}

/* `entry` points at either a struct fat_dir_entry_raw or a struct
 * fat_lfn_entry_raw - both are exactly 32 bytes (verified by construction),
 * so a plain byte copy works for either. */
static int write_dirent_at(struct dirent_location *loc, const void *entry) {
    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(loc->sector_lba, sector)) {
        return 0;
    }
    const uint8_t *raw = (const uint8_t *)entry;
    for (uint32_t i = 0; i < sizeof(struct fat_dir_entry_raw); i++) {
        sector[loc->offset_in_sector + i] = raw[i];
    }
    return ata_write_sector(loc->sector_lba, sector);
}

/* Marks the short-name entry at `loc` and every long-name entry in `span`
 * (if any) as deleted (0xE5), so a rename/delete leaves no orphaned LFN
 * entries pointing at a checksum that no longer matches anything. */
static int erase_dirent_and_lfn(struct dirent_location *loc, struct lfn_span *span) {
    static uint8_t sector[ATA_SECTOR_SIZE];
    for (int i = 0; i < span->count; i++) {
        if (!ata_read_sector(span->locs[i].sector_lba, sector)) {
            return 0;
        }
        sector[span->locs[i].offset_in_sector] = 0xE5;
        if (!ata_write_sector(span->locs[i].sector_lba, sector)) {
            return 0;
        }
    }
    if (!ata_read_sector(loc->sector_lba, sector)) {
        return 0;
    }
    sector[loc->offset_in_sector] = 0xE5;
    return ata_write_sector(loc->sector_lba, sector);
}

/* A name needs VFAT long-name entries if it doesn't fit the classic 8.3
 * short-name form as-is: lowercase letters, spaces, more than one dot, an
 * over-8-char base, or an over-3-char extension all disqualify it (matching
 * real VFAT drivers' "does this name round-trip through 8.3 losslessly"
 * check, simplified to gOS's ASCII-only input). */
static int name_needs_lfn(const char *name) {
    int base_len = 0, ext_len = 0, dot_count = 0, in_ext = 0;
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (c == '.') {
            dot_count++;
            in_ext = 1;
            continue;
        }
        if (c == ' ') {
            return 1;
        }
        if (c >= 'a' && c <= 'z') {
            return 1;
        }
        if (in_ext) {
            ext_len++;
        } else {
            base_len++;
        }
    }
    if (dot_count > 1) {
        return 1;
    }
    if (base_len == 0 || base_len > 8 || ext_len > 3) {
        return 1;
    }
    return 0;
}

static void strip_upper(const char *src, int len, char *dst, int max_dst, int *dst_len) {
    int n = 0;
    for (int i = 0; i < len && n < max_dst; i++) {
        char c = src[i];
        if (c == ' ' || c == '.' || c == '~') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        dst[n++] = c;
    }
    *dst_len = n;
}

/* Generates a legal, directory-unique 8.3 short-name alias for a long
 * `name`, e.g. "a much longer file name.txt" -> "AMUCHL~1.TXT", trying ~1
 * through ~9 on collision (the common-case VFAT scheme; the spec's rarer
 * hash-suffix fallback for >9 colliding basenames isn't implemented, since
 * it can't realistically be reached at gOS's scale). Returns 1 and fills
 * out83 on success, 0 if all of ~1..~9 collide. */
static int generate_short_alias(uint32_t parent_cluster, const char *name, uint8_t out83[11]) {
    int len = 0;
    while (name[len]) len++;
    int dot = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (name[i] == '.') {
            dot = i;
            break;
        }
    }

    char base_stripped[FAT32_NAME_MAX];
    char ext_stripped[FAT32_NAME_MAX];
    int base_len, ext_len;
    if (dot >= 0) {
        strip_upper(name, dot, base_stripped, sizeof(base_stripped), &base_len);
        strip_upper(name + dot + 1, len - dot - 1, ext_stripped, sizeof(ext_stripped), &ext_len);
    } else {
        strip_upper(name, len, base_stripped, sizeof(base_stripped), &base_len);
        ext_len = 0;
    }
    if (base_len > 6) base_len = 6;
    if (base_len == 0) {
        base_stripped[0] = '_';
        base_len = 1;
    }
    if (ext_len > 3) ext_len = 3;

    for (int n = 1; n <= 9; n++) {
        uint8_t candidate[11];
        for (int i = 0; i < 11; i++) candidate[i] = ' ';
        for (int i = 0; i < base_len; i++) candidate[i] = (uint8_t)base_stripped[i];
        candidate[base_len] = '~';
        candidate[base_len + 1] = (uint8_t)('0' + n);
        for (int i = 0; i < ext_len; i++) candidate[8 + i] = (uint8_t)ext_stripped[i];

        char display[FAT32_NAME_MAX];
        format_83_name(candidate, display);
        struct fat_dirent tmp;
        struct dirent_location tmp_loc;
        if (!find_dirent(parent_cluster, display, &tmp, &tmp_loc, 0)) {
            for (int i = 0; i < 11; i++) out83[i] = candidate[i];
            return 1;
        }
    }
    return 0;
}

/* Writes a new directory entry named `name` (generating and writing the
 * necessary VFAT long-name entries first if the name doesn't fit 8.3) into
 * `parent_cluster`, pointing at `data_cluster`/`file_size` with the given
 * attribute byte. Reports the short-name entry's own location in *out_loc.
 * Used by both create (fresh data_cluster, size 0) and rename (existing
 * data_cluster/size, new name). */
static int write_named_entry(uint32_t parent_cluster, const char *name, uint8_t attr,
                              uint32_t data_cluster, uint32_t file_size,
                              struct dirent_location *out_loc) {
    int name_len = 0;
    while (name[name_len] && name_len < FAT32_NAME_MAX - 1) name_len++;

    if (!name_needs_lfn(name)) {
        struct dirent_location slot;
        if (!find_free_slot_n(parent_cluster, 1, &slot)) {
            return 0;
        }
        struct fat_dir_entry_raw entry;
        for (uint32_t i = 0; i < sizeof(entry); i++) ((uint8_t *)&entry)[i] = 0;
        to_83_name(name, entry.name);
        entry.attr = attr;
        entry.fst_clus_hi = (uint16_t)(data_cluster >> 16);
        entry.fst_clus_lo = (uint16_t)(data_cluster & 0xFFFF);
        entry.file_size = file_size;
        if (!write_dirent_at(&slot, &entry)) {
            return 0;
        }
        *out_loc = slot;
        return 1;
    }

    uint8_t raw83[11];
    if (!generate_short_alias(parent_cluster, name, raw83)) {
        return 0; /* all of ~1..~9 collided */
    }

    int num_entries = (name_len + FAT32_LFN_CHARS_PER_ENTRY - 1) / FAT32_LFN_CHARS_PER_ENTRY;
    if (num_entries > FAT32_LFN_MAX_ENTRIES) {
        return 0; /* longer than FAT32_NAME_MAX supports */
    }

    struct dirent_location slot;
    if (!find_free_slot_n(parent_cluster, num_entries + 1, &slot)) {
        return 0;
    }

    uint8_t checksum = lfn_checksum(raw83);
    for (int e = num_entries; e >= 1; e--) {
        int idx = num_entries - e;
        struct dirent_location eloc;
        loc_advance(slot, idx, &eloc);
        struct fat_lfn_entry_raw lfn;
        for (uint32_t i = 0; i < sizeof(lfn); i++) ((uint8_t *)&lfn)[i] = 0;
        lfn.order = (uint8_t)(e | (e == num_entries ? 0x40 : 0));
        lfn.attr = FAT32_ATTR_LONG_NAME;
        lfn.type = 0;
        lfn.checksum = checksum;
        lfn.first_cluster = 0;
        lfn_pack_chars(name, name_len, (e - 1) * FAT32_LFN_CHARS_PER_ENTRY, &lfn);
        if (!write_dirent_at(&eloc, &lfn)) {
            return 0;
        }
    }

    struct dirent_location sfn_loc;
    loc_advance(slot, num_entries, &sfn_loc);
    struct fat_dir_entry_raw entry;
    for (uint32_t i = 0; i < sizeof(entry); i++) ((uint8_t *)&entry)[i] = 0;
    for (int i = 0; i < 11; i++) entry.name[i] = raw83[i];
    entry.attr = attr;
    entry.fst_clus_hi = (uint16_t)(data_cluster >> 16);
    entry.fst_clus_lo = (uint16_t)(data_cluster & 0xFFFF);
    entry.file_size = file_size;
    if (!write_dirent_at(&sfn_loc, &entry)) {
        return 0;
    }
    *out_loc = sfn_loc;
    return 1;
}

/* Splits "a/b/c.txt" into parent path "a/b" and final component "c.txt".
 * A path with no '/' has an empty parent (root). */
static void split_path(const char *path, char *parent, char *name) {
    const char *last_slash = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }
    if (!last_slash) {
        parent[0] = '\0';
        int i = 0;
        for (; path[i]; i++) {
            name[i] = path[i];
        }
        name[i] = '\0';
        return;
    }
    int i = 0;
    for (const char *p = path; p < last_slash; p++, i++) {
        parent[i] = *p;
    }
    parent[i] = '\0';
    int j = 0;
    for (const char *p = last_slash + 1; *p; p++, j++) {
        name[j] = *p;
    }
    name[j] = '\0';
}

static int resolve_dir_cluster(const char *dir_path, uint32_t *out_cluster) {
    if (dir_path[0] == '\0') {
        *out_cluster = root_cluster;
        return 1;
    }
    struct fat_dirent d;
    if (!fat_resolve_path(dir_path, &d)) {
        return 0;
    }
    if (!(d.attr & FAT32_ATTR_DIRECTORY)) {
        return 0;
    }
    *out_cluster = d.first_cluster;
    return 1;
}

static int create_entry(const char *path, uint8_t attr, uint32_t *out_cluster, uint32_t *out_parent_cluster) {
    char parent[256];
    char name[FAT32_NAME_MAX];
    split_path(path, parent, name);

    int name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len == 0 || name_len > FAT32_NAME_MAX - 1) {
        return 0; /* empty, or longer than FAT32_NAME_MAX supports */
    }

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent existing;
    struct dirent_location existing_loc;
    if (find_dirent(parent_cluster, name, &existing, &existing_loc, 0)) {
        return 0; /* already exists */
    }

    uint32_t data_cluster = fat_alloc_cluster();
    if (data_cluster == 0) {
        return 0;
    }

    struct dirent_location slot;
    if (!write_named_entry(parent_cluster, name, attr, data_cluster, 0, &slot)) {
        fat_free_chain(data_cluster);
        return 0;
    }

    *out_cluster = data_cluster;
    *out_parent_cluster = parent_cluster;
    return 1;
}

int fat_create_file(const char *path) {
    uint32_t data_cluster, parent_cluster;
    (void)parent_cluster;
    if (!create_entry(path, FAT32_ATTR_ARCHIVE, &data_cluster, &parent_cluster)) {
        return 0;
    }

    /* Zero the initial cluster so a freshly-created, unwritten file reads
     * back as empty rather than exposing stale disk contents. */
    static uint8_t zero_buf[FAT32_CLUSTER_BUF_MAX];
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    for (uint32_t i = 0; i < cluster_bytes && i < sizeof(zero_buf); i++) {
        zero_buf[i] = 0;
    }
    fat_write_cluster(data_cluster, zero_buf);
    return 1;
}

int fat_create_dir(const char *path) {
    uint32_t data_cluster, parent_cluster;
    if (!create_entry(path, FAT32_ATTR_DIRECTORY, &data_cluster, &parent_cluster)) {
        return 0;
    }

    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t buf[FAT32_CLUSTER_BUF_MAX];
    for (uint32_t i = 0; i < cluster_bytes && i < sizeof(buf); i++) {
        buf[i] = 0;
    }

    struct fat_dir_entry_raw *entries = (struct fat_dir_entry_raw *)buf;
    /* "." entry: points at the new directory's own cluster. */
    for (int i = 0; i < 11; i++) entries[0].name[i] = ' ';
    entries[0].name[0] = '.';
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].fst_clus_hi = (uint16_t)(data_cluster >> 16);
    entries[0].fst_clus_lo = (uint16_t)(data_cluster & 0xFFFF);

    /* ".." entry: points at the parent directory's cluster. */
    for (int i = 0; i < 11; i++) entries[1].name[i] = ' ';
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    entries[1].fst_clus_hi = (uint16_t)(parent_cluster >> 16);
    entries[1].fst_clus_lo = (uint16_t)(parent_cluster & 0xFFFF);

    return fat_write_cluster(data_cluster, buf);
}

int fat_write_file(const char *path, const uint8_t *buffer, uint32_t size) {
    char parent[256];
    char name[FAT32_NAME_MAX];
    split_path(path, parent, name);

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent entry;
    struct dirent_location loc;
    if (!find_dirent(parent_cluster, name, &entry, &loc, 0)) {
        return 0; /* must already exist - call fat_create_file first */
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return 0;
    }

    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    if (cluster_bytes > FAT32_CLUSTER_BUF_MAX) {
        return 0;
    }
    uint32_t clusters_needed = (size + cluster_bytes - 1) / cluster_bytes;
    if (clusters_needed == 0) {
        clusters_needed = 1; /* always keep at least one cluster, even for 0-byte files */
    }

    /* Walk the existing chain, extending or truncating it to exactly
     * clusters_needed clusters. */
    uint32_t clusters[256]; /* supports files up to 256 * cluster_bytes; ample for v1 testing */
    uint32_t existing_count = 0;
    uint32_t cluster = entry.first_cluster;
    /* cluster 0 is never a valid data cluster (data clusters start at 2)
     * and is not caught by is_end_of_chain() (which only recognizes
     * 0x0FFFFFF8+ as EOC) - a zero-length file with no allocated cluster
     * yet (first_cluster == 0, a legitimate on-disk state) must be
     * treated as an empty chain rather than walked, or fat_get_next_cluster(0)
     * would read FAT[0]'s reserved/media-descriptor entry as if it were a
     * real chain link. */
    if (cluster != 0) {
        while (!is_end_of_chain(cluster) && existing_count < 256) {
            clusters[existing_count++] = cluster;
            cluster = fat_get_next_cluster(cluster);
        }
    }

    if (existing_count < clusters_needed) {
        /* existing_count == 0 means no cluster is allocated yet - allocate
         * a fresh first cluster instead of reading clusters[existing_count - 1],
         * which would underflow (existing_count is unsigned) to 0xFFFFFFFF
         * and read garbage off the stack. */
        uint32_t last;
        if (existing_count == 0) {
            uint32_t first = fat_alloc_cluster();
            if (first == 0) {
                return 0; /* disk full */
            }
            clusters[existing_count++] = first;
            last = first;
        } else {
            last = clusters[existing_count - 1];
        }
        while (existing_count < clusters_needed) {
            uint32_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0) {
                return 0; /* disk full */
            }
            fat_set_next_cluster(last, new_cluster);
            clusters[existing_count++] = new_cluster;
            last = new_cluster;
        }
    } else if (existing_count > clusters_needed) {
        fat_set_next_cluster(clusters[clusters_needed - 1], 0x0FFFFFFF);
        for (uint32_t i = clusters_needed; i < existing_count; i++) {
            fat_set_next_cluster(clusters[i], 0);
        }
        existing_count = clusters_needed;
    }

    static uint8_t cluster_buf[FAT32_CLUSTER_BUF_MAX];
    uint32_t bytes_written = 0;
    for (uint32_t c = 0; c < clusters_needed; c++) {
        uint32_t chunk = size - bytes_written;
        if (chunk > cluster_bytes) {
            chunk = cluster_bytes;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            cluster_buf[i] = buffer[bytes_written + i];
        }
        for (uint32_t i = chunk; i < cluster_bytes; i++) {
            cluster_buf[i] = 0;
        }
        if (!fat_write_cluster(clusters[c], cluster_buf)) {
            return 0;
        }
        bytes_written += chunk;
    }

    /* Re-derive the raw entry fields rather than trusting a stale local
     * copy: read the sector fresh, patch only size/cluster, write back. */
    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(loc.sector_lba, sector)) {
        return 0;
    }
    struct fat_dir_entry_raw *e = (struct fat_dir_entry_raw *)(sector + loc.offset_in_sector);
    e->fst_clus_hi = (uint16_t)(clusters[0] >> 16);
    e->fst_clus_lo = (uint16_t)(clusters[0] & 0xFFFF);
    e->file_size = size;
    return ata_write_sector(loc.sector_lba, sector);
}

int fat_delete_file(const char *path) {
    char parent[256];
    char name[FAT32_NAME_MAX];
    split_path(path, parent, name);

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent entry;
    struct dirent_location loc;
    struct lfn_span span;
    if (!find_dirent(parent_cluster, name, &entry, &loc, &span)) {
        return 0;
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return 0;
    }

    fat_free_chain(entry.first_cluster);
    return erase_dirent_and_lfn(&loc, &span);
}

int fat_delete_dir(const char *path) {
    char parent[256];
    char name[FAT32_NAME_MAX];
    split_path(path, parent, name);

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent entry;
    struct dirent_location loc;
    struct lfn_span span;
    if (!find_dirent(parent_cluster, name, &entry, &loc, &span)) {
        return 0;
    }
    if (!(entry.attr & FAT32_ATTR_DIRECTORY)) {
        return 0;
    }

    struct fat_dirent children[FAT32_MAX_DIRENTS];
    int count = fat_list_dir(entry.first_cluster, children, FAT32_MAX_DIRENTS);
    for (int i = 0; i < count; i++) {
        if (children[i].name[0] == '.' &&
            (children[i].name[1] == '\0' || (children[i].name[1] == '.' && children[i].name[2] == '\0'))) {
            continue; /* "." and ".." don't count toward non-empty */
        }
        return 0; /* directory not empty */
    }

    fat_free_chain(entry.first_cluster);
    return erase_dirent_and_lfn(&loc, &span);
}

int fat_rename(const char *path, const char *new_name) {
    char parent[256];
    char name[FAT32_NAME_MAX];
    split_path(path, parent, name);

    int new_len = 0;
    while (new_name[new_len]) new_len++;
    if (new_len == 0 || new_len > FAT32_NAME_MAX - 1) {
        return 0; /* empty, or longer than FAT32_NAME_MAX supports */
    }

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent entry;
    struct dirent_location loc;
    struct lfn_span span;
    if (!find_dirent(parent_cluster, name, &entry, &loc, &span)) {
        return 0; /* source not found */
    }

    struct fat_dirent clash;
    struct dirent_location clash_loc;
    if (find_dirent(parent_cluster, new_name, &clash, &clash_loc, 0)) {
        return 0; /* destination name already exists */
    }

    /* Write the new entry (name/alias generation + slot allocation) before
     * touching the old one at all, so a failure here (disk full, alias
     * space exhausted) leaves the original entry completely untouched
     * instead of erasing it and then failing to write its replacement. */
    struct dirent_location new_loc;
    if (!write_named_entry(parent_cluster, new_name, entry.attr, entry.first_cluster, entry.size, &new_loc)) {
        return 0;
    }
    return erase_dirent_and_lfn(&loc, &span);
}
