#include "vm/frame.h"

#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

#include "devices/block.h"
#include "lib/random.h"

static struct frame *get_frame (void *kpage);
static struct frame *get_frame_to_evict (void);

/* Contains one entry for each frame that contains a user page */
struct list frame_table;

struct frame {
  void *kpage;
  struct page *page;
  struct thread *thread;
  struct list_elem elem;
};

void
falloc_init (void) {
  list_init (&frame_table);
}

/* Allocates a page and returns a pointer to it. If no frames are
 * are availible, evict a used frame. */
void *
falloc (struct page *page, enum palloc_flags flags)
{
  void *kpage = palloc_get_page (flags);
  if (kpage)
  {
    struct frame *frame = malloc (sizeof (struct frame));
    frame->kpage = kpage;
    frame->page = page;
    page->kpage = kpage;
    list_push_back (&frame_table, &frame->elem);
    return kpage;
  }

  /* No frame is available. Evict a page and load it into swap. The page
   * requesting a frame will point to the kpage of the evicted page. */
  swap_page_t swap_page = swalloc ();
  struct frame *evict_frame = get_frame_to_evict ();
  struct page *evict_page = evict_frame->page;

  evict_frame->page = page;
  list_remove (&evict_frame->elem);
  list_push_back (&frame_table, &evict_frame->elem);

  /* TODO: unmodified file pages can be read back to filesys. */
  swap_page_write (swap_page, evict_page->kpage);
  page->kpage = evict_page->kpage;
  pagedir_clear_page (thread_current ()->pagedir, evict_page->upage);
  evict_page->present = PRESENT_SWAP;
  evict_page->swap_page = swap_page;

  return page->kpage;
}

/* Frees a frame. */
void
ffree (void *kpage)
{
  struct frame *frame = get_frame(kpage);
  if (frame) {
    palloc_free_page (frame->kpage);
    list_remove (&frame->elem);
    free (frame);
  }
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

/* Get the next frame to evict. */
static struct frame *
get_frame_to_evict (void)
{
  // TODO: more efficient eviction strategy. Random for now.
  long random = random_ulong () % list_size (&frame_table);
  long i = 0;
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
      e = list_next (e))
  {
    if (i == random)
      return list_entry (e, struct frame, elem);
    ++i;
  }
  return NULL;
}
