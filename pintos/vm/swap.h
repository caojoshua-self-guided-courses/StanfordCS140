#include "lib/stdint.h"

/* Represents which page sequentially in swap. */
typedef int32_t swap_page_t;

void swalloc_init (void);

swap_page_t swalloc (void);
void swfree (swap_page_t swap_page);

void swap_page_read (swap_page_t swap_page, void *buffer);
void swap_page_write (swap_page_t swap_page, const void *buffer);

