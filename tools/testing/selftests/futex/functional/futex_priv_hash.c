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
# define PR_FUTEX_HASH_GET_SLOTS	2
# define PR_FUTEX_HASH_GET_IMMUTABLE	3
#endif

static int futex_hash_slots_set(unsigned int slots, int immutable)
{
	return prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_SET_SLOTS, slots, immutable);
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
		error("Failed to set slots to %d\n", errno, slots);
		exit(1);
	}
	ret = futex_hash_slots_get();
	if (ret != slots) {
		error("Set %d slots but PR_FUTEX_HASH_GET_SLOTS returns: %d\n",
		       errno, slots, ret);
		exit(1);
	}
}

static void futex_hash_slots_set_must_fail(int slots, int immutable)
{
	int ret;

	ret = futex_hash_slots_set(slots, immutable);
	if (ret < 0)
		return;

	fail("futex_hash_slots_set(%d, %d) expected to fail but succeeded.\n",
	       slots, immutable);
	exit(1);
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
		if (ret) {
			error("pthread_create failed\n", errno);
			exit(1);
		}
	}
}

static void join_max_threads(void)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_join(threads[i], NULL);
		if (ret) {
			error("pthread_join failed for thread %d\n", errno, i);
			exit(1);
		}
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

int main(int argc, char *argv[])
{
	int futex_slots1, futex_slotsn, online_cpus;
	pthread_mutexattr_t mutex_attr_pi;
	int use_global_hash = 0;
	int ret;
	char c;

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


	ret = pthread_mutexattr_init(&mutex_attr_pi);
	ret |= pthread_mutexattr_setprotocol(&mutex_attr_pi, PTHREAD_PRIO_INHERIT);
	ret |= pthread_mutex_init(&global_lock, &mutex_attr_pi);
	if (ret != 0) {
		fail("Failed to initialize pthread mutex.\n");
		return 1;
	}

	/* First thread, expect to be 0, not yet initialized */
	ret = futex_hash_slots_get();
	if (ret != 0) {
		error("futex_hash_slots_get() failed: %d\n", errno, ret);
		return 1;
	}
	ret = futex_hash_immutable_get();
	if (ret != 0) {
		error("futex_hash_immutable_get() failed: %d\n", errno, ret);
		return 1;
	}

	ret = pthread_create(&threads[0], NULL, thread_return_fn, NULL);
	if (ret != 0) {
		error("pthread_create() failed: %d\n", errno, ret);
		return 1;
	}
	ret = pthread_join(threads[0], NULL);
	if (ret != 0) {
		error("pthread_join() failed: %d\n", errno, ret);
		return 1;
	}
	/* First thread, has to initialiaze private hash */
	futex_slots1 = futex_hash_slots_get();
	if (futex_slots1 <= 0) {
		fail("Expected > 0 hash buckets, got: %d\n", futex_slots1);
		return 1;
	}

	online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS + 1);
	if (ret != 0) {
		error("pthread_barrier_init failed.\n", errno);
		return 1;
	}

	ret = pthread_mutex_lock(&global_lock);
	if (ret != 0) {
		error("pthread_mutex_lock failed.\n", errno);
		return 1;
	}

	counter = 0;
	create_max_threads(thread_lock_fn);
	pthread_barrier_wait(&barrier_main);

	/*
	 * The current default size of hash buckets is 16. The auto increase
	 * works only if more than 16 CPUs are available.
	 */
	if (online_cpus > 16) {
		futex_slotsn = futex_hash_slots_get();
		if (futex_slotsn < 0 || futex_slots1 == futex_slotsn) {
			fail("Expected increase of hash buckets but got: %d -> %d\n",
			      futex_slots1, futex_slotsn);
			info("Online CPUs: %d\n", online_cpus);
			return 1;
		}
	}
	ret = pthread_mutex_unlock(&global_lock);

	/* Once the user changes it, it has to be what is set */
	futex_hash_slots_set_verify(2);
	futex_hash_slots_set_verify(4);
	futex_hash_slots_set_verify(8);
	futex_hash_slots_set_verify(32);
	futex_hash_slots_set_verify(16);

	ret = futex_hash_slots_set(15, 0);
	if (ret >= 0) {
		fail("Expected to fail with 15 slots but succeeded: %d.\n", ret);
		return 1;
	}
	futex_hash_slots_set_verify(2);
	join_max_threads();
	if (counter != MAX_THREADS) {
		fail("Expected thread counter at %d but is %d\n",
		       MAX_THREADS, counter);
		return 1;
	}
	counter = 0;
	/* Once the user set something, auto reisze must be disabled */
	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);

	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	if (ret != 2) {
		printf("Expected 2 slots, no auto-resize, got %d\n", ret);
		return 1;
	}

	futex_hash_slots_set_must_fail(1 << 29, 0);

	/*
	 * Once the private hash has been made immutable or global hash has been requested,
	 * then this requested can not be undone.
	 */
	if (use_global_hash) {
		ret = futex_hash_slots_set(0, 0);
		if (ret != 0) {
			printf("Can't request global hash: %m\n");
			return 1;
		}
	} else {
		ret = futex_hash_slots_set(4, 1);
		if (ret != 0) {
			printf("Immutable resize to 4 failed: %m\n");
			return 1;
		}
	}

	futex_hash_slots_set_must_fail(4, 0);
	futex_hash_slots_set_must_fail(4, 1);
	futex_hash_slots_set_must_fail(8, 0);
	futex_hash_slots_set_must_fail(8, 1);
	futex_hash_slots_set_must_fail(0, 1);
	futex_hash_slots_set_must_fail(6, 1);

	ret = pthread_barrier_init(&barrier_main, NULL, MAX_THREADS);
	if (ret != 0) {
		error("pthread_barrier_init failed.\n", errno);
		return 1;
	}
	create_max_threads(thread_lock_fn);
	join_max_threads();

	ret = futex_hash_slots_get();
	if (use_global_hash) {
		if (ret != 0) {
			error("Expected global hash, got %d\n", errno, ret);
			return 1;
		}
	} else {
		if (ret != 4) {
			error("Expected 4 slots, no auto-resize, got %d\n", errno, ret);
			return 1;
		}
	}

	ret = futex_hash_immutable_get();
	if (ret != 1) {
		fail("Expected immutable private hash, got %d\n", ret);
		return 1;
	}
	return 0;
}
