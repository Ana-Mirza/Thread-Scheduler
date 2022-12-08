#include "../util/so_scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>

#define SO_MAX_UNITS 32

#define get_rand(min, max) ((rand() % (max - min)) + min)

static inline tid_t get_tid(void)
{
	return pthread_self();
}

static inline int equal_tids(tid_t t1, tid_t t2)
{
	return pthread_equal(t1, t2);
}

static inline int this_tid(tid_t t)
{
	return pthread_equal(t, get_tid());
}



static tid_t test_exec_last_tid;
static tid_t test_tid_13_1;
static tid_t test_tid_13_2;
static unsigned int test_exec_status;

#define SO_TEST_AND_SET(expect_id, new_id) \
	do { \
		if (equal_tids((expect_id), INVALID_TID) || \
				equal_tids((new_id), INVALID_TID)) \
			printf("invalid task id\n"); \
		if (!equal_tids(test_exec_last_tid, (expect_id))) \
			printf("invalid tasks order\n"); \
		test_exec_last_tid = (new_id); \
	} while (0)

static void test_sched_handler_13_2(unsigned int dummy)
{
	SO_TEST_AND_SET(test_tid_13_1, test_tid_13_2);
    printf("test2_1: %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_2, test_tid_13_2);
    printf("test2_2: %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_1, test_tid_13_2);
    printf("test2_3: %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_2, test_tid_13_2);
    printf("test2_4: %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_1, test_tid_13_2);
    printf("test2_5: %ld\n", get_tid());
	so_exec();
	test_exec_status = 1;
}

static void test_sched_handler_13_1(unsigned int dummy)
{
	test_exec_last_tid = test_tid_13_1 = get_tid();
	test_tid_13_2 = so_fork(test_sched_handler_13_2, 0);

	/* allow the other thread to init */
	sched_yield();

	/* I should continue running */
	SO_TEST_AND_SET(test_tid_13_1, test_tid_13_1);
    printf("test1 : %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_2, test_tid_13_1);
    printf("test2 : %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_1, test_tid_13_1);
    printf("test3 : %ld\n", get_tid());
	so_exec();
	SO_TEST_AND_SET(test_tid_13_2, test_tid_13_1);
    printf("test4 : %ld\n", get_tid());
	so_exec();

	/* make sure nobody changed it until now */
	test_exec_status = 0;
}

int main() {
    test_exec_status = 0;

	/* quantum is 2, so each task should be preempted
	 * after running two instructions
	 */
	so_init(2, 0);

	so_fork(test_sched_handler_13_1, 0);

	sched_yield();
	so_end();

	if (test_exec_status)
        printf("trece\n");
    return 0;
}