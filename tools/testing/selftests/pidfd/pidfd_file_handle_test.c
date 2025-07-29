// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/types.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/kcmp.h>
#include <sys/stat.h>

#include "pidfd.h"
#include "../kselftest_harness.h"

FIXTURE(file_handle)
{
	pid_t pid;
	int pidfd;

	pid_t child_pid1;
	int child_pidfd1;

	pid_t child_pid2;
	int child_pidfd2;

	pid_t child_pid3;
	int child_pidfd3;
};

FIXTURE_SETUP(file_handle)
{
	int ret;
	int ipc_sockets[2];
	char c;

	self->pid = getpid();
	self->pidfd = sys_pidfd_open(self->pid, 0);
	ASSERT_GE(self->pidfd, 0);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	self->child_pid1 = create_child(&self->child_pidfd1, CLONE_NEWUSER);
	EXPECT_GE(self->child_pid1, 0);

	if (self->child_pid1 == 0) {
		close(ipc_sockets[0]);

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	close(ipc_sockets[1]);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	close(ipc_sockets[0]);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	self->child_pid2 = create_child(&self->child_pidfd2, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid2, 0);

	if (self->child_pid2 == 0) {
		close(ipc_sockets[0]);

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	close(ipc_sockets[1]);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	close(ipc_sockets[0]);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	self->child_pid3 = create_child(&self->child_pidfd3, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid3, 0);

	if (self->child_pid3 == 0) {
		close(ipc_sockets[0]);

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	close(ipc_sockets[1]);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	close(ipc_sockets[0]);
}

FIXTURE_TEARDOWN(file_handle)
{
	EXPECT_EQ(close(self->pidfd), 0);

	EXPECT_EQ(sys_pidfd_send_signal(self->child_pidfd1, SIGKILL, NULL, 0), 0);
	if (self->child_pidfd1 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd1));

	EXPECT_EQ(sys_waitid(P_PID, self->child_pid1, NULL, WEXITED), 0);

	EXPECT_EQ(sys_pidfd_send_signal(self->child_pidfd2, SIGKILL, NULL, 0), 0);
	if (self->child_pidfd2 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd2));

	EXPECT_EQ(sys_waitid(P_PID, self->child_pid2, NULL, WEXITED), 0);

	if (self->child_pidfd3 >= 0) {
		EXPECT_EQ(sys_pidfd_send_signal(self->child_pidfd3, SIGKILL, NULL, 0), 0);
		EXPECT_EQ(0, close(self->child_pidfd3));
		EXPECT_EQ(sys_waitid(P_PID, self->child_pid3, NULL, WEXITED), 0);
	}
}

/*
 * Test that we can decode a pidfs file handle in the same pid
 * namespace.
 */
TEST_F(file_handle, file_handle_same_pidns)
{
	int mnt_id;
	struct file_handle *fh;
	int pidfd = -EBADF;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd1, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd1, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, O_NONBLOCK);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	free(fh);
}

/*
 * Test that we can decode a pidfs file handle from a child pid
 * namespace.
 */
TEST_F(file_handle, file_handle_child_pidns)
{
	int mnt_id;
	struct file_handle *fh;
	int pidfd = -EBADF;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd2, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd2, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, O_NONBLOCK);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	free(fh);
}

/*
 * Test that we fail to decode a pidfs file handle from an ancestor
 * child pid namespace.
 */
TEST_F(file_handle, file_handle_foreign_pidns)
{
	int mnt_id;
	struct file_handle *fh;
	pid_t pid;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->pidfd, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(setns(self->child_pidfd2, CLONE_NEWUSER | CLONE_NEWPID), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int pidfd = open_by_handle_at(self->pidfd, fh, 0);
		if (pidfd >= 0) {
			TH_LOG("Managed to open pidfd outside of the caller's pid namespace hierarchy");
			_exit(1);
		}
		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);

	free(fh);
}

/*
 * Test that we can decode a pidfs file handle of a process that has
 * exited but not been reaped.
 */
TEST_F(file_handle, pid_has_exited)
{
	int mnt_id, pidfd, child_pidfd3;
	struct file_handle *fh;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd3, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd3, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	child_pidfd3 = self->child_pidfd3;
	self->child_pidfd3 = -EBADF;
	EXPECT_EQ(sys_pidfd_send_signal(child_pidfd3, SIGKILL, NULL, 0), 0);
	EXPECT_EQ(close(child_pidfd3), 0);
	EXPECT_EQ(sys_waitid(P_PID, self->child_pid3, NULL, WEXITED | WNOWAIT), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	EXPECT_EQ(sys_waitid(P_PID, self->child_pid3, NULL, WEXITED), 0);
}

/*
 * Test that we fail to decode a pidfs file handle of a process that has
 * already been reaped.
 */
TEST_F(file_handle, pid_has_been_reaped)
{
	int mnt_id, pidfd, child_pidfd3;
	struct file_handle *fh;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd3, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd3, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	child_pidfd3 = self->child_pidfd3;
	self->child_pidfd3 = -EBADF;
	EXPECT_EQ(sys_pidfd_send_signal(child_pidfd3, SIGKILL, NULL, 0), 0);
	EXPECT_EQ(close(child_pidfd3), 0);
	EXPECT_EQ(sys_waitid(P_PID, self->child_pid3, NULL, WEXITED), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_LT(pidfd, 0);
}

/*
 * Test valid flags to open a pidfd file handle. Note, that
 * PIDFD_NONBLOCK is defined as O_NONBLOCK and O_NONBLOCK is an alias to
 * O_NDELAY. Also note that PIDFD_THREAD is an alias for O_EXCL.
 */
TEST_F(file_handle, open_by_handle_at_valid_flags)
{
	int mnt_id;
	struct file_handle *fh;
	int pidfd = -EBADF;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd2, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd2, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh,
				  O_RDONLY |
				  O_WRONLY |
				  O_RDWR |
				  O_NONBLOCK |
				  O_NDELAY |
				  O_CLOEXEC |
				  O_EXCL);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);
}

/*
 * Test that invalid flags passed to open a pidfd file handle are
 * rejected.
 */
TEST_F(file_handle, open_by_handle_at_invalid_flags)
{
	int mnt_id;
	struct file_handle *fh;
	int pidfd = -EBADF;
	static const struct invalid_pidfs_file_handle_flags {
		int oflag;
		const char *oflag_name;
	}  invalid_pidfs_file_handle_flags[] = {
		{ FASYNC,	"FASYNC"	},
		{ O_CREAT,	"O_CREAT"	},
		{ O_NOCTTY,	"O_NOCTTY"	},
		{ O_CREAT,	"O_CREAT"	},
		{ O_TRUNC,	"O_TRUNC"	},
		{ O_APPEND,	"O_APPEND"	},
		{ O_SYNC,	"O_SYNC"	},
		{ O_DSYNC,	"O_DSYNC"	},
		{ O_DIRECT,	"O_DIRECT"	},
		{ O_DIRECTORY,	"O_DIRECTORY"	},
		{ O_NOFOLLOW,	"O_NOFOLLOW"	},
		{ O_NOATIME,	"O_NOATIME"	},
		{ O_PATH,	"O_PATH"	},
		{ O_TMPFILE,	"O_TMPFILE"	},
		/*
		 * O_LARGEFILE is added implicitly by
		 * open_by_handle_at() so pidfs simply masks it off.
		 */
	};

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd2, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	for (int i = 0; i < ARRAY_SIZE(invalid_pidfs_file_handle_flags); i++) {
		pidfd = open_by_handle_at(self->pidfd, fh, invalid_pidfs_file_handle_flags[i].oflag);
		ASSERT_LT(pidfd, 0) {
			TH_LOG("open_by_handle_at() succeeded with invalid flags: %s", invalid_pidfs_file_handle_flags[i].oflag_name);
		}
	}
}

/* Test that lookup fails. */
TEST_F(file_handle, lookup_must_fail)
{
	int mnt_id;
	struct file_handle *fh;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_NE(name_to_handle_at(self->child_pidfd2, "lookup-is-not-possible-with-pidfs", fh, &mnt_id, AT_EMPTY_PATH), 0);
	ASSERT_EQ(errno, ENOTDIR);
	ASSERT_NE(name_to_handle_at(self->child_pidfd2, "lookup-is-not-possible-with-pidfs", fh, &mnt_id, 0), 0);
	ASSERT_EQ(errno, ENOTDIR);
}

#ifndef AT_HANDLE_CONNECTABLE
#define AT_HANDLE_CONNECTABLE 0x002
#endif

/*
 * Test that AT_HANDLE_CONNECTABLE is rejected. Connectable file handles
 * don't make sense for pidfs. Note that currently AT_HANDLE_CONNECTABLE
 * is rejected because it is incompatible with AT_EMPTY_PATH which is
 * required with pidfds as we don't support lookup.
 */
TEST_F(file_handle, invalid_name_to_handle_at_flags)
{
	int mnt_id;
	struct file_handle *fh;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_NE(name_to_handle_at(self->child_pidfd2, "", fh, &mnt_id, AT_EMPTY_PATH | AT_HANDLE_CONNECTABLE), 0);
}

#ifndef AT_HANDLE_FID
#define AT_HANDLE_FID 0x200
#endif

/*
 * Test that a request with AT_HANDLE_FID always leads to decodable file
 * handle as pidfs always provides export operations.
 */
TEST_F(file_handle, valid_name_to_handle_at_flags)
{
	int mnt_id, pidfd;
	struct file_handle *fh;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd2, "", fh, &mnt_id, AT_EMPTY_PATH | AT_HANDLE_FID), 0);

	ASSERT_EQ(fstat(self->child_pidfd2, &st1), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);
}

/*
 * That we decode a file handle without having to pass a pidfd.
 */
TEST_F(file_handle, decode_purely_based_on_file_handle)
{
	int mnt_id;
	struct file_handle *fh;
	int pidfd = -EBADF;
	struct stat st1, st2;

	fh = malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ);
	ASSERT_NE(fh, NULL);
	memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
	fh->handle_bytes = MAX_HANDLE_SZ;

	ASSERT_EQ(name_to_handle_at(self->child_pidfd1, "", fh, &mnt_id, AT_EMPTY_PATH), 0);

	ASSERT_EQ(fstat(self->child_pidfd1, &st1), 0);

	pidfd = open_by_handle_at(FD_PIDFS_ROOT, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(FD_PIDFS_ROOT, fh, O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(FD_PIDFS_ROOT, fh, O_NONBLOCK);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(self->pidfd, fh, 0);
	ASSERT_GE(pidfd, 0);

	ASSERT_EQ(fstat(pidfd, &st2), 0);
	ASSERT_TRUE(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);

	ASSERT_EQ(close(pidfd), 0);

	pidfd = open_by_handle_at(-EBADF, fh, 0);
	ASSERT_LT(pidfd, 0);

	pidfd = open_by_handle_at(AT_FDCWD, fh, 0);
	ASSERT_LT(pidfd, 0);

	free(fh);
}

TEST_HARNESS_MAIN
