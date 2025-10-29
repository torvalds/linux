// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/nsfs.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"

#ifndef FD_NSFS_ROOT
#define FD_NSFS_ROOT -10003 /* Root of the nsfs filesystem */
#endif

/*
 * Test that initial namespaces can be reopened via file handle.
 * Initial namespaces should have active ref count of 1 from boot.
 */
TEST(init_ns_always_active)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd1, fd2;
	struct stat st1, st2;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open initial network namespace */
	fd1 = open("/proc/1/ns/net", O_RDONLY);
	ASSERT_GE(fd1, 0);

	/* Get file handle for initial namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(fd1, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(fd1);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);

	/* Close the namespace fd */
	close(fd1);

	/* Try to reopen via file handle - should succeed since init ns is always active */
	fd2 = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd2 < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle);
		     return, "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd2, 0);

	/* Verify we opened the same namespace */
	fd1 = open("/proc/1/ns/net", O_RDONLY);
	ASSERT_GE(fd1, 0);
	ASSERT_EQ(fstat(fd1, &st1), 0);
	ASSERT_EQ(fstat(fd2, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);

	close(fd1);
	close(fd2);
	free(handle);
}

/*
 * Test namespace lifecycle: create a namespace in a child process,
 * get a file handle while it's active, then try to reopen after
 * the process exits (namespace becomes inactive).
 */
TEST(ns_inactive_after_exit)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int pipefd[2];
	pid_t pid;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];

	/* Create pipe for passing file handle from child */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Open our new namespace */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get file handle for the namespace */
		handle = (struct file_handle *)buf;
		handle->handle_bytes = MAX_HANDLE_SZ;
		ret = name_to_handle_at(fd, "", handle, &mount_id, AT_EMPTY_PATH);
		close(fd);

		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Send handle to parent */
		write(pipefd[1], buf, sizeof(*handle) + handle->handle_bytes);
		close(pipefd[1]);

		/* Exit - namespace should become inactive */
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	/* Read file handle from child */
	ret = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);

	/* Wait for child to exit */
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_GT(ret, 0);
	handle = (struct file_handle *)buf;

	/* Try to reopen namespace - should fail with ENOENT since it's inactive */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(fd, 0);
	/* Should fail with ENOENT (namespace inactive) or ESTALE */
	ASSERT_TRUE(errno == ENOENT || errno == ESTALE);
}

TEST_HARNESS_MAIN
