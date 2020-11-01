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
static bool page_frame_alloc (struct page *page);
static bool install_page (void *upage, void *kpage, bool writable);
static void internal_page_free (struct page *page);

/* Indicates where the page is. */
enum page_present {
  PRESENT_MEMORY,
  PRESENT_FILESYS,
  PRESENT_SWAP
};

/* Page metadata to be stored in the supplemental page table. */
struct page {
  void *upage;  /* User virtual address. */
  void *kpage;  /* Kernel virtual address. */
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

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry (p_, struct page, hash_elem);
    return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED)
{
    const struct page *a = hash_entry (a_, struct page, hash_elem);
    const struct page *b = hash_entry (b_, struct page, hash_elem);
    return a->upage < b->upage;
}

/* Looks up page with user virtual address uaddr. */
static struct page*
page_lookup (const void *uaddr)
{
	struct page p;
  struct hash_elem *e;

  p.upage = pg_round_down (uaddr);
	struct process *cur = thread_current ()->process;
	if (cur)
	{
		e = hash_find (&cur->spage_table, &p.hash_elem);
		if (e)
			return hash_entry (e, struct page, hash_elem);
	}
	return NULL;
}

/* Checks if there is a supplemental page entry for user virtual address
uaddr. */
bool
page_exists (const void *uaddr)
{
	if (page_lookup (uaddr))
		return true;
	return false;
}

/* Allocates a new page under the bottom of the stack of the current thread. */
void *
stack_page_alloc (void) 
{
  struct page *page = malloc (sizeof (struct page));
  if (page)
  {
    page->upage = get_stack_bottom () - PGSIZE;
    page->writable = true;
    if (page_frame_alloc (page))
    {
      ++thread_current ()->stack_pages; 
    }
    return page->kpage;
  }
  return NULL;
}

/* Allocates pages under the stack until user virtual address uaddr is loaded 
 * into a frame. */
void *
stack_page_alloc_multiple (void *uaddr)
{
  ASSERT (uaddr < PHYS_BASE && uaddr >= MIN_STACK_ADDRESS);

  void *stack_bottom = get_stack_bottom ();
  while (uaddr < stack_bottom)
  {
    if (!stack_page_alloc ())
      return NULL;
    stack_bottom = get_stack_bottom ();
  }
  return stack_bottom;
}

/* Frees a page with base user virtual address uaddr. */
void
page_free (void *uaddr)
{
  struct page *page = page_lookup (uaddr);
  if (page)
    internal_page_free (page);
}

/* Lazy load a segment from executable file. The file metadata will be
stored into the process supplemental page table. */
void
lazy_load_segment (void *uaddr, struct file *file, off_t ofs,
										uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	struct process *process = thread_current ()->process;
	if (process)
	{
		struct page *page = malloc (sizeof (struct page));
		page->present = PRESENT_FILESYS;
		page->upage = uaddr;
		page->writable = writable;
		page->file = file;
		page->ofs = ofs;
		page->read_bytes = read_bytes;
		page->zero_bytes = zero_bytes;
		hash_insert (&process->spage_table, &page->hash_elem);	
	}
}  

/* Looks up the page containing user virtual address upage, and calls the helper
function corresponding to where the page is located. Returns true if the page
is successfully loaded, false otherwise. */
bool
load_page_into_frame (const void *uaddr)
{
	struct page *page = page_lookup (uaddr);
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

  /* Get a page of memory and add it to the process's address space. */
  if (!page_frame_alloc (page))
    return false;

  /* Load this page. */
  if (file_read (page->file, page->kpage, page->read_bytes) != (int) page->read_bytes)
  {
    internal_page_free (page);
    return false; 
  }
  memset (page->kpage + page->read_bytes, 0, page->zero_bytes);

  page->present = PRESENT_MEMORY;
  return true;
}

/* Allocates a frame for page. Return true if successful, false otherwise. */
static bool
page_frame_alloc (struct page *page)
{
  void *upage = pg_round_down (page->upage);
  void *kpage = falloc (PAL_USER | PAL_ZERO); 
  if (kpage && install_page (upage, kpage, page->writable))
  {
    page->kpage = kpage;
    return true;
  }
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Uninstall page from the current process's pagedir and free page. */
static void
internal_page_free (struct page *page)
{
  if (page)
  {
    pagedir_clear_page (thread_current ()->pagedir, page->upage);
    ffree (page->kpage);
  }
}
