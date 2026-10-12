// SPDX-License-Identifier: GPL-2.0
/*
 * F_SET_RW_HINT is refused on an immutable inode. Nothing is ever written
 * to it and it may be shared with everybody, like a namespace file or the
 * root of an empty mount namespace.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "../kselftest_harness.h"

/* <linux/fs.h> and <linux/fcntl.h> don't mix with the libc headers */
#ifndef FS_IOC_GETFLAGS
#define FS_IOC_GETFLAGS		_IOR('f', 1, long)
#define FS_IOC_SETFLAGS		_IOW('f', 2, long)
#endif
#ifndef FS_IMMUTABLE_FL
#define FS_IMMUTABLE_FL		0x00000010
#endif
#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE	1024
#endif
#ifndef F_GET_RW_HINT
#define F_GET_RW_HINT		(F_LINUX_SPECIFIC_BASE + 11)
#define F_SET_RW_HINT		(F_LINUX_SPECIFIC_BASE + 12)
#endif
#ifndef RWH_WRITE_LIFE_SHORT
#define RWH_WRITE_LIFE_SHORT	2
#endif
#ifndef UNSHARE_EMPTY_MNTNS
#define UNSHARE_EMPTY_MNTNS	0x00100000
#endif

static int set_hint(int fd, uint64_t hint)
{
	return fcntl(fd, F_SET_RW_HINT, &hint);
}

static long get_hint(int fd)
{
	uint64_t hint;

	if (fcntl(fd, F_GET_RW_HINT, &hint))
		return -1;
	return hint;
}

TEST(immutable_file)
{
	char path[] = "/tmp/rw_hint.XXXXXX";
	int fd, flags;

	if (geteuid())
		SKIP(return, "test requires root");

	fd = mkstemp(path);
	ASSERT_GE(fd, 0);
	unlink(path);
	ASSERT_EQ(set_hint(fd, RWH_WRITE_LIFE_SHORT), 0);
	EXPECT_EQ(get_hint(fd), RWH_WRITE_LIFE_SHORT);

	if (ioctl(fd, FS_IOC_GETFLAGS, &flags)) {
		close(fd);
		SKIP(return, "no file attributes on this filesystem");
	}
	flags |= FS_IMMUTABLE_FL;
	ASSERT_EQ(ioctl(fd, FS_IOC_SETFLAGS, &flags), 0);
	EXPECT_EQ(set_hint(fd, RWH_WRITE_LIFE_SHORT), -1);
	EXPECT_EQ(errno, EPERM);
	flags &= ~FS_IMMUTABLE_FL;
	ASSERT_EQ(ioctl(fd, FS_IOC_SETFLAGS, &flags), 0);
	EXPECT_EQ(set_hint(fd, RWH_WRITE_LIFE_SHORT), 0);
	close(fd);
}

TEST(namespace_file)
{
	int fd;

	if (geteuid())
		SKIP(return, "test requires root");

	fd = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(set_hint(fd, RWH_WRITE_LIFE_SHORT), -1);
	EXPECT_EQ(errno, EPERM);
	close(fd);
}

TEST(empty_mntns_root)
{
	int status;
	pid_t pid;

	if (geteuid())
		SKIP(return, "test requires root");

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		int fd;

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(errno == EINVAL ? 100 : 1);
		fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd < 0)
			_exit(2);
		if (set_hint(fd, RWH_WRITE_LIFE_SHORT) == 0)
			_exit(3);
		_exit(errno == EPERM ? 0 : 4);
	}
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	if (WEXITSTATUS(status) == 100)
		SKIP(return, "UNSHARE_EMPTY_MNTNS not supported");
	EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST_HARNESS_MAIN
