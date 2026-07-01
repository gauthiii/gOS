#ifndef GOS_ATA_H
#define GOS_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

void ata_init(void);

/* Reads/writes a single 512-byte sector at the given 28-bit LBA from/to the
 * primary master IDE drive. Returns 1 on success, 0 on error (timeout or
 * the device reported an error status). */
int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
