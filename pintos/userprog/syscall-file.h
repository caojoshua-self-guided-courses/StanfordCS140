#ifndef USERPROG_SYSCALL_FILE_H
#define USERPROG_SYSCALL_FILE_H

#include <hash.h>
#include "userprog/process.h"

/* Elements of process->fd_map that map fd to files */
struct file_descriptor
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

/* Elements of process->mapid_map that map mapids to mapped file pages. */
struct mapid_entry
{
  int mapid;
  int fd;
  struct file *file;
  void *addr;
  unsigned length;
  struct hash_elem hash_elem;
};

/* File descriptor functions. */
int create_fd (const char *file_name);
void clean_fds (void);
struct file_descriptor* get_file_descriptor (int fd);

/* Map ID functions. */
unsigned mapid_hash (const struct hash_elem *m_, void *aux UNUSED);
bool mapid_less (const struct hash_elem *a_, const struct hash_elem *b_,
    UNUSED void* aux);
struct mapid_entry* create_mapid (int fd, void *addr);
void remove_mapid (int mapid);
void clean_mapids (void);

#endif /* userprog/syscall-file.h */
