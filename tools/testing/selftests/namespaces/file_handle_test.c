// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/unistd.h>
#include "../kselftest_harness.h"

#ifndef FD_NSFS_ROOT
#define FD_NSFS_ROOT -10003 /* Root of the nsfs filesystem */
#endif

TEST(nsfs_net_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open a namespace file descriptor */
	ns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT as unprivileged user */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	if (fd < 0 && errno == EPERM) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "Permission denied for unprivileged user (expected)");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_uts_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open UTS namespace file descriptor */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_ipc_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open IPC namespace file descriptor */
	ns_fd = open("/proc/self/ns/ipc", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_pid_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open PID namespace file descriptor */
	ns_fd = open("/proc/self/ns/pid", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_mnt_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open mount namespace file descriptor */
	ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_user_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open user namespace file descriptor */
	ns_fd = open("/proc/self/ns/user", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_cgroup_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open cgroup namespace file descriptor */
	ns_fd = open("/proc/self/ns/cgroup", O_RDONLY);
	if (ns_fd < 0) {
		SKIP(free(handle); return, "cgroup namespace not available");
	}

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_time_handle)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	struct stat st1, st2;

	/* Drop to unprivileged uid/gid */
	ASSERT_EQ(setresgid(65534, 65534, 65534), 0); /* nogroup */
	ASSERT_EQ(setresuid(65534, 65534, 65534), 0); /* nobody */

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open time namespace file descriptor */
	ns_fd = open("/proc/self/ns/time", O_RDONLY);
	if (ns_fd < 0) {
		SKIP(free(handle); return, "time namespace not available");
	}

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Try to open using FD_NSFS_ROOT */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (fd < 0 && (errno == EINVAL || errno == EOPNOTSUPP)) {
		SKIP(free(handle); close(ns_fd);
		     return,
			   "open_by_handle_at with FD_NSFS_ROOT not supported");
	}
	ASSERT_GE(fd, 0);

	/* Verify we opened the correct namespace */
	ASSERT_EQ(fstat(ns_fd, &st1), 0);
	ASSERT_EQ(fstat(fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	close(fd);
	close(ns_fd);
	free(handle);
}

TEST(nsfs_user_net_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current network namespace */
	ns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create network namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Try to open parent's network namespace handle from new user+net namespace */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

		if (fd >= 0) {
			/* Should NOT succeed - we're in a different user namespace */
			write(pipefd[1], "S", 1); /* Unexpected success */
			close(fd);
		} else if (errno == ESTALE) {
			/* Expected: Stale file handle */
			write(pipefd[1], "P", 1);
		} else {
			/* Other error */
			write(pipefd[1], "F", 1);
		}

		close(pipefd[1]);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new network namespace");
	}

	/* Should fail with permission denied since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_uts_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current UTS namespace */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new UTS namespace */
		ret = unshare(CLONE_NEWUTS);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create UTS namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Try to open parent's UTS namespace handle from new user+uts namespace */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

		if (fd >= 0) {
			/* Should NOT succeed - we're in a different user namespace */
			write(pipefd[1], "S", 1); /* Unexpected success */
			close(fd);
		} else if (errno == ESTALE) {
			/* Expected: Stale file handle */
			write(pipefd[1], "P", 1);
		} else {
			/* Other error */
			write(pipefd[1], "F", 1);
		}

		close(pipefd[1]);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new UTS namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_ipc_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current IPC namespace */
	ns_fd = open("/proc/self/ns/ipc", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new IPC namespace */
		ret = unshare(CLONE_NEWIPC);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create IPC namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Try to open parent's IPC namespace handle from new user+ipc namespace */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

		if (fd >= 0) {
			/* Should NOT succeed - we're in a different user namespace */
			write(pipefd[1], "S", 1); /* Unexpected success */
			close(fd);
		} else if (errno == ESTALE) {
			/* Expected: Stale file handle */
			write(pipefd[1], "P", 1);
		} else {
			/* Other error */
			write(pipefd[1], "F", 1);
		}

		close(pipefd[1]);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new IPC namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_mnt_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current mount namespace */
	ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new mount namespace */
		ret = unshare(CLONE_NEWNS);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create mount namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Try to open parent's mount namespace handle from new user+mnt namespace */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

		if (fd >= 0) {
			/* Should NOT succeed - we're in a different user namespace */
			write(pipefd[1], "S", 1); /* Unexpected success */
			close(fd);
		} else if (errno == ESTALE) {
			/* Expected: Stale file handle */
			write(pipefd[1], "P", 1);
		} else {
			/* Other error */
			write(pipefd[1], "F", 1);
		}

		close(pipefd[1]);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new mount namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_cgroup_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current cgroup namespace */
	ns_fd = open("/proc/self/ns/cgroup", O_RDONLY);
	if (ns_fd < 0) {
		SKIP(free(handle); close(pipefd[0]); close(pipefd[1]);
		     return, "cgroup namespace not available");
	}

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new cgroup namespace */
		ret = unshare(CLONE_NEWCGROUP);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create cgroup namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Try to open parent's cgroup namespace handle from new user+cgroup namespace */
		fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

		if (fd >= 0) {
			/* Should NOT succeed - we're in a different user namespace */
			write(pipefd[1], "S", 1); /* Unexpected success */
			close(fd);
		} else if (errno == ESTALE) {
			/* Expected: Stale file handle */
			write(pipefd[1], "P", 1);
		} else {
			/* Other error */
			write(pipefd[1], "F", 1);
		}

		close(pipefd[1]);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new cgroup namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_pid_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current PID namespace */
	ns_fd = open("/proc/self/ns/pid", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new PID namespace - requires fork to take effect */
		ret = unshare(CLONE_NEWPID);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create PID namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Fork again for PID namespace to take effect */
		pid_t child_pid = fork();
		if (child_pid < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to fork in PID namespace */
			close(pipefd[1]);
			exit(0);
		}

		if (child_pid == 0) {
			/* Grandchild in new PID namespace */
			/* Try to open parent's PID namespace handle from new user+pid namespace */
			fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

			if (fd >= 0) {
				/* Should NOT succeed - we're in a different user namespace */
				write(pipefd[1], "S",
				      1); /* Unexpected success */
				close(fd);
			} else if (errno == ESTALE) {
				/* Expected: Stale file handle */
				write(pipefd[1], "P", 1);
			} else {
				/* Other error */
				write(pipefd[1], "F", 1);
			}

			close(pipefd[1]);
			exit(0);
		}

		/* Wait for grandchild */
		waitpid(child_pid, NULL, 0);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new PID namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_user_time_namespace_isolation)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;
	pid_t pid;
	int status;
	int pipefd[2];
	char result;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Create pipe for communication */
	ASSERT_EQ(pipe(pipefd), 0);

	/* Get handle for current time namespace */
	ns_fd = open("/proc/self/ns/time", O_RDONLY);
	if (ns_fd < 0) {
		SKIP(free(handle); close(pipefd[0]); close(pipefd[1]);
		     return, "time namespace not available");
	}

	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd); close(pipefd[0]);
		     close(pipefd[1]);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	close(ns_fd);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* First create new user namespace to drop privileges */
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			write(pipefd[1], "U",
			      1); /* Unable to create user namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Write uid/gid mappings to maintain some capabilities */
		int uid_map_fd = open("/proc/self/uid_map", O_WRONLY);
		int gid_map_fd = open("/proc/self/gid_map", O_WRONLY);
		int setgroups_fd = open("/proc/self/setgroups", O_WRONLY);

		if (uid_map_fd < 0 || gid_map_fd < 0 || setgroups_fd < 0) {
			write(pipefd[1], "M", 1); /* Unable to set mappings */
			close(pipefd[1]);
			exit(0);
		}

		/* Disable setgroups to allow gid mapping */
		write(setgroups_fd, "deny", 4);
		close(setgroups_fd);

		/* Map current uid/gid to root in the new namespace */
		char mapping[64];
		snprintf(mapping, sizeof(mapping), "0 %d 1", getuid());
		write(uid_map_fd, mapping, strlen(mapping));
		close(uid_map_fd);

		snprintf(mapping, sizeof(mapping), "0 %d 1", getgid());
		write(gid_map_fd, mapping, strlen(mapping));
		close(gid_map_fd);

		/* Now create new time namespace - requires fork to take effect */
		ret = unshare(CLONE_NEWTIME);
		if (ret < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to create time namespace */
			close(pipefd[1]);
			exit(0);
		}

		/* Fork again for time namespace to take effect */
		pid_t child_pid = fork();
		if (child_pid < 0) {
			write(pipefd[1], "N",
			      1); /* Unable to fork in time namespace */
			close(pipefd[1]);
			exit(0);
		}

		if (child_pid == 0) {
			/* Grandchild in new time namespace */
			/* Try to open parent's time namespace handle from new user+time namespace */
			fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);

			if (fd >= 0) {
				/* Should NOT succeed - we're in a different user namespace */
				write(pipefd[1], "S",
				      1); /* Unexpected success */
				close(fd);
			} else if (errno == ESTALE) {
				/* Expected: Stale file handle */
				write(pipefd[1], "P", 1);
			} else {
				/* Other error */
				write(pipefd[1], "F", 1);
			}

			close(pipefd[1]);
			exit(0);
		}

		/* Wait for grandchild */
		waitpid(child_pid, NULL, 0);
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);
	ASSERT_EQ(read(pipefd[0], &result, 1), 1);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	if (result == 'U') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new user namespace");
	}
	if (result == 'M') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot set uid/gid mappings");
	}
	if (result == 'N') {
		SKIP(free(handle); close(pipefd[0]);
		     return, "Cannot create new time namespace");
	}

	/* Should fail with ESTALE since we're in a different user namespace */
	ASSERT_EQ(result, 'P');

	close(pipefd[0]);
	free(handle);
}

TEST(nsfs_open_flags)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int ns_fd;

	handle = malloc(sizeof(*handle) + MAX_HANDLE_SZ);
	ASSERT_NE(handle, NULL);

	/* Open a namespace file descriptor */
	ns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(ns_fd, 0);

	/* Get handle for the namespace */
	handle->handle_bytes = MAX_HANDLE_SZ;
	ret = name_to_handle_at(ns_fd, "", handle, &mount_id, AT_EMPTY_PATH);
	if (ret < 0 && errno == EOPNOTSUPP) {
		SKIP(free(handle); close(ns_fd);
		     return, "nsfs doesn't support file handles");
	}
	ASSERT_EQ(ret, 0);
	ASSERT_GT(handle->handle_bytes, 0);

	/* Test invalid flags that should fail */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_WRONLY);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, EPERM);

	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDWR);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, EPERM);

	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_TRUNC);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, EPERM);

	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_DIRECT);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, EINVAL);

	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_TMPFILE);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, EINVAL);

	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_DIRECTORY);
	ASSERT_LT(fd, 0);
	ASSERT_EQ(errno, ENOTDIR);

	close(ns_fd);
	free(handle);
}

TEST_HARNESS_MAIN
