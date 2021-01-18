#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "lib/kernel/hash.h"
#include "vm/swap.h"

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
  int tid;
  bool dirty_bit;
  int access_time;
  struct hash_elem hash_elem;

  /* Page is in file system */
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
	uint32_t zero_bytes;

  /* Page is in swap. */
  /* swap_page_t swap_page; */
  int swap_page;
};

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, 
    void *aux);
void page_destructor (struct hash_elem *hash_elem, void *aux);

void spage_init (void);
bool page_exists (const void *vaddr);
bool is_unallocated_stack_access (const void *fault_addr);
void *stack_page_alloc (void);
void *stack_page_alloc_multiple (void *vaddr);
void page_free (void *vaddr);
void lazy_load_segment (void *vaddr, struct file *file, off_t ofs,
	uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool load_page_into_frame (const void *vaddr);

#endif /* vm/page.h */
