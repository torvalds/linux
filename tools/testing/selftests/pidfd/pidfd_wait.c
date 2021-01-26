/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest_harness.h"

#define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

/* Attempt to de-conflict with the selftests tree. */
#ifndef SKIP
#define SKIP(s, ...)	XFAIL(s, ##__VA_ARGS__)
#endif

static pid_t sys_clone3(struct clone_args *args)
{
	return syscall(__NR_clone3, args, sizeof(struct clone_args));
}

static int sys_waitid(int which, pid_t pid, siginfo_t *info, int options,
		      struct rusage *ru)
{
	return syscall(__NR_waitid, which, pid, info, options, ru);
}

TEST(wait_simple)
{
	int pidfd = -1, status = 0;
	pid_t parent_tid = -1;
	struct clone_args args = {
		.parent_tid = ptr_to_u64(&parent_tid),
		.pidfd = ptr_to_u64(&pidfd),
		.flags = CLONE_PIDFD | CLONE_PARENT_SETTID,
		.exit_signal = SIGCHLD,
	};
	int ret;
	pid_t pid;
	siginfo_t info = {
		.si_signo = 0,
	};

	pidfd = open("/proc/self", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	ASSERT_NE(pid, 0);
	EXPECT_EQ(close(pidfd), 0);
	pidfd = -1;

	pidfd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(pidfd, 0);

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	ASSERT_NE(pid, 0);
	EXPECT_EQ(close(pidfd), 0);
	pidfd = -1;

	pid = sys_clone3(&args);
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(EXIT_SUCCESS);

	pid = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	ASSERT_GE(pid, 0);
	ASSERT_EQ(WIFEXITED(info.si_status), true);
	ASSERT_EQ(WEXITSTATUS(info.si_status), 0);
	EXPECT_EQ(close(pidfd), 0);

	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_EXITED);
	ASSERT_EQ(info.si_pid, parent_tid);
}

TEST(wait_states)
{
	int pidfd = -1, status = 0;
	pid_t parent_tid = -1;
	struct clone_args args = {
		.parent_tid = ptr_to_u64(&parent_tid),
		.pidfd = ptr_to_u64(&pidfd),
		.flags = CLONE_PIDFD | CLONE_PARENT_SETTID,
		.exit_signal = SIGCHLD,
	};
	int ret;
	pid_t pid;
	siginfo_t info = {
		.si_signo = 0,
	};

	pid = sys_clone3(&args);
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		kill(getpid(), SIGSTOP);
		kill(getpid(), SIGSTOP);
		exit(EXIT_SUCCESS);
	}

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WSTOPPED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_STOPPED);
	ASSERT_EQ(info.si_pid, parent_tid);

	ASSERT_EQ(sys_pidfd_send_signal(pidfd, SIGCONT, NULL, 0), 0);

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WCONTINUED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_CONTINUED);
	ASSERT_EQ(info.si_pid, parent_tid);

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WUNTRACED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_STOPPED);
	ASSERT_EQ(info.si_pid, parent_tid);

	ASSERT_EQ(sys_pidfd_send_signal(pidfd, SIGKILL, NULL, 0), 0);

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_KILLED);
	ASSERT_EQ(info.si_pid, parent_tid);

	EXPECT_EQ(close(pidfd), 0);
}

TEST(wait_nonblock)
{
	int pidfd, status = 0;
	unsigned int flags = 0;
	pid_t parent_tid = -1;
	struct clone_args args = {
		.parent_tid = ptr_to_u64(&parent_tid),
		.flags = CLONE_PARENT_SETTID,
		.exit_signal = SIGCHLD,
	};
	int ret;
	pid_t pid;
	siginfo_t info = {
		.si_signo = 0,
	};

	/*
	 * Callers need to see ECHILD with non-blocking pidfds when no child
	 * processes exists.
	 */
	pidfd = sys_pidfd_open(getpid(), PIDFD_NONBLOCK);
	EXPECT_GE(pidfd, 0) {
		/* pidfd_open() doesn't support PIDFD_NONBLOCK. */
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "Skipping PIDFD_NONBLOCK test");
	}

	ret = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	ASSERT_LT(ret, 0);
	ASSERT_EQ(errno, ECHILD);
	EXPECT_EQ(close(pidfd), 0);

	pid = sys_clone3(&args);
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		kill(getpid(), SIGSTOP);
		exit(EXIT_SUCCESS);
	}

	pidfd = sys_pidfd_open(pid, PIDFD_NONBLOCK);
	EXPECT_GE(pidfd, 0) {
		/* pidfd_open() doesn't support PIDFD_NONBLOCK. */
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "Skipping PIDFD_NONBLOCK test");
	}

	flags = fcntl(pidfd, F_GETFL, 0);
	ASSERT_GT(flags, 0);
	ASSERT_GT((flags & O_NONBLOCK), 0);

	/*
	 * Callers need to see EAGAIN/EWOULDBLOCK with non-blocking pidfd when
	 * child processes exist but none have exited.
	 */
	ret = sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL);
	ASSERT_LT(ret, 0);
	ASSERT_EQ(errno, EAGAIN);

	/*
	 * Callers need to continue seeing 0 with non-blocking pidfd and
	 * WNOHANG raised explicitly when child processes exist but none have
	 * exited.
	 */
	ret = sys_waitid(P_PIDFD, pidfd, &info, WEXITED | WNOHANG, NULL);
	ASSERT_EQ(ret, 0);

	ASSERT_EQ(fcntl(pidfd, F_SETFL, (flags & ~O_NONBLOCK)), 0);

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WSTOPPED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_STOPPED);
	ASSERT_EQ(info.si_pid, parent_tid);

	ASSERT_EQ(sys_pidfd_send_signal(pidfd, SIGCONT, NULL, 0), 0);

	ASSERT_EQ(sys_waitid(P_PIDFD, pidfd, &info, WEXITED, NULL), 0);
	ASSERT_EQ(info.si_signo, SIGCHLD);
	ASSERT_EQ(info.si_code, CLD_EXITED);
	ASSERT_EQ(info.si_pid, parent_tid);

	EXPECT_EQ(close(pidfd), 0);
}

TEST_HARNESS_MAIN
