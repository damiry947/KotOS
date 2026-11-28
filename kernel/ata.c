/*
 * ATA PIO для KotOS (32-бит, primary master, LBA28, сектор 512 байт).
 *
 * Основано на ByteOS, https://github.com/Igoreeek228/ByteOS
 * (коммит a47869a), Copyright (C) 2026 Igoreeek228 и участники ByteOS.
 * Изменено для KotOS 2026-11-28: типы u8/u16/u32, локальные port I/O,
 * добавлен ata_info (модель и число секторов), единый стиль.
 *
 * Лицензия: GPL-3.0, см. LICENSE и THIRD_PARTY.md.
 */

#include "ata.h"

#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6

#define ATA_SR_ERR  0x01
#define ATA_SR_DRQ  0x08
#define ATA_SR_BSY  0x80

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

#define ATA_LBA28_MAX 0x0FFFFFFFu
#define ATA_TIMEOUT   1000000u

static inline void ata_outb(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 ata_inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline u16 ata_inw(u16 port)
{
    u16 ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void ata_outw(u16 port, u16 val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static void ata_delay_400ns(void)
{
    (void)ata_inb(ATA_ALT_STATUS);
    (void)ata_inb(ATA_ALT_STATUS);
    (void)ata_inb(ATA_ALT_STATUS);
    (void)ata_inb(ATA_ALT_STATUS);
}

static int ata_wait_not_busy(void)
{
    u32 i;
    for (i = 0; i < ATA_TIMEOUT; ++i) {
        u8 st = ata_inb(ATA_STATUS);
        if (!(st & ATA_SR_BSY))
            return 1;
    }
    return 0;
}

static int ata_wait_drq(void)
{
    u32 i;
    for (i = 0; i < ATA_TIMEOUT; ++i) {
        u8 st = ata_inb(ATA_STATUS);
        if (st & ATA_SR_ERR)
            return 0;
        if (st & ATA_SR_DRQ)
            return 1;
    }
    return 0;
}

int ata_init(void)
{
    u8 status;
    int i;

    ata_outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay_400ns();

    ata_outb(ATA_SECCOUNT, 0);
    ata_outb(ATA_LBA_LO, 0);
    ata_outb(ATA_LBA_MID, 0);
    ata_outb(ATA_LBA_HI, 0);

    ata_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    status = ata_inb(ATA_STATUS);
    if (status == 0 || status == 0xFF)
        return 0;

    if (!ata_wait_not_busy())
        return 0;

    /* У ATAPI в этих регистрах ненулевая сигнатура. */
    if (ata_inb(ATA_LBA_MID) != 0 || ata_inb(ATA_LBA_HI) != 0)
        return 0;

    if (!ata_wait_drq())
        return 0;

    for (i = 0; i < 256; ++i)
        (void)ata_inw(ATA_DATA);

    return 1;
}

int ata_info(char *model, int model_len, u32 *sectors)
{
    u16 id[256];
    u32 i;
    int n = 0;

    ata_outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay_400ns();
    ata_outb(ATA_SECCOUNT, 0);
    ata_outb(ATA_LBA_LO, 0);
    ata_outb(ATA_LBA_MID, 0);
    ata_outb(ATA_LBA_HI, 0);
    ata_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    if (ata_inb(ATA_STATUS) == 0)
        return 0;
    if (!ata_wait_not_busy())
        return 0;
    if (ata_inb(ATA_LBA_MID) != 0 || ata_inb(ATA_LBA_HI) != 0)
        return 0;
    if (!ata_wait_drq())
        return 0;

    for (i = 0; i < 256; ++i)
        id[i] = ata_inw(ATA_DATA);

    if (model && model_len > 0) {
        for (i = 27; i <= 46 && n < model_len - 1; i++) {
            char a = (char)(id[i] >> 8);
            char b = (char)(id[i] & 0xFF);
            model[n++] = a ? a : ' ';
            if (n < model_len - 1)
                model[n++] = b ? b : ' ';
        }
        while (n > 0 && model[n - 1] == ' ')
            n--;
        model[n] = 0;
    }

    if (sectors)
        *sectors = (u32)id[60] | ((u32)id[61] << 16);

    return 1;
}

int ata_read_sectors(u32 lba, u8 count, u8 *buf)
{
    u32 last_lba;
    u8 s;

    if (!buf || count == 0)
        return 0;

    last_lba = lba + (u32)count - 1u;
    if (last_lba < lba || last_lba > ATA_LBA28_MAX)
        return 0;

    if (!ata_wait_not_busy())
        return 0;

    ata_outb(ATA_DRIVE_HEAD, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    ata_outb(ATA_SECCOUNT, count);
    ata_outb(ATA_LBA_LO, (u8)(lba & 0xFF));
    ata_outb(ATA_LBA_MID, (u8)((lba >> 8) & 0xFF));
    ata_outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
    ata_outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);

    for (s = 0; s < count; ++s) {
        u16 *dst = (u16 *)(buf + (u32)s * 512u);
        int i;

        if (!ata_wait_not_busy())
            return 0;
        if (!ata_wait_drq())
            return 0;

        for (i = 0; i < 256; ++i)
            dst[i] = ata_inw(ATA_DATA);
    }

    return 1;
}

int ata_write_sectors(u32 lba, u8 count, const u8 *buf)
{
    u32 last_lba;
    u8 s;

    if (!buf || count == 0)
        return 0;

    last_lba = lba + (u32)count - 1u;
    if (last_lba < lba || last_lba > ATA_LBA28_MAX)
        return 0;

    if (!ata_wait_not_busy())
        return 0;

    ata_outb(ATA_DRIVE_HEAD, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    ata_outb(ATA_SECCOUNT, count);
    ata_outb(ATA_LBA_LO, (u8)(lba & 0xFF));
    ata_outb(ATA_LBA_MID, (u8)((lba >> 8) & 0xFF));
    ata_outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
    ata_outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

    for (s = 0; s < count; ++s) {
        const u16 *src = (const u16 *)(buf + (u32)s * 512u);
        int i;

        if (!ata_wait_not_busy())
            return 0;
        if (!ata_wait_drq())
            return 0;

        for (i = 0; i < 256; ++i)
            ata_outw(ATA_DATA, src[i]);

        ata_delay_400ns();
    }

    if (!ata_wait_not_busy())
        return 0;

    ata_outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (!ata_wait_not_busy())
        return 0;

    return 1;
}
