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

#include "../kselftest.h"

#ifndef P_PIDFD
#define P_PIDFD 3
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

int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
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

#endif /* __PIDFD_H */
