/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PIDFD_H
#define __PIDFD_H

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../kselftest.h"
#include "../clone3/clone3_selftests.h"

#ifndef P_PIDFD
#define P_PIDFD 3
#endif

#ifndef CLONE_NEWTIME
#define CLONE_NEWTIME 0x00000080
#endif

#ifndef CLONE_PIDFD
#define CLONE_PIDFD 0x00001000
#endif

#ifndef __NR_pidfd_open
#define __NR_pidfd_open -1
#endif

#ifndef __NR_pidfd_send_signal
#define __NR_pidfd_send_signal -1
#endif

#ifndef __NR_clone3
#define __NR_clone3 -1
#endif

#ifndef __NR_pidfd_getfd
#define __NR_pidfd_getfd -1
#endif

#ifndef PIDFD_NONBLOCK
#define PIDFD_NONBLOCK O_NONBLOCK
#endif

/*
 * The kernel reserves 300 pids via RESERVED_PIDS in kernel/pid.c
 * That means, when it wraps around any pid < 300 will be skipped.
 * So we need to use a pid > 300 in order to test recycling.
 */
#define PID_RECYCLE 1000

/*
 * Define a few custom error codes for the child process to clearly indicate
 * what is happening. This way we can tell the difference between a system
 * error, a test error, etc.
 */
#define PIDFD_PASS 0
#define PIDFD_FAIL 1
#define PIDFD_ERROR 2
#define PIDFD_SKIP 3
#define PIDFD_XFAIL 4

static inline int sys_waitid(int which, pid_t pid, siginfo_t *info, int options)
{
	return syscall(__NR_waitid, which, pid, info, options, NULL);
}

static inline int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		ksft_print_msg("waitpid returned -1, errno=%d\n", errno);
		return -1;
	}

	if (!WIFEXITED(status)) {
		ksft_print_msg(
		       "waitpid !WIFEXITED, WIFSIGNALED=%d, WTERMSIG=%d\n",
		       WIFSIGNALED(status), WTERMSIG(status));
		return -1;
	}

	ret = WEXITSTATUS(status);
	return ret;
}

static inline int sys_pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(__NR_pidfd_open, pid, flags);
}

static inline int sys_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
					unsigned int flags)
{
	return syscall(__NR_pidfd_send_signal, pidfd, sig, info, flags);
}

static inline int sys_pidfd_getfd(int pidfd, int fd, int flags)
{
	return syscall(__NR_pidfd_getfd, pidfd, fd, flags);
}

static inline int sys_memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

static inline pid_t create_child(int *pidfd, unsigned flags)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | flags,
		.exit_signal	= SIGCHLD,
		.pidfd		= ptr_to_u64(pidfd),
	};

	return sys_clone3(&args, sizeof(struct __clone_args));
}

static inline ssize_t read_nointr(int fd, void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static inline ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

#endif /* __PIDFD_H */
