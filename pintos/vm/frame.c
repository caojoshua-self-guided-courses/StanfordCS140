#include "vm/frame.h"

#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"

struct frame *get_frame (void *page);

/* Contains one entry for each frame that contains a user page */
struct list frame_table;

struct frame {
  void *page;
  struct thread *thread;
  struct list_elem elem;
};

/* Returns the frame that is pointed to by page */
struct frame *get_frame (void *page)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table); 
      e = list_next (e))
  {
    struct frame *frame = list_entry (e, struct frame, elem);
    if (frame->page == page)
      return frame;
  }
  return NULL;
}

void
falloc_init (void) {
  list_init (&frame_table);
}

/* Allocates a page and returns a pointer to it. If no frames are
 * are availible, evict a used frame. */
void *
falloc (enum palloc_flags flags)
{
  void *page = palloc_get_page (flags);
  if (page)
  {
    struct frame *frame = malloc (sizeof (frame));
    frame->page = page;
    list_push_back (&frame_table, &frame->elem);
    return page;
  }

  // TODO: evict a page if none are available
  return page;
}

/* Frees a frame. */
void
ffree (void *page)
{
  struct frame *frame = get_frame(page);
  if (frame)
   free (frame);
  palloc_free_page (frame);
}
