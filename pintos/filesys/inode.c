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
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    block_sector_t dblocks[INODE_NUM_DBLOCKS];
    block_sector_t indblock;
    block_sector_t doubly_indblock;
    unsigned magic;                     /* Magic number. */
    uint32_t unused[111];               /* Not used. */
  };

static bool inode_disk_extend (struct inode_disk *inode_disk,
    off_t new_length);

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

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);

  unsigned sector_num = pos / BLOCK_SECTOR_SIZE;

  /* Dblock. */
  if (sector_num < INODE_NUM_DBLOCKS)
    return inode_disk->dblocks[sector_num];

  /* Indblock. */
  sector_num -= INODE_NUM_DBLOCKS;
  if (sector_num < INDBLOCK_NUM_CHILDREN)
  {
    block_sector_t indblock[INDBLOCK_NUM_CHILDREN];
    cache_read (inode_disk->indblock, indblock);
    return indblock[sector_num];
  }

  /* Doubly Indblock. */
  sector_num -= INDBLOCK_NUM_CHILDREN;
  if (sector_num < DOUBLY_INDBLOCK_NUM_GRANDCHILDREN)
  {
    block_sector_t indblock[BLOCK_SECTOR_SIZE];
    cache_read (inode_disk->doubly_indblock, indblock);

    cache_read (indblock[sector_num / INDBLOCK_NUM_CHILDREN], indblock);
    return indblock[sector_num % INDBLOCK_NUM_CHILDREN];
  }

  /* Accessing sector beyond the max possible sector size. */
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
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
  cache_read (inode->sector, &inode->data);
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
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
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

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode->data, offset);
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
      off_t next_inode_left = inode_length(inode) - offset; 
      if (size > 0 && next_inode_left > 0)
        cache_read_async (byte_to_sector (&inode->data, offset));

      /* Read the sectors contents into buffer from cache. */
      cache_read_partial (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      bytes_read += chunk_size;
    }

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
  if (inode_disk_extend (&inode->data, offset + size))
    cache_write (inode->sector, &inode->data);


  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode->data, offset);
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

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Helper function to extend an inode. It writes sectors pointers into
 * inode_disk, writes indblock pointers to disk, and fills dblocks with zeros.
 * Returns if the size of the inode is sucessfully increased. 
 * This should be called from inode_write_at to allow file extension, and
 * from inode_create, with inode_disk of length 0. */
static bool
inode_disk_extend (struct inode_disk *inode_disk, off_t new_length)
{
  if (!inode_disk || new_length <= inode_disk->length)
    return false;

  static char zeros[BLOCK_SECTOR_SIZE];

  /* Compute current and new number of sectors. */
  unsigned new_last_sector_num = bytes_to_sectors (new_length);
  unsigned cur_last_sector_num = bytes_to_sectors (inode_disk->length);
  size_t num_new_sectors = new_last_sector_num - cur_last_sector_num;
  unsigned sector_num = cur_last_sector_num;

  /* Set the inode_disk new length. */
  /* printf ("extend inode from %d to %d\n", inode_disk->length, new_length); */
  inode_disk->length = new_length;

  /* Return if file extension does not require allocating more sectors. */
  if (num_new_sectors == 0)
    return true;

  /* Allocate new sectors. Return if there is an allocation failure. */
  block_sector_t *sectors = malloc (sizeof (block_sector_t) * num_new_sectors);
  if (!sectors || !free_map_allocate (num_new_sectors, sectors))
    return false;


  /***** Write direct blocks. *****/
  if (cur_last_sector_num < DBLOCK_END_BOUND)
  {
    /* Compute number of dblocks to write. */ 
    unsigned dblock_last_sector_num = new_last_sector_num < DBLOCK_END_BOUND ?
      new_last_sector_num : DBLOCK_END_BOUND;

    /* Write to inode_disk dblocks and fill in dblock sectors with zeros. */
    while (sector_num < dblock_last_sector_num)
    {
      unsigned sectors_idx = sector_num - cur_last_sector_num;
      cache_write (sectors[sectors_idx], zeros);
      inode_disk->dblocks[sector_num] = sectors[sectors_idx];
      ++sector_num;
    }

    /* Sanity check. */
    ASSERT (sector_num == DBLOCK_END_BOUND || sector_num == new_last_sector_num);

    /* Return if done writing sectors. */
    if (new_last_sector_num <= DBLOCK_END_BOUND)
    {
      free (sectors);
      return true;
    }
  }


  /***** Write indirect block and its children. *****/
  if (cur_last_sector_num <= INDBLOCK_END_BOUND)
  {
    /* Write to inode_disk indblock if needed. */
    if (sector_num == DBLOCK_END_BOUND)
    {
      inode_disk->indblock = sectors[sector_num - cur_last_sector_num];
      ++sector_num;
    }

    /* Compute needed number of indblock child blocks. */
    block_sector_t indblock_last_sector_num = new_last_sector_num < INDBLOCK_END_BOUND ?
      new_last_sector_num : INDBLOCK_END_BOUND;
    size_t dblocks_to_write = indblock_last_sector_num - sector_num;

    /* Write child pointers to indblock on disk. */
    cache_write_partial (inode_disk->indblock, sectors + sector_num -
        cur_last_sector_num,
        (sector_num - DBLOCK_END_BOUND - 1) * sizeof (block_sector_t),
        dblocks_to_write * sizeof (block_sector_t));

    /* Fill data blocks with zeros. */
    while (sector_num < indblock_last_sector_num)
    {
      cache_write (sectors[sector_num - cur_last_sector_num], zeros);
      ++sector_num;
    }

    /* Return if done writing sectors. */
    if (new_last_sector_num <= INDBLOCK_END_BOUND)
    {
      free (sectors);
      return true;
    }
  }


  /***** Write doubly indirect block and its children. *****/

  /* Compute needed number of doubly indblock child blocks. */
  size_t sectors_to_write = new_last_sector_num - INDBLOCK_END_BOUND - 1;

  /* Write doubly indblock and its children. */
  /* TODO: also allow file extension. */
  if (sector_num <= FILE_MAX_SECTORS)
  {
    /* Write to inode_disk doubly indblock. */
    inode_disk->doubly_indblock = sectors[sector_num];
  
    /* Compute number of doubly indblock immediate children, and write to
     * disk. */
    size_t doubly_indblock_direct_children = DIV_ROUND_UP (sectors_to_write,
        INDBLOCK_NUM_CHILDREN * (INDBLOCK_NUM_CHILDREN + 1));
    cache_write_partial (inode_disk->doubly_indblock, sectors + sector_num + 1,
        0, doubly_indblock_direct_children * sizeof (block_sector_t));
    ++sector_num;

    /* Write doubly indblock children. */
    for (unsigned i = 0; i < doubly_indblock_direct_children; ++i)
    {
      /* Compute direct blocks to write. */
      block_sector_t direct_blocks_to_write = sectors_to_write <= INDBLOCK_NUM_CHILDREN ?
        sectors_to_write : INDBLOCK_NUM_CHILDREN;
      block_sector_t last_dblock_to_write = sector_num + direct_blocks_to_write;

      /* Write the indblock (doubly indblock child). */
      cache_write_partial (sectors[sector_num], sectors + sector_num + 1, 0, direct_blocks_to_write
          * sizeof (block_sector_t));
      ++sector_num;

      /* Fill data blocks with zeros. */
      while (sector_num < last_dblock_to_write)
        cache_write (sectors[sector_num++], zeros);
    }

    /* Sanity Check. */
    ASSERT (sector_num == new_last_sector_num);

    free (sectors);
    return true;
  }

  free (sectors);
  return false;
}
