#include "page.h"
#include "lib/kernel/hash.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/frame.h"

/* Supplementary paging. Each process has its own supplementary page table
 * to store data that cannot be stored in the hardware page table, which has
 * format restrictions. */

/* Maximum number of stack pages. 4kb * 2000 = 8mb */
#define MAX_STACK_PAGES 2000

struct page;
static bool load_page_from_filesys (struct page *page);

/* Indicates where the page is. */
enum page_present {
  PRESENT_MEMORY,
  PRESENT_FILESYS,
  PRESENT_SWAP
};

/* Page metadata to be stored in the supplemental page table. */
struct page {
  void *vaddr;
  struct process *process;
  enum page_present present;
  bool writable;
  bool dirty_bit;
  int access_time;
  struct hash_elem hash_elem;

  /* Page is in file system */
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
	uint32_t zero_bytes;
};

/* Looks up page with virtual address vaddr. */
static struct page*
page_lookup (const void *vaddr)
{
	struct page p;
  struct hash_elem *e;

  p.vaddr = pg_round_down (vaddr);
	struct process *cur = thread_current ()->process;
	if (cur)
	{
		e = hash_find (&cur->spage_table, &p.hash_elem);
		if (e)
			return hash_entry (e, struct page, hash_elem);
	}
	return NULL;
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry (p_, struct page, hash_elem);
    return hash_bytes (&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED)
{
    const struct page *a = hash_entry (a_, struct page, hash_elem);
    const struct page *b = hash_entry (b_, struct page, hash_elem);
    return a->vaddr < b->vaddr;
}

/* Checks if there is a supplemental page entry for user virtual address
vaddr. */
bool
page_exists (const void *vaddr)
{
	if (page_lookup (vaddr))
		return true;
	return false;
}

/* Allocates a new page under the bottom of the stack of the current thread. */
void *
stack_page_alloc (void) 
{
	void *kaddr = page_alloc (get_stack_bottom() - PGSIZE);
  if (kaddr)
  {
    ++thread_current ()->stack_pages; 
  }
  return kaddr;
}

/* Allocates pages under the stack until virtual address vaddr is loaded into
 * a frame. */
void *
stack_page_alloc_multiple (void *vaddr)
{
  ASSERT (vaddr < PHYS_BASE && vaddr >= MIN_STACK_ADDRESS);

  void *stack_bottom = get_stack_bottom ();
  while (vaddr < stack_bottom)
  {
    if (!stack_page_alloc ())
      return NULL;
    stack_bottom = get_stack_bottom ();
  }
  return stack_bottom;
}

/* Allocates a user page with that contains virtual address vaddr. Allocates a 
 * frame and loads the page into the frame. Returns the base kernel virtual 
 * address of the page. */
void *
page_alloc (void *vaddr)
{
  vaddr = pg_round_down (vaddr);
  uint8_t *kaddr = falloc (PAL_USER | PAL_ZERO); 
  if (kaddr)
  {
    if (!install_page (vaddr, kaddr, true))
      return NULL;
  }
  /* return NULL; */
  return kaddr;
}

/* Lazy load a segment from executable file. The file metadata will be
stored into the process supplemental page table. */
void
lazy_load_segment (void *vaddr, struct file *file, off_t ofs,
										uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	struct process *process = thread_current ()->process;
	if (process)
	{
		struct page *page = malloc (sizeof (struct page));
		page->present = PRESENT_FILESYS;
		page->vaddr = vaddr;
		page->writable = writable;
		page->file = file;
		page->ofs = ofs;
		page->read_bytes = read_bytes;
		page->zero_bytes = zero_bytes;
		hash_insert (&process->spage_table, &page->hash_elem);	
	}
}  

/* Looks up the page containing virtual address vaddr, and calls the helper
function corresponding to where the page is located. Returns true if the page
is successfully loaded, false otherwise. */
bool
load_page_into_frame (const void *vaddr)
{
	struct page *page = page_lookup (vaddr);
	if (page)
	{
		switch (page->present)
		{
			case PRESENT_FILESYS:
				return load_page_from_filesys (page);
			case PRESENT_SWAP:
				return false;
			default:
				break;
		}
	}
	return false;
}

/* Loads page into a frame. Returns true if successful, false otherwise. */
static bool
load_page_from_filesys (struct page *page)
{
  ASSERT (page->present == PRESENT_FILESYS);

	file_seek (page->file, page->ofs);

  /* Get a page of memory. */
  uint8_t *kpage = falloc (PAL_USER);
  if (kpage == NULL)
    return false;

  /* Load this page. */
  if (file_read (page->file, kpage, page->read_bytes) != (int) page->read_bytes)
  {
    ffree (kpage);
    return false; 
  }
  memset (kpage + page->read_bytes, 0, page->zero_bytes);

  /* Add the page to the process's address space. */
  if (!install_page (page->vaddr, kpage, page->writable)) 
  {
    free (kpage);
    return false; 
  }
  page->present = PRESENT_MEMORY;
  return true;
}
