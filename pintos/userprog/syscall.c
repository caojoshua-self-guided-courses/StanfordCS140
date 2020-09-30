#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

/* Min fd. fd 0 and fd 1 are reserved for stdin and stdout 
 * respectively. */
static const int MIN_FD = 2;

/* List of fd_entries, sorted by fd */
static struct list fd_list;

/* Elements of fd_list that map fd to files */
struct fd_entry
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

/* register syscall_handler for interrupts */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init (&fd_list);
}

/* Create and return a new file descriptor. */
static int
create_fd (const char *file_name)
{
  if (!file_name)
    return -1;
  struct file *file = filesys_open (file_name);
  if (!file)
    return -1;

  int fd = MIN_FD;
  struct fd_entry *fd_entry = malloc (sizeof (struct fd_entry));
  fd_entry->file = file;

  /* Iterate through the fd list until there is an open fd */
  struct list_elem *e; 
  for (e = list_begin (&fd_list); e != list_end (&fd_list); e = list_next (e))
  {
    struct fd_entry *fde = list_entry (e, struct fd_entry, elem); 
    if (fd != fde->fd)
    {
      fd_entry->fd = fd;
      e = list_next(e);
      break;
    }
    ++fd;
  }
  fd_entry->fd = fd;
  list_insert (e, &fd_entry->elem);
  return fd_entry->fd;
}

/* Returns the file associated with the given fd */
static struct fd_entry*
get_fd_entry (int fd)
{
  struct list_elem *e;
  for (e = list_begin (&fd_list); e != list_end (&fd_list); e = list_next (e))
  {
    struct fd_entry *fd_entry = list_entry (e, struct fd_entry, elem); 
    if (fd_entry->fd == fd)
      return fd_entry;
  }
  return NULL;
}

/* Validate uaddr as a user address. If uaddr is not valid
 * terminate the thread */
static void
validate_uaddr (const void *uaddr)
{
  if (!uaddr || !is_user_vaddr(uaddr) ||
    !pagedir_get_page(thread_current()->pagedir, uaddr))
    exit (-1);
}

/* Validates the first and last bytes in a string */
static void
validate_string (const void *string)
{
  validate_uaddr (string);
  validate_uaddr (string + strlen (string));
}


/********** SYSTEM CALLS **********/

static void
halt (void)
{
  shutdown_power_off();
}

/* The exit syscall is the only non-static syscall because its called in
 * Exception.c when there is a user page fault */
void
exit (int status)
{
  /* NOTE: exit is only syscall that contains process logic because its
   * awkward passing status to thread_exit */

  /* Update the processes exit status. This status will persist after
   * this process is terminating in case the parent process wants
   * to retrieve it */
  struct process *process = get_process (thread_current ()->tid);
  if (process)
    process->status = status;

  /* Error message for passing test cases */
  printf("%s: exit(%d)\n", thread_current ()->file_name, status);

  /* Terminate the thread */
  thread_exit ();
}

static pid_t
exec (const char *cmd_line)
{
  validate_string (cmd_line);

  tid_t tid = process_execute (cmd_line);
  struct thread *t = get_thread (tid);
  if (t)
  {
    sema_down (t->loaded_sema);
    /* Only return the tid if the executable is loaded successfully */
    if (t->loaded_success)
      return tid;
  }
  return PID_ERROR; 
}

static int
wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
create (const char *file, unsigned initial_size)
{
  validate_string (file);
  return filesys_create (file, initial_size); 
}

static bool
remove (const char *file)
{
  validate_string (file);
  return filesys_remove (file);
}

static int
open (const char *file)
{
  validate_string (file);
  return create_fd (file);
}

static int
filesize (int fd)
{
  struct fd_entry *fd_entry = get_fd_entry (fd);
  if (fd_entry)
    return file_length (fd_entry->file);
  return -1;
}

static int
read (int fd, void *buffer, unsigned size)
{
  validate_uaddr (buffer);
  validate_uaddr (buffer + size);
  
  /* fd 0 is keyboard */
  if (fd == 0)
  {
    *(uint8_t *) buffer = input_getc();
    return 1;
  }
  struct fd_entry *fd_entry = get_fd_entry (fd);
  if (fd_entry)
    return file_read (fd_entry->file, buffer, size); 
  return -1;
}

static int
write (int fd, const void *buffer, unsigned size)
{
  validate_uaddr (buffer);
  validate_uaddr (buffer + size);

  if (fd == 1) {
    putbuf (buffer, size);
    return size;
  }
  struct fd_entry *fd_entry = get_fd_entry (fd);
  if (fd_entry)
    return file_write (fd_entry->file, buffer, size);  
  return 0;
}

static void
seek (int fd, unsigned position)
{
  struct fd_entry *fd_entry = get_fd_entry(fd);
  if (fd_entry)
    file_seek (fd_entry->file, position);
}

static unsigned
tell (int fd)
{
  struct fd_entry *fd_entry = get_fd_entry(fd);
  if (fd_entry)
    return file_tell (fd_entry->file);
  return -1;
}

static void
close (int fd)
{
  struct fd_entry *fd_entry = get_fd_entry(fd);
  if (fd_entry)
  {
    list_remove (&fd_entry->elem);
    file_close (fd_entry->file);
  }
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;
  validate_uaddr (esp);
  
  uint32_t sys_call_num = *(int *) esp;
  switch (sys_call_num)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      {
      exit (*(int *)(esp + 4));
      break;
      }
    case SYS_EXEC:
      f->eax = exec (*(const char **)(esp + 4));
      break;
    case SYS_WAIT:
      f->eax = wait (*(int *)(esp + 4));
      break;
    case SYS_CREATE:
      f->eax = create (*(const char **)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_REMOVE:
      f->eax = remove (*(const char **)(esp + 4));
      break;
    case SYS_OPEN:
      f->eax = open (*(const char **)(esp + 4));
      break;
    case SYS_FILESIZE:
      f->eax = filesize (*(int *)(esp + 4));
      break;
    case SYS_READ:
      f->eax = read (*(int *)(esp + 4), (void *) *(int *)(esp + 8), 
          *(unsigned *)(esp + 12));
      break;
    case SYS_WRITE:
      f->eax = write (*(int *)(esp + 4), (void *) *(int *)(esp + 8), 
          *(unsigned *)(esp + 12));
      break;
    case SYS_SEEK:
      seek (*(int *)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_TELL:
      f->eax = tell (*(int *)(esp + 4)); 
      break;
    case SYS_CLOSE:
      close(*(int *)(esp + 4));
      break;
    default:
      thread_exit();
  }
}
