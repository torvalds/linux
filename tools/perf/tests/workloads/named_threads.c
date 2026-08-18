// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/compiler.h>
#include "../tests.h"

#define MAX_THREADS 25

static int iterations = 500;
int named_threads_work = 1234;

typedef void *(*thread_fn_t)(void *);

#define DEFINE_THREAD(n)						\
noinline void *named_threads_thread##n(void *arg __maybe_unused)	\
{									\
	pthread_setname_np(pthread_self(), "thread" #n);		\
	for (int i = 0; i < iterations; i++)				\
		named_threads_work += 3;				\
									\
	return NULL;							\
}

#define THREAD_LIST(macro)	\
	macro(1)		\
	macro(2)		\
	macro(3)		\
	macro(4)		\
	macro(5)		\
	macro(6)		\
	macro(7)		\
	macro(8)		\
	macro(9)		\
	macro(10)		\
	macro(11)		\
	macro(12)		\
	macro(13)		\
	macro(14)		\
	macro(15)		\
	macro(16)		\
	macro(17)		\
	macro(18)		\
	macro(19)		\
	macro(20)		\
	macro(21)		\
	macro(22)		\
	macro(23)		\
	macro(24)		\
	macro(25)

#define DECLARE_THREAD(n) void *named_threads_thread##n(void *arg);

THREAD_LIST(DECLARE_THREAD)
THREAD_LIST(DEFINE_THREAD)

#define THREAD_ENTRY(n) named_threads_thread##n,

static thread_fn_t thread_fns[MAX_THREADS] = {
	THREAD_LIST(THREAD_ENTRY)
};

/*
 * Creates argv[0] threads that run a unique function named "thread[x]" which performs
 * a multiplication in a loop for argv[1] loops.
 */
static int named_threads(int argc, const char **argv)
{
	pthread_t threads[MAX_THREADS];
	int nr_threads = 1;
	int err = 0;

	if (argc > 0)
		nr_threads = atoi(argv[0]);

	if (nr_threads <= 0 || nr_threads > MAX_THREADS) {
		fprintf(stderr, "Error: num threads must be 1 - %d\n", MAX_THREADS);
		return 1;
	}

	if (argc > 1)
		iterations = atoi(argv[1]);

	if (iterations < 0) {
		fprintf(stderr, "Error: iterations must be non-negative\n");
		return 1;
	}

	for (int i = 0; i < nr_threads; i++) {
		int ret;

		ret = pthread_create(&threads[i], NULL, thread_fns[i], NULL);
		if (ret) {
			fprintf(stderr, "Error: failed to create thread%d: %s\n",
				i + 1, strerror(ret));
			return 1;
		}
	}

	for (int i = 0; i < nr_threads; i++)
		pthread_join(threads[i], NULL);

	return err;
}

DEFINE_WORKLOAD(named_threads);
