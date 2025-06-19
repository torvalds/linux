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

FIXTURE(pidfs_setattr)
{
	pid_t child_pid;
	int child_pidfd;
};

FIXTURE_SETUP(pidfs_setattr)
{
	self->child_pid = create_child(&self->child_pidfd, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid, 0);

	if (self->child_pid == 0)
		_exit(EXIT_SUCCESS);
}

FIXTURE_TEARDOWN(pidfs_setattr)
{
	sys_waitid(P_PID, self->child_pid, NULL, WEXITED);
	EXPECT_EQ(close(self->child_pidfd), 0);
}

TEST_F(pidfs_setattr, no_chown)
{
	ASSERT_LT(fchown(self->child_pidfd, 1234, 5678), 0);
	ASSERT_EQ(errno, EOPNOTSUPP);
}

TEST_F(pidfs_setattr, no_chmod)
{
	ASSERT_LT(fchmod(self->child_pidfd, 0777), 0);
	ASSERT_EQ(errno, EOPNOTSUPP);
}

TEST_F(pidfs_setattr, no_exec)
{
	char *const argv[] = { NULL };
	char *const envp[] = { NULL };

	ASSERT_LT(execveat(self->child_pidfd, "", argv, envp, AT_EMPTY_PATH), 0);
	ASSERT_EQ(errno, EACCES);
}

TEST_HARNESS_MAIN
