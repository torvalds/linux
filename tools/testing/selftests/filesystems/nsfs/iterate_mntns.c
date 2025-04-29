// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/auto_dev-ioctl.h>
#include <linux/errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>

#include "../../kselftest_harness.h"

#define MNT_NS_COUNT 11
#define MNT_NS_LAST_INDEX 10

struct mnt_ns_info {
	__u32 size;
	__u32 nr_mounts;
	__u64 mnt_ns_id;
};

#define MNT_NS_INFO_SIZE_VER0 16 /* size of first published struct */

/* Get information about namespace. */
#define NS_MNT_GET_INFO _IOR(0xb7, 10, struct mnt_ns_info)
/* Get next namespace. */
#define NS_MNT_GET_NEXT _IOR(0xb7, 11, struct mnt_ns_info)
/* Get previous namespace. */
#define NS_MNT_GET_PREV _IOR(0xb7, 12, struct mnt_ns_info)

FIXTURE(iterate_mount_namespaces) {
	int fd_mnt_ns[MNT_NS_COUNT];
	__u64 mnt_ns_id[MNT_NS_COUNT];
};

FIXTURE_SETUP(iterate_mount_namespaces)
{
	for (int i = 0; i < MNT_NS_COUNT; i++)
		self->fd_mnt_ns[i] = -EBADF;

	/*
	 * Creating a new user namespace let's us guarantee that we only see
	 * mount namespaces that we did actually create.
	 */
	ASSERT_EQ(unshare(CLONE_NEWUSER), 0);

	for (int i = 0; i < MNT_NS_COUNT; i++) {
		struct mnt_ns_info info = {};

		ASSERT_EQ(unshare(CLONE_NEWNS), 0);
		self->fd_mnt_ns[i] = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(self->fd_mnt_ns[i], 0);
		ASSERT_EQ(ioctl(self->fd_mnt_ns[i], NS_MNT_GET_INFO, &info), 0);
		self->mnt_ns_id[i] = info.mnt_ns_id;
	}
}

FIXTURE_TEARDOWN(iterate_mount_namespaces)
{
	for (int i = 0; i < MNT_NS_COUNT; i++) {
		if (self->fd_mnt_ns[i] < 0)
			continue;
		ASSERT_EQ(close(self->fd_mnt_ns[i]), 0);
	}
}

TEST_F(iterate_mount_namespaces, iterate_all_forward)
{
	int fd_mnt_ns_cur, count = 0;

	fd_mnt_ns_cur = fcntl(self->fd_mnt_ns[0], F_DUPFD_CLOEXEC);
	ASSERT_GE(fd_mnt_ns_cur, 0);

	for (;; count++) {
		struct mnt_ns_info info = {};
		int fd_mnt_ns_next;

		fd_mnt_ns_next = ioctl(fd_mnt_ns_cur, NS_MNT_GET_NEXT, &info);
		if (fd_mnt_ns_next < 0 && errno == ENOENT)
			break;
		ASSERT_GE(fd_mnt_ns_next, 0);
		ASSERT_EQ(close(fd_mnt_ns_cur), 0);
		fd_mnt_ns_cur = fd_mnt_ns_next;
	}
	ASSERT_EQ(count, MNT_NS_LAST_INDEX);
}

TEST_F(iterate_mount_namespaces, iterate_all_backwards)
{
	int fd_mnt_ns_cur, count = 0;

	fd_mnt_ns_cur = fcntl(self->fd_mnt_ns[MNT_NS_LAST_INDEX], F_DUPFD_CLOEXEC);
	ASSERT_GE(fd_mnt_ns_cur, 0);

	for (;; count++) {
		struct mnt_ns_info info = {};
		int fd_mnt_ns_prev;

		fd_mnt_ns_prev = ioctl(fd_mnt_ns_cur, NS_MNT_GET_PREV, &info);
		if (fd_mnt_ns_prev < 0 && errno == ENOENT)
			break;
		ASSERT_GE(fd_mnt_ns_prev, 0);
		ASSERT_EQ(close(fd_mnt_ns_cur), 0);
		fd_mnt_ns_cur = fd_mnt_ns_prev;
	}
	ASSERT_EQ(count, MNT_NS_LAST_INDEX);
}

TEST_F(iterate_mount_namespaces, iterate_forward)
{
	int fd_mnt_ns_cur;

	ASSERT_EQ(setns(self->fd_mnt_ns[0], CLONE_NEWNS), 0);

	fd_mnt_ns_cur = self->fd_mnt_ns[0];
	for (int i = 1; i < MNT_NS_COUNT; i++) {
		struct mnt_ns_info info = {};
		int fd_mnt_ns_next;

		fd_mnt_ns_next = ioctl(fd_mnt_ns_cur, NS_MNT_GET_NEXT, &info);
		ASSERT_GE(fd_mnt_ns_next, 0);
		ASSERT_EQ(close(fd_mnt_ns_cur), 0);
		fd_mnt_ns_cur = fd_mnt_ns_next;
		ASSERT_EQ(info.mnt_ns_id, self->mnt_ns_id[i]);
	}
}

TEST_F(iterate_mount_namespaces, iterate_backward)
{
	int fd_mnt_ns_cur;

	ASSERT_EQ(setns(self->fd_mnt_ns[MNT_NS_LAST_INDEX], CLONE_NEWNS), 0);

	fd_mnt_ns_cur = self->fd_mnt_ns[MNT_NS_LAST_INDEX];
	for (int i = MNT_NS_LAST_INDEX - 1; i >= 0; i--) {
		struct mnt_ns_info info = {};
		int fd_mnt_ns_prev;

		fd_mnt_ns_prev = ioctl(fd_mnt_ns_cur, NS_MNT_GET_PREV, &info);
		ASSERT_GE(fd_mnt_ns_prev, 0);
		ASSERT_EQ(close(fd_mnt_ns_cur), 0);
		fd_mnt_ns_cur = fd_mnt_ns_prev;
		ASSERT_EQ(info.mnt_ns_id, self->mnt_ns_id[i]);
	}
}

TEST_F(iterate_mount_namespaces, nfs_valid_ioctl)
{
	ASSERT_NE(ioctl(self->fd_mnt_ns[0], AUTOFS_DEV_IOCTL_OPENMOUNT, NULL), 0);
	ASSERT_EQ(errno, ENOTTY);

	ASSERT_NE(ioctl(self->fd_mnt_ns[0], AUTOFS_DEV_IOCTL_CLOSEMOUNT, NULL), 0);
	ASSERT_EQ(errno, ENOTTY);

	ASSERT_NE(ioctl(self->fd_mnt_ns[0], AUTOFS_DEV_IOCTL_READY, NULL), 0);
	ASSERT_EQ(errno, ENOTTY);
}

TEST_HARNESS_MAIN
