## Final Thoughts

### Project 1 Threads
Really interesting to see implementation of threads other than simulations we did in UCI. Was really hard at first getting used to developing within a kernel, but once I got the hang of it it was no big deal. Seeing an implementation of context switch was also a good learning experience for something that was previously a black box.

### Project 2 User Programs
This project gave me a great idea of how user programs interact with the kernel API. I realized that it took until working on this project how one might implement print. I think this project could have been pretty easy if it weren't for dealing with the issue with registers being incorrectly pushed on the stack in gcc assembly, described in the main readme.

### Project 3 Virtual Memory
Hands down the hardest project. When you mess up your vm implementation, your eip might end up in the randomest places. Dealing with synchronization in this project was also a huge challenge. That being said, this project was way more inciteful into vm than all the simulations we did at UCI. I have a better understanding of how a program is loaded into memory and is prepared to be executed. I also better understand the advantages of virtual memory.

### Project 4 File Systems
Inode extensibility was a huge pain in the butt to implement to make it work for large files. I reimplemented it multiple times before I was somewhat satisifed. Otherwise, caching and subdirectories weren't much of a fuss. Pretty cool to see how files could be stored in a drive, which I never thought of before. To be honest, this project was the least interesting, since it feels the most disconnected from the core kernel.

### Overall
Going through Stanford CS140 was a better learning experience than 95% of classes I took at UCI. The class involves programming in a real code base and real kernel, neither of which I experienced at UCI. Unfortunately, this class took way longer than I would have liked because 1. I got sidetracked by other projects and started working 2 months into this class. and 2. This class is damn hard. I had no one to get help from, dealt with weird depdency issues that wouldn't have existed on the Stanford school machines, and did a 3-person project by myself.

Overall, my greatest satisfaction is just having a better understanding of how software works. I better understand how my programs are loaded into memory and executed. Many common library functions, such as printing and threading, are less of a black box now. Self-learning this course has been a great experience and I would recommend it anyone wanting to better understand operating systems.
