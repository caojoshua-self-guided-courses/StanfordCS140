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
#include "userprog/process.h"
#include "userprog/syscall-file.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);

/* register syscall_handler for interrupts */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Validate uaddr as a user address. If uaddr is not valid
 * terminate the thread */
static void
validate_uaddr (void *uaddr)
{
	/* First load the page, in case it was lazy loaded. */
	/* TODO: Should just call page_exists, instead of loading the page. This
	currently does not work because the supplemental page table is not
	keeping track of stack pages. */
	load_page_into_frame (uaddr);

  if (!uaddr || 
    !is_user_vaddr(uaddr) ||
    (is_unallocated_stack_access (uaddr) && !stack_page_alloc_multiple (uaddr)) ||
		/* !page_exists (uaddr)) */
    !pagedir_get_page(thread_current()->pagedir, uaddr))
    exit (-1);
}

/* Validate the number of args above esp on the stack */
static void
validate_args (void *esp, unsigned args)
{
  for (unsigned i = 0; i <= args; ++i)
    validate_uaddr (esp + (i * 4));
}

/* Validates the first and last bytes in a string */
static void
validate_string (void *string)
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
  struct process *process = thread_current ()->process;
  if (process)
  {
    process->status = status;
    clean_mapids ();
    clean_fds ();
  }

  /* Error message for passing test cases */
  printf("%s: exit(%d)\n", process->file_name, status);

  /* Terminate the thread */
  thread_exit ();
}

static pid_t
exec (char *cmd_line)
{
  validate_string (cmd_line);

  tid_t tid = process_execute (cmd_line);
  struct thread *t = get_thread (tid);
  if (t)
  {
    struct process *p = t->process;
    sema_down (t->loaded_sema);
    /* Only return the tid if the executable is loaded successfully */
    if (p && p->loaded_success)
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
create (char *file, unsigned initial_size)
{
  validate_string (file);
  return filesys_create (file, initial_size); 
}

static bool
remove (char *file)
{
  validate_string (file);
  return filesys_remove (file);
}

static int
open (char *file)
{
  validate_string (file);
  return create_fd (file);
}

static int
filesize (int fd)
{
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (file_descriptor)
    return file_length (file_descriptor->file);
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
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (file_descriptor)
    return file_read (file_descriptor->file, buffer, size); 
  return -1;
}

static int
write (int fd, void *buffer, unsigned size)
{
  validate_uaddr (buffer);
  validate_uaddr (buffer + size);

  if (fd == 1) {
    putbuf (buffer, size);
    return size;
  }
  struct file_descriptor *file_descriptor = get_file_descriptor (fd);
  if (file_descriptor)
    return file_write (file_descriptor->file, buffer, size);  
  return 0;
}

static void
seek (int fd, unsigned position)
{
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor)
    file_seek (file_descriptor->file, position);
}

static unsigned
tell (int fd)
{
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor)
    return file_tell (file_descriptor->file);
  return -1;
}

static void
close (int fd)
{
  struct file_descriptor *file_descriptor = get_file_descriptor(fd);
  if (file_descriptor)
  {
    list_remove (&file_descriptor->elem);
    file_close (file_descriptor->file);
  }
}

static int
mmap (int fd, void *addr)
{
  if (!addr || pg_ofs (addr) != 0)
    return -1;

  struct mapid_entry *mapid_entry = create_mapid (fd, addr);
  if (mapid_entry)
  {
    struct file* file = get_file_descriptor (fd)->file;
    int len = file_length (file);

    /* Check that all pages are availible. It is more efficient to check in two
     * loops. Checking and mapping in the same loop would require freeing
     * previously allocated pages if there is a failed check. */
    void *paddr = addr;
    int ofs = 0;
    while (ofs < len)
    {
      if (page_exists (paddr + ofs))
        return -1;
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
    return mapid_entry->mapid;
  }
  return -1;
}

static void
munmap (int mapid)
{
  remove_mapid (mapid);
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;
  thread_current ()->esp = esp;
  validate_uaddr (esp);
  
  uint32_t sys_call_num = *(int *) esp;
  switch (sys_call_num)
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
      f->eax = exec (*(char **)(esp + 4));
      break;
    case SYS_WAIT:
      validate_args (esp, 1);
      f->eax = wait (*(int *)(esp + 4));
      break;
    case SYS_CREATE:
      validate_args (esp, 2);
      f->eax = create (*(char **)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_REMOVE:
      validate_args (esp, 1);
      f->eax = remove (*(char **)(esp + 4));
      break;
    case SYS_OPEN:
      validate_args (esp, 1);
      f->eax = open (*(char **)(esp + 4));
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
      f->eax = write (*(int *)(esp + 4), (void *) *(int *)(esp + 8), 
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
    default:
      thread_exit();
  }
}
