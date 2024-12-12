// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <linux/types.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include "../../tools/testing/selftests/pidfd/pidfd.h"

#define die_errno(format, ...)                                             \
	do {                                                               \
		fprintf(stderr, "%m | %s: %d: %s: " format "\n", __FILE__, \
			__LINE__, __func__, ##__VA_ARGS__);                \
		exit(EXIT_FAILURE);                                        \
	} while (0)

/* Get the id for a mount namespace */
#define NS_GET_MNTNS_ID _IO(0xb7, 0x5)
/* Get next mount namespace. */

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

#define PIDFD_GET_MNT_NAMESPACE _IO(0xFF, 3)

#ifndef __NR_listmount
#define __NR_listmount 458
#endif

#ifndef __NR_statmount
#define __NR_statmount 457
#endif

/* @mask bits for statmount(2) */
#define STATMOUNT_SB_BASIC		0x00000001U /* Want/got sb_... */
#define STATMOUNT_MNT_BASIC		0x00000002U /* Want/got mnt_... */
#define STATMOUNT_PROPAGATE_FROM	0x00000004U /* Want/got propagate_from */
#define STATMOUNT_MNT_ROOT		0x00000008U /* Want/got mnt_root  */
#define STATMOUNT_MNT_POINT		0x00000010U /* Want/got mnt_point */
#define STATMOUNT_FS_TYPE		0x00000020U /* Want/got fs_type */
#define STATMOUNT_MNT_NS_ID		0x00000040U /* Want/got mnt_ns_id */
#define STATMOUNT_MNT_OPTS		0x00000080U /* Want/got mnt_opts */

#define STATX_MNT_ID_UNIQUE 0x00004000U /* Want/got extended stx_mount_id */

struct statmount {
	__u32 size;
	__u32 mnt_opts;
	__u64 mask;
	__u32 sb_dev_major;
	__u32 sb_dev_minor;
	__u64 sb_magic;
	__u32 sb_flags;
	__u32 fs_type;
	__u64 mnt_id;
	__u64 mnt_parent_id;
	__u32 mnt_id_old;
	__u32 mnt_parent_id_old;
	__u64 mnt_attr;
	__u64 mnt_propagation;
	__u64 mnt_peer_group;
	__u64 mnt_master;
	__u64 propagate_from;
	__u32 mnt_root;
	__u32 mnt_point;
	__u64 mnt_ns_id;
	__u64 __spare2[49];
	char str[];
};

struct mnt_id_req {
	__u32 size;
	__u32 spare;
	__u64 mnt_id;
	__u64 param;
	__u64 mnt_ns_id;
};

#define MNT_ID_REQ_SIZE_VER1 32 /* sizeof second published struct */

#define LSMT_ROOT 0xffffffffffffffff /* root mount */

static int __statmount(__u64 mnt_id, __u64 mnt_ns_id, __u64 mask,
		       struct statmount *stmnt, size_t bufsize,
		       unsigned int flags)
{
	struct mnt_id_req req = {
		.size		= MNT_ID_REQ_SIZE_VER1,
		.mnt_id		= mnt_id,
		.param		= mask,
		.mnt_ns_id	= mnt_ns_id,
	};

	return syscall(__NR_statmount, &req, stmnt, bufsize, flags);
}

static struct statmount *sys_statmount(__u64 mnt_id, __u64 mnt_ns_id,
				       __u64 mask, unsigned int flags)
{
	size_t bufsize = 1 << 15;
	struct statmount *stmnt = NULL, *tmp = NULL;
	int ret;

	for (;;) {
		tmp = realloc(stmnt, bufsize);
		if (!tmp)
			goto out;

		stmnt = tmp;
		ret = __statmount(mnt_id, mnt_ns_id, mask, stmnt, bufsize, flags);
		if (!ret)
			return stmnt;

		if (errno != EOVERFLOW)
			goto out;

		bufsize <<= 1;
		if (bufsize >= UINT_MAX / 2)
			goto out;
	}

out:
	free(stmnt);
	return NULL;
}

static ssize_t sys_listmount(__u64 mnt_id, __u64 last_mnt_id, __u64 mnt_ns_id,
			     __u64 list[], size_t num, unsigned int flags)
{
	struct mnt_id_req req = {
		.size		= MNT_ID_REQ_SIZE_VER1,
		.mnt_id		= mnt_id,
		.param		= last_mnt_id,
		.mnt_ns_id	= mnt_ns_id,
	};

	return syscall(__NR_listmount, &req, list, num, flags);
}

int main(int argc, char *argv[])
{
#define LISTMNT_BUFFER 10
	__u64 list[LISTMNT_BUFFER], last_mnt_id = 0;
	int ret, pidfd, fd_mntns;
	struct mnt_ns_info info = {};

	pidfd = sys_pidfd_open(getpid(), 0);
	if (pidfd < 0)
		die_errno("pidfd_open failed");

	fd_mntns = ioctl(pidfd, PIDFD_GET_MNT_NAMESPACE, 0);
	if (fd_mntns < 0)
		die_errno("ioctl(PIDFD_GET_MNT_NAMESPACE) failed");

	ret = ioctl(fd_mntns, NS_MNT_GET_INFO, &info);
	if (ret < 0)
		die_errno("ioctl(NS_GET_MNTNS_ID) failed");

	printf("Listing %u mounts for mount namespace %llu\n",
	       info.nr_mounts, info.mnt_ns_id);
	for (;;) {
		ssize_t nr_mounts;
next:
		nr_mounts = sys_listmount(LSMT_ROOT, last_mnt_id,
					  info.mnt_ns_id, list, LISTMNT_BUFFER,
					  0);
		if (nr_mounts <= 0) {
			int fd_mntns_next;

			printf("Finished listing %u mounts for mount namespace %llu\n\n",
			       info.nr_mounts, info.mnt_ns_id);
			fd_mntns_next = ioctl(fd_mntns, NS_MNT_GET_NEXT, &info);
			if (fd_mntns_next < 0) {
				if (errno == ENOENT) {
					printf("Finished listing all mount namespaces\n");
					exit(0);
				}
				die_errno("ioctl(NS_MNT_GET_NEXT) failed");
			}
			close(fd_mntns);
			fd_mntns = fd_mntns_next;
			last_mnt_id = 0;
			printf("Listing %u mounts for mount namespace %llu\n",
			       info.nr_mounts, info.mnt_ns_id);
			goto next;
		}

		for (size_t cur = 0; cur < nr_mounts; cur++) {
			struct statmount *stmnt;

			last_mnt_id = list[cur];

			stmnt = sys_statmount(last_mnt_id, info.mnt_ns_id,
					      STATMOUNT_SB_BASIC |
					      STATMOUNT_MNT_BASIC |
					      STATMOUNT_MNT_ROOT |
					      STATMOUNT_MNT_POINT |
					      STATMOUNT_MNT_NS_ID |
					      STATMOUNT_MNT_OPTS |
					      STATMOUNT_FS_TYPE, 0);
			if (!stmnt) {
				printf("Failed to statmount(%llu) in mount namespace(%llu)\n",
				       last_mnt_id, info.mnt_ns_id);
				continue;
			}

			printf("mnt_id:\t\t%llu\nmnt_parent_id:\t%llu\nfs_type:\t%s\nmnt_root:\t%s\nmnt_point:\t%s\nmnt_opts:\t%s\n\n",
			       stmnt->mnt_id,
			       stmnt->mnt_parent_id,
			       stmnt->str + stmnt->fs_type,
			       stmnt->str + stmnt->mnt_root,
			       stmnt->str + stmnt->mnt_point,
			       stmnt->str + stmnt->mnt_opts);
			free(stmnt);
		}
	}

	exit(0);
}
