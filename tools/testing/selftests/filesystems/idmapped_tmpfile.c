// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <linux/mount.h>
#include <linux/types.h>

#include "kselftest_harness.h"
#include "wrappers.h"
#include "utils.h"

/*
 * The test mount maps caller-visible ids [0, MAP_RANGE) onto the on-disk range
 * [MAP_HOST, MAP_HOST + MAP_RANGE).  An id outside [0, MAP_RANGE) therefore has
 * no mapping in the mount and is not representable in the filesystem.
 */
#define MAP_HOST  10000
#define MAP_RANGE 10000
#define UNMAPPED  50000

#ifndef MOUNT_ATTR_IDMAP
#define MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef __NR_mount_setattr
#define __NR_mount_setattr 442
#endif

static inline int sys_mount_setattr(int dfd, const char *path,
				    unsigned int flags,
				    struct mount_attr *attr, size_t size)
{
	return syscall(__NR_mount_setattr, dfd, path, flags, attr, size);
}

/*
 * Clone @path into a detached mount idmapped so that caller-visible ids
 * [0, MAP_RANGE) map onto the on-disk ids [MAP_HOST, MAP_HOST + MAP_RANGE).
 * Returns the mount fd, or -1 if idmapped mounts are not available.
 */
static int idmapped_clone(const char *path)
{
	struct mount_attr attr = {
		.attr_set = MOUNT_ATTR_IDMAP,
	};
	int fd_tree, userns_fd, ret;

	fd_tree = sys_open_tree(AT_FDCWD, path,
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	if (fd_tree < 0)
		return -1;

	userns_fd = get_userns_fd(MAP_HOST, 0, MAP_RANGE);
	if (userns_fd < 0) {
		close(fd_tree);
		return -1;
	}

	attr.userns_fd = userns_fd;
	ret = sys_mount_setattr(fd_tree, "", AT_EMPTY_PATH, &attr, sizeof(attr));
	close(userns_fd);
	if (ret) {
		close(fd_tree);
		return -1;
	}

	return fd_tree;
}

FIXTURE(idmapped_tmpfile) {
	char dir[64];	/* non-idmapped path to the layer directory */
};

FIXTURE_SETUP(idmapped_tmpfile)
{
	/* Private mount namespace so test mounts need no cleanup. */
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);
	ASSERT_EQ(sys_mount("tmpfs", "/tmp", "tmpfs", 0, NULL), 0);

	snprintf(self->dir, sizeof(self->dir), "/tmp/d");
	ASSERT_EQ(mkdir(self->dir, 0777), 0);
	/* World-writable so an unmapped caller still passes permission(). */
	ASSERT_EQ(chmod(self->dir, 0777), 0);
}

FIXTURE_TEARDOWN(idmapped_tmpfile)
{
}

/*
 * A caller whose fsuid/fsgid have no mapping in the idmapped mount must not be
 * able to create an O_TMPFILE.  Without the check in vfs_tmpfile() the inode
 * would be created owned by (uid_t)-1 and could then be linked into the
 * namespace.
 */
TEST_F(idmapped_tmpfile, unmapped_caller_is_refused)
{
	int mfd, fd;

	mfd = idmapped_clone(self->dir);
	if (mfd < 0)
		SKIP(return, "idmapped mounts not supported");

	/* Become a caller outside the mount's [0, MAP_RANGE) range. */
	setfsgid(UNMAPPED);
	setfsuid(UNMAPPED);
	ASSERT_EQ(setfsuid(-1), UNMAPPED);

	fd = openat(mfd, ".", O_TMPFILE | O_WRONLY, 0644);
	ASSERT_LT(fd, 0);
	EXPECT_EQ(errno, EOVERFLOW);
	if (fd >= 0)
		close(fd);

	EXPECT_EQ(close(mfd), 0);
}

/*
 * A mapped caller can create an O_TMPFILE and link it into the namespace; the
 * ownership round-trips through the mount idmap.  This is what makes refusing
 * the unmapped case above necessary in the first place.
 */
TEST_F(idmapped_tmpfile, mapped_caller_creates_and_links)
{
	char path[PATH_MAX];
	struct stat st;
	int mfd, fd;

	mfd = idmapped_clone(self->dir);
	if (mfd < 0)
		SKIP(return, "idmapped mounts not supported");

	/* Caller is uid/gid 0, which maps to MAP_HOST through the mount. */
	fd = openat(mfd, ".", O_TMPFILE | O_RDWR, 0600);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_uid, 0);
	EXPECT_EQ(st.st_gid, 0);

	/* The tmpfile is linkable: splice it into the directory. */
	ASSERT_EQ(linkat(fd, "", mfd, "linked", AT_EMPTY_PATH), 0);
	EXPECT_EQ(close(fd), 0);

	ASSERT_EQ(fstatat(mfd, "linked", &st, 0), 0);
	EXPECT_EQ(st.st_uid, 0);
	EXPECT_EQ(st.st_gid, 0);

	/* On the underlying, non-idmapped tmpfs it is stored as MAP_HOST. */
	snprintf(path, sizeof(path), "%s/linked", self->dir);
	ASSERT_EQ(stat(path, &st), 0);
	EXPECT_EQ(st.st_uid, MAP_HOST);
	EXPECT_EQ(st.st_gid, MAP_HOST);

	EXPECT_EQ(close(mfd), 0);
}

TEST_HARNESS_MAIN
