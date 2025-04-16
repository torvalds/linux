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
#include <numa.h>
#include <numaif.h>

#include <linux/futex.h>
#include <sys/mman.h>

#include "logging.h"
#include "futextest.h"
#include "futex2test.h"

#define MAX_THREADS	64

static pthread_barrier_t barrier_main;
static pthread_t threads[MAX_THREADS];

struct thread_args {
	void *futex_ptr;
	unsigned int flags;
	int result;
};

static struct thread_args thread_args[MAX_THREADS];

#ifndef FUTEX_NO_NODE
#define FUTEX_NO_NODE (-1)
#endif

#ifndef FUTEX2_MPOL
#define FUTEX2_MPOL	0x08
#endif

static void *thread_lock_fn(void *arg)
{
	struct thread_args *args = arg;
	int ret;

	pthread_barrier_wait(&barrier_main);
	ret = futex2_wait(args->futex_ptr, 0, args->flags, NULL, 0);
	args->result = ret;
	return NULL;
}

static void create_max_threads(void *futex_ptr)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		thread_args[i].futex_ptr = futex_ptr;
		thread_args[i].flags = FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA;
		thread_args[i].result = 0;
		ret = pthread_create(&threads[i], NULL, thread_lock_fn, &thread_args[i]);
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

static void __test_futex(void *futex_ptr, int must_fail, unsigned int futex_flags)
{
	int to_wake, ret, i, need_exit = 0;

	pthread_barrier_init(&barrier_main, NULL, MAX_THREADS + 1);
	create_max_threads(futex_ptr);
	pthread_barrier_wait(&barrier_main);
	to_wake = MAX_THREADS;

	do {
		ret = futex2_wake(futex_ptr, to_wake, futex_flags);
		if (must_fail) {
			if (ret < 0)
				break;
			fail("Should fail, but didn't\n");
			exit(1);
		}
		if (ret < 0) {
			error("Failed futex2_wake(%d)\n", errno, to_wake);
			exit(1);
		}
		if (!ret)
			usleep(50);
		to_wake -= ret;

	} while (to_wake);
	join_max_threads();

	for (i = 0; i < MAX_THREADS; i++) {
		if (must_fail && thread_args[i].result != -1) {
			fail("Thread %d should fail but succeeded (%d)\n", i, thread_args[i].result);
			need_exit = 1;
		}
		if (!must_fail && thread_args[i].result != 0) {
			fail("Thread %d failed (%d)\n", i, thread_args[i].result);
			need_exit = 1;
		}
	}
	if (need_exit)
		exit(1);
}

static void test_futex(void *futex_ptr, int must_fail)
{
	__test_futex(futex_ptr, must_fail, FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA);
}

static void test_futex_mpol(void *futex_ptr, int must_fail)
{
	__test_futex(futex_ptr, must_fail, FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA | FUTEX2_MPOL);
}

static void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c    Use color\n");
	printf("  -h    Display this help message\n");
	printf("  -v L  Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

int main(int argc, char *argv[])
{
	struct futex32_numa *futex_numa;
	int mem_size, i;
	void *futex_ptr;
	char c;

	while ((c = getopt(argc, argv, "chv:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
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

	mem_size = sysconf(_SC_PAGE_SIZE);
	futex_ptr = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (futex_ptr == MAP_FAILED) {
		error("mmap() for %d bytes failed\n", errno, mem_size);
		return 1;
	}
	futex_numa = futex_ptr;

	info("Regular test\n");
	futex_numa->futex = 0;
	futex_numa->numa = FUTEX_NO_NODE;
	test_futex(futex_ptr, 0);

	if (futex_numa->numa == FUTEX_NO_NODE) {
		fail("NUMA node is left unitiliazed\n");
		return 1;
	}

	info("Memory too small\n");
	test_futex(futex_ptr + mem_size - 4, 1);

	info("Memory out of range\n");
	test_futex(futex_ptr + mem_size, 1);

	futex_numa->numa = FUTEX_NO_NODE;
	mprotect(futex_ptr, mem_size, PROT_READ);
	info("Memory, RO\n");
	test_futex(futex_ptr, 1);

	mprotect(futex_ptr, mem_size, PROT_NONE);
	info("Memory, no access\n");
	test_futex(futex_ptr, 1);

	mprotect(futex_ptr, mem_size, PROT_READ | PROT_WRITE);
	info("Memory back to RW\n");
	test_futex(futex_ptr, 0);

	/* MPOL test. Does not work as expected */
	for (i = 0; i < 4; i++) {
		unsigned long nodemask;
		int ret;

		nodemask = 1 << i;
		ret = mbind(futex_ptr, mem_size, MPOL_BIND, &nodemask,
			    sizeof(nodemask) * 8, 0);
		if (ret == 0) {
			info("Node %d test\n", i);
			futex_numa->futex = 0;
			futex_numa->numa = FUTEX_NO_NODE;

			ret = futex2_wake(futex_ptr, 0, FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA | FUTEX2_MPOL);
			if (ret < 0)
				error("Failed to wake 0 with MPOL.\n", errno);
			if (0)
				test_futex_mpol(futex_numa, 0);
			if (futex_numa->numa != i) {
				fail("Returned NUMA node is %d expected %d\n",
				     futex_numa->numa, i);
			}
		}
	}
	return 0;
}
