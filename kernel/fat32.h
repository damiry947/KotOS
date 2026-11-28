#ifndef KOTOS_FAT32_H
#define KOTOS_FAT32_H

#include "types.h"

#define FAT32_MAX_NAME 13 /* 8.3 + точка + NUL */

typedef struct {
    char name[FAT32_MAX_NAME];
    int is_dir;
    u32 size;
    u32 first_cluster;
} fat32_entry_t;

/* Подключает первый primary-раздел FAT32 (0x0B/0x0C) на ATA primary master. */
int fat32_init(void);

int fat32_list_dir(u32 dir_cluster, fat32_entry_t *out, int max_entries);
u32 fat32_root_cluster(void);

u32 fat32_read_file(u32 first_cluster, u32 size, u8 *out_buf, u32 max_len);

/* Новый файл в dir_cluster; существующий с тем же именем не перезаписывает. */
int fat32_write_file(u32 dir_cluster, const char *name, const u8 *data, u32 size);

#endif
