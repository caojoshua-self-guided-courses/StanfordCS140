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

/* Lock to ensure only one thread is accessing the supplemental page table at
 * a time. */
/* TODO: Lock should be per process instead of system-wide. */
struct lock page_lock;

static bool load_page_from_filesys (struct page *page);
static bool load_page_from_swap (struct page *page);
static void page_add_spage_table (struct page *page);
static bool page_frame_alloc (struct page *page);
static bool install_page (void *upage, void *kpage, bool writable);
static void internal_page_free (struct page *page);

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

/* Spage table entry destructor passed into hash_destroy. */
void
page_destructor (struct hash_elem *hash_elem, void *aux UNUSED)
{
  lock_acquire (&page_lock);
  struct page *page = hash_entry (hash_elem, struct page, hash_elem);
  {
    switch (page->present)
    {
      case PRESENT_MEMORY:
        ffree (page->kpage);
        break;
      case PRESENT_SWAP:
        swfree (page->swap_page);
        break;
      default:
        break;
    }
    free (page);
  }
  lock_release (&page_lock);
}

/* Init supplemental page table. NOTE: Individual supplemental page tables are
 * init on process creation. */
void
spage_init (void)
{
  lock_init (&page_lock);
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
  lock_acquire (&page_lock);
  bool exists = page_lookup (uaddr);
  lock_release (&page_lock);
  return exists;
}

/* Returns if fault address fault_addr is an unallocated stack access. 
 * Assumes the current thread's esp is correctly set. Can be used to
 * determine if the stack needs to grow. */ 
bool
is_unallocated_stack_access (const void* fault_addr)
{
  lock_acquire (&page_lock);
  void *esp = thread_current ()->esp;
  bool result = fault_addr < get_stack_bottom() && fault_addr >= esp - PUSHA_BYTES
		&& fault_addr >= MIN_STACK_ADDRESS; 
  lock_release (&page_lock);
  return result;
}

/* Allocates a new page under the bottom of the stack of the current thread. 
 * Return the allocated pages upage. */
void *
stack_page_alloc (void) 
{
  lock_acquire (&page_lock);
  struct page *page = malloc (sizeof (struct page));
  void *kpage = NULL;
  if (page)
  {
    page->upage = get_stack_bottom () - PGSIZE;
    page->present = PRESENT_MEMORY;
    page->writable = true;
    page->tid = thread_current ()->tid;
    page_add_spage_table (page);

    if (page_frame_alloc (page))
      ++thread_current ()->stack_pages; 
    else
      page_free (page);

    kpage = page->upage;
  }
  lock_release (&page_lock);
  return kpage;
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
  lock_acquire (&page_lock);
  struct page *page = page_lookup (uaddr);
  if (page)
    internal_page_free (page);
  lock_release (&page_lock);
}

/* Lazy load a segment from executable file. The file metadata will be
stored into the process supplemental page table. */
void
lazy_load_segment (void *uaddr, struct file *file, off_t ofs,
										uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  lock_acquire (&page_lock);
  struct page *page = malloc (sizeof (struct page));
  page->present = PRESENT_FILESYS;
  page->upage = uaddr;
  page->writable = writable;
  page->tid = thread_current ()->tid;
  /* Open a new file instance because the original may close. */
  page->file = file_reopen (file);
  page->ofs = ofs;
  page->read_bytes = read_bytes;
  page->zero_bytes = zero_bytes;
  page_add_spage_table (page);
  lock_release (&page_lock);
}  

/* Looks up the page containing user virtual address upage, and calls the helper
function corresponding to where the page is located. Returns true if the page
is successfully loaded, false otherwise. */
bool
load_page_into_frame (const void *uaddr)
{
  lock_acquire (&page_lock);
	struct page *page = page_lookup (uaddr);
  bool result = false;
	if (page)
	{
		switch (page->present)
		{
			case PRESENT_FILESYS:
        result = load_page_from_filesys (page);
        break;
			case PRESENT_SWAP:
        result = load_page_from_swap (page);
        break;
			default:
				break;
		}
	}
  lock_release (&page_lock);
	return result;
}

/* Loads page from the filesys into a frame. Returns true if successful. */
static bool
load_page_from_filesys (struct page *page)
{
  ASSERT (page->present == PRESENT_FILESYS);

  /* Get a page of memory and add it to the process's address space. */
  if (!page_frame_alloc (page))
    return false;

  /* Load this page. */
	file_seek (page->file, page->ofs);
  if (file_read (page->file, page->kpage, page->read_bytes) != (int) page->read_bytes)
  {
    internal_page_free (page);
    return false; 
  }
  memset (page->kpage + page->read_bytes, 0, page->zero_bytes);

  page->present = PRESENT_MEMORY;
  return true;
}

/* Loads page from swap into a frame. Returns true if successful. */
static bool
load_page_from_swap (struct page *page)
{
  ASSERT (page->present == PRESENT_SWAP);

  if (!page_frame_alloc (page))
    return false;

  swap_page_read (page->swap_page, page->kpage);
  swfree (page->swap_page);

  page->present = PRESENT_MEMORY;
  return true;
}

/* Adds page to the process's supplemental page table. */
static void
page_add_spage_table (struct page *page)
{
  struct process *p = thread_current ()->process;
  if (p)
		hash_insert (&p->spage_table, &page->hash_elem);	
}

/* Allocates a frame for page. Return true if successful, false otherwise. */
static bool
page_frame_alloc (struct page *page)
{
  page->upage = pg_round_down (page->upage);
  void *kpage = falloc (page, PAL_USER | PAL_ZERO); 
  return kpage && install_page (page->upage, kpage, page->writable);
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
  struct process *p = thread_current ()->process;
  if (page)
  {
    pagedir_clear_page (thread_current ()->pagedir, page->upage);
    if (page->present == PRESENT_MEMORY)
      palloc_free_page (page->kpage);
    ffree (page->kpage);
    file_close (page->file);
    hash_delete (&p->spage_table, &page->hash_elem);
    free (page);
  }
}
