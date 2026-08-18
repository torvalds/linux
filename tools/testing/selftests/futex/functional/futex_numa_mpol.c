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
#ifdef LIBNUMA_VER_SUFFICIENT
#include <numa.h>
#include <numaif.h>
#endif

#include <linux/futex.h>
#include <sys/mman.h>

#include "futextest.h"
#include "futex2test.h"
#include "kselftest_harness.h"

#define MAX_THREADS	64

static pthread_barrier_t barrier_main;
static pthread_t threads[MAX_THREADS];

struct thread_args {
	void		*futex_ptr;
	unsigned int	flags;
	int		result;
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

static void create_max_threads(struct __test_metadata *_metadata, void *futex_ptr)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		thread_args[i].futex_ptr = futex_ptr;
		thread_args[i].flags = FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA;
		thread_args[i].result = 0;
		ret = pthread_create(&threads[i], NULL, thread_lock_fn, &thread_args[i]);
		ASSERT_EQ(ret, 0)
			TH_LOG("pthread_create failed");
	}
}

static void join_max_threads(struct __test_metadata *_metadata)
{
	int i, ret;

	for (i = 0; i < MAX_THREADS; i++) {
		ret = pthread_join(threads[i], NULL);
		ASSERT_EQ(ret, 0)
			TH_LOG("pthread_join failed for thread %d", i);
	}
}

static void __test_futex(struct __test_metadata *_metadata, void *futex_ptr, int err_value,
			 unsigned int futex_flags)
{
	int to_wake, ret, i;

	pthread_barrier_init(&barrier_main, NULL, MAX_THREADS + 1);
	create_max_threads(_metadata, futex_ptr);
	pthread_barrier_wait(&barrier_main);
	to_wake = MAX_THREADS;

	do {
		ret = futex2_wake(futex_ptr, to_wake, futex_flags);

		if (err_value) {
			EXPECT_LT(ret, 0) {
				TH_LOG("futex2_wake(%d, 0x%x) should fail, but didn't",
				       to_wake, futex_flags);
			}

			EXPECT_EQ(errno, err_value) {
				TH_LOG("futex2_wake(%d, 0x%x) expected error was %d, but returned %d (%s)",
				       to_wake, futex_flags, err_value, errno, strerror(errno));
			}

			break;
		}
		if (ret < 0) {
			if (errno == ENOSYS || (errno == EINVAL && (futex_flags & FUTEX2_NUMA)))
				SKIP(return, "futex2 or FUTEX2_NUMA not supported by kernel");

			ASSERT_GE(ret, 0) {
				TH_LOG("Failed futex2_wake(%d, 0x%x): %s",
				       to_wake, futex_flags, strerror(errno));
			}
		}
		if (!ret)
			usleep(50);
		to_wake -= ret;

	} while (to_wake);
	join_max_threads(_metadata);

	for (i = 0; i < MAX_THREADS; i++) {
		if (err_value) {
			EXPECT_EQ(thread_args[i].result, -1) {
				TH_LOG("Thread %d should fail but succeeded (%d)",
				       i, thread_args[i].result);
			}
		} else {
			EXPECT_EQ(thread_args[i].result, 0)
				TH_LOG("Thread %d failed (%d)", i, thread_args[i].result);
		}
	}
}

static void test_futex(struct __test_metadata *_metadata, void *futex_ptr, int err_value)
{
	__test_futex(_metadata, futex_ptr, err_value, FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG | FUTEX2_NUMA);
}

TEST(futex_numa_mpol)
{
	struct futex32_numa *futex_numa;
	void *futex_ptr;
	int mem_size;

	mem_size = sysconf(_SC_PAGE_SIZE);
	futex_ptr = mmap(NULL, mem_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	ASSERT_NE(futex_ptr, MAP_FAILED)
		TH_LOG("mmap() for %d bytes failed: %s", mem_size, strerror(errno));

	/* Create an invalid memory region for the "Memory out of range" test */
	mprotect(futex_ptr + mem_size, mem_size, PROT_NONE);

	futex_numa = futex_ptr;

	TH_LOG("Regular test");
	futex_numa->futex = 0;
	futex_numa->numa = FUTEX_NO_NODE;
	test_futex(_metadata, futex_ptr, 0);

	EXPECT_NE(futex_numa->numa, FUTEX_NO_NODE)
		TH_LOG("NUMA node is left uninitialized");

	/* FUTEX2_NUMA futex must be 8-byte aligned */
	TH_LOG("Mis-aligned futex");
	test_futex(_metadata, futex_ptr + mem_size - 4, EINVAL);

	TH_LOG("Memory out of range");
	test_futex(_metadata, futex_ptr + mem_size, EFAULT);

	futex_numa->numa = FUTEX_NO_NODE;
	mprotect(futex_ptr, mem_size, PROT_READ);
	TH_LOG("Memory, RO");
	test_futex(_metadata, futex_ptr, EFAULT);

	mprotect(futex_ptr, mem_size, PROT_NONE);
	TH_LOG("Memory, no access");
	test_futex(_metadata, futex_ptr, EFAULT);

	mprotect(futex_ptr, mem_size, PROT_READ | PROT_WRITE);
	TH_LOG("Memory back to RW");
	test_futex(_metadata, futex_ptr, 0);

	/* MPOL test. Does not work as expected */
#ifdef LIBNUMA_VER_SUFFICIENT
	for (int i = 0; i < 4; i++) {
		unsigned long nodemask;
		int ret;

		nodemask = 1 << i;
		ret = mbind(futex_ptr, mem_size, MPOL_BIND, &nodemask,
			    sizeof(nodemask) * 8, 0);
		if (ret == 0) {
			ret = numa_set_mempolicy_home_node(futex_ptr, mem_size, i, 0);
			ASSERT_EQ(ret, 0)
				TH_LOG("Failed to set home node: %s, %d", strerror(errno), errno);

			TH_LOG("Node %d test", i);
			futex_numa->futex = 0;
			futex_numa->numa = FUTEX_NO_NODE;

			ret = futex2_wake(futex_ptr, 0, FUTEX2_SIZE_U32 | FUTEX_PRIVATE_FLAG |
					  FUTEX2_NUMA | FUTEX2_MPOL);
			EXPECT_GE(ret, 0)
				TH_LOG("Failed to wake 0 with MPOL: %s", strerror(errno));
			EXPECT_EQ(futex_numa->numa, i)
				TH_LOG("Returned NUMA node is %d expected %d", futex_numa->numa, i);
		}
	}
#else
	SKIP(return, "futex2 MPOL hints test requires libnuma 2.0.18+");
#endif
	munmap(futex_ptr, mem_size * 2);
}

TEST_HARNESS_MAIN
