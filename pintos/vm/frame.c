#include "vm/frame.h"

#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"

struct frame *get_frame (void *kpage);

/* Contains one entry for each frame that contains a user page */
struct list frame_table;

struct frame {
  void *kpage;
  struct thread *thread;
  struct list_elem elem;
};

/* Returns the frame that is pointed to by page */
struct frame *get_frame (void *kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table); 
      e = list_next (e))
  {
    struct frame *frame = list_entry (e, struct frame, elem);
    if (frame->kpage == kpage)
    {
      return frame;
    }
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
  void *kpage = palloc_get_page (flags);
  if (kpage)
  {
    struct frame *frame = malloc (sizeof (frame));
    frame->kpage = kpage;
    list_push_back (&frame_table, &frame->elem);
    return kpage;
  }

  // TODO: evict a page if none are available
  return kpage;
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
