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
#include <sys/ioctl.h>
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

#ifndef PIDFD_SELF_THREAD
#define PIDFD_SELF_THREAD		-10000 /* Current thread. */
#endif

#ifndef PIDFD_SELF_THREAD_GROUP
#define PIDFD_SELF_THREAD_GROUP		-20000 /* Current thread group leader. */
#endif

#ifndef PIDFD_SELF
#define PIDFD_SELF		PIDFD_SELF_THREAD
#endif

#ifndef PIDFD_SELF_PROCESS
#define PIDFD_SELF_PROCESS	PIDFD_SELF_THREAD_GROUP
#endif

#ifndef PIDFS_IOCTL_MAGIC
#define PIDFS_IOCTL_MAGIC 0xFF
#endif

#ifndef PIDFD_GET_CGROUP_NAMESPACE
#define PIDFD_GET_CGROUP_NAMESPACE            _IO(PIDFS_IOCTL_MAGIC, 1)
#endif

#ifndef PIDFD_GET_IPC_NAMESPACE
#define PIDFD_GET_IPC_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 2)
#endif

#ifndef PIDFD_GET_MNT_NAMESPACE
#define PIDFD_GET_MNT_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 3)
#endif

#ifndef PIDFD_GET_NET_NAMESPACE
#define PIDFD_GET_NET_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 4)
#endif

#ifndef PIDFD_GET_PID_NAMESPACE
#define PIDFD_GET_PID_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 5)
#endif

#ifndef PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE
#define PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE  _IO(PIDFS_IOCTL_MAGIC, 6)
#endif

#ifndef PIDFD_GET_TIME_NAMESPACE
#define PIDFD_GET_TIME_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 7)
#endif

#ifndef PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE
#define PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 8)
#endif

#ifndef PIDFD_GET_USER_NAMESPACE
#define PIDFD_GET_USER_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 9)
#endif

#ifndef PIDFD_GET_UTS_NAMESPACE
#define PIDFD_GET_UTS_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 10)
#endif

#ifndef PIDFD_GET_INFO
#define PIDFD_GET_INFO			      _IOWR(PIDFS_IOCTL_MAGIC, 11, struct pidfd_info)
#endif

#ifndef PIDFD_INFO_PID
#define PIDFD_INFO_PID			(1UL << 0) /* Always returned, even if not requested */
#endif

#ifndef PIDFD_INFO_CREDS
#define PIDFD_INFO_CREDS		(1UL << 1) /* Always returned, even if not requested */
#endif

#ifndef PIDFD_INFO_CGROUPID
#define PIDFD_INFO_CGROUPID		(1UL << 2) /* Always returned if available, even if not requested */
#endif

#ifndef PIDFD_INFO_EXIT
#define PIDFD_INFO_EXIT			(1UL << 3) /* Always returned if available, even if not requested */
#endif

#ifndef PIDFD_THREAD
#define PIDFD_THREAD O_EXCL
#endif

struct pidfd_info {
	__u64 mask;
	__u64 cgroupid;
	__u32 pid;
	__u32 tgid;
	__u32 ppid;
	__u32 ruid;
	__u32 rgid;
	__u32 euid;
	__u32 egid;
	__u32 suid;
	__u32 sgid;
	__u32 fsuid;
	__u32 fsgid;
	__s32 exit_code;
};

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

static inline int sys_execveat(int dirfd, const char *pathname,
			       char *const argv[], char *const envp[],
			       int flags)
{
        return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}

#endif /* __PIDFD_H */
