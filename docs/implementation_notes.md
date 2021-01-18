## Random Implementation Notes
Adding some random tidbits here because I'm too lazy to create design docs.

### Project 1 Threads
* Nothing too interesting about this one. Not as much room for design decisions.

### Project 2 User Programs
* There was a bug in the assembly in `pintos/lib/user/syscall.c` that incorrectly pushed parameters onto the stack. This took FOREVER to fix, and is the only code that was not intended to be touched by the instructors but was changed anyway. See the comment in the top of the file for the fix. See [GCC inline assembly constraint docs](https://www.felixcloutier.com/documents/gcc-asm.html#constraints) to better understand the problem. UPDATE: apparently [some has found this bug before](https://github.com/saurvs/pintos/commit/ea904493370d2a752855cc93aad1e27b009dd917)
* For simplicity, I am treating TID and PID the same. This is fine for the parent thread of a process, but this can break in multi-threaded processes eg. child thread of a process should still have the same PID, but my implementation uses its TID, which will be different from the parent TID. Pintos does not supported multi-threaded processes, so the implementation works for this project.

### Project 3 Virtual Memory
* There is a buttload of disk access syscall synchronization I added to pass parallel testcases. Maybe not all is needed, but keeping all because its safe.
* In this implementation of virtual memory, user pages are allocated through page_alloc, which calls f(rame)alloc, which calls sw(ap)alloc if needed. falloc calls palloc to get a free kernel page, but the chain feels unintuitive. I am not a huge fan in Pintos having de-coupling between hardware page table, supplemental page table, and page allocations. Would be interesting to see real implementations of page/frame/swap. 

### Project 4 File Systems
* The inode implementation involves 12 direct blocks, 1 indirect block, and 1 doubly indirect block. It places a limit on file size, but the limit is much greater than the max size of the Pintos file system (8mb).
* The subdirectory vs ordinary file is a little messy. Process file descriptor structs have a union of struct dir and struct file, and always does a type check to determine whether to call the file or dir equivalent functions. Could have not seperated file and dir functions (eg. there are separate file_create and dir_create) and do type checking in each function, but thats way too much rewriting of skeleton code. Maybe this would have been nicer in an OOP language having file and dir inherit from a base class.


### Overall
* Some areas have too much or too little synchornization, but its good enough to pass testcases. There is especially way more synchronization than necessary in file systems, and does not allow multiple processes to access the file system at the same time.
* Some areas that should guarentee no page faults don't really guarentee it.
