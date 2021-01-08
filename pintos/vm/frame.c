#include "vm/frame.h"
#include "devices/timer.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

static struct frame *get_frame (void *kpage);
static struct frame *get_frame_to_evict (void);

/* Contains one entry for each frame that contains a user page */
struct list frame_table;

struct frame {
  void *kpage;
  struct page *page;

  /* Used for eviction clock algorithm. */
  int64_t last_accessed_tick;

  struct list_elem elem;
};

/* Lock to ensure only one thread is accessing the frame at a time. */
struct lock frame_lock;

void
falloc_init (void) {
  list_init (&frame_table);
  lock_init (&frame_lock);
}

/* Allocates a page and returns a pointer to it. If no frames are
 * are availible, evict a used frame. */
void *
falloc (struct page *page, enum palloc_flags flags)
{
  lock_acquire (&frame_lock);
  void *kpage = palloc_get_page (flags);
  if (kpage)
  {
    struct frame *frame = malloc (sizeof (struct frame));
    frame->kpage = kpage;
    frame->page = page;
    frame->last_accessed_tick = timer_ticks ();
    page->kpage = kpage;
    list_push_back (&frame_table, &frame->elem);
    lock_release (&frame_lock);
    return kpage;
  }

  /* No frame is available. Evict a page and load it into swap. The page
   * requesting a frame will point to the kpage of the evicted page. */
  struct frame *evict_frame = get_frame_to_evict ();
  struct page *evict_page = evict_frame->page;

  list_remove (&evict_frame->elem);
  list_push_back (&frame_table, &evict_frame->elem);

  /* If page is unmodified and comes from a file, evict the page to
   * filesys. Check upage and kpage dirty bit, since
   * they are both aliased to the same frame. */
  uint32_t *pagedir = get_thread (evict_page->tid)->pagedir;
  if (!pagedir_is_dirty (pagedir, evict_page->upage) &&
      !pagedir_is_dirty (pagedir, evict_page->kpage) &&
      evict_page->file)
    evict_page->present = PRESENT_FILESYS;

  /* If unable to evict page to filesys, evict to swap. */
  else
  {
    swap_page_t swap_page = swalloc ();
    swap_page_write (swap_page, evict_page->kpage);
    evict_page->present = PRESENT_SWAP;
    evict_page->swap_page = swap_page;
  }

  evict_frame->page = page;
  page->kpage = evict_page->kpage;
  evict_page->kpage = NULL;

  /* Clear the evicted pages upage mapping from its process's page
   * directory. */
  struct thread *t = get_thread (evict_page->tid);
  if (t)
    pagedir_clear_page (t->pagedir, evict_page->upage);

  lock_release (&frame_lock);
  return page->kpage;
}

/* Frees a frame entry in frame table.
 * NOTE: don't free the hardware page here because different use cases free
 * in different ways. */
void
ffree (void *kpage)
{
  lock_acquire (&frame_lock);
  struct frame *frame = get_frame (kpage);
  if (frame) {
    list_remove (&frame->elem);
    free (frame);
  }
  lock_release (&frame_lock);
}

/* Returns the frame that is pointed to by page. */
struct frame
*get_frame (void *kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
      e = list_next (e))
  {
    struct frame *frame = list_entry (e, struct frame, elem);
    if (frame->kpage == kpage)
      return frame;
  }
  return NULL;
}

/* Get the next frame to evict. Use the "clock" algorithm, which uses timer
 * ticks to estimate LRU. */
static struct frame *
get_frame_to_evict (void)
{
  ASSERT (list_size (&frame_table) > 0);

  struct list_elem *e = list_begin (&frame_table);
  struct frame *lru_frame = list_entry (e, struct frame, elem);
  e = list_next (e);
  for (; e != list_end (&frame_table); e = list_next (e))
  {
    struct frame *next_frame = list_entry (e, struct frame, elem);
    if (next_frame->last_accessed_tick < lru_frame->last_accessed_tick)
      lru_frame = next_frame;
  }
  return lru_frame;
}

/* Update frame last accessed tick. Used to approximate a LUR eviction
 * strategy. This function should be called each timer interrupt. */
void
frame_tick (void)
{
  int64_t cur_tick = timer_ticks ();
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
      e = list_next (e))
  {
    struct frame *frame = list_entry (e, struct frame, elem);
    struct page *page = frame->page;
    uint32_t *pagedir = get_thread (page->tid)->pagedir; 

    if (pagedir_is_accessed (pagedir, page->upage))
    {
      frame->last_accessed_tick = cur_tick;
      pagedir_set_accessed (get_thread (page->tid)->pagedir, page->upage,
          false);
    }
  }
}
