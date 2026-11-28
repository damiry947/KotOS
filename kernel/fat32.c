/*
 * FAT32 для KotOS: MBR, первый primary-раздел 0x0B/0x0C, 8.3-имена,
 * чтение файлов, создание новых файлов. Сектор 512 байт, LBA28.
 *
 * Основано на ByteOS, https://github.com/Igoreeek228/ByteOS
 * (коммит a47869a), Copyright (C) 2026 Igoreeek228 и участники ByteOS.
 * Изменено для KotOS 2026-11-28: типы u8/u32, имена без префикса s_ сохранены,
 * вызовы ata_* из нашего ATA-модуля, стиль KotOS.
 *
 * Лицензия: GPL-3.0, см. LICENSE и THIRD_PARTY.md.
 */

#include "fat32.h"
#include "ata.h"

#define SECTOR_SIZE 512u
#define MAX_CLUSTER_SECTORS 64u
#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_BAD_CLUSTER 0x0FFFFFF7u

static u32 s_partition_lba = 0;
static u32 s_partition_sectors = 0;
static u32 s_fat_start_lba = 0;
static u32 s_data_start_lba = 0;
static u32 s_sectors_per_cluster = 0;
static u32 s_root_cluster = 0;
static u32 s_fat_size_sectors = 0;
static u32 s_max_cluster = 0;
static u32 s_num_fats = 0;
static int s_ready = 0;

static u8 sector_buf[SECTOR_SIZE];
static u8 cluster_buf[MAX_CLUSTER_SECTORS * SECTOR_SIZE];
static u8 fat_sector_buf[SECTOR_SIZE];

static u16 rd_u16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int valid_boot_signature(const u8 *sector)
{
    return sector[510] == 0x55 && sector[511] == 0xAA;
}

u32 fat32_root_cluster(void)
{
    return s_root_cluster;
}

int fat32_init(void)
{
    u8 num_fats;
    u32 part_lba = 0;
    u32 part_sectors = 0;
    u32 total_sectors;
    u32 fat_area_sectors;
    u32 first_data_relative;
    u32 data_sectors;
    u32 cluster_count;
    u32 max_cluster;
    u32 fat_start;
    u32 data_start;
    u32 root_cluster;
    u32 fat_size_32;
    u16 bytes_per_sector;
    u16 reserved_sectors;
    u16 root_entry_count;
    u16 total_sectors_16;
    u8 sectors_per_cluster;
    int superfloppy = 0;
    int i;

    s_ready = 0;

    if (!ata_init())
        return 0;
    if (!ata_read_sectors(0, 1, sector_buf))
        return 0;
    if (!valid_boot_signature(sector_buf))
        return 0;

    for (i = 0; i < 4; ++i) {
        const u8 *entry = sector_buf + 0x1BEu + (u32)i * 16u;
        u8 type = entry[4];

        if (type == 0x0B || type == 0x0C) {
            part_lba = rd_u32(entry + 8);
            part_sectors = rd_u32(entry + 12);
            break;
        }
    }

    /* Нет MBR-раздела — пробуем «superfloppy»: ФС прямо с сектора 0. */
    if (part_lba == 0 || part_sectors < 1) {
        superfloppy = 1;
        part_lba = 0;
        part_sectors = 0;
    } else {
        if (part_lba > 0x0FFFFFFFu)
            return 0;
        if (part_sectors > 0x10000000u - part_lba)
            return 0;
    }

    s_partition_lba = part_lba;
    s_partition_sectors = part_sectors;

    if (!ata_read_sectors(part_lba, 1, sector_buf))
        return 0;
    if (!valid_boot_signature(sector_buf))
        return 0;

    bytes_per_sector = rd_u16(sector_buf + 11);
    sectors_per_cluster = sector_buf[13];
    reserved_sectors = rd_u16(sector_buf + 14);
    num_fats = sector_buf[16];
    root_entry_count = rd_u16(sector_buf + 17);
    total_sectors_16 = rd_u16(sector_buf + 19);
    total_sectors = rd_u32(sector_buf + 32);
    fat_size_32 = rd_u32(sector_buf + 36);
    root_cluster = rd_u32(sector_buf + 44);

    if (bytes_per_sector != SECTOR_SIZE)
        return 0;
    if (sectors_per_cluster == 0 || sectors_per_cluster > MAX_CLUSTER_SECTORS)
        return 0;
    if ((sectors_per_cluster & (sectors_per_cluster - 1u)) != 0)
        return 0;
    if (reserved_sectors == 0)
        return 0;
    if (num_fats == 0 || num_fats > 2)
        return 0;
    if (root_entry_count != 0)
        return 0;
    if (total_sectors_16 != 0)
        return 0;
    if (fat_size_32 == 0)
        return 0;
    if (root_cluster < 2)
        return 0;

    if (superfloppy) {
        if (total_sectors < 2048)
            return 0;
        part_sectors = total_sectors;
        s_partition_sectors = part_sectors;
    } else if (total_sectors == 0 || total_sectors > part_sectors) {
        return 0;
    }

    fat_area_sectors = (u32)num_fats * fat_size_32;
    first_data_relative = (u32)reserved_sectors + fat_area_sectors;
    if (first_data_relative >= total_sectors)
        return 0;

    data_sectors = total_sectors - first_data_relative;
    cluster_count = data_sectors / sectors_per_cluster;
    if (cluster_count < 8u)
        return 0;

    max_cluster = cluster_count + 1u;
    if (max_cluster < 2 || max_cluster > 0x0FFFFFEFu)
        return 0;

    /* FAT должна вмещать записи для всех кластеров. */
    if ((u64)(max_cluster + 1u) * 4u > (u64)fat_size_32 * SECTOR_SIZE)
        return 0;

    fat_start = part_lba + reserved_sectors;
    data_start = fat_start + fat_area_sectors;
    if (fat_start < part_lba || data_start < fat_start)
        return 0;
    if (data_start - part_lba >= part_sectors)
        return 0;
    if (data_sectors > part_sectors - (data_start - part_lba))
        return 0;
    if (root_cluster > max_cluster)
        return 0;

    s_fat_start_lba = fat_start;
    s_data_start_lba = data_start;
    s_sectors_per_cluster = sectors_per_cluster;
    s_fat_size_sectors = fat_size_32;
    s_root_cluster = root_cluster;
    s_max_cluster = max_cluster;
    s_num_fats = num_fats;
    s_ready = 1;

    return 1;
}

static int valid_cluster(u32 cluster)
{
    return cluster >= 2 && cluster <= s_max_cluster;
}

static int cluster_to_lba(u32 cluster, u32 *out_lba)
{
    u32 relative;

    if (!valid_cluster(cluster) || !out_lba)
        return 0;

    relative = (cluster - 2u) * s_sectors_per_cluster;
    if (relative > s_partition_sectors)
        return 0;
    if (s_data_start_lba > 0x0FFFFFFFu - relative)
        return 0;

    *out_lba = s_data_start_lba + relative;
    return 1;
}

static u32 fat_next_cluster(u32 cluster)
{
    u32 fat_offset, fat_sector_index, offset_in_sector, fat_sector_lba, value;

    if (!s_ready || !valid_cluster(cluster))
        return 0;

    fat_offset = cluster * 4u;
    fat_sector_index = fat_offset / SECTOR_SIZE;
    offset_in_sector = fat_offset % SECTOR_SIZE;

    if (fat_sector_index >= s_fat_size_sectors)
        return 0;

    fat_sector_lba = s_fat_start_lba + fat_sector_index;
    if (!ata_read_sectors(fat_sector_lba, 1, fat_sector_buf))
        return 0;

    value = rd_u32(fat_sector_buf + offset_in_sector) & 0x0FFFFFFFu;

    if (value >= FAT32_EOC)
        return 0;
    if (value == FAT32_BAD_CLUSTER)
        return 0;
    if (value < 2 || value > s_max_cluster)
        return 0;

    return value;
}

static int read_cluster(u32 cluster, u8 *out)
{
    u32 lba;

    if (!out || !cluster_to_lba(cluster, &lba))
        return 0;
    return ata_read_sectors(lba, (u8)s_sectors_per_cluster, out);
}

static int write_cluster(u32 cluster, const u8 *data)
{
    u32 lba;

    if (!data || !cluster_to_lba(cluster, &lba))
        return 0;
    return ata_write_sectors(lba, (u8)s_sectors_per_cluster, data);
}

/* Обновляет запись FAT во всех копиях: иначе резервная разойдётся с первой. */
static int fat_set_entry(u32 cluster, u32 value)
{
    u32 fat_offset, fat_sector_index, offset_in_sector, copy;

    if (!s_ready || cluster < 2)
        return 0;

    fat_offset = cluster * 4u;
    fat_sector_index = fat_offset / SECTOR_SIZE;
    offset_in_sector = fat_offset % SECTOR_SIZE;
    if (fat_sector_index >= s_fat_size_sectors)
        return 0;

    for (copy = 0; copy < s_num_fats; copy++) {
        u32 fat_sector_lba = s_fat_start_lba + copy * s_fat_size_sectors + fat_sector_index;

        if (!ata_read_sectors(fat_sector_lba, 1, fat_sector_buf))
            return 0;

        fat_sector_buf[offset_in_sector + 0] = (u8)(value & 0xFF);
        fat_sector_buf[offset_in_sector + 1] = (u8)((value >> 8) & 0xFF);
        fat_sector_buf[offset_in_sector + 2] = (u8)((value >> 16) & 0xFF);
        fat_sector_buf[offset_in_sector + 3] =
            (u8)((fat_sector_buf[offset_in_sector + 3] & 0xF0) | ((value >> 24) & 0x0F));

        if (!ata_write_sectors(fat_sector_lba, 1, fat_sector_buf))
            return 0;
    }

    return 1;
}

static u32 find_free_cluster(void)
{
    u32 sector_idx;

    if (!s_ready)
        return 0;

    for (sector_idx = 0; sector_idx < s_fat_size_sectors; sector_idx++) {
        u32 e;

        if (!ata_read_sectors(s_fat_start_lba + sector_idx, 1, fat_sector_buf))
            return 0;

        for (e = 0; e < SECTOR_SIZE / 4u; e++) {
            u32 cluster = sector_idx * (SECTOR_SIZE / 4u) + e;
            u32 value;

            if (cluster < 2 || cluster > s_max_cluster)
                continue;

            value = rd_u32(fat_sector_buf + e * 4u) & 0x0FFFFFFFu;
            if (value == 0)
                return cluster;
        }
    }

    return 0;
}

static void format_short_name(const u8 *raw, char *out)
{
    char name[9];
    char ext[4];
    int ni = 0, ei = 0, oi = 0, i;

    for (i = 0; i < 8 && raw[i] != ' '; ++i)
        name[ni++] = (char)raw[i];
    name[ni] = 0;

    for (i = 0; i < 3 && raw[8 + i] != ' '; ++i)
        ext[ei++] = (char)raw[8 + i];
    ext[ei] = 0;

    for (i = 0; name[i] && oi < FAT32_MAX_NAME - 1; ++i)
        out[oi++] = name[i];

    if (ei > 0 && oi < FAT32_MAX_NAME - 1) {
        out[oi++] = '.';
        for (i = 0; ext[i] && oi < FAT32_MAX_NAME - 1; ++i)
            out[oi++] = ext[i];
    }

    out[oi] = 0;
}

int fat32_list_dir(u32 dir_cluster, fat32_entry_t *out, int max_entries)
{
    int count = 0;
    u32 cluster, safety = 0;

    if (!s_ready || !out || max_entries <= 0 || !valid_cluster(dir_cluster))
        return 0;

    cluster = dir_cluster;

    while (cluster != 0 && count < max_entries && safety++ <= s_max_cluster) {
        u32 entries_in_cluster;
        u32 i;

        if (!read_cluster(cluster, cluster_buf))
            break;

        entries_in_cluster = (s_sectors_per_cluster * SECTOR_SIZE) / 32u;

        for (i = 0; i < entries_in_cluster && count < max_entries; ++i) {
            const u8 *raw = cluster_buf + i * 32u;
            u8 attr;
            fat32_entry_t *e;

            if (raw[0] == 0x00)
                return count;
            if (raw[0] == 0xE5)
                continue;

            attr = raw[11];
            if (attr == 0x0F)
                continue;
            if (attr & 0x08)
                continue;
            if (raw[0] == '.')
                continue;

            e = &out[count];
            format_short_name(raw, e->name);
            e->is_dir = (attr & 0x10) ? 1 : 0;
            e->size = rd_u32(raw + 28);
            e->first_cluster = ((u32)rd_u16(raw + 20) << 16) | rd_u16(raw + 26);

            if (e->is_dir) {
                if (!valid_cluster(e->first_cluster))
                    continue;
            } else if (e->first_cluster != 0 && !valid_cluster(e->first_cluster)) {
                continue;
            }

            count++;
        }

        cluster = fat_next_cluster(cluster);
    }

    return count;
}

u32 fat32_read_file(u32 first_cluster, u32 size, u8 *out_buf, u32 max_len)
{
    u32 total_read = 0;
    u32 cluster;
    u32 cluster_bytes;
    u32 safety = 0;

    if (!s_ready || !out_buf || max_len == 0 || size == 0)
        return 0;
    if (first_cluster < 2)
        return 0;
    if (!valid_cluster(first_cluster))
        return 0;

    cluster = first_cluster;
    cluster_bytes = s_sectors_per_cluster * SECTOR_SIZE;

    while (cluster != 0 && total_read < size && total_read < max_len &&
           safety++ <= s_max_cluster) {
        u32 remaining, space_left, to_copy, i;

        if (!read_cluster(cluster, cluster_buf))
            break;

        remaining = size - total_read;
        space_left = max_len - total_read;
        to_copy = cluster_bytes;
        if (to_copy > remaining)
            to_copy = remaining;
        if (to_copy > space_left)
            to_copy = space_left;

        for (i = 0; i < to_copy; ++i)
            out_buf[total_read + i] = cluster_buf[i];

        total_read += to_copy;
        if (total_read >= size || total_read >= max_len)
            break;

        cluster = fat_next_cluster(cluster);
    }

    return total_read;
}

static void to_short_name(const char *input, u8 out[11])
{
    int i = 0, ni = 0, ei = 0;

    for (i = 0; i < 11; i++)
        out[i] = ' ';

    i = 0;
    while (input[i] && input[i] != '.' && ni < 8) {
        char c = input[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[ni++] = (u8)c;
        i++;
    }
    while (input[i] && input[i] != '.')
        i++;
    if (input[i] == '.') {
        i++;
        while (input[i] && ei < 3) {
            char c = input[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            out[8 + ei++] = (u8)c;
            i++;
        }
    }
}

static int find_free_dir_slot(u32 dir_cluster, u32 *out_cluster, u32 *out_index)
{
    u32 cluster;
    u32 last_cluster;
    u32 entries_in_cluster;
    u32 safety = 0;
    u32 new_cluster;
    u32 i;

    if (!s_ready || !valid_cluster(dir_cluster))
        return 0;

    cluster = dir_cluster;
    last_cluster = dir_cluster;
    entries_in_cluster = (s_sectors_per_cluster * SECTOR_SIZE) / 32u;

    while (cluster != 0 && safety++ <= s_max_cluster) {
        if (!read_cluster(cluster, cluster_buf))
            return 0;

        for (i = 0; i < entries_in_cluster; i++) {
            u8 first_byte = cluster_buf[i * 32u];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                *out_cluster = cluster;
                *out_index = i;
                return 1;
            }
        }

        last_cluster = cluster;
        cluster = fat_next_cluster(cluster);
    }

    new_cluster = find_free_cluster();
    if (new_cluster == 0)
        return 0;

    for (i = 0; i < sizeof(cluster_buf); i++)
        cluster_buf[i] = 0;
    if (!write_cluster(new_cluster, cluster_buf))
        return 0;
    if (!fat_set_entry(new_cluster, FAT32_EOC))
        return 0;
    if (!fat_set_entry(last_cluster, new_cluster))
        return 0;

    *out_cluster = new_cluster;
    *out_index = 0;
    return 1;
}

int fat32_write_file(u32 dir_cluster, const char *name, const u8 *data, u32 size)
{
    u8 short_name[11];
    fat32_entry_t existing[16];
    int existing_count;
    int i, k;
    u32 cluster_bytes;
    u32 clusters_needed;
    u32 first_cluster = 0;
    u32 prev_cluster = 0;
    u32 written = 0;
    u32 c;
    u32 slot_cluster, slot_index;
    u8 *raw;

    if (!s_ready || !name || (!data && size > 0))
        return 0;
    if (!valid_cluster(dir_cluster))
        return 0;

    to_short_name(name, short_name);

    existing_count = fat32_list_dir(dir_cluster, existing, 16);
    for (i = 0; i < existing_count; i++) {
        u8 candidate[11];
        int same = 1;

        to_short_name(existing[i].name, candidate);
        for (k = 0; k < 11; k++) {
            if (candidate[k] != short_name[k]) {
                same = 0;
                break;
            }
        }
        if (same)
            return 0;
    }

    cluster_bytes = s_sectors_per_cluster * SECTOR_SIZE;
    clusters_needed = (size + cluster_bytes - 1u) / cluster_bytes;
    if (clusters_needed == 0)
        clusters_needed = 1;

    for (c = 0; c < clusters_needed; c++) {
        u32 cluster = find_free_cluster();
        u32 remaining, to_write, j;

        if (cluster == 0)
            return 0;

        if (!fat_set_entry(cluster, FAT32_EOC))
            return 0;
        if (prev_cluster != 0) {
            if (!fat_set_entry(prev_cluster, cluster))
                return 0;
        } else {
            first_cluster = cluster;
        }

        remaining = size - written;
        to_write = (remaining < cluster_bytes) ? remaining : cluster_bytes;

        for (j = 0; j < sizeof(cluster_buf); j++)
            cluster_buf[j] = 0;
        for (j = 0; j < to_write; j++)
            cluster_buf[j] = data[written + j];

        if (!write_cluster(cluster, cluster_buf))
            return 0;

        written += to_write;
        prev_cluster = cluster;
    }

    if (!find_free_dir_slot(dir_cluster, &slot_cluster, &slot_index))
        return 0;
    if (!read_cluster(slot_cluster, cluster_buf))
        return 0;

    raw = cluster_buf + slot_index * 32u;
    for (i = 0; i < 11; i++)
        raw[i] = short_name[i];
    raw[11] = 0x20;
    for (i = 12; i < 20; i++)
        raw[i] = 0;
    raw[20] = (u8)((first_cluster >> 16) & 0xFF);
    raw[21] = (u8)((first_cluster >> 24) & 0xFF);
    for (i = 22; i < 26; i++)
        raw[i] = 0;
    raw[26] = (u8)(first_cluster & 0xFF);
    raw[27] = (u8)((first_cluster >> 8) & 0xFF);
    raw[28] = (u8)(size & 0xFF);
    raw[29] = (u8)((size >> 8) & 0xFF);
    raw[30] = (u8)((size >> 16) & 0xFF);
    raw[31] = (u8)((size >> 24) & 0xFF);

    return write_cluster(slot_cluster, cluster_buf);
}
