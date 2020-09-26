#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
halt (void)
{
  shutdown_power_off();
}

static void
exit (int status)
{
  printf("%s: exit(%d)\n", thread_current ()->file_name, status);
}

static bool
create (const char *file, unsigned initial_size)
{
  return filesys_create (file, initial_size); 
}

static bool
remove (const char *file)
{
  return filesys_remove (file);
}

static int
open (const char *file)
{
  return file_open;
}

static int
write (int fd, const void *buffer, unsigned length)
{
  if (fd == 1) {
    putbuf (buffer, length);
    return length;
  }
  // TODO: write to file
  return length;
}

static bool
is_valid_user_addr(const void *uaddr)
{
  return uaddr && is_user_vaddr(uaddr) &&
    pagedir_get_page(thread_current()->pagedir, uaddr);
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;

  if (!is_valid_user_addr(esp)) {
    printf("stack pointer not a valid user address!\n");
    return;
  }
  
  uint32_t sys_call_num = *(int *) esp;
  switch (sys_call_num)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      {
      int status = *(int *)(esp + 4);
      exit(status);
      f->eax = status;
      thread_exit ();
      break;
      }
    case SYS_CREATE:
      f->eax = create (*(const char **)(esp + 4), *(int *)(esp + 8));
      break;
    case SYS_REMOVE:
      f->eax = remove (*(const char **)(esp + 4));
      break;
    case SYS_OPEN:
      /* f->eax = open (*(const char **)(esp + 4)); */
      break;
    case SYS_WRITE:
      f->eax = write (*(int *)(esp + 4), (void *) *(int *)(esp + 8), 
          *(unsigned *)(esp + 12));
      break;
    default:
      thread_exit();
  }
}
