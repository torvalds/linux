// SPDX-License-Identifier: LGPL-2.1
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

#include <linux/prctl.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include "rseq.h"

#include "../kselftest_harness.h"

#ifndef __NR_rseq_slice_yield
# define __NR_rseq_slice_yield	471
#endif

#define BITS_PER_INT	32
#define BITS_PER_BYTE	8

#ifndef PR_RSEQ_SLICE_EXTENSION
# define PR_RSEQ_SLICE_EXTENSION		79
#  define PR_RSEQ_SLICE_EXTENSION_GET		1
#  define PR_RSEQ_SLICE_EXTENSION_SET		2
#  define PR_RSEQ_SLICE_EXT_ENABLE		0x01
#endif

#ifndef RSEQ_SLICE_EXT_REQUEST_BIT
# define RSEQ_SLICE_EXT_REQUEST_BIT	0
# define RSEQ_SLICE_EXT_GRANTED_BIT	1
#endif

#ifndef asm_inline
# define asm_inline	asm __inline
#endif

#define NSEC_PER_SEC	1000000000L
#define NSEC_PER_USEC	      1000L

struct noise_params {
	int64_t	noise_nsecs;
	int64_t	sleep_nsecs;
	int64_t	run;
};

FIXTURE(slice_ext)
{
	pthread_t		noise_thread;
	struct noise_params	noise_params;
};

FIXTURE_VARIANT(slice_ext)
{
	int64_t	total_nsecs;
	int64_t	slice_nsecs;
	int64_t	noise_nsecs;
	int64_t	sleep_nsecs;
	bool	no_yield;
};

FIXTURE_VARIANT_ADD(slice_ext, n2_2_50)
{
	.total_nsecs	=  5LL * NSEC_PER_SEC,
	.slice_nsecs	=  2LL * NSEC_PER_USEC,
	.noise_nsecs    =  2LL * NSEC_PER_USEC,
	.sleep_nsecs	= 50LL * NSEC_PER_USEC,
};

FIXTURE_VARIANT_ADD(slice_ext, n50_2_50)
{
	.total_nsecs	=  5LL * NSEC_PER_SEC,
	.slice_nsecs	= 50LL * NSEC_PER_USEC,
	.noise_nsecs    =  2LL * NSEC_PER_USEC,
	.sleep_nsecs	= 50LL * NSEC_PER_USEC,
};

FIXTURE_VARIANT_ADD(slice_ext, n2_2_50_no_yield)
{
	.total_nsecs	=  5LL * NSEC_PER_SEC,
	.slice_nsecs	=  2LL * NSEC_PER_USEC,
	.noise_nsecs    =  2LL * NSEC_PER_USEC,
	.sleep_nsecs	= 50LL * NSEC_PER_USEC,
	.no_yield	= true,
};


static inline bool elapsed(struct timespec *start, struct timespec *now,
			   int64_t span)
{
	int64_t delta = now->tv_sec - start->tv_sec;

	delta *= NSEC_PER_SEC;
	delta += now->tv_nsec - start->tv_nsec;
	return delta >= span;
}

static void *noise_thread(void *arg)
{
	struct noise_params *p = arg;

	while (RSEQ_READ_ONCE(p->run)) {
		struct timespec ts_start, ts_now;

		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		do {
			clock_gettime(CLOCK_MONOTONIC, &ts_now);
		} while (!elapsed(&ts_start, &ts_now, p->noise_nsecs));

		ts_start.tv_sec = 0;
		ts_start.tv_nsec = p->sleep_nsecs;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts_start, NULL);
	}
	return NULL;
}

FIXTURE_SETUP(slice_ext)
{
	cpu_set_t affinity;

	ASSERT_EQ(sched_getaffinity(0, sizeof(affinity), &affinity), 0);

	/* Pin it on a single CPU. Avoid CPU 0 */
	for (int i = 1; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &affinity))
			continue;

		CPU_ZERO(&affinity);
		CPU_SET(i, &affinity);
		ASSERT_EQ(sched_setaffinity(0, sizeof(affinity), &affinity), 0);
		break;
	}

	ASSERT_EQ(rseq_register_current_thread(), 0);

	ASSERT_EQ(prctl(PR_RSEQ_SLICE_EXTENSION, PR_RSEQ_SLICE_EXTENSION_SET,
			PR_RSEQ_SLICE_EXT_ENABLE, 0, 0), 0);

	self->noise_params.noise_nsecs = variant->noise_nsecs;
	self->noise_params.sleep_nsecs = variant->sleep_nsecs;
	self->noise_params.run = 1;

	ASSERT_EQ(pthread_create(&self->noise_thread, NULL, noise_thread, &self->noise_params), 0);
}

FIXTURE_TEARDOWN(slice_ext)
{
	self->noise_params.run = 0;
	pthread_join(self->noise_thread, NULL);
}

TEST_F(slice_ext, slice_test)
{
	unsigned long success = 0, yielded = 0, scheduled = 0, raced = 0;
	unsigned long total = 0, aborted = 0;
	struct rseq_abi *rs = rseq_get_abi();
	struct timespec ts_start, ts_now;

	ASSERT_NE(rs, NULL);

	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	do {
		struct timespec ts_cs;
		bool req = false;

		clock_gettime(CLOCK_MONOTONIC, &ts_cs);

		total++;
		RSEQ_WRITE_ONCE(rs->slice_ctrl.request, 1);
		do {
			clock_gettime(CLOCK_MONOTONIC, &ts_now);
		} while (!elapsed(&ts_cs, &ts_now, variant->slice_nsecs));

		/*
		 * request can be cleared unconditionally, but for making
		 * the stats work this is actually checking it first
		 */
		if (RSEQ_READ_ONCE(rs->slice_ctrl.request)) {
			RSEQ_WRITE_ONCE(rs->slice_ctrl.request, 0);
			/* Race between check and clear! */
			req = true;
			success++;
		}

		if (RSEQ_READ_ONCE(rs->slice_ctrl.granted)) {
			/* The above raced against a late grant */
			if (req)
				success--;
			if (variant->no_yield) {
				syscall(__NR_getpid);
				aborted++;
			} else {
				yielded++;
				if (!syscall(__NR_rseq_slice_yield))
					raced++;
			}
		} else {
			if (!req)
				scheduled++;
		}

		clock_gettime(CLOCK_MONOTONIC, &ts_now);
	} while (!elapsed(&ts_start, &ts_now, variant->total_nsecs));

	printf("# Total     %12ld\n", total);
	printf("# Success   %12ld\n", success);
	printf("# Yielded   %12ld\n", yielded);
	printf("# Aborted   %12ld\n", aborted);
	printf("# Scheduled %12ld\n", scheduled);
	printf("# Raced     %12ld\n", raced);
}

TEST_HARNESS_MAIN
