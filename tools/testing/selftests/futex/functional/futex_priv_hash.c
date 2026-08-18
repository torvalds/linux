// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/prctl.h>
#include <sys/prctl.h>

#include "kselftest_harness.h"

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

static void futex_hash_slots_set_verify(struct __test_metadata *_metadata, int slots)
{
	int ret;

	ret = futex_hash_slots_set(slots);
	ASSERT_EQ(ret, 0)
		TH_LOG("Failed to set slots to %d: %s", slots, strerror(errno));

	ret = futex_hash_slots_get();
	ASSERT_EQ(ret, slots) {
		TH_LOG("Set %d slots but PR_FUTEX_HASH_GET_SLOTS returns: %d, %s",
		       slots, ret, strerror(errno));
	}
}

static void futex_hash_slots_set_must_fail(struct __test_metadata *_metadata, int slots)
{
	int ret;

	ret = futex_hash_slots_set(slots);
	EXPECT_LT(ret, 0)
		TH_LOG("futex_hash_slots_set(%d) should fail but succeeded", slots);
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

static void create_max_threads(struct __test_metadata *_metadata, void *(*thread_fn)(void *))
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_create(&threads[i], NULL, thread_fn, NULL);
		ASSERT_EQ(ret, 0)
			TH_LOG("pthread_create failed: %s", strerror(errno));
	}
}

static void join_max_threads(struct __test_metadata *_metadata)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_join(threads[i], NULL);
		ASSERT_EQ(ret, 0)
			TH_LOG("pthread_join failed for thread %d: %s", i, strerror(errno));
	}
}

#define SEC_IN_NSEC	1000000000
#define MSEC_IN_NSEC	1000000

static void futex_dummy_op(struct __test_metadata *_metadata)
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
	ASSERT_NE(ret, 0)
		TH_LOG("Successfully locked an already locked mutex");

	ASSERT_EQ(ret, ETIMEDOUT)
		TH_LOG("pthread_mutex_timedlock() did not timeout: %d", ret);
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
	ASSERT_EQ(ret, 0)
		TH_LOG("Failed to initialize pthread mutex");

	/* First thread, expect to be 0, not yet initialized */
	ret = futex_hash_slots_get();
	if (ret < 0 && errno == EINVAL)
		SKIP(return, "PR_FUTEX_HASH not supported by kernel");

	ASSERT_EQ(ret, 0)
		TH_LOG("futex_hash_slots_get() failed: %d, %s", ret, strerror(errno));

	ret = pthread_create(&threads[0], NULL, thread_return_fn, NULL);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_create() failed: %d, %s", ret, strerror(errno));

	ret = pthread_join(threads[0], NULL);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_join() failed: %d, %s", ret, strerror(errno));

	/* First thread, has to initialize private hash */
	futex_slots1 = futex_hash_slots_get();
	EXPECT_GT(futex_slots1, 0)
		TH_LOG("Current hash buckets: %d. %s", futex_slots1, test_msg_auto_create);

	online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS + 1);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_barrier_init failed: %s", strerror(errno));

	ret = pthread_mutex_lock(&global_lock);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_mutex_lock failed: %s", strerror(errno));

	counter = 0;
	create_max_threads(_metadata, thread_lock_fn);
	pthread_barrier_wait(&barrier_main);

	/*
	 * The current default size of hash buckets is 16. The auto increase
	 * works only if more than 16 CPUs are available.
	 */
	TH_LOG("Online CPUs: %d", online_cpus);
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
				futex_dummy_op(_metadata);
				goto retry_getslots;
			}
			EXPECT_NE(futex_slots1, futex_slotsn) {
				TH_LOG("Expected increase of hash buckets but got: %d -> %d. %s",
				       futex_slots1, futex_slotsn, test_msg_auto_inc);
			}
		}
	} else {
		SKIP(return, "Automatic increase with more than 16 CPUs (only %d online)", online_cpus);
	}
	ret = pthread_mutex_unlock(&global_lock);

	/* Once the user changes it, it has to be what is set */
	futex_hash_slots_set_verify(_metadata, 2);
	futex_hash_slots_set_verify(_metadata, 4);
	futex_hash_slots_set_verify(_metadata, 8);
	futex_hash_slots_set_verify(_metadata, 32);
	futex_hash_slots_set_verify(_metadata, 16);

	ret = futex_hash_slots_set(15);
	EXPECT_LT(ret, 0)
		TH_LOG("Use 15 slots should fail but succeeded");

	futex_hash_slots_set_verify(_metadata, 2);
	join_max_threads(_metadata);

	EXPECT_EQ(counter, MAX_THREADS)
		TH_LOG("Created and waited for %d of %d threads", counter, MAX_THREADS);

	counter = 0;
	/* Once the user set something, auto resize must be disabled */
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_barrier_init failed: %s", strerror(errno));

	create_max_threads(_metadata, thread_lock_fn);
	join_max_threads(_metadata);

	ret = futex_hash_slots_get();
	EXPECT_EQ(ret, 2)
		TH_LOG("No more auto-resize after manual setting, got %d", ret);

	futex_hash_slots_set_must_fail(_metadata, 1 << 29);
	futex_hash_slots_set_verify(_metadata, 4);

	/*
	 * Once the global hash has been requested, then this requested can not
	 * be undone.
	 */
	ret = futex_hash_slots_set(0);
	ASSERT_EQ(ret, 0)
		TH_LOG("Global hash request failed: %s", strerror(errno));

	futex_hash_slots_set_must_fail(_metadata, 4);
	futex_hash_slots_set_must_fail(_metadata, 8);
	futex_hash_slots_set_must_fail(_metadata, 8);
	futex_hash_slots_set_must_fail(_metadata, 0);
	futex_hash_slots_set_must_fail(_metadata, 6);

	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_barrier_init failed: %s", strerror(errno));

	create_max_threads(_metadata, thread_lock_fn);
	join_max_threads(_metadata);

	ret = futex_hash_slots_get();
	EXPECT_EQ(ret, 0)
		TH_LOG("Continue to use global hash failed");
}

TEST_HARNESS_MAIN
