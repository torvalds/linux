// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/types.h>
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
#include <sys/stat.h>
#include <linux/ioctl.h>

#include "pidfd.h"
#include "../clone3/clone3_selftests.h"
#include "../kselftest_harness.h"

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

enum {
	PIDFD_NS_USER,
	PIDFD_NS_MNT,
	PIDFD_NS_PID,
	PIDFD_NS_UTS,
	PIDFD_NS_IPC,
	PIDFD_NS_NET,
	PIDFD_NS_CGROUP,
	PIDFD_NS_PIDCLD,
	PIDFD_NS_TIME,
	PIDFD_NS_TIMECLD,
	PIDFD_NS_MAX
};

const struct ns_info {
	const char *name;
	int flag;
	unsigned int pidfd_ioctl;
} ns_info[] = {
	[PIDFD_NS_USER]    = { "user",              CLONE_NEWUSER,   PIDFD_GET_USER_NAMESPACE,              },
	[PIDFD_NS_MNT]     = { "mnt",               CLONE_NEWNS,     PIDFD_GET_MNT_NAMESPACE,               },
	[PIDFD_NS_PID]     = { "pid",               CLONE_NEWPID,    PIDFD_GET_PID_NAMESPACE,               },
	[PIDFD_NS_UTS]     = { "uts",               CLONE_NEWUTS,    PIDFD_GET_UTS_NAMESPACE,               },
	[PIDFD_NS_IPC]     = { "ipc",               CLONE_NEWIPC,    PIDFD_GET_IPC_NAMESPACE,               },
	[PIDFD_NS_NET]     = { "net",               CLONE_NEWNET,    PIDFD_GET_NET_NAMESPACE,               },
	[PIDFD_NS_CGROUP]  = { "cgroup",            CLONE_NEWCGROUP, PIDFD_GET_CGROUP_NAMESPACE,            },
	[PIDFD_NS_TIME]	   = { "time",              CLONE_NEWTIME,   PIDFD_GET_TIME_NAMESPACE,              },
	[PIDFD_NS_PIDCLD]  = { "pid_for_children",  0,               PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE,  },
	[PIDFD_NS_TIMECLD] = { "time_for_children", 0,               PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE, },
};

FIXTURE(current_nsset)
{
	pid_t pid;
	int pidfd;
	int nsfds[PIDFD_NS_MAX];
	int child_pidfd_derived_nsfds[PIDFD_NS_MAX];

	pid_t child_pid_exited;
	int child_pidfd_exited;

	pid_t child_pid1;
	int child_pidfd1;
	int child_nsfds1[PIDFD_NS_MAX];
	int child_pidfd_derived_nsfds1[PIDFD_NS_MAX];

	pid_t child_pid2;
	int child_pidfd2;
	int child_nsfds2[PIDFD_NS_MAX];
	int child_pidfd_derived_nsfds2[PIDFD_NS_MAX];
};

static int sys_waitid(int which, pid_t pid, int options)
{
	return syscall(__NR_waitid, which, pid, NULL, options, NULL);
}

pid_t create_child(int *pidfd, unsigned flags)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | flags,
		.exit_signal	= SIGCHLD,
		.pidfd		= ptr_to_u64(pidfd),
	};

	return sys_clone3(&args, sizeof(struct clone_args));
}

static bool switch_timens(void)
{
	int fd, ret;

	if (unshare(CLONE_NEWTIME))
		return false;

	fd = open("/proc/self/ns/time_for_children", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	ret = setns(fd, CLONE_NEWTIME);
	close(fd);
	return ret == 0;
}

static ssize_t read_nointr(int fd, void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

FIXTURE_SETUP(current_nsset)
{
	int i, proc_fd, ret;
	int ipc_sockets[2];
	char c;

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		self->nsfds[i]				= -EBADF;
		self->child_nsfds1[i]			= -EBADF;
		self->child_nsfds2[i]			= -EBADF;
		self->child_pidfd_derived_nsfds[i]	= -EBADF;
		self->child_pidfd_derived_nsfds1[i]	= -EBADF;
		self->child_pidfd_derived_nsfds2[i]	= -EBADF;
	}

	proc_fd = open("/proc/self/ns", O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(proc_fd, 0) {
		TH_LOG("%m - Failed to open /proc/self/ns");
	}

	self->pid = getpid();
	self->pidfd = sys_pidfd_open(self->pid, 0);
	EXPECT_GT(self->pidfd, 0) {
		TH_LOG("%m - Failed to open pidfd for process %d", self->pid);
	}

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		self->nsfds[i] = openat(proc_fd, info->name, O_RDONLY | O_CLOEXEC);
		if (self->nsfds[i] < 0) {
			EXPECT_EQ(errno, ENOENT) {
				TH_LOG("%m - Failed to open %s namespace for process %d",
				       info->name, self->pid);
			}
		}

		self->child_pidfd_derived_nsfds[i] = ioctl(self->pidfd, info->pidfd_ioctl, 0);
		if (self->child_pidfd_derived_nsfds[i] < 0) {
			EXPECT_EQ(errno, EOPNOTSUPP) {
				TH_LOG("%m - Failed to derive %s namespace from pidfd of process %d",
				       info->name, self->pid);
			}
		}
	}

	/* Create task that exits right away. */
	self->child_pid_exited = create_child(&self->child_pidfd_exited, 0);
	EXPECT_GE(self->child_pid_exited, 0);

	if (self->child_pid_exited == 0) {
		if (self->nsfds[PIDFD_NS_USER] >= 0 && unshare(CLONE_NEWUSER) < 0)
			_exit(EXIT_FAILURE);
		if (self->nsfds[PIDFD_NS_NET] >= 0 && unshare(CLONE_NEWNET) < 0)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}

	ASSERT_EQ(sys_waitid(P_PID, self->child_pid_exited, WEXITED | WNOWAIT), 0);

	self->pidfd = sys_pidfd_open(self->pid, 0);
	EXPECT_GE(self->pidfd, 0) {
		TH_LOG("%m - Failed to open pidfd for process %d", self->pid);
	}

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	/* Create tasks that will be stopped. */
	if (self->nsfds[PIDFD_NS_USER] >= 0 && self->nsfds[PIDFD_NS_PID] >= 0)
		self->child_pid1 = create_child(&self->child_pidfd1, CLONE_NEWUSER | CLONE_NEWPID);
	else if (self->nsfds[PIDFD_NS_PID] >= 0)
		self->child_pid1 = create_child(&self->child_pidfd1, CLONE_NEWPID);
	else if (self->nsfds[PIDFD_NS_USER] >= 0)
		self->child_pid1 = create_child(&self->child_pidfd1, CLONE_NEWUSER);
	else
		self->child_pid1 = create_child(&self->child_pidfd1, 0);
	EXPECT_GE(self->child_pid1, 0);

	if (self->child_pid1 == 0) {
		close(ipc_sockets[0]);

		if (self->nsfds[PIDFD_NS_MNT] >= 0 && unshare(CLONE_NEWNS) < 0) {
			TH_LOG("%m - Failed to unshare mount namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_CGROUP] >= 0 && unshare(CLONE_NEWCGROUP) < 0) {
			TH_LOG("%m - Failed to unshare cgroup namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_IPC] >= 0 && unshare(CLONE_NEWIPC) < 0) {
			TH_LOG("%m - Failed to unshare ipc namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_UTS] >= 0 && unshare(CLONE_NEWUTS) < 0) {
			TH_LOG("%m - Failed to unshare uts namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_NET] >= 0 && unshare(CLONE_NEWNET) < 0) {
			TH_LOG("%m - Failed to unshare net namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_TIME] >= 0 && !switch_timens()) {
			TH_LOG("%m - Failed to unshare time namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	close(ipc_sockets[1]);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	close(ipc_sockets[0]);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	if (self->nsfds[PIDFD_NS_USER] >= 0 && self->nsfds[PIDFD_NS_PID] >= 0)
		self->child_pid2 = create_child(&self->child_pidfd2, CLONE_NEWUSER | CLONE_NEWPID);
	else if (self->nsfds[PIDFD_NS_PID] >= 0)
		self->child_pid2 = create_child(&self->child_pidfd2, CLONE_NEWPID);
	else if (self->nsfds[PIDFD_NS_USER] >= 0)
		self->child_pid2 = create_child(&self->child_pidfd2, CLONE_NEWUSER);
	else
		self->child_pid2 = create_child(&self->child_pidfd2, 0);
	EXPECT_GE(self->child_pid2, 0);

	if (self->child_pid2 == 0) {
		close(ipc_sockets[0]);

		if (self->nsfds[PIDFD_NS_MNT] >= 0 && unshare(CLONE_NEWNS) < 0) {
			TH_LOG("%m - Failed to unshare mount namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_CGROUP] >= 0 && unshare(CLONE_NEWCGROUP) < 0) {
			TH_LOG("%m - Failed to unshare cgroup namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_IPC] >= 0 && unshare(CLONE_NEWIPC) < 0) {
			TH_LOG("%m - Failed to unshare ipc namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_UTS] >= 0 && unshare(CLONE_NEWUTS) < 0) {
			TH_LOG("%m - Failed to unshare uts namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_NET] >= 0 && unshare(CLONE_NEWNET) < 0) {
			TH_LOG("%m - Failed to unshare net namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}
		if (self->nsfds[PIDFD_NS_TIME] >= 0 && !switch_timens()) {
			TH_LOG("%m - Failed to unshare time namespace for process %d", self->pid);
			_exit(EXIT_FAILURE);
		}

		if (write_nointr(ipc_sockets[1], "1", 1) < 0)
			_exit(EXIT_FAILURE);

		close(ipc_sockets[1]);

		pause();
		_exit(EXIT_SUCCESS);
	}

	close(ipc_sockets[1]);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &c, 1), 1);
	close(ipc_sockets[0]);

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		char p[100];

		const struct ns_info *info = &ns_info[i];

		self->nsfds[i] = openat(proc_fd, info->name, O_RDONLY | O_CLOEXEC);
		if (self->nsfds[i] < 0) {
			EXPECT_EQ(errno, ENOENT) {
				TH_LOG("%m - Failed to open %s namespace for process %d",
				       info->name, self->pid);
			}
		}

		ret = snprintf(p, sizeof(p), "/proc/%d/ns/%s",
			       self->child_pid1, info->name);
		EXPECT_GT(ret, 0);
		EXPECT_LT(ret, sizeof(p));

		self->child_nsfds1[i] = open(p, O_RDONLY | O_CLOEXEC);
		if (self->child_nsfds1[i] < 0) {
			EXPECT_EQ(errno, ENOENT) {
				TH_LOG("%m - Failed to open %s namespace for process %d",
				       info->name, self->child_pid1);
			}
		}

		ret = snprintf(p, sizeof(p), "/proc/%d/ns/%s",
			       self->child_pid2, info->name);
		EXPECT_GT(ret, 0);
		EXPECT_LT(ret, sizeof(p));

		self->child_nsfds2[i] = open(p, O_RDONLY | O_CLOEXEC);
		if (self->child_nsfds2[i] < 0) {
			EXPECT_EQ(errno, ENOENT) {
				TH_LOG("%m - Failed to open %s namespace for process %d",
				       info->name, self->child_pid1);
			}
		}

		self->child_pidfd_derived_nsfds1[i] = ioctl(self->child_pidfd1, info->pidfd_ioctl, 0);
		if (self->child_pidfd_derived_nsfds1[i] < 0) {
			EXPECT_EQ(errno, EOPNOTSUPP) {
				TH_LOG("%m - Failed to derive %s namespace from pidfd of process %d",
				       info->name, self->child_pid1);
			}
		}

		self->child_pidfd_derived_nsfds2[i] = ioctl(self->child_pidfd2, info->pidfd_ioctl, 0);
		if (self->child_pidfd_derived_nsfds2[i] < 0) {
			EXPECT_EQ(errno, EOPNOTSUPP) {
				TH_LOG("%m - Failed to derive %s namespace from pidfd of process %d",
				       info->name, self->child_pid2);
			}
		}
	}

	close(proc_fd);
}

FIXTURE_TEARDOWN(current_nsset)
{
	int i;

	ASSERT_EQ(sys_pidfd_send_signal(self->child_pidfd1,
					SIGKILL, NULL, 0), 0);
	ASSERT_EQ(sys_pidfd_send_signal(self->child_pidfd2,
					SIGKILL, NULL, 0), 0);

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		if (self->nsfds[i] >= 0)
			close(self->nsfds[i]);
		if (self->child_nsfds1[i] >= 0)
			close(self->child_nsfds1[i]);
		if (self->child_nsfds2[i] >= 0)
			close(self->child_nsfds2[i]);
		if (self->child_pidfd_derived_nsfds[i] >= 0)
			close(self->child_pidfd_derived_nsfds[i]);
		if (self->child_pidfd_derived_nsfds1[i] >= 0)
			close(self->child_pidfd_derived_nsfds1[i]);
		if (self->child_pidfd_derived_nsfds2[i] >= 0)
			close(self->child_pidfd_derived_nsfds2[i]);
	}

	if (self->child_pidfd1 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd1));
	if (self->child_pidfd2 >= 0)
		EXPECT_EQ(0, close(self->child_pidfd2));
	ASSERT_EQ(sys_waitid(P_PID, self->child_pid_exited, WEXITED), 0);
	ASSERT_EQ(sys_waitid(P_PID, self->child_pid1, WEXITED), 0);
	ASSERT_EQ(sys_waitid(P_PID, self->child_pid2, WEXITED), 0);
}

static int preserve_ns(const int pid, const char *ns)
{
	int ret;
	char path[50];

	ret = snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns);
	if (ret < 0 || (size_t)ret >= sizeof(path))
		return -EIO;

	return open(path, O_RDONLY | O_CLOEXEC);
}

static int in_same_namespace(int ns_fd1, pid_t pid2, const char *ns)
{
	int ns_fd2 = -EBADF;
	int ret = -1;
	struct stat ns_st1, ns_st2;

	ret = fstat(ns_fd1, &ns_st1);
	if (ret < 0)
		return -1;

	ns_fd2 = preserve_ns(pid2, ns);
	if (ns_fd2 < 0)
		return -1;

	ret = fstat(ns_fd2, &ns_st2);
	close(ns_fd2);
	if (ret < 0)
		return -1;

	/* processes are in the same namespace */
	if ((ns_st1.st_dev == ns_st2.st_dev) &&
	    (ns_st1.st_ino == ns_st2.st_ino))
		return 1;

	/* processes are in different namespaces */
	return 0;
}

/* Test that we can't pass garbage to the kernel. */
TEST_F(current_nsset, invalid_flags)
{
	ASSERT_NE(setns(self->pidfd, 0), 0);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_NE(setns(self->pidfd, -1), 0);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_NE(setns(self->pidfd, CLONE_VM), 0);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_NE(setns(self->pidfd, CLONE_NEWUSER | CLONE_VM), 0);
	EXPECT_EQ(errno, EINVAL);
}

/* Test that we can't attach to a task that has already exited. */
TEST_F(current_nsset, pidfd_exited_child)
{
	int i;
	pid_t pid;

	ASSERT_NE(setns(self->child_pidfd_exited, CLONE_NEWUSER | CLONE_NEWNET),
		  0);
	EXPECT_EQ(errno, ESRCH);

	pid = getpid();
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		/* Verify that we haven't changed any namespaces. */
		if (self->nsfds[i] >= 0)
			ASSERT_EQ(in_same_namespace(self->nsfds[i], pid, info->name), 1);
	}
}

TEST_F(current_nsset, pidfd_incremental_setns)
{
	int i;
	pid_t pid;

	pid = getpid();
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		int nsfd;

		if (self->child_nsfds1[i] < 0)
			continue;

		if (info->flag) {
			ASSERT_EQ(setns(self->child_pidfd1, info->flag), 0) {
				TH_LOG("%m - Failed to setns to %s namespace of %d via pidfd %d",
				       info->name, self->child_pid1,
				       self->child_pidfd1);
			}
		}

		/* Verify that we have changed to the correct namespaces. */
		if (info->flag == CLONE_NEWPID)
			nsfd = self->nsfds[i];
		else
			nsfd = self->child_nsfds1[i];
		ASSERT_EQ(in_same_namespace(nsfd, pid, info->name), 1) {
			TH_LOG("setns failed to place us correctly into %s namespace of %d via pidfd %d",
			       info->name, self->child_pid1,
			       self->child_pidfd1);
		}
		TH_LOG("Managed to correctly setns to %s namespace of %d via pidfd %d",
		       info->name, self->child_pid1, self->child_pidfd1);
	}
}

TEST_F(current_nsset, nsfd_incremental_setns)
{
	int i;
	pid_t pid;

	pid = getpid();
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		int nsfd;

		if (self->child_nsfds1[i] < 0)
			continue;

		if (info->flag) {
			ASSERT_EQ(setns(self->child_nsfds1[i], info->flag), 0) {
				TH_LOG("%m - Failed to setns to %s namespace of %d via nsfd %d",
				       info->name, self->child_pid1,
				       self->child_nsfds1[i]);
			}
		}

		/* Verify that we have changed to the correct namespaces. */
		if (info->flag == CLONE_NEWPID)
			nsfd = self->nsfds[i];
		else
			nsfd = self->child_nsfds1[i];
		ASSERT_EQ(in_same_namespace(nsfd, pid, info->name), 1) {
			TH_LOG("setns failed to place us correctly into %s namespace of %d via nsfd %d",
			       info->name, self->child_pid1,
			       self->child_nsfds1[i]);
		}
		TH_LOG("Managed to correctly setns to %s namespace of %d via nsfd %d",
		       info->name, self->child_pid1, self->child_nsfds1[i]);
	}
}

TEST_F(current_nsset, pidfd_derived_nsfd_incremental_setns)
{
	int i;
	pid_t pid;

	pid = getpid();
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		int nsfd;

		if (self->child_pidfd_derived_nsfds1[i] < 0)
			continue;

		if (info->flag) {
			ASSERT_EQ(setns(self->child_pidfd_derived_nsfds1[i], info->flag), 0) {
				TH_LOG("%m - Failed to setns to %s namespace of %d via nsfd %d",
				       info->name, self->child_pid1,
				       self->child_pidfd_derived_nsfds1[i]);
			}
		}

		/* Verify that we have changed to the correct namespaces. */
		if (info->flag == CLONE_NEWPID)
			nsfd = self->child_pidfd_derived_nsfds[i];
		else
			nsfd = self->child_pidfd_derived_nsfds1[i];
		ASSERT_EQ(in_same_namespace(nsfd, pid, info->name), 1) {
			TH_LOG("setns failed to place us correctly into %s namespace of %d via nsfd %d",
			       info->name, self->child_pid1,
			       self->child_pidfd_derived_nsfds1[i]);
		}
		TH_LOG("Managed to correctly setns to %s namespace of %d via nsfd %d",
		       info->name, self->child_pid1, self->child_pidfd_derived_nsfds1[i]);
	}
}

TEST_F(current_nsset, pidfd_one_shot_setns)
{
	unsigned flags = 0;
	int i;
	pid_t pid;

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];

		if (self->child_nsfds1[i] < 0)
			continue;

		flags |= info->flag;
		TH_LOG("Adding %s namespace of %d to list of namespaces to attach to",
		       info->name, self->child_pid1);
	}

	ASSERT_EQ(setns(self->child_pidfd1, flags), 0) {
		TH_LOG("%m - Failed to setns to namespaces of %d",
		       self->child_pid1);
	}

	pid = getpid();
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];
		int nsfd;

		if (self->child_nsfds1[i] < 0)
			continue;

		/* Verify that we have changed to the correct namespaces. */
		if (info->flag == CLONE_NEWPID)
			nsfd = self->nsfds[i];
		else
			nsfd = self->child_nsfds1[i];
		ASSERT_EQ(in_same_namespace(nsfd, pid, info->name), 1) {
			TH_LOG("setns failed to place us correctly into %s namespace of %d",
			       info->name, self->child_pid1);
		}
		TH_LOG("Managed to correctly setns to %s namespace of %d",
		       info->name, self->child_pid1);
	}
}

TEST_F(current_nsset, no_foul_play)
{
	unsigned flags = 0;
	int i;

	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];

		if (self->child_nsfds1[i] < 0)
			continue;

		flags |= info->flag;
		if (info->flag) /* No use logging pid_for_children. */
			TH_LOG("Adding %s namespace of %d to list of namespaces to attach to",
			       info->name, self->child_pid1);
	}

	ASSERT_EQ(setns(self->child_pidfd1, flags), 0) {
		TH_LOG("%m - Failed to setns to namespaces of %d vid pidfd %d",
		       self->child_pid1, self->child_pidfd1);
	}

	/*
	 * Can't setns to a user namespace outside of our hierarchy since we
	 * don't have caps in there and didn't create it. That means that under
	 * no circumstances should we be able to setns to any of the other
	 * ones since they aren't owned by our user namespace.
	 */
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];

		if (self->child_nsfds2[i] < 0 || !info->flag)
			continue;

		ASSERT_NE(setns(self->child_pidfd2, info->flag), 0) {
			TH_LOG("Managed to setns to %s namespace of %d via pidfd %d",
			       info->name, self->child_pid2,
			       self->child_pidfd2);
		}
		TH_LOG("%m - Correctly failed to setns to %s namespace of %d via pidfd %d",
		       info->name, self->child_pid2,
		       self->child_pidfd2);

		ASSERT_NE(setns(self->child_nsfds2[i], info->flag), 0) {
			TH_LOG("Managed to setns to %s namespace of %d via nsfd %d",
			       info->name, self->child_pid2,
			       self->child_nsfds2[i]);
		}
		TH_LOG("%m - Correctly failed to setns to %s namespace of %d via nsfd %d",
		       info->name, self->child_pid2,
		       self->child_nsfds2[i]);
	}

	/*
	 * Can't setns to a user namespace outside of our hierarchy since we
	 * don't have caps in there and didn't create it. That means that under
	 * no circumstances should we be able to setns to any of the other
	 * ones since they aren't owned by our user namespace.
	 */
	for (i = 0; i < PIDFD_NS_MAX; i++) {
		const struct ns_info *info = &ns_info[i];

		if (self->child_pidfd_derived_nsfds2[i] < 0 || !info->flag)
			continue;

		ASSERT_NE(setns(self->child_pidfd_derived_nsfds2[i], info->flag), 0) {
			TH_LOG("Managed to setns to %s namespace of %d via nsfd %d",
			       info->name, self->child_pid2,
			       self->child_pidfd_derived_nsfds2[i]);
		}
		TH_LOG("%m - Correctly failed to setns to %s namespace of %d via nsfd %d",
		       info->name, self->child_pid2,
		       self->child_pidfd_derived_nsfds2[i]);
	}
}

TEST(setns_einval)
{
	int fd;

	fd = sys_memfd_create("rostock", 0);
	EXPECT_GT(fd, 0);

	ASSERT_NE(setns(fd, 0), 0);
	EXPECT_EQ(errno, EINVAL);
	close(fd);
}

TEST_HARNESS_MAIN
