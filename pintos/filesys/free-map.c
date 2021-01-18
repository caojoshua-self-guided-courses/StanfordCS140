#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per sector. */

/* Initializes the free map. */
void
free_map_init (void) 
{
  free_map = bitmap_create (block_size (fs_device));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT sectors from the free map and stores them into SECTORP.
   SECTORP should be allocated to hold CNT sectors.
   Returns true if successful, false if not enough sectors were
   available or if the free_map file could not be written. */
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{
  if (cnt == 0)
    return true;

  size_t allocated = 0;
  size_t free_map_size = bitmap_size (free_map);
  for (size_t i = 0; i < free_map_size; ++i)
  {
    if (!bitmap_test (free_map, i))
    {
      sectorp[allocated++] = i;
      bitmap_mark (free_map, i);
    }
    if (allocated == cnt)
    {
      if (!free_map_file || bitmap_write (free_map, free_map_file))
        return true;
      break;
    }
  }

  /* On failure, free all the sectors allocated. */
  for (size_t i = 0; i < allocated; ++i)
    bitmap_reset (free_map, i);
  return false;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (block_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);
}

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map), false))
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}
