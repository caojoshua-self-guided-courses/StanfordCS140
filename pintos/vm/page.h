#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "lib/kernel/hash.h"

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, 
    void *aux);

void lazy_load_segment (void *vaddr, struct file *file, off_t ofs,
	uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool load_page_into_frame (const void *vaddr);

#endif /* vm/page.h */
