#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Struct to hold process data. Note that this only contains
 * enough data for exit status logic. Process structs live
 * after process termination to hold their exit status */
struct process
{
  int status;
  pid_t pid;
  pid_t parent_pid;
  bool is_waited_on;
  struct list_elem elem;
};

/* List of processes */
struct list process_list;

struct process *get_process (pid_t pid);
void clean_child_processes (pid_t pid);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
