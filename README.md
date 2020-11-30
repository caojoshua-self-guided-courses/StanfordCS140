# StanfordCS140 self learning
Learn some stuff about OS. UCI CS143a intro OS class is kind of bust. [Link to class website](http://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/index.php). The class features four main projects: threads, user programs, virtual memory, and file systems. I will only be working on the coding, and not the design docs of each project. I am going through lecture notes and may consider looking at exams.

## Setup
Everything is run on docker. Run `sudo make docker-container`. This will mount the local code base into `/pintos` of the container, and changes made from a local text editor will be propagated. Run `sudo make docker-bash` to bash into the container to build, run, and debug pintos.

## Additional info
* When switching between projects, make sure to edit `pintos/utils/Pintos.pm` line 362 and `pintos/utils/pintos` line 259 so they are looking in the right build directory. Otherwise pintos will be unable to run tests for the project.
* A project is considered complete when all the given test cases pass. There is some possibility that test cases from previous projects are broken when working on new projects. Here are the commits where a project was completed:
  * [Project 1 Threads](https://github.com/caojoshua/StanfordCS140/tree/ec0d98c73b93d8ff03eaeffa0d05de29efb6d827). Some of the docker setup might be messed up here.
  * [Project 2 User Programs](https://github.com/caojoshua/StanfordCS140/tree/cce3d0c15f86a31a0621c84ddfa0847799169517)
  * [Project 3 Virtual Memory](https://github.com/caojoshua/StanfordCS140/commit/2ef27ee68171523f5bf70511ecf782c0bea0607c)

## Random Implementation Notes
Adding some random tidbits here because I'm too lazy to create design docs.

### Project 2 User Programs
* There was a bug in the assembly in `pintos/lib/user/syscall.c` that incorrectly pushed parameters onto the stack. This took FOREVER to fix, and is the only code that was not intended to be touched by the instructors but was changed anyway. See the comment in the top of the file for the fix. See [GCC inline assembly constraint docs](https://www.felixcloutier.com/documents/gcc-asm.html#constraints) to better understand the problem. UPDATE: apparently [some has found this bug before](https://github.com/saurvs/pintos/commit/ea904493370d2a752855cc93aad1e27b009dd917)
* For simplicity, I am treating TID and PID the same. This is fine for the parent thread of a process, but this can break in multi-threaded processes eg. child thread of a process should still have the same PID, but my implementation uses its TID, which will be different from the parent TID. Pintos does not supported multi-threaded processes, so the implementation works for this project.

### Project 3 Virtual memory
On branch virtual-memory
* There is a buttload of disk access syscall synchronization I added to pass parallel testcases. Maybe not all is needed, but keeping all because its safe.
* In this implementation of virtual memory, user pages are allocated through page_alloc, which calls f(rame)alloc, which calls sw(ap)alloc if needed. falloc calls palloc to get a free kernel page, but the chain feels unintuitive. I am not a huge fan in Pintos having de-coupling between hardware page table, supplemental page table, and page allocations. Would be interesting to see real implementations of page/frame/swap. 

