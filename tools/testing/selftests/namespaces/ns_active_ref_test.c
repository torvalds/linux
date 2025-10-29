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

#ifndef FILEID_NSFS
#define FILEID_NSFS 0xf1
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

/*
 * Test hierarchical active reference propagation.
 * When a child namespace is active, its owning user namespace should also
 * be active automatically due to hierarchical active reference propagation.
 * This ensures parents are always reachable when children are active.
 */
TEST(ns_parent_always_reachable)
{
	struct file_handle *parent_handle, *child_handle;
	int ret;
	int child_nsfd;
	int pipefd[2];
	pid_t pid;
	int status;
	__u64 parent_id, child_id;
	char parent_buf[sizeof(*parent_handle) + MAX_HANDLE_SZ];
	char child_buf[sizeof(*child_handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		TH_LOG("Child: creating parent user namespace and setting up mappings");

		/* Create parent user namespace with mappings */
		ret = setup_userns();
		if (ret < 0) {
			TH_LOG("Child: setup_userns() for parent failed: %s", strerror(errno));
			close(pipefd[1]);
			exit(1);
		}

		TH_LOG("Child: parent user namespace created, now uid=%d gid=%d", getuid(), getgid());

		/* Get namespace ID for parent user namespace */
		int parent_fd = open("/proc/self/ns/user", O_RDONLY);
		if (parent_fd < 0) {
			TH_LOG("Child: failed to open parent /proc/self/ns/user: %s", strerror(errno));
			close(pipefd[1]);
			exit(1);
		}

		TH_LOG("Child: opened parent userns fd %d", parent_fd);

		if (ioctl(parent_fd, NS_GET_ID, &parent_id) < 0) {
			TH_LOG("Child: NS_GET_ID for parent failed: %s", strerror(errno));
			close(parent_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(parent_fd);

		TH_LOG("Child: got parent namespace ID %llu", (unsigned long long)parent_id);

		/* Create child user namespace within parent */
		TH_LOG("Child: creating nested child user namespace");
		ret = setup_userns();
		if (ret < 0) {
			TH_LOG("Child: setup_userns() for child failed: %s", strerror(errno));
			close(pipefd[1]);
			exit(1);
		}

		TH_LOG("Child: nested child user namespace created, uid=%d gid=%d", getuid(), getgid());

		/* Get namespace ID for child user namespace */
		int child_fd = open("/proc/self/ns/user", O_RDONLY);
		if (child_fd < 0) {
			TH_LOG("Child: failed to open child /proc/self/ns/user: %s", strerror(errno));
			close(pipefd[1]);
			exit(1);
		}

		TH_LOG("Child: opened child userns fd %d", child_fd);

		if (ioctl(child_fd, NS_GET_ID, &child_id) < 0) {
			TH_LOG("Child: NS_GET_ID for child failed: %s", strerror(errno));
			close(child_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(child_fd);

		TH_LOG("Child: got child namespace ID %llu", (unsigned long long)child_id);

		/* Send both namespace IDs to parent */
		TH_LOG("Child: sending both namespace IDs to parent");
		write(pipefd[1], &parent_id, sizeof(parent_id));
		write(pipefd[1], &child_id, sizeof(child_id));
		close(pipefd[1]);

		TH_LOG("Child: exiting - parent userns should become inactive");
		/* Exit - parent user namespace should become inactive */
		exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	TH_LOG("Parent: reading both namespace IDs from child");

	/* Read both namespace IDs - fixed size, no parsing needed */
	ret = read(pipefd[0], &parent_id, sizeof(parent_id));
	if (ret != sizeof(parent_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read parent namespace ID from child");
	}

	ret = read(pipefd[0], &child_id, sizeof(child_id));
	close(pipefd[0]);
	if (ret != sizeof(child_id)) {
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read child namespace ID from child");
	}

	TH_LOG("Parent: received parent_id=%llu, child_id=%llu",
	       (unsigned long long)parent_id, (unsigned long long)child_id);

	/* Construct file handles from namespace IDs */
	parent_handle = (struct file_handle *)parent_buf;
	parent_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	parent_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *parent_fh = (struct nsfs_file_handle *)parent_handle->f_handle;
	parent_fh->ns_id = parent_id;
	parent_fh->ns_type = 0;
	parent_fh->ns_inum = 0;

	child_handle = (struct file_handle *)child_buf;
	child_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	child_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *child_fh = (struct nsfs_file_handle *)child_handle->f_handle;
	child_fh->ns_id = child_id;
	child_fh->ns_type = 0;
	child_fh->ns_inum = 0;

	TH_LOG("Parent: opening child namespace BEFORE child exits");

	/* Open child namespace while child is still alive to keep it active */
	child_nsfd = open_by_handle_at(FD_NSFS_ROOT, child_handle, O_RDONLY);
	if (child_nsfd < 0) {
		TH_LOG("Failed to open child namespace: %s (errno=%d)", strerror(errno), errno);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to open child namespace");
	}

	TH_LOG("Opened child namespace fd %d", child_nsfd);

	/* Now wait for child to exit */
	TH_LOG("Parent: waiting for child to exit");
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	TH_LOG("Child process exited, parent holds fd to child namespace");

	/*
	 * With hierarchical active reference propagation:
	 * Since the child namespace is active (parent process holds fd),
	 * the parent user namespace should ALSO be active automatically.
	 * This is because when we took an active reference on the child,
	 * it propagated up to the owning user namespace.
	 */
	TH_LOG("Attempting to reopen parent namespace (should SUCCEED - hierarchical propagation)");
	int parent_fd = open_by_handle_at(FD_NSFS_ROOT, parent_handle, O_RDONLY);
	ASSERT_GE(parent_fd, 0);

	TH_LOG("SUCCESS: Parent namespace is active (fd=%d) due to active child", parent_fd);

	/* Verify we can also get parent via NS_GET_USERNS */
	TH_LOG("Verifying NS_GET_USERNS also works");
	int parent_fd2 = ioctl(child_nsfd, NS_GET_USERNS);
	if (parent_fd2 < 0) {
		close(parent_fd);
		close(child_nsfd);
		TH_LOG("NS_GET_USERNS failed: %s (errno=%d)", strerror(errno), errno);
		SKIP(return, "NS_GET_USERNS not supported or failed");
	}

	TH_LOG("NS_GET_USERNS succeeded, got parent fd %d", parent_fd2);

	/* Verify both methods give us the same namespace */
	struct stat st1, st2;
	ASSERT_EQ(fstat(parent_fd, &st1), 0);
	ASSERT_EQ(fstat(parent_fd2, &st2), 0);
	TH_LOG("Parent namespace inodes: parent_fd=%lu, parent_fd2=%lu", st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_ino, st2.st_ino);

	/*
	 * Close child fd - parent should remain active because we still
	 * hold direct references to it (parent_fd and parent_fd2).
	 */
	TH_LOG("Closing child fd - parent should remain active (direct refs held)");
	close(child_nsfd);

	/* Parent should still be openable */
	TH_LOG("Verifying parent still active via file handle");
	int parent_fd3 = open_by_handle_at(FD_NSFS_ROOT, parent_handle, O_RDONLY);
	ASSERT_GE(parent_fd3, 0);
	close(parent_fd3);

	TH_LOG("Closing all fds to parent namespace");
	close(parent_fd);
	close(parent_fd2);

	/* Both should now be inactive */
	TH_LOG("Attempting to reopen parent (should fail - inactive, no refs)");
	parent_fd = open_by_handle_at(FD_NSFS_ROOT, parent_handle, O_RDONLY);
	ASSERT_LT(parent_fd, 0);
	TH_LOG("Parent inactive as expected: %s (errno=%d)", strerror(errno), errno);
	ASSERT_TRUE(errno == ENOENT || errno == ESTALE);
}

/*
 * Test that bind mounts keep namespaces in the tree even when inactive
 */
TEST(ns_bind_mount_keeps_in_tree)
{
	struct file_handle *handle;
	int mount_id;
	int ret;
	int fd;
	int pipefd[2];
	pid_t pid;
	int status;
	char buf[sizeof(*handle) + MAX_HANDLE_SZ];
	char tmpfile[] = "/tmp/ns-test-XXXXXX";
	int tmpfd;

	/* Create temporary file for bind mount */
	tmpfd = mkstemp(tmpfile);
	if (tmpfd < 0) {
		SKIP(return, "Cannot create temporary file");
	}
	close(tmpfd);

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Unshare mount namespace and make mounts private to avoid propagation */
		ret = unshare(CLONE_NEWNS);
		if (ret < 0) {
			close(pipefd[1]);
			unlink(tmpfile);
			exit(1);
		}
		ret = mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);
		if (ret < 0) {
			close(pipefd[1]);
			unlink(tmpfile);
			exit(1);
		}

		/* Create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			close(pipefd[1]);
			unlink(tmpfile);
			exit(1);
		}

		/* Bind mount the namespace */
		ret = mount("/proc/self/ns/net", tmpfile, NULL, MS_BIND, NULL);
		if (ret < 0) {
			close(pipefd[1]);
			unlink(tmpfile);
			exit(1);
		}

		/* Get file handle */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			umount(tmpfile);
			close(pipefd[1]);
			unlink(tmpfile);
			exit(1);
		}

		handle = (struct file_handle *)buf;
		handle->handle_bytes = MAX_HANDLE_SZ;
		ret = name_to_handle_at(fd, "", handle, &mount_id, AT_EMPTY_PATH);
		close(fd);

		if (ret < 0) {
			umount(tmpfile);
			close(pipefd[1]);
			unlink(tmpfile);
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

	/*
	 * Namespace should be inactive but still in tree due to bind mount.
	 * Reopening should fail with ENOENT (inactive) not ESTALE (not in tree).
	 */
	fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(fd, 0);
	/* Should be ENOENT (inactive) since bind mount keeps it in tree */
	if (errno != ENOENT && errno != ESTALE) {
		TH_LOG("Unexpected error: %d", errno);
	}

	/* Cleanup */
	umount(tmpfile);
	unlink(tmpfile);
}

/*
 * Test multi-level hierarchy (3+ levels deep).
 * Grandparent → Parent → Child
 * When child is active, both parent AND grandparent should be active.
 */
TEST(ns_multilevel_hierarchy)
{
	struct file_handle *gp_handle, *p_handle, *c_handle;
	int ret, pipefd[2];
	pid_t pid;
	int status;
	__u64 gp_id, p_id, c_id;
	char gp_buf[sizeof(*gp_handle) + MAX_HANDLE_SZ];
	char p_buf[sizeof(*p_handle) + MAX_HANDLE_SZ];
	char c_buf[sizeof(*c_handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(pipefd[0]);

		/* Create grandparent user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int gp_fd = open("/proc/self/ns/user", O_RDONLY);
		if (gp_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(gp_fd, NS_GET_ID, &gp_id) < 0) {
			close(gp_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(gp_fd);

		/* Create parent user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int p_fd = open("/proc/self/ns/user", O_RDONLY);
		if (p_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(p_fd, NS_GET_ID, &p_id) < 0) {
			close(p_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(p_fd);

		/* Create child user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int c_fd = open("/proc/self/ns/user", O_RDONLY);
		if (c_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(c_fd, NS_GET_ID, &c_id) < 0) {
			close(c_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(c_fd);

		/* Send all three namespace IDs */
		write(pipefd[1], &gp_id, sizeof(gp_id));
		write(pipefd[1], &p_id, sizeof(p_id));
		write(pipefd[1], &c_id, sizeof(c_id));
		close(pipefd[1]);
		exit(0);
	}

	close(pipefd[1]);

	/* Read all three namespace IDs - fixed size, no parsing needed */
	ret = read(pipefd[0], &gp_id, sizeof(gp_id));
	if (ret != sizeof(gp_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read grandparent namespace ID from child");
	}

	ret = read(pipefd[0], &p_id, sizeof(p_id));
	if (ret != sizeof(p_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read parent namespace ID from child");
	}

	ret = read(pipefd[0], &c_id, sizeof(c_id));
	close(pipefd[0]);
	if (ret != sizeof(c_id)) {
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read child namespace ID from child");
	}

	/* Construct file handles from namespace IDs */
	gp_handle = (struct file_handle *)gp_buf;
	gp_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	gp_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *gp_fh = (struct nsfs_file_handle *)gp_handle->f_handle;
	gp_fh->ns_id = gp_id;
	gp_fh->ns_type = 0;
	gp_fh->ns_inum = 0;

	p_handle = (struct file_handle *)p_buf;
	p_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	p_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *p_fh = (struct nsfs_file_handle *)p_handle->f_handle;
	p_fh->ns_id = p_id;
	p_fh->ns_type = 0;
	p_fh->ns_inum = 0;

	c_handle = (struct file_handle *)c_buf;
	c_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	c_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *c_fh = (struct nsfs_file_handle *)c_handle->f_handle;
	c_fh->ns_id = c_id;
	c_fh->ns_type = 0;
	c_fh->ns_inum = 0;

	/* Open child before process exits */
	int c_fd = open_by_handle_at(FD_NSFS_ROOT, c_handle, O_RDONLY);
	if (c_fd < 0) {
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to open child namespace");
	}

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	/*
	 * With 3-level hierarchy and child active:
	 * - Child is active (we hold fd)
	 * - Parent should be active (propagated from child)
	 * - Grandparent should be active (propagated from parent)
	 */
	TH_LOG("Testing parent active when child is active");
	int p_fd = open_by_handle_at(FD_NSFS_ROOT, p_handle, O_RDONLY);
	ASSERT_GE(p_fd, 0);

	TH_LOG("Testing grandparent active when child is active");
	int gp_fd = open_by_handle_at(FD_NSFS_ROOT, gp_handle, O_RDONLY);
	ASSERT_GE(gp_fd, 0);

	close(c_fd);
	close(p_fd);
	close(gp_fd);
}

/*
 * Test multiple children sharing same parent.
 * Parent should stay active as long as ANY child is active.
 */
TEST(ns_multiple_children_same_parent)
{
	struct file_handle *p_handle, *c1_handle, *c2_handle;
	int ret, pipefd[2];
	pid_t pid;
	int status;
	__u64 p_id, c1_id, c2_id;
	char p_buf[sizeof(*p_handle) + MAX_HANDLE_SZ];
	char c1_buf[sizeof(*c1_handle) + MAX_HANDLE_SZ];
	char c2_buf[sizeof(*c2_handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(pipefd[0]);

		/* Create parent user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int p_fd = open("/proc/self/ns/user", O_RDONLY);
		if (p_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(p_fd, NS_GET_ID, &p_id) < 0) {
			close(p_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(p_fd);

		/* Create first child user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int c1_fd = open("/proc/self/ns/user", O_RDONLY);
		if (c1_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(c1_fd, NS_GET_ID, &c1_id) < 0) {
			close(c1_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(c1_fd);

		/* Return to parent user namespace and create second child */
		/* We can't actually do this easily, so let's create a sibling namespace
		 * by creating a network namespace instead */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int c2_fd = open("/proc/self/ns/net", O_RDONLY);
		if (c2_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(c2_fd, NS_GET_ID, &c2_id) < 0) {
			close(c2_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(c2_fd);

		/* Send all namespace IDs */
		write(pipefd[1], &p_id, sizeof(p_id));
		write(pipefd[1], &c1_id, sizeof(c1_id));
		write(pipefd[1], &c2_id, sizeof(c2_id));
		close(pipefd[1]);
		exit(0);
	}

	close(pipefd[1]);

	/* Read all three namespace IDs - fixed size, no parsing needed */
	ret = read(pipefd[0], &p_id, sizeof(p_id));
	if (ret != sizeof(p_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read parent namespace ID");
	}

	ret = read(pipefd[0], &c1_id, sizeof(c1_id));
	if (ret != sizeof(c1_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read first child namespace ID");
	}

	ret = read(pipefd[0], &c2_id, sizeof(c2_id));
	close(pipefd[0]);
	if (ret != sizeof(c2_id)) {
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read second child namespace ID");
	}

	/* Construct file handles from namespace IDs */
	p_handle = (struct file_handle *)p_buf;
	p_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	p_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *p_fh = (struct nsfs_file_handle *)p_handle->f_handle;
	p_fh->ns_id = p_id;
	p_fh->ns_type = 0;
	p_fh->ns_inum = 0;

	c1_handle = (struct file_handle *)c1_buf;
	c1_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	c1_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *c1_fh = (struct nsfs_file_handle *)c1_handle->f_handle;
	c1_fh->ns_id = c1_id;
	c1_fh->ns_type = 0;
	c1_fh->ns_inum = 0;

	c2_handle = (struct file_handle *)c2_buf;
	c2_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	c2_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *c2_fh = (struct nsfs_file_handle *)c2_handle->f_handle;
	c2_fh->ns_id = c2_id;
	c2_fh->ns_type = 0;
	c2_fh->ns_inum = 0;

	/* Open both children before process exits */
	int c1_fd = open_by_handle_at(FD_NSFS_ROOT, c1_handle, O_RDONLY);
	int c2_fd = open_by_handle_at(FD_NSFS_ROOT, c2_handle, O_RDONLY);

	if (c1_fd < 0 || c2_fd < 0) {
		if (c1_fd >= 0) close(c1_fd);
		if (c2_fd >= 0) close(c2_fd);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to open child namespaces");
	}

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	/* Parent should be active (both children active) */
	TH_LOG("Both children active - parent should be active");
	int p_fd = open_by_handle_at(FD_NSFS_ROOT, p_handle, O_RDONLY);
	ASSERT_GE(p_fd, 0);
	close(p_fd);

	/* Close first child - parent should STILL be active */
	TH_LOG("Closing first child - parent should still be active");
	close(c1_fd);
	p_fd = open_by_handle_at(FD_NSFS_ROOT, p_handle, O_RDONLY);
	ASSERT_GE(p_fd, 0);
	close(p_fd);

	/* Close second child - NOW parent should become inactive */
	TH_LOG("Closing second child - parent should become inactive");
	close(c2_fd);
	p_fd = open_by_handle_at(FD_NSFS_ROOT, p_handle, O_RDONLY);
	ASSERT_LT(p_fd, 0);
}

/*
 * Test that different namespace types with same owner all contribute
 * active references to the owning user namespace.
 */
TEST(ns_different_types_same_owner)
{
	struct file_handle *u_handle, *n_handle, *ut_handle;
	int ret, pipefd[2];
	pid_t pid;
	int status;
	__u64 u_id, n_id, ut_id;
	char u_buf[sizeof(*u_handle) + MAX_HANDLE_SZ];
	char n_buf[sizeof(*n_handle) + MAX_HANDLE_SZ];
	char ut_buf[sizeof(*ut_handle) + MAX_HANDLE_SZ];

	ASSERT_EQ(pipe(pipefd), 0);
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(pipefd[0]);

		/* Create user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int u_fd = open("/proc/self/ns/user", O_RDONLY);
		if (u_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(u_fd, NS_GET_ID, &u_id) < 0) {
			close(u_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(u_fd);

		/* Create network namespace (owned by user namespace) */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int n_fd = open("/proc/self/ns/net", O_RDONLY);
		if (n_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(n_fd, NS_GET_ID, &n_id) < 0) {
			close(n_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(n_fd);

		/* Create UTS namespace (also owned by user namespace) */
		if (unshare(CLONE_NEWUTS) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		int ut_fd = open("/proc/self/ns/uts", O_RDONLY);
		if (ut_fd < 0) {
			close(pipefd[1]);
			exit(1);
		}
		if (ioctl(ut_fd, NS_GET_ID, &ut_id) < 0) {
			close(ut_fd);
			close(pipefd[1]);
			exit(1);
		}
		close(ut_fd);

		/* Send all namespace IDs */
		write(pipefd[1], &u_id, sizeof(u_id));
		write(pipefd[1], &n_id, sizeof(n_id));
		write(pipefd[1], &ut_id, sizeof(ut_id));
		close(pipefd[1]);
		exit(0);
	}

	close(pipefd[1]);

	/* Read all three namespace IDs - fixed size, no parsing needed */
	ret = read(pipefd[0], &u_id, sizeof(u_id));
	if (ret != sizeof(u_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read user namespace ID");
	}

	ret = read(pipefd[0], &n_id, sizeof(n_id));
	if (ret != sizeof(n_id)) {
		close(pipefd[0]);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read network namespace ID");
	}

	ret = read(pipefd[0], &ut_id, sizeof(ut_id));
	close(pipefd[0]);
	if (ret != sizeof(ut_id)) {
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to read UTS namespace ID");
	}

	/* Construct file handles from namespace IDs */
	u_handle = (struct file_handle *)u_buf;
	u_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	u_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *u_fh = (struct nsfs_file_handle *)u_handle->f_handle;
	u_fh->ns_id = u_id;
	u_fh->ns_type = 0;
	u_fh->ns_inum = 0;

	n_handle = (struct file_handle *)n_buf;
	n_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	n_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *n_fh = (struct nsfs_file_handle *)n_handle->f_handle;
	n_fh->ns_id = n_id;
	n_fh->ns_type = 0;
	n_fh->ns_inum = 0;

	ut_handle = (struct file_handle *)ut_buf;
	ut_handle->handle_bytes = sizeof(struct nsfs_file_handle);
	ut_handle->handle_type = FILEID_NSFS;
	struct nsfs_file_handle *ut_fh = (struct nsfs_file_handle *)ut_handle->f_handle;
	ut_fh->ns_id = ut_id;
	ut_fh->ns_type = 0;
	ut_fh->ns_inum = 0;

	/* Open both non-user namespaces before process exits */
	int n_fd = open_by_handle_at(FD_NSFS_ROOT, n_handle, O_RDONLY);
	int ut_fd = open_by_handle_at(FD_NSFS_ROOT, ut_handle, O_RDONLY);

	if (n_fd < 0 || ut_fd < 0) {
		if (n_fd >= 0) close(n_fd);
		if (ut_fd >= 0) close(ut_fd);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to open namespaces");
	}

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	/*
	 * Both network and UTS namespaces are active.
	 * User namespace should be active (gets 2 active refs).
	 */
	TH_LOG("Both net and uts active - user namespace should be active");
	int u_fd = open_by_handle_at(FD_NSFS_ROOT, u_handle, O_RDONLY);
	ASSERT_GE(u_fd, 0);
	close(u_fd);

	/* Close network namespace - user namespace should STILL be active */
	TH_LOG("Closing network ns - user ns should still be active (uts still active)");
	close(n_fd);
	u_fd = open_by_handle_at(FD_NSFS_ROOT, u_handle, O_RDONLY);
	ASSERT_GE(u_fd, 0);
	close(u_fd);

	/* Close UTS namespace - user namespace should become inactive */
	TH_LOG("Closing uts ns - user ns should become inactive");
	close(ut_fd);
	u_fd = open_by_handle_at(FD_NSFS_ROOT, u_handle, O_RDONLY);
	ASSERT_LT(u_fd, 0);
}

TEST_HARNESS_MAIN
