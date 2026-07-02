#include <ata.h>
#include <serial.h>

#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE_HEAD  0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_CONTROL     0x3F6

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_SRV  0x10
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_BSY  0x80

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

static int drive_present = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outsw(uint16_t port, const uint16_t *buf, int count) {
    __asm__ volatile ("rep outsw" : "+S"(buf), "+c"(count) : "d"(port));
}

static inline void insw(uint16_t port, uint16_t *buf, int count) {
    __asm__ volatile ("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}

static void io_wait(void) {
    /* Reading the (unused) alternate status port is the standard ~400ns
     * delay idiom for ATA PIO, per the OSDev Wiki. */
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}

#if defined(GOS_TEST_ATA_PROBE)
uint32_t ata_debug_busy_wait_reads = 0;
#endif

static int ata_wait_not_busy(void) {
    for (int timeout = 100000; timeout > 0; timeout--) {
#if defined(GOS_TEST_ATA_PROBE)
        ata_debug_busy_wait_reads++;
#endif
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            return 1;
        }
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (int timeout = 100000; timeout > 0; timeout--) {
#if defined(GOS_TEST_ATA_PROBE)
        ata_debug_busy_wait_reads++;
#endif
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) {
            return 0;
        }
        if (status & ATA_STATUS_DRQ) {
            return 1;
        }
    }
    return 0;
}

void ata_init(void) {
    io_wait();

    /* Finding #15: probe for a real drive via IDENTIFY before trusting
     * the bus at all. Without this, every read/write call burns the
     * full ~100,000-iteration busy-wait in ata_wait_not_busy()/
     * ata_wait_drq() before failing when no drive is attached - a single
     * fast "status == 0 means nothing's there" check up front avoids
     * that repeated, compounding stall for every I/O call afterward. */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);
    io_wait();
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LOW, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HIGH, 0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        /* Per the OSDev Wiki IDENTIFY procedure: a status of 0 right
         * after selecting the drive and issuing IDENTIFY means the
         * drive doesn't exist at all - no need to poll further. */
        drive_present = 0;
        serial_write_string("ATA: no drive detected on primary master (status=0x00) - I/O calls will fail fast\n");
        return;
    }

    if (!ata_wait_not_busy() || !ata_wait_drq()) {
        drive_present = 0;
        serial_write_string("ATA: drive present but IDENTIFY did not complete - treating as absent\n");
        return;
    }

    /* Drain the 256-word IDENTIFY response so the drive isn't left with
     * pending data the next real command might trip over; the contents
     * aren't parsed since only presence, not capability details, is
     * needed here. */
    static uint16_t identify_buf[256];
    insw(ATA_PRIMARY_DATA, identify_buf, 256);

    drive_present = 1;
    serial_write_string("ATA: primary master initialized (PIO, ports 0x1F0-0x1F7/0x3F6), drive detected\n");
}

static void ata_setup_lba(uint32_t lba) {
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
}

int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (!drive_present) {
        return 0; /* fail fast instead of burning the full busy-wait timeout */
    }
    if (!ata_wait_not_busy()) {
        return 0;
    }
    ata_setup_lba(lba);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq()) {
        return 0;
    }
    insw(ATA_PRIMARY_DATA, (uint16_t *)buffer, ATA_SECTOR_SIZE / 2);
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (!drive_present) {
        return 0; /* fail fast instead of burning the full busy-wait timeout */
    }
    if (!ata_wait_not_busy()) {
        return 0;
    }
    ata_setup_lba(lba);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq()) {
        return 0;
    }
    outsw(ATA_PRIMARY_DATA, (const uint16_t *)buffer, ATA_SECTOR_SIZE / 2);

    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (!ata_wait_not_busy()) {
        return 0;
    }

    /* BSY clearing alone only means the drive finished processing the
     * flush command - it says nothing about whether the flush (or the
     * write itself) actually succeeded. Check ERR/DF explicitly so a
     * real drive-reported failure isn't silently treated as success. */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
#if defined(GOS_TEST_ATA_STATUS_CHECK)
    static uint32_t ata_status_check_count = 0;
    ata_status_check_count++;
    serial_write_string("DEBUG: ATA post-flush status check #");
    serial_write_uint(ata_status_check_count);
    serial_write_string(" = 0x");
    serial_write_hex64(status);
    serial_write_string("\n");
#endif
    if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
        serial_write_string("ATA: write/flush failed (status=0x");
        serial_write_hex64(status);
        serial_write_string(")\n");
        return 0;
    }
    return 1;
}
