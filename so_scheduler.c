#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../util/so_scheduler.h"

#define MAX_CAPACITY 1000

static inline tid_t get_tid(void)
{
	return pthread_self();
}

/*
 * States a thread can be in
 */
typedef enum {
	NEW,
	READY,
	RUNNING,
    WAITING,
	TERMINATED
} thread_status;

/*
 * Struct defining a thread
 *
 * @status saves status of thread
 * @priority saves priority of thread
 * @quatum saves time quantum remained for thread
 * @device saves io device that blockes the thread
 * @handler saves handler of thread
 * @tid saves tid of thread
 * @sem semaphore used for syncronization
 */
typedef struct {
    thread_status status;
    unsigned int priority;
    unsigned int quantum;
    unsigned int io;
    so_handler *handler;
    pthread_t tid;
    sem_t sem;
} thread_t;

/*
 * Struct defining the priority queue used for running threads
 *
 * @size stores size of thread priority queue 
 * @threads is priority queue of threads
 */
typedef struct {
    unsigned int size;
    thread_t **threads;
} pq_t;

/*
 * Struct defining the scheduler
 *
 * @init specifies whether the scheduler was initialized or not
 * @quantum specifies number time quatum after which a process is preempted
 * @nrTotalThreads keeps score of number of threads in the system
 * @currentThread stores current thread running
 * @totalThreads is array containing all threads in system
 * @waitingPq is array of queues for threads waiting for io device
 * @readyPq is priority queue used for threads in READY state
 */
typedef struct {
    unsigned int quantum;
    unsigned int io;
    unsigned int nrTotalThreads;
    thread_t *runningThread;
    thread_t **totalThreads;
    pq_t **waitingPq;
    pq_t *readyPq;
} scheduler_t;

/*
 * @scheduler used for scheduling threads
 * @notFirstFork determines if fork creates first thread in system
 *               or another thread
 */
scheduler_t scheduler;
int schedulerInit;
int notFirstFork;



/******************* priority queue functions *******************/


/*
 * Adds thread in priority queue of threads
 */
void pq_add(pq_t *pq, thread_t *thread) {
    pq->threads[pq->size] = thread;
    pq->size++;
}

/*
 * Returns thread with highest priority in queue
 * and deletes it from queue
 */
thread_t *pq_pop(pq_t *pq) {
    // check if there are threads in the queue
    if (!pq->size) {
        return NULL;
    }

    thread_t *thread = pq->threads[0];
    int max = thread->priority;
    int index = 0;

    // Round Robin: find thread with maximum priority
    for(int i = 1; i < pq->size; i++) {
        thread_t *current = pq->threads[i];
        if (current->priority > max) {
            thread = current;
            max = current->priority;
            index = i;
        }
    }

    // delete thread from queue
    for (int i = index; i < pq->size; i++) {
        thread_t *nextThread = pq->threads[i + 1];
        pq->threads[i] = nextThread;
    }
    pq->size -= 1;

    return thread;
}

/* 
 * Returns next thread in priority queue without
 * removing it from queue
 */
thread_t *pq_peek(pq_t *pq) {
    // check if there are threads in the queue
    if (!pq->size) {
        return NULL;
    }

    thread_t *thread = pq->threads[0];
    int max = thread->priority;
    // find first thread with maximum priority
    for(int i = 1; i < pq->size; i++) {
        thread_t *current = pq->threads[i];
        if (current->priority > max) {
            thread = current;
            max = current->priority;
        }
    }

    return thread;
}



/******************* helper functions *******************/


/*
 * Updates scheduler's running thread
 */
void update_scheduler(thread_t *thread) {
    if (thread == NULL) {
        return;
    }

    // start thread and reset time quantum
    scheduler.runningThread = thread;
    thread->status = RUNNING;
    thread->quantum = 0;
    int rc = sem_post(&scheduler.runningThread->sem);
    if (rc != 0) {
        perror("sem_post");
        return;
    }
}

/*
 * Checks if scheduler needs to change running thread
 */
int check_scheduler() {
    // check if running thread is waiting on io device
    if (scheduler.runningThread->status == WAITING) {
        // reset thread status and place in waiting queue
        thread_t *thread = scheduler.runningThread;
        pq_add(scheduler.waitingPq[thread->io], thread);

        // schedule next thread from queue
        update_scheduler(pq_pop(scheduler.readyPq));
        return 0;
    }

    // check if thread is terminated
    if (scheduler.runningThread->status == TERMINATED) {
        if (!scheduler.readyPq->size)
            return 0;
        update_scheduler(pq_pop(scheduler.readyPq));
        return 0;
    }

    // check if quantum expired
    if (scheduler.runningThread->quantum == scheduler.quantum) {
        // reset thread status
        thread_t *thread = scheduler.runningThread;
        thread->status = READY;

        // check if other threads are in READY state
        if (!scheduler.readyPq->size) {
            update_scheduler(thread);
            return 1;
        }
        // place current thread back on queue
        pq_add(scheduler.readyPq, thread);
        // scheduler thread with highest priority
        update_scheduler(pq_pop(scheduler.readyPq));

        return 1;
    }

    return 0;
}

/*
 * Chcks if any thread signaled has higher priority than
 * current thread running
 */
int check_signaled_threads() {
    // check if higher priority thread was signaled
    if (pq_peek(scheduler.readyPq)->priority > scheduler.runningThread->priority) {
        // scheduler higher priority thread
        scheduler.runningThread->status = READY;
        pq_add(scheduler.readyPq, scheduler.runningThread);
        update_scheduler(pq_pop(scheduler.readyPq));
        return 1;
    }
    return 0;
}

/*
 * Chcks if thread forked has higher priority and
 * if there is a thread scheduled in the system
 */
int check_new_thread() {
    // check if there is a thread running
    if (scheduler.runningThread == NULL) {
        update_scheduler(pq_pop(scheduler.readyPq));
        return 0;
    }

    // check if new thread with higher priority entered the system
    if (scheduler.totalThreads[scheduler.nrTotalThreads - 1]->priority
        > scheduler.runningThread->priority) {
            // preempt current thread
            scheduler.runningThread->status = READY;
            pq_add(scheduler.readyPq, scheduler.runningThread);
            update_scheduler(pq_pop(scheduler.readyPq));
            return 1;
    }

    return 0;
}

/*
 * Allocates memory for a thread structure and initializes it 
 */
thread_t *load_thread(so_handler *handler, unsigned int priority) {
    thread_t *thread = malloc(sizeof(thread_t));
    thread->handler = handler;
    thread->priority = priority;
    thread->quantum = 0;
    thread->status = NEW;
    thread->tid = INVALID_TID;

    // create semaphore for new thread
    int rc = sem_init(&thread->sem, 0, 0);
    if (rc != 0) {
        perror("sem_init");
    }

    return thread;
}

/*
 * Frees all thread structures
 */
void free_threads() {
    for (int i = 0; i < scheduler.nrTotalThreads; i++) {
        thread_t *thread = scheduler.totalThreads[i];
        free(thread);
    }
}

/*
 * Frees memory of queues for waiting threads
 */
void free_waiting_threads() {
    for (int i = 0; i < SO_MAX_NUM_EVENTS; i++) {
        free(scheduler.waitingPq[i]->threads);
        free(scheduler.waitingPq[i]);
    }
    free(scheduler.waitingPq);
}

/*
 * Routine function performed by all threads in system
 */
void *start_routine(void *arg) {
    // wait for thread to be scheduled
    thread_t *thread = (thread_t *)arg;
    int rc = sem_wait(&thread->sem);
    if (rc != 0) {
        perror("sem_post");
        return NULL;
    }

    // run handler
    thread->handler(thread->priority);
    // end thread
    thread->status = TERMINATED;
    // update scheduler with next thread scheduled if exists
    if (!scheduler.readyPq->size) {
        return NULL;
    }
    update_scheduler(pq_pop(scheduler.readyPq));

    return NULL;
}



/******************* scheduler functions *******************/

/*
 * Initializes the scheduler. Returns 0 if successfully initialized
 * and -1 in case of error.
 *
 * @time_quantum defines time quantum after which the thread is preempted
 * @io defines maximum number of io events in the system
 */
int so_init(unsigned int time_quantum, unsigned int io) {
    // check if arguments are valid and if scheduler is already initialized
    if (time_quantum == 0 || io > SO_MAX_NUM_EVENTS
        || schedulerInit) {
        return -1;
    }

    // initialize scheduler
    scheduler.quantum = time_quantum;
    scheduler.io = io;
    schedulerInit = 1;

    // initialize array containing all threads in system
    scheduler.nrTotalThreads = 0;
    scheduler.totalThreads = malloc(MAX_CAPACITY * sizeof(thread_t*));

    // initialize queues for waiting threads
    scheduler.waitingPq = malloc(SO_MAX_NUM_EVENTS * sizeof(pq_t*));
    for (int i = 0; i < SO_MAX_NUM_EVENTS; i++) {
        scheduler.waitingPq[i] = malloc(sizeof(pq_t));
        scheduler.waitingPq[i]->size = 0;
        scheduler.waitingPq[i]->threads =
         malloc(MAX_CAPACITY * sizeof(thread_t*));
    }

    // initialize queue for ready threads
    scheduler.readyPq = malloc(sizeof(pq_t));
    scheduler.readyPq->size = 0;
    (scheduler.readyPq)->threads = malloc(MAX_CAPACITY * sizeof(thread_t*));
    scheduler.runningThread = NULL;

    return 0;
}

/*
 * Creates new thread and adds it in scheduler.
 * Returns tid of new thread created.
 *
 * @func stores handler fucion for new thread forked
 * @priority defines priority of new thread forked
 */
tid_t so_fork(so_handler *func, unsigned int priority) {
    // check for errors
    if (func == 0 || priority > SO_MAX_PRIO) {
        return INVALID_TID;
    }

    // initialize thread struct
    thread_t *newThread = load_thread(func, priority);

    // create new thread
    if (pthread_create(&newThread->tid, NULL, &start_routine, (void *)newThread)) {
        perror("pthread_create");
        return INVALID_TID;
    }

    // add new thread in array of threads and in queue
    scheduler.totalThreads[scheduler.nrTotalThreads++] = newThread;
    newThread->status = READY;
    pq_add(scheduler.readyPq, newThread);

    // check if current thread needs to be preempted
    thread_t *current = scheduler.runningThread;
    if (check_new_thread()) {
        int rc = sem_wait(&current->sem);
        if (rc != 0) {
            perror("sem_wait");
        }
    } else if (notFirstFork) {
        // add time quantum if the fork was not for first thread
        so_exec();
    }
    notFirstFork = 1;

    return newThread->tid;
}

/*
 * Puts current thread on wait for io device and starts next thread
 * in priority queue. Returns 0 if io device existst and -1 
 * for invalid io.
 * 
 * @io device for which current thread waits
 */
int so_wait(unsigned int io) {
    // check if io is valid
    if (io < 0 || io > scheduler.io || scheduler.readyPq->size == 0) {
        return -1;
    }

    // update thread status and update scheduler
    scheduler.runningThread->status = WAITING;
    scheduler.runningThread->io = io;
    thread_t *thread = scheduler.runningThread;
    check_scheduler();

    // blocks running thread
    int rc = sem_wait(&thread->sem);
    if (rc != 0) {
        perror("sem_wait");
        return -1;
    }

    return 0;
}


/*
 * Sends signal to all threads waiting for io device. Returns
 * number of threads signales or -1 in case of error.
 * 
 * @io device signaled
 */
int so_signal(unsigned int io) {
    // check if io device is valid
    if (io < 0 || io >= scheduler.io) {
        return -1;
    }

    // wake up threads waiting on io device
    int nrWokeThreads = 0;
    pq_t *ioQueue = scheduler.waitingPq[io];
    while (ioQueue->size) {
        thread_t *thread = pq_pop(ioQueue);
        thread->status = READY;
        // add in ready queue
        pq_add(scheduler.readyPq, thread);
        nrWokeThreads++;
    }

    // check if current thread signaled higher priority thread
    if (nrWokeThreads) {
        thread_t *currentThread = scheduler.runningThread;
        int preempted = check_signaled_threads();
        if (preempted) {
            int rc = sem_wait(&currentThread->sem);
            if (rc != 0) {
                perror("sem_wait");
                return -1;
            }
        } else {
            goto exec;
        }
    }
exec:
    so_exec();
    return nrWokeThreads;
}

/*
 * Simulates a generic instruction. Uses time on cpu.
 */
void so_exec(void) {
    // increase number of operations done by thread
    scheduler.runningThread->quantum++;
    thread_t *thread = scheduler.runningThread;

    // check if time is up
    int preempted = check_scheduler();
    if (preempted) {
        int rc = sem_wait(&thread->sem);
        if (rc != 0) {
            perror("sem_wait");
            return;
        }
    }
}

/*
 * Frees scheduler resources and waits for all threads to finish 
 */
void so_end(void) {
    // check if scheduler was created
    if (!schedulerInit) {
        return;
    }

    // wait for all threads
    for (int i = 0; i < scheduler.nrTotalThreads; i++) {
        if(pthread_join(scheduler.totalThreads[i]->tid, NULL)) {
            perror("pthread_join");
        }
    }

    // destroy semaphores
    for (int i = 0; i < scheduler.nrTotalThreads; i++) {
        int rc = sem_destroy(&scheduler.totalThreads[i]->sem);
        if (rc != 0) {
            perror("sem_destroy");
            return;
        }
    }

    // free memory occupied by threads
    free_threads();
    free(scheduler.totalThreads);

    // reset scheduler
    schedulerInit = 0;
    notFirstFork = 0;
    scheduler.nrTotalThreads = 0;
    scheduler.runningThread = NULL;

    // free waiting threads queues
    free_waiting_threads();
    // free queue of ready threads
    free((scheduler.readyPq)->threads);
    free(scheduler.readyPq);

    return;
}
