Name: Mîrza Ana-Maria

Group: 321CA


# Thread Scheduler

Organization
-
The thread scheduler uses POSIX functions such as <span style="color:lightblue">*pthread_create()*</span>, and also <span style="color:lightblue">*malloc()*</span> and <span style="color:lightblue">*free()*</span> in order to simulate a preemptiv process scheduler for a single processor system. The scheduler controls the thread execution in user-space using a Round-Robin priority scheduling algorithm. The thread scheduler's dynamic library exports the following functions used by threads to be scheduled:

* INIT - initializes scheduler structure
* FORK - starts new thread
* EXEC - simulates execution of an instruction
* WAIT - waits for an I/O operation
* SIGNAL - signals threads waiting for an I/O operation
* END - destroys scheduler and frees memory occupied by its structures

Also, the scheduler's Round Robin algorithm is implemented using a priority queue that uses the following functions:

* ADD - adds a thread at the end of a priority queue
* POP - returns thread with maximum priority and deletes it from queue
* PEEK - returns thread with maximum priority without deleting it from queue

***Improvements:***
* An improvemet that could be made is using linked lists instead of arrays so that the memory could be used more efficiently
* Another improvement could be using a separate queue for each priority, to make the schedulig process more efficient.

Implementation
-
The scheduler is initialized only once and begins to schedule threads as they are being created. Each thread has a priority assigned, so that the scheduler always schedules the thread with highest priority and a time quantum on the processor. For each instruction, the thread consumes a unit of time and is preempted after consuming its time quantum. For syncronization, the scheduler uses a semaphore for each thread. A thread is preempted:

* when its time quantum expires, if there is a thread with higher or equal priority than his, otherwise the running thread is rescheduled
* if a new thread created from fork has a higher priority than the running thread
* if a thread signaled has a higher priority than the running thread
* if it waits on I/O operation
* if it has no operations to execute

The scheduler uses the thread's state to determine what to do with a thread. Initially, a newly created thread has the state NEW, then is placed in the priority queue for ready threads and takes the state READY, as it waits to be scheduled. When the scheduler schedules a thread, the thread enters the RUNNING state, from witch it can either:
* be interupted by higher priority thread forked or signaled and returns to READY state
* exit the scheduler as it finished its job and enter TERMINATED state
* end up in WAITING state for an I/O operation, where the scheduler places the thread in a queue for that specific I/O device

A corner case discovered is that a thread can not call wait operation if there are no ready threads available. The operation will return an error.


At the end, the scheduler waits for all threads to finish and frees the memory.

How to compile and run?
-
* create < insert name >.c file in which call (in main) *so_init( time_quantum, io )* and *so_fork( func, priority )* functions first, to initialize scheduler and create first thread in the system. Define handler functions used by fork, in which call whatever operation to be executed by thread. Last, call *sched_yield()*, then *so_end()* to exit the program correctly.
* give the following command in linux bash to create executable
```
gcc -pthread <name>.c so_scheduler.c so_scheduler.h
```
* give following command to run executable and simulate scheduler
```
./a.out
```

Bibliography
-
* https://ocw.cs.pub.ro/courses/so/cursuri/curs-04
* https://ocw.cs.pub.ro/courses/so/cursuri/curs-08