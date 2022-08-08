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
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/sysinfo.h>
#include <asm/ptrace.h>
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

int nprocs;

static volatile int a = 10;
static volatile int b = 10;
static volatile char c[512 + 8] __attribute__((aligned(512)));

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

static int perf_cpu_event_open(long cpu, __u32 type, __u64 addr, __u64 len)
{
	struct perf_event_attr attr;

	perf_event_attr_set(&attr, type, addr, len, 0);
	return syscall(__NR_perf_event_open, &attr, -1, cpu, -1, 0);
}

static void close_fds(int *fd, int n)
{
	int i;

	for (i = 0; i < n; i++)
		close(fd[i]);
}

static unsigned long read_fds(int *fd, int n)
{
	int i;
	unsigned long c = 0;
	unsigned long count = 0;
	size_t res;

	for (i = 0; i < n; i++) {
		res = read(fd[i], &c, sizeof(c));
		assert(res == sizeof(unsigned long long));
		count += c;
	}
	return count;
}

static void reset_fds(int *fd, int n)
{
	int i;

	for (i = 0; i < n; i++)
		ioctl(fd[i], PERF_EVENT_IOC_RESET);
}

static void enable_fds(int *fd, int n)
{
	int i;

	for (i = 0; i < n; i++)
		ioctl(fd[i], PERF_EVENT_IOC_ENABLE);
}

static void disable_fds(int *fd, int n)
{
	int i;

	for (i = 0; i < n; i++)
		ioctl(fd[i], PERF_EVENT_IOC_DISABLE);
}

static int perf_systemwide_event_open(int *fd, __u32 type, __u64 addr, __u64 len)
{
	int i = 0;

	/* Assume online processors are 0 to nprocs for simplisity */
	for (i = 0; i < nprocs; i++) {
		fd[i] = perf_cpu_event_open(i, type, addr, len);
		if (fd[i] < 0) {
			close_fds(fd, i);
			return fd[i];
		}
	}
	return 0;
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

static void multi_dawr_workload(void)
{
	a += 10;
	b += 10;
	c[512 + 1] += 'a';
}

static int test_process_multi_diff_addr(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int fd1, fd2;
	char *desc = "Process specific, Two events, diff addr";
	size_t res;

	fd1 = perf_process_event_open(HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (fd1 < 0) {
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	fd2 = perf_process_event_open(HW_BREAKPOINT_RW, (__u64)&b, (__u64)sizeof(b));
	if (fd2 < 0) {
		close(fd1);
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	ioctl(fd1, PERF_EVENT_IOC_RESET);
	ioctl(fd2, PERF_EVENT_IOC_RESET);
	ioctl(fd1, PERF_EVENT_IOC_ENABLE);
	ioctl(fd2, PERF_EVENT_IOC_ENABLE);
	multi_dawr_workload();
	ioctl(fd1, PERF_EVENT_IOC_DISABLE);
	ioctl(fd2, PERF_EVENT_IOC_DISABLE);

	res = read(fd1, &breaks1, sizeof(breaks1));
	assert(res == sizeof(unsigned long long));
	res = read(fd2, &breaks2, sizeof(breaks2));
	assert(res == sizeof(unsigned long long));

	close(fd1);
	close(fd2);

	if (breaks1 != 2 || breaks2 != 2) {
		printf("FAILED: %s: %lld != 2 || %lld != 2\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_process_multi_same_addr(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int fd1, fd2;
	char *desc = "Process specific, Two events, same addr";
	size_t res;

	fd1 = perf_process_event_open(HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (fd1 < 0) {
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	fd2 = perf_process_event_open(HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (fd2 < 0) {
		close(fd1);
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	ioctl(fd1, PERF_EVENT_IOC_RESET);
	ioctl(fd2, PERF_EVENT_IOC_RESET);
	ioctl(fd1, PERF_EVENT_IOC_ENABLE);
	ioctl(fd2, PERF_EVENT_IOC_ENABLE);
	multi_dawr_workload();
	ioctl(fd1, PERF_EVENT_IOC_DISABLE);
	ioctl(fd2, PERF_EVENT_IOC_DISABLE);

	res = read(fd1, &breaks1, sizeof(breaks1));
	assert(res == sizeof(unsigned long long));
	res = read(fd2, &breaks2, sizeof(breaks2));
	assert(res == sizeof(unsigned long long));

	close(fd1);
	close(fd2);

	if (breaks1 != 2 || breaks2 != 2) {
		printf("FAILED: %s: %lld != 2 || %lld != 2\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_process_multi_diff_addr_ro_wo(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int fd1, fd2;
	char *desc = "Process specific, Two events, diff addr, one is RO, other is WO";
	size_t res;

	fd1 = perf_process_event_open(HW_BREAKPOINT_W, (__u64)&a, (__u64)sizeof(a));
	if (fd1 < 0) {
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	fd2 = perf_process_event_open(HW_BREAKPOINT_R, (__u64)&b, (__u64)sizeof(b));
	if (fd2 < 0) {
		close(fd1);
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	ioctl(fd1, PERF_EVENT_IOC_RESET);
	ioctl(fd2, PERF_EVENT_IOC_RESET);
	ioctl(fd1, PERF_EVENT_IOC_ENABLE);
	ioctl(fd2, PERF_EVENT_IOC_ENABLE);
	multi_dawr_workload();
	ioctl(fd1, PERF_EVENT_IOC_DISABLE);
	ioctl(fd2, PERF_EVENT_IOC_DISABLE);

	res = read(fd1, &breaks1, sizeof(breaks1));
	assert(res == sizeof(unsigned long long));
	res = read(fd2, &breaks2, sizeof(breaks2));
	assert(res == sizeof(unsigned long long));

	close(fd1);
	close(fd2);

	if (breaks1 != 1 || breaks2 != 1) {
		printf("FAILED: %s: %lld != 1 || %lld != 1\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_process_multi_same_addr_ro_wo(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int fd1, fd2;
	char *desc = "Process specific, Two events, same addr, one is RO, other is WO";
	size_t res;

	fd1 = perf_process_event_open(HW_BREAKPOINT_R, (__u64)&a, (__u64)sizeof(a));
	if (fd1 < 0) {
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	fd2 = perf_process_event_open(HW_BREAKPOINT_W, (__u64)&a, (__u64)sizeof(a));
	if (fd2 < 0) {
		close(fd1);
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	ioctl(fd1, PERF_EVENT_IOC_RESET);
	ioctl(fd2, PERF_EVENT_IOC_RESET);
	ioctl(fd1, PERF_EVENT_IOC_ENABLE);
	ioctl(fd2, PERF_EVENT_IOC_ENABLE);
	multi_dawr_workload();
	ioctl(fd1, PERF_EVENT_IOC_DISABLE);
	ioctl(fd2, PERF_EVENT_IOC_DISABLE);

	res = read(fd1, &breaks1, sizeof(breaks1));
	assert(res == sizeof(unsigned long long));
	res = read(fd2, &breaks2, sizeof(breaks2));
	assert(res == sizeof(unsigned long long));

	close(fd1);
	close(fd2);

	if (breaks1 != 1 || breaks2 != 1) {
		printf("FAILED: %s: %lld != 1 || %lld != 1\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_syswide_multi_diff_addr(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int *fd1 = malloc(nprocs * sizeof(int));
	int *fd2 = malloc(nprocs * sizeof(int));
	char *desc = "Systemwide, Two events, diff addr";
	int ret;

	ret = perf_systemwide_event_open(fd1, HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	ret = perf_systemwide_event_open(fd2, HW_BREAKPOINT_RW, (__u64)&b, (__u64)sizeof(b));
	if (ret) {
		close_fds(fd1, nprocs);
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	reset_fds(fd1, nprocs);
	reset_fds(fd2, nprocs);
	enable_fds(fd1, nprocs);
	enable_fds(fd2, nprocs);
	multi_dawr_workload();
	disable_fds(fd1, nprocs);
	disable_fds(fd2, nprocs);

	breaks1 = read_fds(fd1, nprocs);
	breaks2 = read_fds(fd2, nprocs);

	close_fds(fd1, nprocs);
	close_fds(fd2, nprocs);

	free(fd1);
	free(fd2);

	if (breaks1 != 2 || breaks2 != 2) {
		printf("FAILED: %s: %lld != 2 || %lld != 2\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_syswide_multi_same_addr(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int *fd1 = malloc(nprocs * sizeof(int));
	int *fd2 = malloc(nprocs * sizeof(int));
	char *desc = "Systemwide, Two events, same addr";
	int ret;

	ret = perf_systemwide_event_open(fd1, HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	ret = perf_systemwide_event_open(fd2, HW_BREAKPOINT_RW, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		close_fds(fd1, nprocs);
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	reset_fds(fd1, nprocs);
	reset_fds(fd2, nprocs);
	enable_fds(fd1, nprocs);
	enable_fds(fd2, nprocs);
	multi_dawr_workload();
	disable_fds(fd1, nprocs);
	disable_fds(fd2, nprocs);

	breaks1 = read_fds(fd1, nprocs);
	breaks2 = read_fds(fd2, nprocs);

	close_fds(fd1, nprocs);
	close_fds(fd2, nprocs);

	free(fd1);
	free(fd2);

	if (breaks1 != 2 || breaks2 != 2) {
		printf("FAILED: %s: %lld != 2 || %lld != 2\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_syswide_multi_diff_addr_ro_wo(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int *fd1 = malloc(nprocs * sizeof(int));
	int *fd2 = malloc(nprocs * sizeof(int));
	char *desc = "Systemwide, Two events, diff addr, one is RO, other is WO";
	int ret;

	ret = perf_systemwide_event_open(fd1, HW_BREAKPOINT_W, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	ret = perf_systemwide_event_open(fd2, HW_BREAKPOINT_R, (__u64)&b, (__u64)sizeof(b));
	if (ret) {
		close_fds(fd1, nprocs);
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	reset_fds(fd1, nprocs);
	reset_fds(fd2, nprocs);
	enable_fds(fd1, nprocs);
	enable_fds(fd2, nprocs);
	multi_dawr_workload();
	disable_fds(fd1, nprocs);
	disable_fds(fd2, nprocs);

	breaks1 = read_fds(fd1, nprocs);
	breaks2 = read_fds(fd2, nprocs);

	close_fds(fd1, nprocs);
	close_fds(fd2, nprocs);

	free(fd1);
	free(fd2);

	if (breaks1 != 1 || breaks2 != 1) {
		printf("FAILED: %s: %lld != 1 || %lld != 1\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int test_syswide_multi_same_addr_ro_wo(void)
{
	unsigned long long breaks1 = 0, breaks2 = 0;
	int *fd1 = malloc(nprocs * sizeof(int));
	int *fd2 = malloc(nprocs * sizeof(int));
	char *desc = "Systemwide, Two events, same addr, one is RO, other is WO";
	int ret;

	ret = perf_systemwide_event_open(fd1, HW_BREAKPOINT_W, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	ret = perf_systemwide_event_open(fd2, HW_BREAKPOINT_R, (__u64)&a, (__u64)sizeof(a));
	if (ret) {
		close_fds(fd1, nprocs);
		perror("perf_systemwide_event_open");
		exit(EXIT_FAILURE);
	}

	reset_fds(fd1, nprocs);
	reset_fds(fd2, nprocs);
	enable_fds(fd1, nprocs);
	enable_fds(fd2, nprocs);
	multi_dawr_workload();
	disable_fds(fd1, nprocs);
	disable_fds(fd2, nprocs);

	breaks1 = read_fds(fd1, nprocs);
	breaks2 = read_fds(fd2, nprocs);

	close_fds(fd1, nprocs);
	close_fds(fd2, nprocs);

	free(fd1);
	free(fd2);

	if (breaks1 != 1 || breaks2 != 1) {
		printf("FAILED: %s: %lld != 1 || %lld != 1\n", desc, breaks1, breaks2);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

static int runtest_multi_dawr(void)
{
	int ret = 0;

	ret |= test_process_multi_diff_addr();
	ret |= test_process_multi_same_addr();
	ret |= test_process_multi_diff_addr_ro_wo();
	ret |= test_process_multi_same_addr_ro_wo();
	ret |= test_syswide_multi_diff_addr();
	ret |= test_syswide_multi_same_addr();
	ret |= test_syswide_multi_diff_addr_ro_wo();
	ret |= test_syswide_multi_same_addr_ro_wo();

	return ret;
}

static int runtest_unaligned_512bytes(void)
{
	unsigned long long breaks = 0;
	int fd;
	char *desc = "Process specific, 512 bytes, unaligned";
	__u64 addr = (__u64)&c + 8;
	size_t res;

	fd = perf_process_event_open(HW_BREAKPOINT_RW, addr, 512);
	if (fd < 0) {
		perror("perf_process_event_open");
		exit(EXIT_FAILURE);
	}

	ioctl(fd, PERF_EVENT_IOC_RESET);
	ioctl(fd, PERF_EVENT_IOC_ENABLE);
	multi_dawr_workload();
	ioctl(fd, PERF_EVENT_IOC_DISABLE);

	res = read(fd, &breaks, sizeof(breaks));
	assert(res == sizeof(unsigned long long));

	close(fd);

	if (breaks != 2) {
		printf("FAILED: %s: %lld != 2\n", desc, breaks);
		return 1;
	}

	printf("TESTED: %s\n", desc);
	return 0;
}

/* There is no perf api to find number of available watchpoints. Use ptrace. */
static int get_nr_wps(bool *arch_31)
{
	struct ppc_debug_info dbginfo;
	int child_pid;

	child_pid = fork();
	if (!child_pid) {
		int ret = ptrace(PTRACE_TRACEME, 0, NULL, 0);
		if (ret) {
			perror("PTRACE_TRACEME failed\n");
			exit(EXIT_FAILURE);
		}
		kill(getpid(), SIGUSR1);

		sleep(1);
		exit(EXIT_SUCCESS);
	}

	wait(NULL);
	if (ptrace(PPC_PTRACE_GETHWDBGINFO, child_pid, NULL, &dbginfo)) {
		perror("Can't get breakpoint info");
		exit(EXIT_FAILURE);
	}

	*arch_31 = !!(dbginfo.features & PPC_DEBUG_FEATURE_DATA_BP_ARCH_31);
	return dbginfo.num_data_bps;
}

static int runtest(void)
{
	int rwflag;
	int exclude_user;
	int ret;
	bool dawr = dawr_supported();
	bool arch_31 = false;
	int nr_wps = get_nr_wps(&arch_31);

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
			if (!dawr)
				continue;
			ret = runtestsingle(rwflag, exclude_user, 1);
			if (ret)
				return ret;
		}
	}

	ret = runtest_dar_outside();
	if (ret)
		return ret;

	if (dawr && nr_wps > 1) {
		nprocs = get_nprocs();
		ret = runtest_multi_dawr();
		if (ret)
			return ret;
	}

	if (dawr && arch_31)
		ret = runtest_unaligned_512bytes();

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
