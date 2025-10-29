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

/*
 * Test that a namespace remains active while a process is using it,
 * even after the creating process exits.
 */
TEST(ns_active_with_multiple_processes)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int pipefd[2];
	int syncpipe[2];
	pid_t pid1, pid2;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];
	char sync_byte;

	/* Create pipes for communication */
	ASSERT_EQ(pipe(pipefd), 0);
	ASSERT_EQ(pipe(syncpipe), 0);

	pid1 = fork();
	ASSERT_GE(pid1, 0);

	if (pid1 == 0) {
		/* First child - creates namespace */
		close(pipefd[0]);
		close(syncpipe[1]);

		/* Create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			close(pipefd[1]);
			close(syncpipe[0]);
			exit(1);
		}

		/* Open and get handle */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			close(syncpipe[0]);
			exit(1);
		}

		handle = (struct file_handle *)buf;
		handle->handle_bytes = MAX_HANDLE_SZ;
		ret = name_to_handle_at(fd, "", handle, &mount_id, AT_EMPTY_PATH);
		close(fd);

		if (ret < 0) {
			close(pipefd[1]);
			close(syncpipe[0]);
			exit(1);
		}

		/* Send handle to parent */
		write(pipefd[1], buf, sizeof(*handle) + handle->handle_bytes);
		close(pipefd[1]);

		/* Wait for signal before exiting */
		read(syncpipe[0], &sync_byte, 1);
		close(syncpipe[0]);
		exit(0);
	}

	/* Parent reads handle */
	close(pipefd[1]);
	ret = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);
	ASSERT_GT(ret, 0);

	handle = (struct file_handle *)buf;

	/* Create second child that will keep namespace active */
	pid2 = fork();
	ASSERT_GE(pid2, 0);

	if (pid2 == 0) {
		/* Second child - reopens the namespace */
		close(syncpipe[0]);
		close(syncpipe[1]);

		/* Open the namespace via handle */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
		if (fd < 0) {
			exit(1);
		}

		/* Join the namespace */
		ret = setns(fd, CLONE_NEWNET);
		close(fd);
		if (ret < 0) {
			exit(1);
		}

		/* Sleep to keep namespace active */
		sleep(1);
		exit(0);
	}

	/* Let second child enter the namespace */
	usleep(100000); /* 100ms */

	/* Signal first child to exit */
	close(syncpipe[0]);
	sync_byte = 'X';
	write(syncpipe[1], &sync_byte, 1);
	close(syncpipe[1]);

	/* Wait for first child */
	waitpid(pid1, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));

	/* Namespace should still be active because second child is using it */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_GE(fd, 0);
	close(fd);

	/* Wait for second child */
	waitpid(pid2, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
}

/*
 * Test user namespace active ref tracking via credential lifecycle
 */
TEST(userns_active_ref_lifecycle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int pipefd[2];
	pid_t pid;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new user namespace */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Set up uid/gid mappings */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd >= 0 && gid_map_fd >= 0 && setgroups_fd >= 0) {
			write(setgroups_fd, "deny", 4);
			close(setgroups_fd);

			char mapping[64];
			snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
			write(uid_map_fd, mapping, strlen(mapping));
			close(uid_map_fd);

			snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
			write(gid_map_fd, mapping, strlen(mapping));
			close(gid_map_fd);
		}

		/* Get file handle */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

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
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);
	ret = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_GT(ret, 0);
	handle = (struct file_handle *)buf;

	/* Namespace should be inactive after all tasks exit */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(fd, 0);
	ASSERT_TRUE(errno == ENOENT || errno == ESTALE);
}

/*
 * Test PID namespace active ref tracking
 */
TEST(pidns_active_ref_lifecycle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int pipefd[2];
	pid_t pid;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new PID namespace */
		ret = unshare(CLONE_NEWPID);
		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Fork to actually enter the PID namespace */
		pid_t child = fork();
		if (child < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (child == 0) {
			/* Grandchild - in new PID namespace */
			fd = open("/proc/self/ns/pid", O_RDONLY);
			if (fd < 0) {
				exit(1);
			}

			handle = (struct file_handle *)buf;
			handle->handle_bytes = MAX_HANDLE_SZ;
			ret = name_to_handle_at(fd, "", handle, &mount_id, AT_EMPTY_PATH);
			close(fd);

			if (ret < 0) {
				exit(1);
			}

			/* Send handle to grandparent */
			write(pipefd[1], buf, sizeof(*handle) + handle->handle_bytes);
			close(pipefd[1]);
			exit(0);
		}

		/* Wait for grandchild */
		waitpid(child, NULL, 0);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);
	ret = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_GT(ret, 0);
	handle = (struct file_handle *)buf;

	/* Namespace should be inactive after all processes exit */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(fd, 0);
	ASSERT_TRUE(errno == ENOENT || errno == ESTALE);
}

/*
 * Test that an open file descriptor keeps a namespace active.
 * Even after the creating process exits, the namespace should remain
 * active as long as an fd is held open.
 */
TEST(ns_fd_keeps_active)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int nsfd;
	int pipe_child_ready[2];
	int pipe_parent_ready[2];
	pid_t pid;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];
	char sync_byte;
	char proc_path[64];

	ASSERT_EQ(pipe(pipe_child_ready), 0);
	ASSERT_EQ(pipe(pipe_parent_ready), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipe_child_ready[0]);
		close(pipe_parent_ready[1]);

		TH_LOG("Child: creating new network namespace");

		/* Create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			TH_LOG("Child: unshare(CLONE_NEWNET) failed: %s", strerror(errno));
			close(pipe_child_ready[1]);
			close(pipe_parent_ready[0]);
			exit(1);
		}

		TH_LOG("Child: network namespace created successfully");

		/* Get file handle for the namespace */
		nsfd = open("/proc/self/ns/net", O_RDONLY);
		if (nsfd < 0) {
			TH_LOG("Child: failed to open /proc/self/ns/net: %s", strerror(errno));
			close(pipe_child_ready[1]);
			close(pipe_parent_ready[0]);
			exit(1);
		}

		TH_LOG("Child: opened namespace fd %d", nsfd);

		handle = (struct file_handle *)buf;
		handle->handle_bytes = MAX_HANDLE_SZ;
		ret = name_to_handle_at(nsfd, "", handle, &mount_id, AT_EMPTY_PATH);
		close(nsfd);

		if (ret < 0) {
			TH_LOG("Child: name_to_handle_at failed: %s", strerror(errno));
			close(pipe_child_ready[1]);
			close(pipe_parent_ready[0]);
			exit(1);
		}

		TH_LOG("Child: got file handle (bytes=%u)", handle->handle_bytes);

		/* Send file handle to parent */
		ret = write(pipe_child_ready[1], buf, sizeof(*handle) + handle->handle_bytes);
		TH_LOG("Child: sent %d bytes of file handle to parent", ret);
		close(pipe_child_ready[1]);

		/* Wait for parent to open the fd */
		TH_LOG("Child: waiting for parent to open fd");
		ret = read(pipe_parent_ready[0], &sync_byte, 1);
		close(pipe_parent_ready[0]);

		TH_LOG("Child: parent signaled (read %d bytes), exiting now", ret);
		/* Exit - namespace should stay active because parent holds fd */
		exit(0);
	}

	/* Parent process */
	close(pipe_child_ready[1]);
	close(pipe_parent_ready[0]);

	TH_LOG("Parent: reading file handle from child");

	/* Read file handle from child */
	ret = read(pipe_child_ready[0], buf, sizeof(buf));
	close(pipe_child_ready[0]);
	ASSERT_GT(ret, 0);
	handle = (struct file_handle *)buf;

	TH_LOG("Parent: received %d bytes, handle size=%u", ret, handle->handle_bytes);

	/* Open the child's namespace while it's still alive */
	snprintf(proc_path, sizeof(proc_path), "/proc/%d/ns/net", pid);
	TH_LOG("Parent: opening child's namespace at %s", proc_path);
	nsfd = open(proc_path, O_RDONLY);
	if (nsfd < 0) {
		TH_LOG("Parent: failed to open %s: %s", proc_path, strerror(errno));
		close(pipe_parent_ready[1]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to open child's namespace");
	}

	TH_LOG("Parent: opened child's namespace, got fd %d", nsfd);

	/* Signal child that we have the fd */
	sync_byte = 'G';
	write(pipe_parent_ready[1], &sync_byte, 1);
	close(pipe_parent_ready[1]);
	TH_LOG("Parent: signaled child that we have the fd");

	/* Wait for child to exit */
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	TH_LOG("Child exited, parent holds fd %d to namespace", nsfd);

	/*
	 * Namespace should still be ACTIVE because we hold an fd.
	 * We should be able to reopen it via file handle.
	 */
	TH_LOG("Attempting to reopen namespace via file handle (should succeed - fd held)");
	int fd2 = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_GE(fd2, 0);

	TH_LOG("Successfully reopened namespace via file handle, got fd %d", fd2);

	/* Verify it's the same namespace */
	struct stat st1, st2;
	ASSERT_EQ(fstat(nsfd, &st1), 0);
	ASSERT_EQ(fstat(fd2, &st2), 0);
	TH_LOG("Namespace inodes: nsfd=%lu, fd2=%lu", st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	close(fd2);

	/* Now close the fd - namespace should become inactive */
	TH_LOG("Closing fd %d - namespace should become inactive", nsfd);
	close(nsfd);

	/* Now reopening should fail - namespace is inactive */
	TH_LOG("Attempting to reopen namespace via file handle (should fail - inactive)");
	fd2 = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(fd2, 0);
	/* Should fail with ENOENT (inactive) or ESTALE (gone) */
	TH_LOG("Reopen failed as expected: %s (errno=%d)", strerror(errno), errno);
	ASSERT_TRUE(errno == ENOENT || errno == ESTALE);
}

TEST_HARNESS_MAIN
