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

int fat_list_dir(uint32_t dir_cluster, struct fat_dirent *out, int max) {
    int count = 0;
    static uint8_t cluster_buf[64 * 1024 / 8]; /* generously sized; actual size checked below */
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;

    uint32_t cluster = dir_cluster;
    while (!is_end_of_chain(cluster) && count < max) {
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
                continue; /* deleted entry */
            }
            if (entries[i].attr == FAT32_ATTR_LONG_NAME) {
                continue; /* skip VFAT long-name entries (not supported in v1) */
            }
            if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                continue;
            }

            format_83_name(entries[i].name, out[count].name);
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

    while (bytes_read < to_read && !is_end_of_chain(cluster)) {
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

struct dirent_location {
    uint32_t sector_lba;
    uint32_t offset_in_sector;
};

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
    while (cluster != 0 && !is_end_of_chain(cluster)) {
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

/* Same traversal as fat_list_dir, but stops at the first name match and
 * additionally reports exactly where on disk that 32-byte entry lives, so
 * callers can patch it in place (used by write/delete). */
static int find_dirent(uint32_t dir_cluster, const char *component,
                        struct fat_dirent *out, struct dirent_location *loc) {
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t buf[FAT32_CLUSTER_BUF_MAX];
    if (cluster_bytes > sizeof(buf)) {
        return 0;
    }

    uint32_t cluster = dir_cluster;
    while (!is_end_of_chain(cluster)) {
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
            if (first_byte == 0xE5 || entries[i].attr == FAT32_ATTR_LONG_NAME ||
                (entries[i].attr & FAT32_ATTR_VOLUME_ID)) {
                continue;
            }
            char name[FAT32_NAME_MAX];
            format_83_name(entries[i].name, name);
            if (name_matches(name, component)) {
                int k = 0;
                for (; name[k]; k++) {
                    out->name[k] = name[k];
                }
                out->name[k] = '\0';
                out->attr = entries[i].attr;
                out->first_cluster = ((uint32_t)entries[i].fst_clus_hi << 16) | entries[i].fst_clus_lo;
                out->size = entries[i].file_size;

                uint32_t byte_offset = i * sizeof(struct fat_dir_entry_raw);
                loc->sector_lba = cluster_to_lba(cluster) + byte_offset / bytes_per_sector;
                loc->offset_in_sector = byte_offset % bytes_per_sector;
                return 1;
            }
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return 0;
}

static int find_free_slot(uint32_t dir_cluster, struct dirent_location *loc) {
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    static uint8_t buf[FAT32_CLUSTER_BUF_MAX];
    if (cluster_bytes > sizeof(buf)) {
        return 0;
    }

    uint32_t cluster = dir_cluster;
    uint32_t prev_cluster = 0;
    for (;;) {
        if (is_end_of_chain(cluster)) {
            uint32_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0) {
                return 0; /* disk full */
            }
            for (uint32_t i = 0; i < cluster_bytes; i++) {
                buf[i] = 0;
            }
            if (!fat_write_cluster(new_cluster, buf)) {
                return 0;
            }
            if (!fat_set_next_cluster(prev_cluster, new_cluster)) {
                return 0;
            }
            cluster = new_cluster;
        }

        if (!fat_read_cluster(cluster, buf)) {
            return 0;
        }
        uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dir_entry_raw);
        struct fat_dir_entry_raw *entries = (struct fat_dir_entry_raw *)buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = entries[i].name[0];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                uint32_t byte_offset = i * sizeof(struct fat_dir_entry_raw);
                loc->sector_lba = cluster_to_lba(cluster) + byte_offset / bytes_per_sector;
                loc->offset_in_sector = byte_offset % bytes_per_sector;
                return 1;
            }
        }

        prev_cluster = cluster;
        cluster = fat_get_next_cluster(cluster);
    }
}

static int write_dirent_at(struct dirent_location *loc, struct fat_dir_entry_raw *entry) {
    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(loc->sector_lba, sector)) {
        return 0;
    }
    uint8_t *raw = (uint8_t *)entry;
    for (uint32_t i = 0; i < sizeof(struct fat_dir_entry_raw); i++) {
        sector[loc->offset_in_sector + i] = raw[i];
    }
    return ata_write_sector(loc->sector_lba, sector);
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

    uint32_t parent_cluster;
    if (!resolve_dir_cluster(parent, &parent_cluster)) {
        return 0;
    }

    struct fat_dirent existing;
    struct dirent_location existing_loc;
    if (find_dirent(parent_cluster, name, &existing, &existing_loc)) {
        return 0; /* already exists */
    }

    uint32_t data_cluster = fat_alloc_cluster();
    if (data_cluster == 0) {
        return 0;
    }

    struct dirent_location slot;
    if (!find_free_slot(parent_cluster, &slot)) {
        fat_free_chain(data_cluster);
        return 0;
    }

    struct fat_dir_entry_raw entry;
    for (uint32_t i = 0; i < sizeof(entry); i++) {
        ((uint8_t *)&entry)[i] = 0;
    }
    to_83_name(name, entry.name);
    entry.attr = attr;
    entry.fst_clus_hi = (uint16_t)(data_cluster >> 16);
    entry.fst_clus_lo = (uint16_t)(data_cluster & 0xFFFF);
    entry.file_size = 0;

    if (!write_dirent_at(&slot, &entry)) {
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
    if (!find_dirent(parent_cluster, name, &entry, &loc)) {
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
    while (!is_end_of_chain(cluster) && existing_count < 256) {
        clusters[existing_count++] = cluster;
        cluster = fat_get_next_cluster(cluster);
    }

    if (existing_count < clusters_needed) {
        uint32_t last = clusters[existing_count - 1];
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
    if (!find_dirent(parent_cluster, name, &entry, &loc)) {
        return 0;
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return 0;
    }

    fat_free_chain(entry.first_cluster);

    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(loc.sector_lba, sector)) {
        return 0;
    }
    sector[loc.offset_in_sector] = 0xE5;
    return ata_write_sector(loc.sector_lba, sector);
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
    if (!find_dirent(parent_cluster, name, &entry, &loc)) {
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

    static uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(loc.sector_lba, sector)) {
        return 0;
    }
    sector[loc.offset_in_sector] = 0xE5;
    return ata_write_sector(loc.sector_lba, sector);
}
