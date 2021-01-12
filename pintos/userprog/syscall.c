#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall-file.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);
static void acquire_filesys_syscall_lock (void);
static void release_filesys_syscall_lock (void);

/* Lock to make sure only one thread can execute a filesys ssycall at a
 * time. */
struct lock filesys_syscall_lock;

/* Acquire the filesys syscall lock. */
static void
acquire_filesys_syscall_lock ()
{
  lock_acquire (&filesys_syscall_lock);
}

/* Release the filesys syscall lock. */
static void
release_filesys_syscall_lock ()
{
  lock_release (&filesys_syscall_lock);
}

/* Register syscall_handler for interrupts */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_syscall_lock);
}

/* Validate uaddr as a user address. If uaddr is not valid
 * terminate the thread */
static void
validate_uaddr (const void *uaddr)
{
  if (!uaddr || 
    !is_user_vaddr (uaddr) ||
		!page_exists (uaddr))
    exit (-1);
}

/* Validate the number of args above esp on the stack */
static void
validate_args (const void *esp, unsigned args)
{
  for (unsigned i = 0; i <= args; ++i)
    validate_uaddr (esp + (i * 4));
}

/* Validates the first and last bytes in a string */
static void
validate_string (const void *string)
{
  validate_uaddr (string);
  validate_uaddr (string + strlen (string));
}

/* Creates a file or directory. Called by sys open and create. */
static bool
create_generic (const char *name, int initial_size, bool is_dir)
{
  validate_string (name);
  acquire_filesys_syscall_lock ();
  bool result = filesys_create (name, initial_size, is_dir);
  release_filesys_syscall_lock ();
  return result;
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
  struct process *process = thread_current ()->process;
  if (process)
  {
    process->status = status;
    if (process->dir)
      dir_close (process->dir);
    clean_mapids ();
    clean_fds ();
  }

  /* Error message for passing test cases */
  printf("%s: exit(%d)\n", process->file_name, status);

  /* Terminate the thread */
  thread_exit ();
}

static pid_t
exec (const char *cmd_line)
{
  validate_string (cmd_line);

  acquire_filesys_syscall_lock ();
  tid_t tid = process_execute (cmd_line);
  struct thread *t = get_thread (tid);
  if (t)
  {
    struct process *p = t->process;
    sema_down (t->loaded_sema);
    /* Only return the tid if the executable is loaded successfully */
    if (p && p->loaded_success)
    {
      release_filesys_syscall_lock ();
      return tid;
    }
  }
  release_filesys_syscall_lock ();
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
  return create_generic (file, initial_size, false);
}

static bool
remove (const char *file)
{
  validate_string (file);
  acquire_filesys_syscall_lock ();
  bool result = filesys_remove (file);
  release_filesys_syscall_lock ();
  return result;
}

static int
open (const char *file)
{
  validate_string (file);
  acquire_filesys_syscall_lock ();
  int fd = create_fd (file);
  release_filesys_syscall_lock ();
  return fd;
}

static int
filesize (int fd)
{
  int filesize = -1;
  acquire_filesys_syscall_lock ();
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (file_descriptor && !file_descriptor->is_dir)
      filesize = file_length (file_descriptor->file.file);
  release_filesys_syscall_lock ();
  return filesize;
}

static int
read (int fd, void *buffer, unsigned size)
{
  validate_uaddr (buffer);
  validate_uaddr (buffer + size);
  
  int read_bytes = -1;
  acquire_filesys_syscall_lock ();

  /* fd 0 is keyboard */
  if (fd == 0)
  {
    *(uint8_t *) buffer = input_getc();
    read_bytes = 1;
  }
  else
  {
    struct file_descriptor *file_descriptor = get_file_descriptor (fd);
    if (file_descriptor && !file_descriptor->is_dir)
      read_bytes = file_read (file_descriptor->file.file, buffer, size);
  }
  release_filesys_syscall_lock ();
  return read_bytes;
}

static int
write (int fd, const void *buffer, unsigned size)
{
  validate_uaddr (buffer);
  validate_uaddr (buffer + size);

  int write_bytes = 0;
  acquire_filesys_syscall_lock ();

  if (fd == 1) {
    putbuf (buffer, size);
    write_bytes = size;
  }
  else
  {
    struct file_descriptor *file_descriptor = get_file_descriptor (fd);
    if (file_descriptor && !file_descriptor->is_dir)
      write_bytes = file_write (file_descriptor->file.file, buffer, size);
  }
  release_filesys_syscall_lock ();
  return write_bytes;
}

static void
seek (int fd, unsigned position)
{
  acquire_filesys_syscall_lock ();
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor && !file_descriptor->is_dir)
    file_seek (file_descriptor->file.file, position);
  release_filesys_syscall_lock ();
}

static unsigned
tell (int fd)
{
  unsigned pos = -1;
  acquire_filesys_syscall_lock ();
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor && !file_descriptor->is_dir)
    pos = file_tell (file_descriptor->file.file);
  release_filesys_syscall_lock ();
  return pos;
}

static void
close (int fd)
{
  acquire_filesys_syscall_lock ();
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor)
  {
    list_remove (&file_descriptor->elem);
    fd_close_file (file_descriptor);
  }
  release_filesys_syscall_lock ();
}

static int
mmap (int fd, void *addr)
{
  if (!addr || pg_ofs (addr) != 0)
    return -1;

  acquire_filesys_syscall_lock ();
  int mapid = -1;

  struct mapid_entry *mapid_entry = create_mapid (fd, addr);
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (mapid_entry && file_descriptor && !file_descriptor->is_dir)
  {
    struct file* file = file_descriptor->file.file;
    int len = file_length (file);

    /* Check that all pages are availible. It is more efficient to check in two
     * loops. Checking and mapping in the same loop would require freeing
     * previously allocated pages if there is a failed check. */
    void *paddr = addr;
    int ofs = 0;
    while (ofs < len)
    {
      if (page_exists (paddr + ofs))
      {
        release_filesys_syscall_lock ();
        return -1;
      }
      paddr += PGSIZE;
      ofs += PGSIZE;
    }

    /* Lazy load a page in each loop iteration. */
    paddr = addr;
    ofs = 0;
    while (ofs < len)
    {
      /* Only read the remaining bytes for the last page. */
      int bytes_left = len - ofs;
      int page_read_bytes = bytes_left > PGSIZE ? PGSIZE : bytes_left;

      lazy_load_segment (paddr, file, ofs, page_read_bytes,
          PGSIZE - page_read_bytes, true);
      paddr += PGSIZE;
      ofs += PGSIZE;
    }
    mapid = mapid_entry->mapid;
  }
  release_filesys_syscall_lock ();
  return mapid;
}

static void
munmap (int mapid)
{
  acquire_filesys_syscall_lock ();
  remove_mapid (mapid);
  release_filesys_syscall_lock ();
}

static bool
chdir (const char *dir_name)
{
  struct dir *dir = filesys_open_dir (dir_name);
  if (dir)
  {
    struct process *p = thread_current ()->process;
    if (p)
    {
      if (p->dir)
      dir_close (p->dir);
      p->dir = dir;
      return true;
    }
  }
  return false;
}

static bool
mkdir (const char *dir)
{
  return create_generic (dir, 0, true);
}

/* static bool */
/* readdir (int fd, char *name) */
/* { */
/*   return false; */
/* } */

static bool
isdir (int fd)
{
  bool result = false;
  acquire_filesys_syscall_lock ();
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (file_descriptor)
    result = file_descriptor->is_dir;
  release_filesys_syscall_lock ();
  return result;
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;
  thread_current ()->esp = esp;
  validate_uaddr (esp);
  
  uint32_t syscall_num = *(int *) esp;
  switch (syscall_num)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      validate_args (esp, 1);
      exit (*(int *)(esp + 4));
      break;
    case SYS_EXEC:
      validate_args (esp, 1);
      f->eax = exec (*(const char **)(esp + 4));
      break;
    case SYS_WAIT:
      validate_args (esp, 1);
      f->eax = wait (*(int *)(esp + 4));
      break;
    case SYS_CREATE:
      validate_args (esp, 2);
      f->eax = create (*(const char **)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_REMOVE:
      validate_args (esp, 1);
      f->eax = remove (*(const char **)(esp + 4));
      break;
    case SYS_OPEN:
      validate_args (esp, 1);
      f->eax = open (*(const char **)(esp + 4));
      break;
    case SYS_FILESIZE:
      validate_args (esp, 1);
      f->eax = filesize (*(int *)(esp + 4));
      break;
    case SYS_READ:
      validate_args (esp, 3);
      f->eax = read (*(int *)(esp + 4), (void *) *(int *)(esp + 8), 
          *(unsigned *)(esp + 12));
      break;
    case SYS_WRITE:
      validate_args (esp, 3);
      f->eax = write (*(int *)(esp + 4), (const void *) *(int *)(esp + 8), 
          *(unsigned *)(esp + 12));
      break;
    case SYS_SEEK:
      validate_args (esp, 2);
      seek (*(int *)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_TELL:
      validate_args (esp, 1);
      f->eax = tell (*(int *)(esp + 4)); 
      break;
    case SYS_CLOSE:
      validate_args (esp, 1);
      close(*(int *)(esp + 4));
      break;
    case SYS_MMAP:
      validate_args (esp, 2);
      f->eax = mmap (*(int *)(esp + 4), *(void **)(esp + 8));
      break;
    case SYS_MUNMAP:
      validate_args (esp, 1);
      munmap (*(int *)(esp + 4));
      break;
    case SYS_CHDIR:
      validate_args (esp, 1);
      f->eax = chdir (*(const char **) (esp + 4));
      break;
    case SYS_MKDIR:
      validate_args (esp, 1);
      f->eax = mkdir (*(const char **) (esp + 4));
      break;
    case SYS_READDIR:
      break;
    case SYS_ISDIR:
      validate_args (esp, 1);
      f->eax = isdir (*(int *)(esp + 4));
      break;
    case SYS_INUMBER:
      break;
    default:
      thread_exit();
  }
}
