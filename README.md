# StanfordCS140 self learning
Learn some stuff about OS. UCI CS143a intro OS class is kind of bust, so I am taking a self-guided run through [Stanford CS140 Operating Systems](http://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/index.php). The class features four main projects:

* Project 1: Threads
* Project 2: User Programs
* Project 3: Virtual Memory
* Project 4: File systems

I will only be working on the coding, and not the design docs of each project. I am going through lecture notes and may consider looking at exams.

## Setup
Everything is run on docker. Run `sudo make docker-container`. This will mount the local code base into `/pintos` of the container, and changes made from a local text editor will be propagated. Run `sudo make docker-bash` to bash into the container to build, run, and debug pintos.

## Additional info
* When switching between projects, make sure to edit `pintos/utils/Pintos.pm` line 362 and `pintos/utils/pintos` line 259 so they are looking in the right build directory. Otherwise pintos will be unable to run tests for the project.
* A project is considered complete when all the given test cases pass. There is some possibility that test cases from previous projects are broken when working on new projects. Here are the commits where a project was completed:
  * [Project 1 Threads](https://github.com/caojoshua/StanfordCS140/tree/ec0d98c73b93d8ff03eaeffa0d05de29efb6d827). Some of the docker setup might be messed up here.
  * [Project 2 User Programs](https://github.com/caojoshua/StanfordCS140/commit/56ba585e8be42591e8f8ab97b2dd7013bcdf1089)
  * [Project 3 Virtual Memory](https://github.com/caojoshua/StanfordCS140/commit/2ef27ee68171523f5bf70511ecf782c0bea0607c)
* The project is now **complete** with a 100% test pass rate. Virtual memory was originally branched from master, and file system branched from virtual memory, but now everything is merged into master. The old branches will stay alive as stale branches. The course recommended building both project 3 and 4 on top of project 2 to make life easier, but I decided to build project 4 on top of project 3 for completeness. The commits at which each project finished (all testcases for that project pass):
  * [Project 1 Threads](https://github.com/caojoshua/StanfordCS140/commit/ec0d98c73b93d8ff03eaeffa0d05de29efb6d827). Some of the docker setup might be messed up here.
  * [Project 2 User Programs](https://github.com/caojoshua/StanfordCS140/commit/56ba585e8be42591e8f8ab97b2dd7013bcdf1089)
  * [Project 3 Virtual Memory](https://github.com/caojoshua/StanfordCS140/commit/2ef27ee68171523f5bf70511ecf782c0bea0607c)
  * Project 4 File Systems is HEAD on master branch
* See /docs for some implementation notes and final thoughts of the class.
