#include "vm/swap.h"

#include "devices/block.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

/* Invalid swap page. */
#define SWAP_PAGE_ERROR -1

/* Number of sectors in each page. Assumes page size is greater than sector
 * size. */
#define PG_NUM_SECTORS PGSIZE / BLOCK_SECTOR_SIZE 

struct block *swap_block;
struct swap_slot *swap_table;
int swap_num_pages;

/* Lock so that multiple threads cannot alloc/free swap at the same time. */
struct lock swap_lock;

struct swap_slot
{
  bool free;
  block_sector_t block_sector;
};

/* Initialize the swap table. */
void
swalloc_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  swap_num_pages = block_size (swap_block) * BLOCK_SECTOR_SIZE / PGSIZE;
  // TODO: static alloc instead of malloc
  swap_table = malloc (swap_num_pages * sizeof (struct swap_slot));

  for (swap_page_t i = 0; i < swap_num_pages; ++i)
  {
    struct swap_slot *swap_slot = swap_table + i;
    swap_slot->free = true;
  }

  lock_init (&swap_lock);
}

/* Allocate a page in swap. Return the swap page in which the page starts on. */
swap_page_t
swalloc (void)
{
  lock_acquire (&swap_lock);
  swap_page_t swap_page = SWAP_PAGE_ERROR;
  for (swap_page_t i = 0; i < swap_num_pages; ++i)
  {
    struct swap_slot *swap_slot = swap_table + i;
    if (swap_slot->free)
    {
      swap_slot->free = false;
      swap_page = i;
      break;
    }
  }
  lock_release (&swap_lock);
  return swap_page;
}

/* Frees swap page at swap_page. */
void
swfree (swap_page_t swap_page)
{
  lock_acquire (&swap_lock);
  struct swap_slot *swap_slot = swap_table + swap_page;
  swap_slot->free = true;
  lock_release (&swap_lock);
}

/* Reads page at swap_page into buffer. */
void
swap_page_read (swap_page_t swap_page, void *buffer)
{
  int sector_ofs = 0;
  int page_ofs = 0;
  int base_sector = swap_page * PG_NUM_SECTORS;
  while (sector_ofs < PG_NUM_SECTORS)
  {
    block_read (swap_block, base_sector + sector_ofs, buffer + page_ofs);
    ++sector_ofs;
    page_ofs += BLOCK_SECTOR_SIZE;
  }
}

/* Writes buffer into page at swap_page. */
void
swap_page_write (swap_page_t swap_page, const void *buffer)
{
  int sector_ofs = 0;
  int page_ofs = 0;
  int base_sector = swap_page * PG_NUM_SECTORS;
  while (sector_ofs < PG_NUM_SECTORS)
  {
    block_write (swap_block, base_sector + sector_ofs, buffer + page_ofs);
    ++sector_ofs;
    page_ofs += BLOCK_SECTOR_SIZE;
  }
}

