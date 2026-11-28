#ifndef KOTOS_ATA_H
#define KOTOS_ATA_H

#include "types.h"

/*
 * ATA PIO: только primary master, LBA28, сектор 512 байт.
 * Порт драйвера из ByteOS (см. THIRD_PARTY.md).
 */
int ata_init(void);

/* IDENTIFY: модель (обрезается, дополняется нулём) и число секторов LBA28. */
int ata_info(char *model, int model_len, u32 *sectors);

int ata_read_sectors(u32 lba, u8 count, u8 *buf);
int ata_write_sectors(u32 lba, u8 count, const u8 *buf);

#endif
