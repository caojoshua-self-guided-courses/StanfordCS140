#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "lib/stdbool.h"

#define CACHE_WRITE_FREQ 10

void cache_init (void);
void cache_read (block_sector_t, void *buffer);
void cache_read_partial (block_sector_t sector, void *buffer, int sector_ofs, int size);
void cache_read_async (block_sector_t sector);
void cache_write (block_sector_t, void *buffer);
void cache_write_partial (block_sector_t sector, const void *buffer, int sector_ofs,
    int size);
void cache_print_stats (void);

#endif /* filesys/cache.h */
