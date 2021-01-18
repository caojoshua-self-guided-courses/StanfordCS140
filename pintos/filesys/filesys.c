#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"

struct directory;

/* Partition that contains the file system. */
struct block *fs_device;

static struct inode * filesys_open_internal (const char *name);
static void do_format (void);
static bool parse_name (const char *name, struct dir **dir_, char *name_);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  write_cache_to_disk ();
  free_map_close ();
}

/* Creates a file or directory named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   the subdirectory to the path does not exist,
   or if internal memory allocation fails. */
bool
filesys_create (const char *full_name, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  char name[NAME_MAX + 1];

  if (!parse_name (full_name, &dir, name))
    return false;

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, name, inode_sector, is_dir));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the inode with the given NAME.
   Returns the new inode if successful or a null pointer
   otherwise.
   Fails if no inode named NAME exists,
   or if an internal memory allocation fails. */
static struct inode *
filesys_open_internal (const char *full_name)
{
  struct dir *dir = NULL;
  char name[NAME_MAX + 1];

  if (!parse_name (full_name, &dir, name))
    return NULL;

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return inode;
}

/* Opens the file with the given NAME. Return NULL if unsuccessful. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = filesys_open_internal (name);
  if (inode && !inode_is_dir (inode))
    return file_open (inode);
  return NULL;
}

struct dir *
filesys_open_dir (const char *name)
{
  struct inode *inode = filesys_open_internal (name);
  if (inode && inode_is_dir (inode))
    return dir_open (inode);
  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *full_name) 
{
  struct dir *dir = NULL;
  char name[NAME_MAX + 1];

  if (!parse_name (full_name, &dir, name))
    return false;

  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create_root ())
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Parses name to store the directory in dir_ and the last file name in name_.
 * Returns true if successful.
 *
 * For example, if given name = "/a/b/c", dir_ will be set to dir /a/b, and
 * name_ will be set to "c". */
static bool
/* parse_name (const char *name, struct dir **dir_, char name_[NAME_MAX + 1]) */
parse_name (const char *name, struct dir **dir_, char *name_)
{
  /* Copy name to ensure const. */
  size_t len = strlen (name) + 1;
  char *name_cpy = malloc (len);
  strlcpy (name_cpy, name, len);

  struct dir *dir;

  char *save_ptr, *next;
  char *token = strtok_r (name_cpy, "/", &save_ptr);

  /* Special case in which we start in the root dir. */
  if (*name_cpy == '/')
  {
    dir = dir_open_root();
    /* If token is null, file is just regex /+, or the root dir. */
    if (!token)
    {
      *dir_ = dir;
      strlcpy (name_, CURRENT_DIR, strlen(CURRENT_DIR) + 1);
      return true;
    }
    ++name_cpy;
  }
  else
  {
    /* If null token, file name is invalid. */
    if (!token)
      return false;
    /* Open the thread's current directory. */
    else
    {
      dir = dir_open_current ();
      if (!dir)
        return false;
    }
  }

  /* Iterate through subdirectories. */
  while (true)
  {
    /* If next token is NULL, break out of the loop. */
    next = strtok_r (NULL, "/", &save_ptr);
    if (!next)
      break;

    struct inode *inode;

    /* If the next token does not exist, return error. */
    if (!dir_lookup (dir, token, &inode))
    {
      dir_close (dir);
      return false;
    }

    dir_close (dir);

    /* Move into the next subdirectory. */
    if (inode && inode_is_dir (inode))
      dir = dir_open (inode);
    /* If the next subdirectory isn't a directory, return an error. */
    else
      return false;

    token = next;
  }

  size_t token_len = strlen (token) + 1;
  if (token_len > NAME_MAX + 1)
  {
    dir_close (dir);
    return false;
  }

  *dir_ = dir;
  strlcpy (name_, token, token_len);
  memcpy (name_, token, token_len);
  return true;
}
