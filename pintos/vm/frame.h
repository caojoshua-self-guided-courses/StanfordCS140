#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "vm/page.h"
#include "vm/swap.h"

struct page;

void falloc_init (void);
void *falloc (struct page *page, enum palloc_flags flags);
void ffree (void *page);

#endif /* threads/frame.h */
