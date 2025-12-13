// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/prctl.h>
#include <sys/prctl.h>

#include "../../kselftest_harness.h"

#define MAX_THREADS	64

static pthread_barrier_t barrier_main;
static pthread_mutex_t global_lock;
static pthread_t threads[MAX_THREADS];
static int counter;

#ifndef PR_FUTEX_HASH
#define PR_FUTEX_HASH			78
# define PR_FUTEX_HASH_SET_SLOTS	1
# define PR_FUTEX_HASH_GET_SLOTS	2
#endif

static int futex_hash_slots_set(unsigned int slots)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_SET_SLOTS, slots, 0);
}

static int futex_hash_slots_get(void)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_GET_SLOTS);
}

static void futex_hash_slots_set_verify(int slots)
{
	int ret;

	ret = futex_hash_slots_set(slots);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set slots to %d: %m\n", slots);
		ksft_finished();
	}
	ret = futex_hash_slots_get();
	if (ret != slots) {
		ksft_test_result_fail("Set %d slots but PR_FUTEX_HASH_GET_SLOTS returns: %d, %m\n",
		       slots, ret);
		ksft_finished();
	}
	ksft_test_result_pass("SET and GET slots %d passed\n", slots);
}

static void futex_hash_slots_set_must_fail(int slots)
{
	int ret;

	ret = futex_hash_slots_set(slots);
	ksft_test_result(ret < 0, "futex_hash_slots_set(%d)\n",
			 slots);
}

static void *thread_return_fn(void *arg)
{
	return NULL;
}

static void *thread_lock_fn(void *arg)
{
	pthread_barrier_wait(&barrier_main);

	pthread_mutex_lock(&global_lock);
	counter++;
	usleep(20);
	pthread_mutex_unlock(&global_lock);
	return NULL;
}

static void create_max_threads(void *(*thread_fn)(void *))
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_create(&threads[i], NULL, thread_fn, NULL);
		if (ret)
			ksft_exit_fail_msg("pthread_create failed: %m\n");
	}
}

static void join_max_threads(void)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_join(threads[i], NULL);
		if (ret)
			ksft_exit_fail_msg("pthread_join failed for thread %d\n", i);
	}
}

#define SEC_IN_NSEC	1000000000
#define MSEC_IN_NSEC	1000000

static void futex_dummy_op(void)
{
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	struct timespec timeout;
	int ret;

	pthread_mutex_lock(&lock);
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_nsec += 100 * MSEC_IN_NSEC;
	if (timeout.tv_nsec >=  SEC_IN_NSEC) {
		timeout.tv_nsec -= SEC_IN_NSEC;
		timeout.tv_sec++;
	}
	ret = pthread_mutex_timedlock(&lock, &timeout);
	if (ret == 0)
		ksft_exit_fail_msg("Successfully locked an already locked mutex.\n");

	if (ret != ETIMEDOUT)
		ksft_exit_fail_msg("pthread_mutex_timedlock() did not timeout: %d.\n", ret);
}

static const char *test_msg_auto_create = "Automatic hash bucket init on thread creation.\n";
static const char *test_msg_auto_inc = "Automatic increase with more than 16 CPUs\n";

TEST(priv_hash)
{
	int futex_slots1, futex_slotsn, online_cpus;
	pthread_mutexattr_t mutex_attr_pi;
	int ret, retry = 20;

	ret = pthread_mutexattr_init(&mutex_attr_pi);
	ret |= pthread_mutexattr_setprotocol(&mutex_attr_pi, PTHREAD_PRIO_INHERIT);
	ret |= pthread_mutex_init(&global_lock, &mutex_attr_pi);
	if (ret != 0) {
		ksft_exit_fail_msg("Failed to initialize pthread mutex.\n");
	}
	/* First thread, expect to be 0, not yet initialized */
	ret = futex_hash_slots_get();
	if (ret != 0)
		ksft_exit_fail_msg("futex_hash_slots_get() failed: %d, %m\n", ret);

	ksft_test_result_pass("Basic get slots and immutable status.\n");
	ret = pthread_create(&threads[0], NULL, thread_return_fn, NULL);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_create() failed: %d, %m\n", ret);

	ret = pthread_join(threads[0], NULL);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_join() failed: %d, %m\n", ret);

	/* First thread, has to initialize private hash */
	futex_slots1 = futex_hash_slots_get();
	if (futex_slots1 <= 0) {
		ksft_print_msg("Current hash buckets: %d\n", futex_slots1);
		ksft_exit_fail_msg("%s", test_msg_auto_create);
	}

	ksft_test_result_pass("%s", test_msg_auto_create);

	online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS + 1);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_barrier_init failed: %m.\n");

	ret = pthread_mutex_lock(&global_lock);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_mutex_lock failed: %m.\n");

	counter = 0;
	create_max_threads(thread_lock_fn);
	pthread_barrier_wait(&barrier_main);

	/*
	 * The current default size of hash buckets is 16. The auto increase
	 * works only if more than 16 CPUs are available.
	 */
	ksft_print_msg("Online CPUs: %d\n", online_cpus);
	if (online_cpus > 16) {
retry_getslots:
		futex_slotsn = futex_hash_slots_get();
		if (futex_slotsn < 0 || futex_slots1 == futex_slotsn) {
			retry--;
			/*
			 * Auto scaling on thread creation can be slightly delayed
			 * because it waits for a RCU grace period twice. The new
			 * private hash is assigned upon the first futex operation
			 * after grace period.
			 * To cover all this for testing purposes the function
			 * below will acquire a lock and acquire it again with a
			 * 100ms timeout which must timeout. This ensures we
			 * sleep for 100ms and issue a futex operation.
			 */
			if (retry > 0) {
				futex_dummy_op();
				goto retry_getslots;
			}
			ksft_print_msg("Expected increase of hash buckets but got: %d -> %d\n",
				       futex_slots1, futex_slotsn);
			ksft_exit_fail_msg("%s", test_msg_auto_inc);
		}
		ksft_test_result_pass("%s", test_msg_auto_inc);
	} else {
		ksft_test_result_skip("%s", test_msg_auto_inc);
	}
	ret = pthread_mutex_unlock(&global_lock);

	/* Once the user changes it, it has to be what is set */
	futex_hash_slots_set_verify(2);
	futex_hash_slots_set_verify(4);
	futex_hash_slots_set_verify(8);
	futex_hash_slots_set_verify(32);
	futex_hash_slots_set_verify(16);

	ret = futex_hash_slots_set(15);
	ksft_test_result(ret < 0, "Use 15 slots\n");

	futex_hash_slots_set_verify(2);
	join_max_threads();
	ksft_test_result(counter == MAX_THREADS, "Created and waited for %d of %d threads\n",
			 counter, MAX_THREADS);
	counter = 0;
	/* Once the user set something, auto resize must be disabled */
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);

	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	ksft_test_result(ret == 2, "No more auto-resize after manual setting, got %d\n",
			 ret);

	futex_hash_slots_set_must_fail(1 << 29);
	futex_hash_slots_set_verify(4);

	/*
	 * Once the global hash has been requested, then this requested can not
	 * be undone.
	 */
	ret = futex_hash_slots_set(0);
	ksft_test_result(ret == 0, "Global hash request\n");
	if (ret != 0)
		return;

	futex_hash_slots_set_must_fail(4);
	futex_hash_slots_set_must_fail(8);
	futex_hash_slots_set_must_fail(8);
	futex_hash_slots_set_must_fail(0);
	futex_hash_slots_set_must_fail(6);

	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_barrier_init failed: %m\n");

	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	ksft_test_result(ret == 0, "Continue to use global hash\n");
}

TEST_HARNESS_MAIN
