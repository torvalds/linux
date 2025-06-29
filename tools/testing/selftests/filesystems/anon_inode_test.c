// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#include "../kselftest_harness.h"
#include "wrappers.h"

TEST(anon_inode_no_chown)
{
	int fd_context;

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_LT(fchown(fd_context, 1234, 5678), 0);
	ASSERT_EQ(errno, EOPNOTSUPP);

	EXPECT_EQ(close(fd_context), 0);
}

TEST(anon_inode_no_chmod)
{
	int fd_context;

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_LT(fchmod(fd_context, 0777), 0);
	ASSERT_EQ(errno, EOPNOTSUPP);

	EXPECT_EQ(close(fd_context), 0);
}

TEST(anon_inode_no_exec)
{
	int fd_context;

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_LT(execveat(fd_context, "", NULL, NULL, AT_EMPTY_PATH), 0);
	ASSERT_EQ(errno, EACCES);

	EXPECT_EQ(close(fd_context), 0);
}

TEST(anon_inode_no_open)
{
	int fd_context;

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_GE(dup2(fd_context, 500), 0);
	ASSERT_EQ(close(fd_context), 0);
	fd_context = 500;

	ASSERT_LT(open("/proc/self/fd/500", 0), 0);
	ASSERT_EQ(errno, ENXIO);

	EXPECT_EQ(close(fd_context), 0);
}

TEST_HARNESS_MAIN

