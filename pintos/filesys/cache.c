#include "filesys/cache.h"
#include "lib/kernel/list.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/timer.h"

#define CACHE_NUM_SECTORS 64
#define INVALID_SECTOR -1

/* How many ticks in between flushing cache to disk. */
#define DISK_WRITE_FREQUENCY 10

static struct cache_entry * get_cache_entry (block_sector_t sector);
static void read_cache_entry_from_disk (struct cache_entry *cache_entry);
static void write_cache_entry_to_disk (struct cache_entry *cache_entry);
static void write_cache_to_disk (void *aux);
static void cache_read_thread (void *sector);
static struct cache_entry * get_cache_entry_to_evict (void);

static unsigned cache_reads = 0;
static unsigned cache_writes = 0;

struct block *fs_device;

struct cache_entry
{
  block_sector_t sector;
  bool free;
  bool dirty;
  int64_t last_accessed_tick;
  uint8_t data[BLOCK_SECTOR_SIZE];
};

struct cache_entry cache[CACHE_NUM_SECTORS];

struct lock cache_lock;

/* Init the buffer cache. */
void
cache_init (void)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  for (int i = 0; i < CACHE_NUM_SECTORS; ++i)
    cache[i].free = true;
  lock_init (&cache_lock);
  thread_create ("cache to disk writer", PRI_DEFAULT, write_cache_to_disk, NULL);
}

/* Read size bytes from sector base address + sector_ofs into buffer. */
void
cache_read (block_sector_t sector, void *buffer, int sector_ofs, int size)
{
  ASSERT (sector_ofs + size <= BLOCK_SECTOR_SIZE);

  /* printf ("cache read at sector %d\n", sector); */

  struct cache_entry *cache_entry = get_cache_entry (sector);

  /* This memcpy might have synchronization issues. For example, it might try
   * to read a cache entry that is getting evicted and replaced. Releasing the
   * lock after memcpy does not work as well, because buffer can cause a page
   * fault, which might attempt to load a page from filesys and return to this
   * function, while still holding the lock.
   * TODO: solve this problem. Maybe a lock per cache entry? */
  memcpy (buffer, cache_entry->data + sector_ofs, size);
  cache_entry->last_accessed_tick = timer_ticks ();
  ++cache_reads;
}

/* Read a sector into cache asynchronously. */
void cache_read_async (block_sector_t sector)
{
  block_sector_t *sector_ptr = malloc (sizeof (block_sector_t));
  *sector_ptr = sector;
  thread_create ("cache_read_async", PRI_DEFAULT, cache_read_thread, sector_ptr);
}

/* Write size bytes from buffer into sector base address + sector_ofs. */
void
cache_write (block_sector_t sector, const void *buffer, int sector_ofs,
    int size)
{
  ASSERT (sector_ofs + size <= BLOCK_SECTOR_SIZE);

  /* printf ("cache read at sector %d\n", sector); */

  struct cache_entry *cache_entry = get_cache_entry (sector);

  /* This memcpy might have synchronization issues. See comment block in
   * cache_read. */
  memcpy (cache_entry->data + sector_ofs, buffer, size);
  cache_entry->dirty = true;
  cache_entry->last_accessed_tick = timer_ticks ();
  ++cache_writes;
}

/* Print buffer cache stats. */
void
cache_print_stats (void)
{
  printf ("Filesys buffer cache: %d reads, %d writes\n", cache_reads, cache_writes);
}

/* Get the cache entry with sector. If it does not exist in cache, read the
 * sector from disk and write it into a free cache entry. Evict a cache entry
 * if none are available. */
static struct cache_entry *
get_cache_entry (block_sector_t sector)
{
  struct cache_entry *cache_entry;
  struct cache_entry *free_cache_entry = NULL;

  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_NUM_SECTORS; ++i)
  {
    cache_entry = cache + i;
    if (cache_entry->sector == sector)
    {
      lock_release (&cache_lock);
      return cache_entry;
    }

    else if (!free_cache_entry && cache_entry->free)
      free_cache_entry = cache_entry;
  }

  /* Could not find the sector in cache, but there is a free entry. Write
   * the sector into the free entry. */
  if (free_cache_entry)
  {
    free_cache_entry->sector = sector;
    free_cache_entry->free = false;
    read_cache_entry_from_disk (free_cache_entry);
    cache_entry = free_cache_entry;
  }

  /* If could not find the sector in cache and there are no free sectors,
   * evict an existing cache entry. */
  else
  {
    cache_entry = get_cache_entry_to_evict ();
    write_cache_entry_to_disk (cache_entry);
    cache_entry->sector = sector;
    read_cache_entry_from_disk (cache_entry);
  }

  lock_release (&cache_lock);
  return cache_entry;
}

/* Read contents from disk into cache_entry. */
static void
read_cache_entry_from_disk (struct cache_entry *cache_entry)
{
  block_read (fs_device, cache_entry->sector, cache_entry->data);
  cache_entry->dirty = false;
}

/* Write contents from cache_entry into disk if cache_entry is dirty. */
static void
write_cache_entry_to_disk (struct cache_entry *cache_entry)
{
  if (cache_entry->dirty)
  {
    block_write (fs_device, cache_entry->sector, cache_entry->data);
    cache_entry->dirty = false;
  }
}

/* Write the entire cache to disk periodically. This function should be run as
 * its own thread through thread_create.
 * (write behind). */
static void
write_cache_to_disk (void *aux UNUSED)
{
  while (true)
  {
    lock_acquire (&cache_lock);
    struct cache_entry *cache_entry;
    for (int i = 0; i < CACHE_NUM_SECTORS; ++i)
    {
      cache_entry = cache + i;
      if (!cache_entry->free)
        write_cache_entry_to_disk (cache_entry);
    }
    lock_release (&cache_lock);
    timer_sleep (DISK_WRITE_FREQUENCY);
  }
}

/* Read sector from disk into cache. This function should be called as a
 * thread with thread_create. */
static void
cache_read_thread (void *sector)
{
  get_cache_entry (*(block_sector_t *) sector);
  free (sector);
}

/* Get the cache_entry to evict. Evicts the least recently used cache entry
 * as indicated by the timer tick it was last accessed. Assumes there are
 * no free cache entries. 
 * This basic LRU performs equally to random eviction. Could explore other
 * options. */
static struct cache_entry *
get_cache_entry_to_evict (void)
{
  struct cache_entry *cache_entry = cache;
  for (int i = 1; i < CACHE_NUM_SECTORS; ++i)
  {
    if (cache[i].last_accessed_tick < cache_entry->last_accessed_tick)
      cache_entry = cache + i;
  }
  return cache_entry;
}
