// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest_harness.h"
#include "../filesystems/wrappers.h"

FIXTURE(pidfd_bind_mount) {
	char template[PATH_MAX];
	int fd_tmp;
	int pidfd;
	struct stat st1;
	struct stat st2;
	__u32 gen1;
	__u32 gen2;
	bool must_unmount;
};

FIXTURE_SETUP(pidfd_bind_mount)
{
	self->fd_tmp = -EBADF;
	self->must_unmount = false;
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_LE(snprintf(self->template, PATH_MAX, "%s", P_tmpdir "/pidfd_bind_mount_XXXXXX"), PATH_MAX);
	self->fd_tmp = mkstemp(self->template);
	ASSERT_GE(self->fd_tmp, 0);
	self->pidfd = sys_pidfd_open(getpid(), 0);
	ASSERT_GE(self->pidfd, 0);
	ASSERT_GE(fstat(self->pidfd, &self->st1), 0);
	ASSERT_EQ(ioctl(self->pidfd, FS_IOC_GETVERSION, &self->gen1), 0);
}

FIXTURE_TEARDOWN(pidfd_bind_mount)
{
	ASSERT_EQ(close(self->fd_tmp), 0);
	if (self->must_unmount)
		ASSERT_EQ(umount2(self->template, 0), 0);
	ASSERT_EQ(unlink(self->template), 0);
}

/*
 * Test that a detached mount can be created for a pidfd and then
 * attached to the filesystem hierarchy.
 */
TEST_F(pidfd_bind_mount, bind_mount)
{
	int fd_tree;

	fd_tree = sys_open_tree(self->pidfd, "", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH);
	ASSERT_GE(fd_tree, 0);

	ASSERT_EQ(move_mount(fd_tree, "", self->fd_tmp, "", MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH), 0);
	self->must_unmount = true;

	ASSERT_EQ(close(fd_tree), 0);
}

/* Test that a pidfd can be reopened through procfs. */
TEST_F(pidfd_bind_mount, reopen)
{
	int pidfd;
	char proc_path[PATH_MAX];

	sprintf(proc_path, "/proc/self/fd/%d", self->pidfd);
	pidfd = open(proc_path, O_RDONLY | O_NOCTTY | O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	ASSERT_GE(fstat(self->pidfd, &self->st2), 0);
	ASSERT_EQ(ioctl(self->pidfd, FS_IOC_GETVERSION, &self->gen2), 0);

	ASSERT_TRUE(self->st1.st_dev == self->st2.st_dev && self->st1.st_ino == self->st2.st_ino);
	ASSERT_TRUE(self->gen1 == self->gen2);

	ASSERT_EQ(close(pidfd), 0);
}

/*
 * Test that a detached mount can be created for a pidfd and then
 * attached to the filesystem hierarchy and reopened.
 */
TEST_F(pidfd_bind_mount, bind_mount_reopen)
{
	int fd_tree, fd_pidfd_mnt;

	fd_tree = sys_open_tree(self->pidfd, "", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH);
	ASSERT_GE(fd_tree, 0);

	ASSERT_EQ(move_mount(fd_tree, "", self->fd_tmp, "", MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH), 0);
	self->must_unmount = true;

	fd_pidfd_mnt = openat(-EBADF, self->template, O_RDONLY | O_NOCTTY | O_CLOEXEC);
	ASSERT_GE(fd_pidfd_mnt, 0);

	ASSERT_GE(fstat(fd_tree, &self->st2), 0);
	ASSERT_EQ(ioctl(fd_pidfd_mnt, FS_IOC_GETVERSION, &self->gen2), 0);

	ASSERT_TRUE(self->st1.st_dev == self->st2.st_dev && self->st1.st_ino == self->st2.st_ino);
	ASSERT_TRUE(self->gen1 == self->gen2);

	ASSERT_EQ(close(fd_tree), 0);
	ASSERT_EQ(close(fd_pidfd_mnt), 0);
}

TEST_HARNESS_MAIN
