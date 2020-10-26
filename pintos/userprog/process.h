#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "lib/kernel/hash.h"
#include "threads/thread.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Struct to hold all data for processes. This is particularly important for
 * data that must live after a thread exits, such as the exit status. The 
 * other data could alternatively be stored in struct thread. Note
 * that a process has the same PID as its thread's TID. */
struct process
{
  /* If multithreaded processes were supported, we should maintain a
   * list of threads. In pintos, we can just use the thread with
   * the same PID/TID. */
  int status;
  pid_t pid;
  pid_t parent_pid;
  char file_name[16];
  struct file *executable;
  bool loaded_success;
  bool is_waited_on;
  struct list fd_map;
  struct hash mapid_map;
	struct hash spage_table;		/* Supplemental page table. */
  struct list_elem elem;
};

/* List of processes */
struct list process_list;

void clean_child_processes (pid_t pid);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
