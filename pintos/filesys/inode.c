#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* TERMINOLOGY:
 * DBLOCK = direct block. INDBLOCK = indirect block.
 * INDBLOCK has children. eg. DOUBLY INDBLOCK children are INDBLOCKS. INDBLOCK
 * children are DBLOCKS.
 *
 * Although in practice, inodes would point to blocks, we use the same size
 * for blocks and sectors. Hence why inodes point to block_sector_t type. */

/* EXTENSIBLE INODE DESIGN:
 * Each inode has
 * - 12 pointers to direct blocks
 * - 1 pointer to an indblock, which has 128 pointers to direct blocks
 * - 1 pointesr to a doubly indblock, which has 128 pointers to indblocks.
 *   Each of those indblocks has 128 pointers to direct blocks. */

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Direct blocks in an inode. */
#define INODE_NUM_DBLOCKS 12

/* Indblocks in an inode or doubly indblock. */
/* Why you gotta bust me preprocessor. */
#define INDBLOCK_NUM_CHILDREN (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

/* Max number of grandchildren for a doubly indblock. */
#define DOUBLY_INDBLOCK_NUM_GRANDCHILDREN (INDBLOCK_NUM_CHILDREN * \
  INDBLOCK_NUM_CHILDREN)

/* Number of all nested children for a doubly indblock. */
#define DOUBLY_INDBLOCK_NUM_CHILDREN (INDBLOCK_NUM_CHILDREN * (1 + INDBLOCK_NUM_CHILDREN))

/* Sector inclusive boundary for dblocks. */
#define DBLOCK_END_BOUND INODE_NUM_DBLOCKS

/* Sector inclusive boundary for indblock. Add 1 for indblock itself. */
#define INDBLOCK_END_BOUND (DBLOCK_END_BOUND + INDBLOCK_NUM_CHILDREN + 1)

/* Max number of sectors a file can contain. Add one for doubly indblock itself.
 * Note that pintos file system size is 8mb, so we never reach this limit. */
#define FILE_MAX_SECTORS (INDBLOCK_END_BOUND + INDBLOCK_NUM_CHILDREN + \
  DOUBLY_INDBLOCK_NUM_GRANDCHILDREN + 1)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    bool is_dir;                        /* True if is directory. */
    block_sector_t dblocks[INODE_NUM_DBLOCKS];
    block_sector_t indblock;
    block_sector_t doubly_indblock;
    unsigned magic;                     /* Magic number. */
    uint32_t unused[111];               /* Not used. */
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  };

/* Array of zeros to fill in empty sectors. */
static char zeros[BLOCK_SECTOR_SIZE];

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static bool inode_disk_extend (struct inode_disk *inode_disk,
    off_t new_length);
static struct inode_disk *inode_get_data (const struct inode *inode);
static void inode_disk_free (struct inode_disk *inode_disk);
static void inode_disk_free_sector (block_sector_t sector,
    unsigned *sectors_left);

/* Returns the number of data sectors(dblocks) to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_data_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the number of indblocks for a file so SIZE bytes
 * long. */
static size_t
bytes_to_indirect_sectors (off_t size)
{
  size_t data_sectors_left = bytes_to_data_sectors (size);

  /* File only consists of dblocks. */
  if (data_sectors_left <= INODE_NUM_DBLOCKS)
    return 0;

  /* File contains indblock. */
  data_sectors_left -= INODE_NUM_DBLOCKS;
  if (data_sectors_left <= INDBLOCK_NUM_CHILDREN)
    return 1;

  /* File contains doubly indblocks. */
  data_sectors_left -= INDBLOCK_NUM_CHILDREN;
  block_sector_t doubly_indblocks = DIV_ROUND_UP (data_sectors_left, INDBLOCK_NUM_CHILDREN);
  return 2 + doubly_indblocks;
}

/* Returns the total number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
  return bytes_to_data_sectors (size) + bytes_to_indirect_sectors (size);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);

  unsigned sector_num = pos / BLOCK_SECTOR_SIZE;

  /* printf ("sector num %d\n", sector_num); */

  /* Dblock. */
  if (sector_num < INODE_NUM_DBLOCKS)
    return inode_disk->dblocks[sector_num];

  /* Indblock. */
  sector_num -= INODE_NUM_DBLOCKS;
  if (sector_num < INDBLOCK_NUM_CHILDREN)
  {
    block_sector_t *indblock = malloc (INDBLOCK_NUM_CHILDREN *
        sizeof (block_sector_t));
    cache_read (inode_disk->indblock, indblock);
    block_sector_t sector = indblock[sector_num];
    free (indblock);
    return sector;
  }

  /* Doubly Indblock. */
  sector_num -= INDBLOCK_NUM_CHILDREN;
  if (sector_num < DOUBLY_INDBLOCK_NUM_GRANDCHILDREN)
  {
    block_sector_t *indblock = malloc (INDBLOCK_NUM_CHILDREN *
        sizeof (block_sector_t));
    cache_read (inode_disk->doubly_indblock, indblock);
    cache_read (indblock[sector_num / INDBLOCK_NUM_CHILDREN], indblock);

    block_sector_t sector = indblock[sector_num % INDBLOCK_NUM_CHILDREN];
    free (indblock);
    /* printf ("return sector %d\n", sector); */
    return sector;
  }

  /* Accessing sector beyond the max possible sector size. */
  return -1;
}

/* Gets the inodes on disk data. */
static struct inode_disk *
inode_get_data (const struct inode *inode)
{
  if (!inode)
    return false;
  struct inode_disk *inode_disk = malloc (sizeof (struct inode_disk));
  cache_read (inode->sector, inode_disk);
  return inode_disk;
}

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device. is_dir is true if its directory, false otherwise.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *inode_disk = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *inode_disk == BLOCK_SECTOR_SIZE);

  inode_disk = calloc (1, sizeof *inode_disk);
  if (inode_disk != NULL)
    {
      inode_disk->length = 0;
      inode_disk->is_dir = is_dir;
      inode_disk->magic = INODE_MAGIC;
      if (inode_disk_extend (inode_disk, length))
        cache_write (sector, inode_disk);
      free (inode_disk);
      return true;
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          struct inode_disk *inode_disk = inode_get_data (inode);
          inode_disk_free (inode_disk);
          free (inode_disk);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct inode_disk *inode_disk = inode_get_data (inode);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;

      /* Read the next sector asynchronously if this is not the last sector.
       * This could be cleaner, but don't want to break logic so whatevs. */
      off_t next_inode_left = inode_length (inode) - offset; 
      if (size > 0 && next_inode_left > 0)
        cache_read_async (byte_to_sector (inode_disk, offset));

      /* Read the sectors contents into buffer from cache. */
      cache_read_partial (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      bytes_read += chunk_size;
    }

  free (inode_disk);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* Extend the inode if necessary. */
  struct inode_disk *inode_disk = inode_get_data (inode);
  if (inode_disk_extend (inode_disk, offset + size))
    cache_write (inode->sector, inode_disk);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Write the buffer contents into cache. */
      cache_write_partial (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  free (inode_disk);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the inodes sector. */
block_sector_t
inode_get_sector (const struct inode *inode)
{
  if (inode)
    return inode->sector;
  return -1;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *inode_disk = inode_get_data (inode);
  off_t length = inode_disk->length;
  free (inode_disk);
  return length;
}

/* Returns if inode is a directory. */
bool
inode_is_dir (const struct inode *inode)
{
  if (!inode)
    return false;
  struct inode_disk *inode_disk = inode_get_data (inode);
  bool is_dir = inode_disk->is_dir;
  free (inode_disk);
  return is_dir;
}

/* inode_disk_extend helper to write dblocks. */
static void
inode_disk_extend_dblock (struct inode_disk *inode_disk,
    block_sector_t **sectors,
    unsigned *sector_ofs,
    unsigned *total_sectors_to_write)
{
  if (*sector_ofs >= INODE_NUM_DBLOCKS)
  {
    *sector_ofs -= INODE_NUM_DBLOCKS;
    return;
  }

  unsigned sectors_to_write = *total_sectors_to_write;
  unsigned sectors_to_write_max = INODE_NUM_DBLOCKS - *sector_ofs;
  if (sectors_to_write > sectors_to_write_max)
    sectors_to_write = sectors_to_write_max;

  for (unsigned i = 0; i < sectors_to_write; ++i)
  {
    cache_write (**sectors, zeros);
    inode_disk->dblocks[*sector_ofs + i] = **sectors;
    ++(*sectors);
  }

  *sector_ofs = 0;
  *total_sectors_to_write -= sectors_to_write;
}

/* inode_disk_extend helper to write singly indblock children. */
static void
inode_disk_extend_indblock_children (block_sector_t indblock,
    block_sector_t **sectors,
    unsigned *sector_ofs,
    unsigned *total_sectors_to_write)
{
  if (*sector_ofs >= INDBLOCK_NUM_CHILDREN)
  {
    *sector_ofs -= INDBLOCK_NUM_CHILDREN;
    return;
  }

  unsigned sectors_to_write = *total_sectors_to_write;
  if (sectors_to_write > INDBLOCK_NUM_CHILDREN)
    sectors_to_write = INDBLOCK_NUM_CHILDREN;

  cache_write_partial (indblock,
      *sectors,
      *sector_ofs * sizeof (block_sector_t),
      sectors_to_write * sizeof (block_sector_t));

  for (unsigned i = 0; i < sectors_to_write; ++i)
  {
    cache_write (**sectors, zeros);
    ++(*sectors);
  }
  
  *sector_ofs = 0;
  *total_sectors_to_write -= sectors_to_write;
}

/* inode_disk_extend helper to write doubly indblock children. */
static void
inode_disk_extend_doubly_indblock_children (block_sector_t doubly_indblock,
    block_sector_t **sectors,
    block_sector_t **indirect_sectors,
    unsigned *sector_ofs,
    unsigned *sectors_to_write)
{
  /* printf ("sector ofs is %d\n", *sector_ofs); */
  /* printf ("doubly indblock total sectors to write %d\n", *sectors_to_write); */
  /* Number of doubly indblock direct children. Add 1 for the indblock
   * sector. */
  unsigned num_direct_children= DIV_ROUND_UP 
      (*sectors_to_write, INDBLOCK_NUM_CHILDREN + 1);
  unsigned direct_children_left = num_direct_children;
  /* printf ("num_direct_children %d\n", num_direct_children); */

  /* Compute offsets. */
  unsigned first_indblock = *sector_ofs / (INDBLOCK_NUM_CHILDREN + 1);
  *sector_ofs = *sector_ofs % (INDBLOCK_NUM_CHILDREN + 1);

  if (*sector_ofs > 0)
    --(*sector_ofs);

  /* Separate array to hold direct children sectors. */
  block_sector_t *direct_children_sectors = malloc (num_direct_children *
      sizeof (block_sector_t));
  unsigned direct_children_sectors_idx = 0;

  while (direct_children_left > 0)
  {
    direct_children_sectors[direct_children_sectors_idx] = **indirect_sectors;
    ++(*indirect_sectors);

    inode_disk_extend_indblock_children (
        direct_children_sectors[direct_children_sectors_idx++], sectors,
        sector_ofs, sectors_to_write);

    --direct_children_left;
  }

  /* Write the doubly indblock. */
  /* TODO: make this extensible. */
  cache_write_partial (doubly_indblock,
      direct_children_sectors,
      first_indblock,
      num_direct_children * sizeof (block_sector_t));
}

/* Helper function to extend an inode. It writes sectors pointers into
 * inode_disk, writes indblock pointers to disk, and fills dblocks with zeros.
 * Returns if the size of the inode is the same or sucessfully increased. 
 * This should be called from inode_write_at to allow file extension, and
 * from inode_create, with inode_disk->length = 0. */
static bool
inode_disk_extend (struct inode_disk *inode_disk, off_t new_length)
{
  if (!inode_disk || new_length < inode_disk->length)
    return false;

  /* Compute current and new number of sectors. */
  unsigned new_last_sector_num = bytes_to_sectors (new_length);
  unsigned cur_last_sector_num = bytes_to_sectors (inode_disk->length);
  size_t sectors_to_write = new_last_sector_num - cur_last_sector_num;

  /* TODO: use these. will make doubly indblock life easier. */
  size_t data_sectors_to_write = bytes_to_data_sectors (new_length) -
    bytes_to_data_sectors (inode_disk->length);
  size_t indirect_sectors_to_write = bytes_to_indirect_sectors (new_length) -
    bytes_to_indirect_sectors (inode_disk->length);
  ASSERT (sectors_to_write == data_sectors_to_write + indirect_sectors_to_write);
  
  /* Indicates which dblock or indblock/doubly indblock child to write to.
   * Helpful for file extension. */
  unsigned sector_ofs = bytes_to_data_sectors (inode_disk->length);

  /* Set the inode_disk new length. */
  /* printf ("extend inode from %d to %d bytes\n", inode_disk->length, new_length); */
  inode_disk->length = new_length;

  /* Return if file extension does not require allocating more sectors. */
  if (sectors_to_write == 0)
    return true;

  /* Allocate new sectors. Return if there is an allocation failure. */
  block_sector_t *sectors = malloc (sizeof (block_sector_t) * sectors_to_write);
  if (!sectors || !free_map_allocate (sectors_to_write, sectors))
    return false;
  block_sector_t *sectors_orig = sectors;
  block_sector_t *indirect_sectors = sectors + data_sectors_to_write;

  /* Write dblocks. */
  inode_disk_extend_dblock (inode_disk, &sectors, &sector_ofs,
      &data_sectors_to_write);
  if (data_sectors_to_write <= 0)
  {
    free (sectors_orig);
    return true;
  }

  /* Write indblock if needed. */
  if (sector_ofs == 0)
  {
    inode_disk->indblock = *indirect_sectors;
    ++indirect_sectors;
  }

  /* Write indblock children. */
  inode_disk_extend_indblock_children (inode_disk->indblock, &sectors,
      &sector_ofs, &data_sectors_to_write);
  if (data_sectors_to_write <= 0)
  {
    free (sectors_orig);
    return true;
  }

  /* Write doubly indblock if needed. */
  if (sector_ofs == 0)
  {
    inode_disk->doubly_indblock = *indirect_sectors;
    ++indirect_sectors;
  }

  /* Write doubly indblock_children. */
  inode_disk_extend_doubly_indblock_children (inode_disk->doubly_indblock,
      &sectors, &indirect_sectors, &sector_ofs, &data_sectors_to_write);
  
  free (sectors_orig);
  return true;
}

/* Free a single sector. Helper to inode_free. */
static void
inode_disk_free_sector (block_sector_t sector, unsigned *sectors_left)
{
  free_map_release (sector, 1);
  --*sectors_left;
}

/* Free all of the inode_disk's sectors from the free map. */
static void
inode_disk_free (struct inode_disk *inode_disk)
{
  unsigned last_sector_num = bytes_to_sectors (inode_disk->length);
  unsigned sectors_left = last_sector_num;

  /* Dblocks. */
  unsigned dblocks_to_free = sectors_left < DBLOCK_END_BOUND ?
    sectors_left : DBLOCK_END_BOUND;

  for (unsigned i = 0; i < dblocks_to_free; ++i)
    inode_disk_free_sector (inode_disk->dblocks[i], &sectors_left);

  sectors_left -= DBLOCK_END_BOUND;
  if ((int) sectors_left <= 0)
    return;


  /* Indblock. */
  block_sector_t *indblock = malloc (BLOCK_SECTOR_SIZE);

  cache_read (inode_disk->indblock, indblock);
  inode_disk_free_sector (inode_disk->indblock, &sectors_left);

  unsigned indblocks_to_free = sectors_left < INDBLOCK_END_BOUND ?
    sectors_left : INDBLOCK_END_BOUND;

  for (unsigned i = 0; i < indblocks_to_free; ++i)
    inode_disk_free_sector (indblock[i], &sectors_left);

  sectors_left -= INDBLOCK_END_BOUND;
  if (sectors_left <= 0)
  {
    free (indblock);
    return;
  }
  

  /* Doubly indblock. */
  cache_read (inode_disk->doubly_indblock, indblock);
  inode_disk_free_sector (inode_disk->doubly_indblock, &sectors_left);

  unsigned i = 0;
  block_sector_t *nested_indblock = malloc (BLOCK_SECTOR_SIZE);
  while (sectors_left > 0)
  {
    cache_read (indblock[i], nested_indblock); 
    inode_disk_free_sector (indblock[i], &sectors_left);
    unsigned dblocks_to_free = sectors_left < INDBLOCK_NUM_CHILDREN ?
      sectors_left : INDBLOCK_NUM_CHILDREN;

    for (unsigned j = 0; j < dblocks_to_free; ++j)
      inode_disk_free_sector (nested_indblock[j], &sectors_left);
  }
  free (indblock);
  free (nested_indblock);
}
