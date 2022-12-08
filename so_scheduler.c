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
 * different types of states a thread can be in
 */
typedef enum {
	NEW,
	READY,
	RUNNING,
    WAITING,
	TERMINATED
} thread_status;

/*
 * struct defining a thread
 *
 * @status - saves status of thread
 * @priority - saves priority of thread
 * @quatum - saves time quantum remained for thread
 * @device - saves io device that blockes the thread
 * @handler - saves handler of thread
 * @tid - saves tid of thread
 * @sem - semaphore used for syncronization
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
 * struct defining the priority queue used for running threads
 */
typedef struct {
    unsigned int size;
    thread_t **readyThreads;
} pq_t;

/*
 * struct defining the scheduler
 *
 * @init - specifies whether the scheduler was initialized or not
 * @quantum - specifies number time quatum after which a process is preempted
 * @nrTotalThreads - total number of threads in the system
 * @nrWaitingThreads - number of threads waiting for io device
 * @currentThread - current thread running
 * @totalThreads - array containing all threads in system
 * @waitingThreads - array containing all threads waiting for io device
 * @pq - priority que used for threads in READY state
 */
typedef struct {
    unsigned int quantum;
    unsigned int io;
    unsigned int nrTotalThreads;
    unsigned int nrWaitingThreads;
    thread_t *runningThread;
    thread_t **totalThreads;
    pq_t **waitingThreads;
    pq_t *pq;
} scheduler_t;

/*
 * @scheduler - scheduler used for threads
 * @sync - semaphore used for syncronizing threads
 */
scheduler_t scheduler;
//static sem_t mainSem;
int schedulerInit;
int notFirstFork;


/************** helper functions **************/

// adds thread in priority queue of ready threads
void pq_add(pq_t *pq, thread_t *thread) {
    pq->readyThreads[pq->size] = thread;
    pq->size++;
    //printf("pq added thread to ready\n");
}

// returns next thread to run
thread_t *pq_pop(pq_t *pq) {
    // check if there are threads in the ready state
    if (!pq->size) {
        return NULL;
    }

    thread_t *thread = pq->readyThreads[0];
    int max = thread->priority;
    int index = 0;

    // Round Robin: find thread with maximum priority
    for(int i = 1; i < pq->size; i++) {
        thread_t *current = pq->readyThreads[i];
        // printf("current priority = %d\n", current->priority);
        // printf("max priority = %d\n", max);
        if (current->priority > max) {
            thread = current;
            max = current->priority;
            index = i;
        }
    }

    // delete thread from queue
    for (int i = index; i < pq->size; i++) {
        thread_t *nextThread = pq->readyThreads[i + 1];
        pq->readyThreads[i] = nextThread;
    }
    pq->size -= 1;

    //printf("pop\n");
    return thread;
}

thread_t *queue_pop(pq_t *pq) {
    thread_t *thread = pq->readyThreads[0];
    for (int i = 0; i < pq->size; i++) {
        pq->readyThreads[i] = pq->readyThreads[i + 1];
    }
    pq->size--;

    return thread;
}

/*
 * Updates scheduler's running thread
 */
void update_scheduler(thread_t *thread) {
    if (thread == NULL) {
        return;
    }
    printf("start thread\n");
    scheduler.runningThread = thread;

    // start thread
    thread->status = RUNNING;
    //thread->quantum = 0;
    int rc = sem_post(&thread->sem);
    if (rc != 0) {
        perror("sem_post");
        return;
    }
    printf("current scheduler tid: %ld\n", scheduler.runningThread->tid);
}

/*
 * Checks if scheduler needs to change running thread
 */
int check_scheduler() {
    //printf("check scheduler\n");

    // check if there is a thread running
    if (scheduler.runningThread == NULL) {
        //printf("first thread\n");
        update_scheduler(pq_pop(scheduler.pq));
        return 0;
    }

    // check if new thread with higher priority entered the system
    if (scheduler.totalThreads[scheduler.nrTotalThreads - 1]->priority
        > scheduler.runningThread->priority) {
            printf("check higher priority\n");
            // preempt current thread
            scheduler.runningThread->quantum = 0;
            scheduler.runningThread->status = READY;
            pq_add(scheduler.pq, scheduler.runningThread);
            update_scheduler(pq_pop(scheduler.pq));
            return 1;
    }

    // check if current thread finished
    if (scheduler.runningThread->status == TERMINATED) {
        printf("terminated fork\n");
        // printf("queue size: %d\n", scheduler.pq->size);

        // check if there are ready threads
        if (!scheduler.pq->size) {
            return 0;
        }
        update_scheduler(pq_pop(scheduler.pq));
        return 0;
    }

    // check if running thread's time quantum expired
    if (scheduler.runningThread->quantum == scheduler.quantum) {
        printf("quantum expired\n");
        // reset thread status
        thread_t *thread = scheduler.runningThread;
        thread->status = READY;
        thread->quantum = 0;

        // check if other threads are in READY state
        if (!scheduler.pq->size) {
            //printf("same thread\n");
            update_scheduler(thread);
            return 1;
        }
        // place thread back on queue and take next thread scheduled
        thread_t *nextThread = pq_pop(scheduler.pq);
        pq_add(scheduler.pq, thread);
        //printf("thread id before: %ld\n", scheduler.runningThread->tid);
        update_scheduler(nextThread);
        //printf("2. thread id after: %ld\n", scheduler.runningThread->tid);
        //sem_wait(&thread->sem);

        return 1;
    }

    // check if running thread is waiting on io device
    if (scheduler.runningThread->status == WAITING) {
        //printf("thread waiting\n");
        // reset thread status and place in waiting queue
        thread_t *thread = scheduler.runningThread;
        thread->quantum = 0;
        // add in waiting queue
        pq_add(scheduler.waitingThreads[thread->io], thread);

        // schedule next thread from queue
        update_scheduler(pq_pop(scheduler.pq));
        return 0;
    }

    // // check if new thread with higher priority entered the system
    // if (scheduler.totalThreads[scheduler.nrTotalThreads - 1]->priority
    //     > scheduler.runningThread->priority) {
    //         printf("check higher priority\n");
    //         // preempt current thread
    //         scheduler.runningThread->quantum = 0;
    //         scheduler.runningThread->status = READY;
    //         pq_add(scheduler.runningThread);
    //         update_scheduler(pq_pop());
    //         return 1;
    //     }

    //printf("scheduler none\n");

    return 0;
}

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

void free_threads() {
    for (int i = 0; i < scheduler.nrTotalThreads; i++) {
        thread_t *thread = scheduler.totalThreads[i];
        free(thread);
    }
}

void free_waiting_threads() {
    for (int i = 0; i < SO_MAX_NUM_EVENTS; i++) {
        free(scheduler.waitingThreads[i]->readyThreads);
        free(scheduler.waitingThreads[i]);
    }
    free(scheduler.waitingThreads);
}

void *start_routine(void *arg) {
    thread_t *thread = (thread_t *)arg;

    int rc = sem_wait(&thread->sem);
    if (rc != 0) {
        perror("sem_post");
        return NULL;
    }

    // run handler
    printf("start thread handler %ld\n", get_tid());
    thread->handler(thread->priority);
    printf("ended thread handler %ld\n", get_tid());
    
    // end thread
    thread->status = TERMINATED;
    printf("thread terminated\n");
    check_scheduler();
    //printf("end\n");

    return NULL;
}


/************** scheduler functions **************/

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

    scheduler.nrTotalThreads = 0;
    scheduler.nrWaitingThreads = 0;
    scheduler.totalThreads = malloc(MAX_CAPACITY * sizeof(thread_t*));

    // initialize queues for waiting threads
    scheduler.waitingThreads = malloc(SO_MAX_NUM_EVENTS * sizeof(pq_t*));
    for (int i = 0; i < SO_MAX_NUM_EVENTS; i++) {
        scheduler.waitingThreads[i] = malloc(sizeof(pq_t));
        scheduler.waitingThreads[i]->readyThreads =
         malloc(MAX_CAPACITY * sizeof(thread_t*));
    }

    // initialize queue for ready threads
    scheduler.pq = malloc(sizeof(pq_t));
    scheduler.pq->size = 0;
    (scheduler.pq)->readyThreads = malloc(MAX_CAPACITY * sizeof(thread_t*));
    scheduler.runningThread = NULL;

    return 0;
}

tid_t so_fork(so_handler *func, unsigned int priority) {
    printf("entered fork\n");
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
    //newThread->status = READY;
    pq_add(scheduler.pq, newThread);

    ////////////// TEST ////////////////////

    // check if current thread needs to be preempted
    thread_t *current = scheduler.runningThread;
    if (check_scheduler()) {
        printf("fork face check\n");
        int rc = sem_wait(&current->sem);
        if (rc != 0) {
            perror("sem_wait");
        }
    } else if (notFirstFork) {
        printf("fork add quantum: %d\n", scheduler.runningThread->quantum);
        scheduler.runningThread->quantum++;
        printf("quantum: %d\n", scheduler.runningThread->quantum);
    }
    notFirstFork = 1;

    /////////////// TEST ///////////////////

    //so_exec();
    printf("ended fork\n");
    return newThread->tid;
}

int so_wait(unsigned int io) {
    // check if io is valid
    if (io < 0 || io > scheduler.io) {
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

int so_signal(unsigned int io) {
    // check if io device is valid
    if (io < 0 || io > scheduler.io) {
        return -1;
    }

    // // wake up threads waiting on io device given
    int nrWokeThreads = 0;
    // int updateScheduler = 0;
    // thread_t *nextThread;
    // for (int i = 0; i < scheduler.nrWaitingThreads; i++) {
    //     if (scheduler.waitingThreads[i]->io == io) {
    //         nrWokeThreads++;
    //         // reset thread status and add  thred in queue
    //         thread_t *thread = scheduler.waitingThreads[i];
    //         thread->status = READY;

    //         // check if thread has greater priority
    //         if (thread->priority > scheduler.runningThread->priority
    //             || (updateScheduler && thread->priority > nextThread->priority)) {
    //             if (updateScheduler) {
    //                 pq_add(nextThread);
    //             }
    //             updateScheduler++;
    //             nextThread = thread;
    //         } else {
    //             // add woken thread in queue
    //             pq_add(thread);
    //         }
    //     }
    // }

    // // check if task with greater priority was woken and update scheduler
    // if (updateScheduler) {
    //     thread_t *currentThread = scheduler.runningThread;
    //     currentThread->status = READY;
    //     currentThread->quantum = 0;
    //     pq_add(currentThread);
    //     update_scheduler(nextThread);
    // } else {
    //     so_exec();
    // }

    return nrWokeThreads;
}

void so_exec(void) {
    // check if there is a thread started
    printf("entered exec\n");
    if (scheduler.runningThread == NULL) {
        check_scheduler();
        return;
    }
    // increase number of operations done by thread

    //printf("quantum: %d/%d\n", scheduler.runningThread->quantum, scheduler.quantum);
    scheduler.runningThread->quantum++;
    thread_t *thread = scheduler.runningThread;
    printf("quantum: %d/%d\n", scheduler.runningThread->quantum, scheduler.quantum);


    // printf("exec: tid %ld\n", scheduler.runningThread->tid);
    // printf("quatum: %d / %d\n", scheduler.runningThread->quantum, scheduler.quantum);

    // check if time is up

    //printf("queue size: %d\n", scheduler.pq->size);
    int preempted = check_scheduler();
    if (preempted) {
        // printf("preempted\n");
        // printf("queue size: %d\n", scheduler.pq->size);
        int rc = sem_wait(&thread->sem);
        //printf("da\n");
        if (rc != 0) {
            perror("sem_wait");
            return;
        }
    }

    //scheduler.runningThread->quantum++;
    printf("ended exec\n\n");
}

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
    scheduler.nrWaitingThreads = 0;
    scheduler.runningThread = NULL;

    // free waiting threads queues
    free_waiting_threads();
    // free queue of ready threads
    free((scheduler.pq)->readyThreads);
    free(scheduler.pq);

    return;
}
