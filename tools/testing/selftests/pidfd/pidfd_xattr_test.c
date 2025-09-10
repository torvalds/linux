// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/types.h>
#include <poll.h>
#include <pthread.h>
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
#include <sys/xattr.h>

#include "pidfd.h"
#include "../kselftest_harness.h"

FIXTURE(pidfs_xattr)
{
	pid_t child_pid;
	int child_pidfd;
};

FIXTURE_SETUP(pidfs_xattr)
{
	self->child_pid = create_child(&self->child_pidfd, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid, 0);

	if (self->child_pid == 0)
		_exit(EXIT_SUCCESS);
}

FIXTURE_TEARDOWN(pidfs_xattr)
{
	sys_waitid(P_PID, self->child_pid, NULL, WEXITED);
}

TEST_F(pidfs_xattr, set_get_list_xattr_multiple)
{
	int ret, i;
	char xattr_name[32];
	char xattr_value[32];
	char buf[32];
	const int num_xattrs = 10;
	char list[PATH_MAX] = {};

	for (i = 0; i < num_xattrs; i++) {
		snprintf(xattr_name, sizeof(xattr_name), "trusted.testattr%d", i);
		snprintf(xattr_value, sizeof(xattr_value), "testvalue%d", i);
		ret = fsetxattr(self->child_pidfd, xattr_name, xattr_value, strlen(xattr_value), 0);
		ASSERT_EQ(ret, 0);
	}

	for (i = 0; i < num_xattrs; i++) {
		snprintf(xattr_name, sizeof(xattr_name), "trusted.testattr%d", i);
		snprintf(xattr_value, sizeof(xattr_value), "testvalue%d", i);
		memset(buf, 0, sizeof(buf));
		ret = fgetxattr(self->child_pidfd, xattr_name, buf, sizeof(buf));
		ASSERT_EQ(ret, strlen(xattr_value));
		ASSERT_EQ(strcmp(buf, xattr_value), 0);
	}

	ret = flistxattr(self->child_pidfd, list, sizeof(list));
	ASSERT_GT(ret, 0);
	for (i = 0; i < num_xattrs; i++) {
		snprintf(xattr_name, sizeof(xattr_name), "trusted.testattr%d", i);
		bool found = false;
		for (char *it = list; it < list + ret; it += strlen(it) + 1) {
			if (strcmp(it, xattr_name))
				continue;
			found = true;
			break;
		}
		ASSERT_TRUE(found);
	}

	for (i = 0; i < num_xattrs; i++) {
		snprintf(xattr_name, sizeof(xattr_name), "trusted.testattr%d", i);
		ret = fremovexattr(self->child_pidfd, xattr_name);
		ASSERT_EQ(ret, 0);

		ret = fgetxattr(self->child_pidfd, xattr_name, buf, sizeof(buf));
		ASSERT_EQ(ret, -1);
		ASSERT_EQ(errno, ENODATA);
	}
}

TEST_F(pidfs_xattr, set_get_list_xattr_persistent)
{
	int ret;
	char buf[32];
	char list[PATH_MAX] = {};

	ret = fsetxattr(self->child_pidfd, "trusted.persistent", "persistent value", strlen("persistent value"), 0);
	ASSERT_EQ(ret, 0);

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->child_pidfd, "trusted.persistent", buf, sizeof(buf));
	ASSERT_EQ(ret, strlen("persistent value"));
	ASSERT_EQ(strcmp(buf, "persistent value"), 0);

	ret = flistxattr(self->child_pidfd, list, sizeof(list));
	ASSERT_GT(ret, 0);
	ASSERT_EQ(strcmp(list, "trusted.persistent"), 0)

	ASSERT_EQ(close(self->child_pidfd), 0);
	self->child_pidfd = -EBADF;
	sleep(2);

	self->child_pidfd = sys_pidfd_open(self->child_pid, 0);
	ASSERT_GE(self->child_pidfd, 0);

	memset(buf, 0, sizeof(buf));
	ret = fgetxattr(self->child_pidfd, "trusted.persistent", buf, sizeof(buf));
	ASSERT_EQ(ret, strlen("persistent value"));
	ASSERT_EQ(strcmp(buf, "persistent value"), 0);

	ret = flistxattr(self->child_pidfd, list, sizeof(list));
	ASSERT_GT(ret, 0);
	ASSERT_EQ(strcmp(list, "trusted.persistent"), 0);
}

TEST_HARNESS_MAIN
