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

#include "../kselftest_harness.h"
#include "../pidfd/pidfd.h"

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

	ret = write(fd, "500", sizeof("500") - 1);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return -1;
	}

	for (int i = 0; i < 501; i++) {
		pid = fork();
		if (pid == 0)
			exit(EXIT_SUCCESS);
		wait_for_pid(pid);
		if (pid > 500) {
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

	ret = write(fd, "500", sizeof("500") - 1);
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

	/* Now make sure that we wrap pids at 400. */
	for (i = 0; i < 510; i++) {
		pid_t pid;

		pid = fork();
		if (pid < 0)
			return fret;

		if (pid == 0)
			exit(EXIT_SUCCESS);

		wait_for_pid(pid);
		if (pid >= 500) {
			fprintf(stderr, "Managed to create process with pid %d beyond configured limit\n", pid);
			return fret;
		}
	}

	return 0;
}

static int pid_max_nested_outer(void *data)
{
	int fret = -1, nr_procs = 400;
	pid_t pids[1000];
	int fd, i, ret;
	pid_t pid;

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

	ret = write(fd, "400", sizeof("400") - 1);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return fret;
	}

	/*
	 * Create 397 processes. This leaves room for do_clone() (398) and
	 * one more 399. So creating another process needs to fail.
	 */
	for (nr_procs = 0; nr_procs < 396; nr_procs++) {
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

	return fret;
}

static int pid_max_nested_limit_inner(void *data)
{
	int fret = -1, nr_procs = 400;
	int fd, ret;
	pid_t pid;
	pid_t pids[1000];

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

	ret = write(fd, "500", sizeof("500") - 1);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to write pid_max\n");
		return fret;
	}

	for (nr_procs = 0; nr_procs < 500; nr_procs++) {
		pid = fork();
		if (pid < 0)
			break;

		if (pid == 0)
			exit(EXIT_SUCCESS);

		pids[nr_procs] = pid;
	}

	if (nr_procs >= 400) {
		fprintf(stderr, "Managed to create processes beyond the configured outer limit\n");
		goto reap;
	}

	fret = 0;

reap:
	for (int i = 0; i < nr_procs; i++)
		wait_for_pid(pids[i]);

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

	ret = write(fd, "400", sizeof("400") - 1);
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

TEST(pid_max_simple)
{
	pid_t pid;


	pid = do_clone(pid_max_cb, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST(pid_max_nested_limit)
{
	pid_t pid;

	pid = do_clone(pid_max_nested_limit_outer, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST(pid_max_nested)
{
	pid_t pid;

	pid = do_clone(pid_max_nested_outer, NULL, CLONE_NEWPID | CLONE_NEWNS);
	ASSERT_GT(pid, 0);
	ASSERT_EQ(0, wait_for_pid(pid));
}

TEST_HARNESS_MAIN
