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

#include "logging.h"

#define MAX_THREADS	64

static pthread_barrier_t barrier_main;
static pthread_mutex_t global_lock;
static pthread_t threads[MAX_THREADS];
static int counter;

#ifndef PR_FUTEX_HASH
#define PR_FUTEX_HASH			78
# define PR_FUTEX_HASH_SET_SLOTS	1
# define FH_FLAG_IMMUTABLE		(1ULL << 0)
# define PR_FUTEX_HASH_GET_SLOTS	2
# define PR_FUTEX_HASH_GET_IMMUTABLE	3
#endif

static int futex_hash_slots_set(unsigned int slots, int flags)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_SET_SLOTS, slots, flags);
}

static int futex_hash_slots_get(void)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_GET_SLOTS);
}

static int futex_hash_immutable_get(void)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_GET_IMMUTABLE);
}

static void futex_hash_slots_set_verify(int slots)
{
	int ret;

	ret = futex_hash_slots_set(slots, 0);
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

static void futex_hash_slots_set_must_fail(int slots, int flags)
{
	int ret;

	ret = futex_hash_slots_set(slots, flags);
	ksft_test_result(ret < 0, "futex_hash_slots_set(%d, %d)\n",
			 slots, flags);
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

static void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c    Use color\n");
	printf("  -g    Test global hash instead intead local immutable \n");
	printf("  -h    Display this help message\n");
	printf("  -v L  Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

static const char *test_msg_auto_create = "Automatic hash bucket init on thread creation.\n";
static const char *test_msg_auto_inc = "Automatic increase with more than 16 CPUs\n";

int main(int argc, char *argv[])
{
	int futex_slots1, futex_slotsn, online_cpus;
	pthread_mutexattr_t mutex_attr_pi;
	int use_global_hash = 0;
	int ret;
	int c;

	while ((c = getopt(argc, argv, "cghv:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'g':
			use_global_hash = 1;
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
			break;
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_set_plan(22);

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

	ret = futex_hash_immutable_get();
	if (ret != 0)
		ksft_exit_fail_msg("futex_hash_immutable_get() failed: %d, %m\n", ret);

	ksft_test_result_pass("Basic get slots and immutable status.\n");
	ret = pthread_create(&threads[0], NULL, thread_return_fn, NULL);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_create() failed: %d, %m\n", ret);

	ret = pthread_join(threads[0], NULL);
	if (ret != 0)
		ksft_exit_fail_msg("pthread_join() failed: %d, %m\n", ret);

	/* First thread, has to initialiaze private hash */
	futex_slots1 = futex_hash_slots_get();
	if (futex_slots1 <= 0) {
		ksft_print_msg("Current hash buckets: %d\n", futex_slots1);
		ksft_exit_fail_msg(test_msg_auto_create);
	}

	ksft_test_result_pass(test_msg_auto_create);

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
		futex_slotsn = futex_hash_slots_get();
		if (futex_slotsn < 0 || futex_slots1 == futex_slotsn) {
			ksft_print_msg("Expected increase of hash buckets but got: %d -> %d\n",
				       futex_slots1, futex_slotsn);
			ksft_exit_fail_msg(test_msg_auto_inc);
		}
		ksft_test_result_pass(test_msg_auto_inc);
	} else {
		ksft_test_result_skip(test_msg_auto_inc);
	}
	ret = pthread_mutex_unlock(&global_lock);

	/* Once the user changes it, it has to be what is set */
	futex_hash_slots_set_verify(2);
	futex_hash_slots_set_verify(4);
	futex_hash_slots_set_verify(8);
	futex_hash_slots_set_verify(32);
	futex_hash_slots_set_verify(16);

	ret = futex_hash_slots_set(15, 0);
	ksft_test_result(ret < 0, "Use 15 slots\n");

	futex_hash_slots_set_verify(2);
	join_max_threads();
	ksft_test_result(counter == MAX_THREADS, "Created of waited for %d of %d threads\n",
			 counter, MAX_THREADS);
	counter = 0;
	/* Once the user set something, auto reisze must be disabled */
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);

	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	ksft_test_result(ret == 2, "No more auto-resize after manaul setting, got %d\n",
			 ret);

	futex_hash_slots_set_must_fail(1 << 29, 0);

	/*
	 * Once the private hash has been made immutable or global hash has been requested,
	 * then this requested can not be undone.
	 */
	if (use_global_hash) {
		ret = futex_hash_slots_set(0, 0);
		ksft_test_result(ret == 0, "Global hash request\n");
	} else {
		ret = futex_hash_slots_set(4, FH_FLAG_IMMUTABLE);
		ksft_test_result(ret == 0, "Immutable resize to 4\n");
	}
	if (ret != 0)
		goto out;

	futex_hash_slots_set_must_fail(4, 0);
	futex_hash_slots_set_must_fail(4, FH_FLAG_IMMUTABLE);
	futex_hash_slots_set_must_fail(8, 0);
	futex_hash_slots_set_must_fail(8, FH_FLAG_IMMUTABLE);
	futex_hash_slots_set_must_fail(0, FH_FLAG_IMMUTABLE);
	futex_hash_slots_set_must_fail(6, FH_FLAG_IMMUTABLE);

	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);
	if (ret != 0) {
		ksft_exit_fail_msg("pthread_barrier_init failed: %m\n");
		return 1;
	}
	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	if (use_global_hash) {
		ksft_test_result(ret == 0, "Continue to use global hash\n");
	} else {
		ksft_test_result(ret == 4, "Continue to use the 4 hash buckets\n");
	}

	ret = futex_hash_immutable_get();
	ksft_test_result(ret == 1, "Hash reports to be immutable\n");

out:
	ksft_finished();
	return 0;
}
