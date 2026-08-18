/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest_harness.h"
#include "../pidfd/pidfd.h"

/*
 * The kernel computes the minimum allowed pid_max as:
 *   max(RESERVED_PIDS + 1, PIDS_PER_CPU_MIN * num_possible_cpus())
 * Mirror that here so the test values are always valid.
 *
 * Note: glibc's get_nprocs_conf() returns the number of *configured*
 * (present) CPUs, not *possible* CPUs.  The kernel uses
 * num_possible_cpus() which corresponds to /sys/devices/system/cpu/possible.
 * These can differ significantly (e.g. 16 configured vs 128 possible).
 */
#define RESERVED_PIDS		300
#define PIDS_PER_CPU_MIN	8

/* Count CPUs from a range list like "0-31" or "0-15,32-47". */
static int num_possible_cpus(void)
{
	FILE *f;
	int count = 0;
	int lo, hi;

	f = fopen("/sys/devices/system/cpu/possible", "r");
	if (!f)
		return 0;

	while (fscanf(f, "%d", &lo) == 1) {
		if (fscanf(f, "-%d", &hi) == 1)
			count += hi - lo + 1;
		else
			count++;
		/* skip comma separator */
		fscanf(f, ",");
	}

	fclose(f);
	return count;
}

static int pid_min(void)
{
	int cpu_min = PIDS_PER_CPU_MIN * num_possible_cpus();

	return cpu_min > (RESERVED_PIDS + 1) ? cpu_min : (RESERVED_PIDS + 1);
}

/*
 * Outer and inner pid_max limits used by the tests.  The outer limit is
 * the more restrictive ancestor; the inner limit is set higher in a
 * nested namespace but must still be capped by the outer limit.
 * Both are derived from the kernel's minimum so they are always writable.
 *
 * Global so that clone callbacks can access them without parameter plumbing.
 */
static int outer_limit;
static int inner_limit;

static int write_int_to_fd(int fd, int val)
{
	char buf[12];
	int len = snprintf(buf, sizeof(buf), "%d", val);

	return write(fd, buf, len);
}

#define __STACK_SIZE (8 * 1024 * 1024)
static pid_t do_clone(int (*fn)(void *), void *arg, int flags)
{
	char *stack;
	pid_t ret;

	stack = malloc(__STACK_SIZE);
	if (!stack)
		return -ENOMEM;

#ifdef __ia64__
	ret = __clone2(fn, stack, __STACK_SIZE, flags | SIGCHLD, arg);
#else
	ret = clone(fn, stack + __STACK_SIZE, flags | SIGCHLD, arg);
#endif
	free(stack);
	return ret;
}

static int pid_max_cb(void *data)
{
	int fd, ret;
	pid_t pid;

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret) {
		fprintf(stderr, "%m - Failed to make rootfs private mount\n");
		return -1;
	}

	umount2("/proc", MNT_DETACH);

	ret = mount("proc", "/proc", "proc", 0, NULL);
	if (ret) {
		fprintf(stderr, "%m - Failed to mount proc\n");
		return -1;
	}

	fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open pid_max\n");
		return -1;
	}

	ret = write_int_to_fd(fd, inner_limit);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return -1;
	}

	for (int i = 0; i < inner_limit + 1; i++) {
		pid = fork();
		if (pid == 0)
			exit(EXIT_SUCCESS);
		wait_for_pid(pid);
		if (pid > inner_limit) {
			fprintf(stderr, "Managed to create pid number beyond limit\n");
			return -1;
		}
	}

	return 0;
}

static int pid_max_nested_inner(void *data)
{
	int fret = -1;
	pid_t pids[2];
	int fd, i, ret;

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret) {
		fprintf(stderr, "%m - Failed to make rootfs private mount\n");
		return fret;
	}

	umount2("/proc", MNT_DETACH);

	ret = mount("proc", "/proc", "proc", 0, NULL);
	if (ret) {
		fprintf(stderr, "%m - Failed to mount proc\n");
		return fret;
	}

	fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open pid_max\n");
		return fret;
	}

	ret = write_int_to_fd(fd, inner_limit);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return fret;
	}

	pids[0] = fork();
	if (pids[0] < 0) {
		fprintf(stderr, "Failed to create first new process\n");
		return fret;
	}

	if (pids[0] == 0)
		exit(EXIT_SUCCESS);

	pids[1] = fork();
	wait_for_pid(pids[0]);
	if (pids[1] >= 0) {
		if (pids[1] == 0)
			exit(EXIT_SUCCESS);
		wait_for_pid(pids[1]);

		fprintf(stderr, "Managed to create process even though ancestor pid namespace had a limit\n");
		return fret;
	}

	/* Now make sure that we wrap pids at outer_limit. */
	for (i = 0; i < inner_limit + 10; i++) {
		pid_t pid;

		pid = fork();
		if (pid < 0)
			return fret;

		if (pid == 0)
			exit(EXIT_SUCCESS);

		wait_for_pid(pid);
		if (pid >= inner_limit) {
			fprintf(stderr, "Managed to create process with pid %d beyond configured limit\n", pid);
			return fret;
		}
	}

	return 0;
}

static int pid_max_nested_outer(void *data)
{
	int fret = -1, nr_procs = 0;
	pid_t *pids;
	int fd, ret;
	pid_t pid;

	pids = malloc(outer_limit * sizeof(pid_t));
	if (!pids)
		return -1;

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret) {
		fprintf(stderr, "%m - Failed to make rootfs private mount\n");
		goto out;
	}

	umount2("/proc", MNT_DETACH);

	ret = mount("proc", "/proc", "proc", 0, NULL);
	if (ret) {
		fprintf(stderr, "%m - Failed to mount proc\n");
		goto out;
	}

	fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open pid_max\n");
		goto out;
	}

	ret = write_int_to_fd(fd, outer_limit);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		goto out;
	}

	/*
	 * Create (outer_limit - 4) processes. This leaves room for
	 * do_clone() and one more. So creating another process needs
	 * to fail.
	 */
	for (nr_procs = 0; nr_procs < outer_limit - 4; nr_procs++) {
		pid = fork();
		if (pid < 0)
			goto reap;

		if (pid == 0)
			exit(EXIT_SUCCESS);

		pids[nr_procs] = pid;
	}

	pid = do_clone(pid_max_nested_inner, NULL, CLONE_NEWPID | CLONE_NEWNS);
	if (pid < 0) {
		fprintf(stderr, "%m - Failed to clone nested pidns\n");
		goto reap;
	}

	if (wait_for_pid(pid)) {
		fprintf(stderr, "%m - Nested pid_max failed\n");
		goto reap;
	}

	fret = 0;

reap:
	for (int i = 0; i < nr_procs; i++)
		wait_for_pid(pids[i]);

out:
	free(pids);
	return fret;
}

static int pid_max_nested_limit_inner(void *data)
{
	int fret = -1, nr_procs = 0;
	int fd, ret;
	pid_t pid;
	pid_t *pids;

	pids = malloc(inner_limit * sizeof(pid_t));
	if (!pids)
		return -1;

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret) {
		fprintf(stderr, "%m - Failed to make rootfs private mount\n");
		goto out;
	}

	umount2("/proc", MNT_DETACH);

	ret = mount("proc", "/proc", "proc", 0, NULL);
	if (ret) {
		fprintf(stderr, "%m - Failed to mount proc\n");
		goto out;
	}

	fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open pid_max\n");
		goto out;
	}

	ret = write_int_to_fd(fd, inner_limit);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		goto out;
	}

	for (nr_procs = 0; nr_procs < inner_limit; nr_procs++) {
		pid = fork();
		if (pid < 0)
			break;

		if (pid == 0)
			exit(EXIT_SUCCESS);

		pids[nr_procs] = pid;
	}

	if (nr_procs >= outer_limit) {
		fprintf(stderr, "Managed to create processes beyond the configured outer limit\n");
		goto reap;
	}

	fret = 0;

reap:
	for (int i = 0; i < nr_procs; i++)
		wait_for_pid(pids[i]);

out:
	free(pids);
	return fret;
}

static int pid_max_nested_limit_outer(void *data)
{
	int fd, ret;
	pid_t pid;

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret) {
		fprintf(stderr, "%m - Failed to make rootfs private mount\n");
		return -1;
	}

	umount2("/proc", MNT_DETACH);

	ret = mount("proc", "/proc", "proc", 0, NULL);
	if (ret) {
		fprintf(stderr, "%m - Failed to mount proc\n");
		return -1;
	}

	fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open pid_max\n");
		return -1;
	}

	ret = write_int_to_fd(fd, outer_limit);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return -1;
	}

	pid = do_clone(pid_max_nested_limit_inner, NULL, CLONE_NEWPID | CLONE_NEWNS);
	if (pid < 0) {
		fprintf(stderr, "%m - Failed to clone nested pidns\n");
		return -1;
	}

	if (wait_for_pid(pid)) {
		fprintf(stderr, "%m - Nested pid_max failed\n");
		return -1;
	}

	return 0;
}

FIXTURE(pid_max) {
	int dummy;
};

FIXTURE_SETUP(pid_max)
{
	int min = pid_min();

	outer_limit = min + 100;
	inner_limit = min + 200;
}

FIXTURE_TEARDOWN(pid_max)
{
}

TEST_F(pid_max, simple)
{
	pid_t pid;

	pid = do_clone(pid_max_cb, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST_F(pid_max, nested_limit)
{
	pid_t pid;

	pid = do_clone(pid_max_nested_limit_outer, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST_F(pid_max, nested)
{
	pid_t pid;

	pid = do_clone(pid_max_nested_outer, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST_HARNESS_MAIN
