// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * perf events self profiling example test case for hw breakpoints.
 *
 * This tests perf PERF_TYPE_BREAKPOINT parameters
 * 1) tests all variants of the break on read/write flags
 * 2) tests exclude_user == 0 and 1
 * 3) test array matches (if DAWR is supported))
 * 4) test different numbers of breakpoints matches
 *
 * Configure this breakpoint, then read and write the data a number of
 * times. Then check the output count from perf is as expected.
 *
 * Based on:
 *   http://ozlabs.org/~anton/junkcode/perf_events_example1.c
 *
 * Copyright (C) 2018 Michael Neuling, IBM Corporation.
 */

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <elf.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include "utils.h"

#ifndef PPC_DEBUG_FEATURE_DATA_BP_ARCH_31
#define PPC_DEBUG_FEATURE_DATA_BP_ARCH_31	0x20
#endif

#define MAX_LOOPS 10000

#define DAWR_LENGTH_MAX ((0x3f + 1) * 8)

static void perf_event_attr_set(struct perf_event_attr *attr,
				__u32 type, __u64 addr, __u64 len,
				bool exclude_user)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type           = PERF_TYPE_BREAKPOINT;
	attr->size           = sizeof(struct perf_event_attr);
	attr->bp_type        = type;
	attr->bp_addr        = addr;
	attr->bp_len         = len;
	attr->exclude_kernel = 1;
	attr->exclude_hv     = 1;
	attr->exclude_guest  = 1;
	attr->exclude_user   = exclude_user;
	attr->disabled       = 1;
}

static int
perf_process_event_open_exclude_user(__u32 type, __u64 addr, __u64 len, bool exclude_user)
{
	struct perf_event_attr attr;

	perf_event_attr_set(&attr, type, addr, len, exclude_user);
	return syscall(__NR_perf_event_open, &attr, getpid(), -1, -1, 0);
}

static int perf_process_event_open(__u32 type, __u64 addr, __u64 len)
{
	struct perf_event_attr attr;

	perf_event_attr_set(&attr, type, addr, len, 0);
	return syscall(__NR_perf_event_open, &attr, getpid(), -1, -1, 0);
}

static inline bool breakpoint_test(int len)
{
	int fd;

	/* bp_addr can point anywhere but needs to be aligned */
	fd = perf_process_event_open(HW_BREAKPOINT_R, (__u64)(&fd) & 0xfffffffffffff800, len);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

static inline bool perf_breakpoint_supported(void)
{
	return breakpoint_test(4);
}

static inline bool dawr_supported(void)
{
	return breakpoint_test(DAWR_LENGTH_MAX);
}

static int runtestsingle(int readwriteflag, int exclude_user, int arraytest)
{
	int i,j;
	size_t res;
	unsigned long long breaks, needed;
	int readint;
	int readintarraybig[2*DAWR_LENGTH_MAX/sizeof(int)];
	int *readintalign;
	volatile int *ptr;
	int break_fd;
	int loop_num = MAX_LOOPS - (rand() % 100); /* provide some variability */
	volatile int *k;
	__u64 len;

	/* align to 0x400 boundary as required by DAWR */
	readintalign = (int *)(((unsigned long)readintarraybig + 0x7ff) &
			       0xfffffffffffff800);

	ptr = &readint;
	if (arraytest)
		ptr = &readintalign[0];

	len = arraytest ? DAWR_LENGTH_MAX : sizeof(int);
	break_fd = perf_process_event_open_exclude_user(readwriteflag, (__u64)ptr,
							len, exclude_user);
	if (break_fd < 0) {
		perror("perf_process_event_open_exclude_user");
		exit(1);
	}

	/* start counters */
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);

	/* Test a bunch of reads and writes */
	k = &readint;
	for (i = 0; i < loop_num; i++) {
		if (arraytest)
			k = &(readintalign[i % (DAWR_LENGTH_MAX/sizeof(int))]);

		j = *k;
		*k = j;
	}

	/* stop counters */
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);

	/* read and check counters */
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	/* we read and write each loop, so subtract the ones we are counting */
	needed = 0;
	if (readwriteflag & HW_BREAKPOINT_R)
		needed += loop_num;
	if (readwriteflag & HW_BREAKPOINT_W)
		needed += loop_num;
	needed = needed * (1 - exclude_user);
	printf("TESTED: addr:0x%lx brks:% 8lld loops:% 8i rw:%i !user:%i array:%i\n",
	       (unsigned long int)ptr, breaks, loop_num, readwriteflag, exclude_user, arraytest);
	if (breaks != needed) {
		printf("FAILED: 0x%lx brks:%lld needed:%lli %i %i %i\n\n",
		       (unsigned long int)ptr, breaks, needed, loop_num, readwriteflag, exclude_user);
		return 1;
	}
	close(break_fd);

	return 0;
}

static int runtest_dar_outside(void)
{
	void *target;
	volatile __u16 temp16;
	volatile __u64 temp64;
	int break_fd;
	unsigned long long breaks;
	int fail = 0;
	size_t res;

	target = malloc(8);
	if (!target) {
		perror("malloc failed");
		exit(EXIT_FAILURE);
	}

	/* watch middle half of target array */
	break_fd = perf_process_event_open(HW_BREAKPOINT_RW, (__u64)(target + 2), 4);
	if (break_fd < 0) {
		free(target);
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	/* Shouldn't hit. */
	ioctl(break_fd, PERF_EVENT_IOC_RESET);
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);
	temp16 = *((__u16 *)target);
	*((__u16 *)target) = temp16;
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	if (breaks == 0) {
		printf("TESTED: No overlap\n");
	} else {
		printf("FAILED: No overlap: %lld != 0\n", breaks);
		fail = 1;
	}

	/* Hit */
	ioctl(break_fd, PERF_EVENT_IOC_RESET);
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);
	temp16 = *((__u16 *)(target + 1));
	*((__u16 *)(target + 1)) = temp16;
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	if (breaks == 2) {
		printf("TESTED: Partial overlap\n");
	} else {
		printf("FAILED: Partial overlap: %lld != 2\n", breaks);
		fail = 1;
	}

	/* Hit */
	ioctl(break_fd, PERF_EVENT_IOC_RESET);
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);
	temp16 = *((__u16 *)(target + 5));
	*((__u16 *)(target + 5)) = temp16;
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	if (breaks == 2) {
		printf("TESTED: Partial overlap\n");
	} else {
		printf("FAILED: Partial overlap: %lld != 2\n", breaks);
		fail = 1;
	}

	/* Shouldn't Hit */
	ioctl(break_fd, PERF_EVENT_IOC_RESET);
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);
	temp16 = *((__u16 *)(target + 6));
	*((__u16 *)(target + 6)) = temp16;
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	if (breaks == 0) {
		printf("TESTED: No overlap\n");
	} else {
		printf("FAILED: No overlap: %lld != 0\n", breaks);
		fail = 1;
	}

	/* Hit */
	ioctl(break_fd, PERF_EVENT_IOC_RESET);
	ioctl(break_fd, PERF_EVENT_IOC_ENABLE);
	temp64 = *((__u64 *)target);
	*((__u64 *)target) = temp64;
	ioctl(break_fd, PERF_EVENT_IOC_DISABLE);
	res = read(break_fd, &breaks, sizeof(unsigned long long));
	assert(res == sizeof(unsigned long long));
	if (breaks == 2) {
		printf("TESTED: Full overlap\n");
	} else {
		printf("FAILED: Full overlap: %lld != 2\n", breaks);
		fail = 1;
	}

	free(target);
	close(break_fd);
	return fail;
}

static int runtest(void)
{
	int rwflag;
	int exclude_user;
	int ret;

	/*
	 * perf defines rwflag as two bits read and write and at least
	 * one must be set.  So range 1-3.
	 */
	for (rwflag = 1 ; rwflag < 4; rwflag++) {
		for (exclude_user = 0 ; exclude_user < 2; exclude_user++) {
			ret = runtestsingle(rwflag, exclude_user, 0);
			if (ret)
				return ret;

			/* if we have the dawr, we can do an array test */
			if (!dawr_supported())
				continue;
			ret = runtestsingle(rwflag, exclude_user, 1);
			if (ret)
				return ret;
		}
	}

	ret = runtest_dar_outside();
	return ret;
}


static int perf_hwbreak(void)
{
	srand ( time(NULL) );

	SKIP_IF(!perf_breakpoint_supported());

	return runtest();
}

int main(int argc, char *argv[], char **envp)
{
	return test_harness(perf_hwbreak, "perf_hwbreak");
}
