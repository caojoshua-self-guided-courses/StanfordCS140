#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"

void falloc_init (void);
void *falloc (enum palloc_flags flags);
void ffree (void *page);

#endif /* threads/frame.h */
