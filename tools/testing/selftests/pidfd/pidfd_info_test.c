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

#include "pidfd.h"
#include "../kselftest_harness.h"

FIXTURE(pidfd_info)
{
	pid_t child_pid1;
	int child_pidfd1;

	pid_t child_pid2;
	int child_pidfd2;

	pid_t child_pid3;
	int child_pidfd3;

	pid_t child_pid4;
	int child_pidfd4;
};

FIXTURE_SETUP(pidfd_info)
{
	int ret;
	int ipc_sockets[2];
	char c;

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	self->child_pid1 = create_child(&self->child_pidfd1, 0);
	EXPECT_GE(self->child_pid1, 0);

	if (self->child_pid1 == 0) {
		close(ipc_sockets[0]);

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	/* SIGKILL but don't reap. */
	EXPECT_EQ(sys_pidfd_send_signal(self->child_pidfd1, SIGKILL, NULL, 0), 0);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	self->child_pid2 = create_child(&self->child_pidfd2, 0);
	EXPECT_GE(self->child_pid2, 0);

	if (self->child_pid2 == 0) {
		close(ipc_sockets[0]);

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	/* SIGKILL and reap. */
	EXPECT_EQ(sys_pidfd_send_signal(self->child_pidfd2, SIGKILL, NULL, 0), 0);
	EXPECT_EQ(sys_waitid(P_PID, self->child_pid2, NULL, WEXITED), 0);

	self->child_pid3 = create_child(&self->child_pidfd3, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid3, 0);

	if (self->child_pid3 == 0)
		_exit(EXIT_SUCCESS);

	self->child_pid4 = create_child(&self->child_pidfd4, CLONE_NEWUSER | CLONE_NEWPID);
	EXPECT_GE(self->child_pid4, 0);

	if (self->child_pid4 == 0)
		_exit(EXIT_SUCCESS);

	EXPECT_EQ(sys_waitid(P_PID, self->child_pid4, NULL, WEXITED), 0);
}

FIXTURE_TEARDOWN(pidfd_info)
{
	sys_pidfd_send_signal(self->child_pidfd1, SIGKILL, NULL, 0);
	if (self->child_pidfd1 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd1));

	sys_waitid(P_PID, self->child_pid1, NULL, WEXITED);

	sys_pidfd_send_signal(self->child_pidfd2, SIGKILL, NULL, 0);
	if (self->child_pidfd2 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd2));

	sys_waitid(P_PID, self->child_pid2, NULL, WEXITED);
	sys_waitid(P_PID, self->child_pid3, NULL, WEXITED);
	sys_waitid(P_PID, self->child_pid4, NULL, WEXITED);
}

TEST_F(pidfd_info, sigkill_exit)
{
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID,
	};

	/* Process has exited but not been reaped so this must work. */
	ASSERT_EQ(ioctl(self->child_pidfd1, PIDFD_GET_INFO, &info), 0);

	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(self->child_pidfd1, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	/* Process has exited but not been reaped, so no PIDFD_INFO_EXIT information yet. */
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
}

TEST_F(pidfd_info, sigkill_reaped)
{
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID,
	};

	/* Process has already been reaped and PIDFD_INFO_EXIT hasn't been set. */
	ASSERT_NE(ioctl(self->child_pidfd2, PIDFD_GET_INFO, &info), 0);
	ASSERT_EQ(errno, ESRCH);

	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(self->child_pidfd2, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_TRUE(WIFSIGNALED(info.exit_code));
	ASSERT_EQ(WTERMSIG(info.exit_code), SIGKILL);
}

TEST_F(pidfd_info, success_exit)
{
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID,
	};

	/* Process has exited but not been reaped so this must work. */
	ASSERT_EQ(ioctl(self->child_pidfd3, PIDFD_GET_INFO, &info), 0);

	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(self->child_pidfd3, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	/* Process has exited but not been reaped, so no PIDFD_INFO_EXIT information yet. */
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
}

TEST_HARNESS_MAIN
