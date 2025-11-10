// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Igalia S.L.
 *
 * Robust list test by André Almeida <andrealmeid@igalia.com>
 *
 * The robust list uAPI allows userspace to create "robust" locks, in the sense
 * that if the lock holder thread dies, the remaining threads that are waiting
 * for the lock won't block forever, waiting for a lock that will never be
 * released.
 *
 * This is achieve by userspace setting a list where a thread can enter all the
 * locks (futexes) that it is holding. The robust list is a linked list, and
 * userspace register the start of the list with the syscall set_robust_list().
 * If such thread eventually dies, the kernel will walk this list, waking up one
 * thread waiting for each futex and marking the futex word with the flag
 * FUTEX_OWNER_DIED.
 *
 * See also
 *	man set_robust_list
 *	Documententation/locking/robust-futex-ABI.rst
 *	Documententation/locking/robust-futexes.rst
 */

#define _GNU_SOURCE

#include "futextest.h"
#include "../../kselftest_harness.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define STACK_SIZE (1024 * 1024)

#define FUTEX_TIMEOUT 3

#define SLEEP_US 100

static pthread_barrier_t barrier, barrier2;

static int set_robust_list(struct robust_list_head *head, size_t len)
{
	return syscall(SYS_set_robust_list, head, len);
}

static int get_robust_list(int pid, struct robust_list_head **head, size_t *len_ptr)
{
	return syscall(SYS_get_robust_list, pid, head, len_ptr);
}

/*
 * Basic lock struct, contains just the futex word and the robust list element
 * Real implementations have also a *prev to easily walk in the list
 */
struct lock_struct {
	_Atomic(unsigned int)	futex;
	struct robust_list	list;
};

/*
 * Helper function to spawn a child thread. Returns -1 on error, pid on success
 */
static int create_child(int (*fn)(void *arg), void *arg)
{
	char *stack;
	pid_t pid;

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (stack == MAP_FAILED)
		return -1;

	stack += STACK_SIZE;

	pid = clone(fn, stack, CLONE_VM | SIGCHLD, arg);

	if (pid == -1)
		return -1;

	return pid;
}

/*
 * Helper function to prepare and register a robust list
 */
static int set_list(struct robust_list_head *head)
{
	int ret;

	ret = set_robust_list(head, sizeof(*head));
	if (ret)
		return ret;

	head->futex_offset = (size_t) offsetof(struct lock_struct, futex) -
			     (size_t) offsetof(struct lock_struct, list);
	head->list.next = &head->list;
	head->list_op_pending = NULL;

	return 0;
}

/*
 * A basic (and incomplete) mutex lock function with robustness
 */
static int mutex_lock(struct lock_struct *lock, struct robust_list_head *head, bool error_inject)
{
	_Atomic(unsigned int) *futex = &lock->futex;
	unsigned int zero = 0;
	pid_t tid = gettid();
	int ret = -1;

	/*
	 * Set list_op_pending before starting the lock, so the kernel can catch
	 * the case where the thread died during the lock operation
	 */
	head->list_op_pending = &lock->list;

	if (atomic_compare_exchange_strong(futex, &zero, tid)) {
		/*
		 * We took the lock, insert it in the robust list
		 */
		struct robust_list *list = &head->list;

		/* Error injection to test list_op_pending */
		if (error_inject)
			return 0;

		while (list->next != &head->list)
			list = list->next;

		list->next = &lock->list;
		lock->list.next = &head->list;

		ret = 0;
	} else {
		/*
		 * We didn't take the lock, wait until the owner wakes (or dies)
		 */
		struct timespec to;

		to.tv_sec = FUTEX_TIMEOUT;
		to.tv_nsec = 0;

		tid = atomic_load(futex);
		/* Kernel ignores futexes without the waiters flag */
		tid |= FUTEX_WAITERS;
		atomic_store(futex, tid);

		ret = futex_wait((futex_t *) futex, tid, &to, 0);

		/*
		 * A real mutex_lock() implementation would loop here to finally
		 * take the lock. We don't care about that, so we stop here.
		 */
	}

	head->list_op_pending = NULL;

	return ret;
}

/*
 * This child thread will succeed taking the lock, and then will exit holding it
 */
static int child_fn_lock(void *arg)
{
	struct lock_struct *lock = arg;
	struct robust_list_head head;
	int ret;

	ret = set_list(&head);
	if (ret) {
		ksft_test_result_fail("set_robust_list error\n");
		return ret;
	}

	ret = mutex_lock(lock, &head, false);
	if (ret) {
		ksft_test_result_fail("mutex_lock error\n");
		return ret;
	}

	pthread_barrier_wait(&barrier);

	/*
	 * There's a race here: the parent thread needs to be inside
	 * futex_wait() before the child thread dies, otherwise it will miss the
	 * wakeup from handle_futex_death() that this child will emit. We wait a
	 * little bit just to make sure that this happens.
	 */
	usleep(SLEEP_US);

	return 0;
}

/*
 * Spawns a child thread that will set a robust list, take the lock, register it
 * in the robust list and die. The parent thread will wait on this futex, and
 * should be waken up when the child exits.
 */
TEST(test_robustness)
{
	struct lock_struct lock = { .futex = 0 };
	_Atomic(unsigned int) *futex = &lock.futex;
	struct robust_list_head head;
	int ret, pid, wstatus;

	ret = set_list(&head);
	ASSERT_EQ(ret, 0);

	/*
	 * Lets use a barrier to ensure that the child thread takes the lock
	 * before the parent
	 */
	ret = pthread_barrier_init(&barrier, NULL, 2);
	ASSERT_EQ(ret, 0);

	pid = create_child(&child_fn_lock, &lock);
	ASSERT_NE(pid, -1);

	pthread_barrier_wait(&barrier);
	ret = mutex_lock(&lock, &head, false);

	/*
	 * futex_wait() should return 0 and the futex word should be marked with
	 * FUTEX_OWNER_DIED
	 */
	ASSERT_EQ(ret, 0);

	ASSERT_TRUE(*futex & FUTEX_OWNER_DIED);

	wait(&wstatus);
	pthread_barrier_destroy(&barrier);

	/* Pass only if the child hasn't return error */
	if (!WEXITSTATUS(wstatus))
		ksft_test_result_pass("%s\n", __func__);
}

/*
 * The only valid value for len is sizeof(*head)
 */
TEST(test_set_robust_list_invalid_size)
{
	struct robust_list_head head;
	size_t head_size = sizeof(head);
	int ret;

	ret = set_robust_list(&head, head_size);
	ASSERT_EQ(ret, 0);

	ret = set_robust_list(&head, head_size * 2);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	ret = set_robust_list(&head, head_size - 1);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	ret = set_robust_list(&head, 0);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	ksft_test_result_pass("%s\n", __func__);
}

/*
 * Test get_robust_list with pid = 0, getting the list of the running thread
 */
TEST(test_get_robust_list_self)
{
	struct robust_list_head head, head2, *get_head;
	size_t head_size = sizeof(head), len_ptr;
	int ret;

	ret = set_robust_list(&head, head_size);
	ASSERT_EQ(ret, 0);

	ret = get_robust_list(0, &get_head, &len_ptr);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(get_head, &head);
	ASSERT_EQ(head_size, len_ptr);

	ret = set_robust_list(&head2, head_size);
	ASSERT_EQ(ret, 0);

	ret = get_robust_list(0, &get_head, &len_ptr);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(get_head, &head2);
	ASSERT_EQ(head_size, len_ptr);

	ksft_test_result_pass("%s\n", __func__);
}

static int child_list(void *arg)
{
	struct robust_list_head *head = arg;
	int ret;

	ret = set_robust_list(head, sizeof(*head));
	if (ret) {
		ksft_test_result_fail("set_robust_list error\n");
		return -1;
	}

	/*
	 * After setting the list head, wait until the main thread can call
	 * get_robust_list() for this thread before exiting.
	 */
	pthread_barrier_wait(&barrier);
	pthread_barrier_wait(&barrier2);

	return 0;
}

/*
 * Test get_robust_list from another thread. We use two barriers here to ensure
 * that:
 *   1) the child thread set the list before we try to get it from the
 * parent
 *   2) the child thread still alive when we try to get the list from it
 */
TEST(test_get_robust_list_child)
{
	struct robust_list_head head, *get_head;
	int ret, wstatus;
	size_t len_ptr;
	pid_t tid;

	ret = pthread_barrier_init(&barrier, NULL, 2);
	ret = pthread_barrier_init(&barrier2, NULL, 2);
	ASSERT_EQ(ret, 0);

	tid = create_child(&child_list, &head);
	ASSERT_NE(tid, -1);

	pthread_barrier_wait(&barrier);

	ret = get_robust_list(tid, &get_head, &len_ptr);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(&head, get_head);

	pthread_barrier_wait(&barrier2);

	wait(&wstatus);
	pthread_barrier_destroy(&barrier);
	pthread_barrier_destroy(&barrier2);

	/* Pass only if the child hasn't return error */
	if (!WEXITSTATUS(wstatus))
		ksft_test_result_pass("%s\n", __func__);
}

static int child_fn_lock_with_error(void *arg)
{
	struct lock_struct *lock = arg;
	struct robust_list_head head;
	int ret;

	ret = set_list(&head);
	if (ret) {
		ksft_test_result_fail("set_robust_list error\n");
		return -1;
	}

	ret = mutex_lock(lock, &head, true);
	if (ret) {
		ksft_test_result_fail("mutex_lock error\n");
		return -1;
	}

	pthread_barrier_wait(&barrier);

	/* See comment at child_fn_lock() */
	usleep(SLEEP_US);

	return 0;
}

/*
 * Same as robustness test, but inject an error where the mutex_lock() exits
 * earlier, just after setting list_op_pending and taking the lock, to test the
 * list_op_pending mechanism
 */
TEST(test_set_list_op_pending)
{
	struct lock_struct lock = { .futex = 0 };
	_Atomic(unsigned int) *futex = &lock.futex;
	struct robust_list_head head;
	int ret, wstatus;

	ret = set_list(&head);
	ASSERT_EQ(ret, 0);

	ret = pthread_barrier_init(&barrier, NULL, 2);
	ASSERT_EQ(ret, 0);

	ret = create_child(&child_fn_lock_with_error, &lock);
	ASSERT_NE(ret, -1);

	pthread_barrier_wait(&barrier);
	ret = mutex_lock(&lock, &head, false);

	ASSERT_EQ(ret, 0);

	ASSERT_TRUE(*futex & FUTEX_OWNER_DIED);

	wait(&wstatus);
	pthread_barrier_destroy(&barrier);

	/* Pass only if the child hasn't return error */
	if (!WEXITSTATUS(wstatus))
		ksft_test_result_pass("%s\n", __func__);
	else
		ksft_test_result_fail("%s\n", __func__);
}

#define CHILD_NR 10

static int child_lock_holder(void *arg)
{
	struct lock_struct *locks = arg;
	struct robust_list_head head;
	int i;

	set_list(&head);

	for (i = 0; i < CHILD_NR; i++) {
		locks[i].futex = 0;
		mutex_lock(&locks[i], &head, false);
	}

	pthread_barrier_wait(&barrier);
	pthread_barrier_wait(&barrier2);

	/* See comment at child_fn_lock() */
	usleep(SLEEP_US);

	return 0;
}

static int child_wait_lock(void *arg)
{
	struct lock_struct *lock = arg;
	struct robust_list_head head;
	int ret;

	pthread_barrier_wait(&barrier2);
	ret = mutex_lock(lock, &head, false);

	if (ret) {
		ksft_test_result_fail("mutex_lock error\n");
		return -1;
	}

	if (!(lock->futex & FUTEX_OWNER_DIED)) {
		ksft_test_result_fail("futex not marked with FUTEX_OWNER_DIED\n");
		return -1;
	}

	return 0;
}

/*
 * Test a robust list of more than one element. All the waiters should wake when
 * the holder dies
 */
TEST(test_robust_list_multiple_elements)
{
	struct lock_struct locks[CHILD_NR];
	pid_t pids[CHILD_NR + 1];
	int i, ret, wstatus;

	ret = pthread_barrier_init(&barrier, NULL, 2);
	ASSERT_EQ(ret, 0);
	ret = pthread_barrier_init(&barrier2, NULL, CHILD_NR + 1);
	ASSERT_EQ(ret, 0);

	pids[0] = create_child(&child_lock_holder, &locks);

	/* Wait until the locker thread takes the look */
	pthread_barrier_wait(&barrier);

	for (i = 0; i < CHILD_NR; i++)
		pids[i+1] = create_child(&child_wait_lock, &locks[i]);

	/* Wait for all children to return */
	ret = 0;

	for (i = 0; i < CHILD_NR; i++) {
		waitpid(pids[i], &wstatus, 0);
		if (WEXITSTATUS(wstatus))
			ret = -1;
	}

	pthread_barrier_destroy(&barrier);
	pthread_barrier_destroy(&barrier2);

	/* Pass only if the child hasn't return error */
	if (!ret)
		ksft_test_result_pass("%s\n", __func__);
}

static int child_circular_list(void *arg)
{
	static struct robust_list_head head;
	struct lock_struct a, b, c;
	int ret;

	ret = set_list(&head);
	if (ret) {
		ksft_test_result_fail("set_list error\n");
		return -1;
	}

	head.list.next = &a.list;

	/*
	 * The last element should point to head list, but we short circuit it
	 */
	a.list.next = &b.list;
	b.list.next = &c.list;
	c.list.next = &a.list;

	return 0;
}

/*
 * Create a circular robust list. The kernel should be able to destroy the list
 * while processing it so it won't be trapped in an infinite loop while handling
 * a process exit
 */
TEST(test_circular_list)
{
	int wstatus;

	create_child(child_circular_list, NULL);

	wait(&wstatus);

	/* Pass only if the child hasn't return error */
	if (!WEXITSTATUS(wstatus))
		ksft_test_result_pass("%s\n", __func__);
}

TEST_HARNESS_MAIN
