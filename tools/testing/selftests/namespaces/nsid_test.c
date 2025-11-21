// SPDX-License-Identifier: GPL-2.0

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/mount.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/nsfs.h>
#include "../kselftest_harness.h"

TEST(nsid_mntns_basic)
{
	__u64 mnt_ns_id = 0;
	int fd_mntns;
	int ret;

	/* Open the current mount namespace */
	fd_mntns = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_GE(fd_mntns, 0);

	/* Get the mount namespace ID */
	ret = ioctl(fd_mntns, NS_GET_MNTNS_ID, &mnt_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(mnt_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 mnt_ns_id2 = 0;
	ret = ioctl(fd_mntns, NS_GET_ID, &mnt_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(mnt_ns_id, mnt_ns_id2);

	close(fd_mntns);
}

TEST(nsid_mntns_separate)
{
	__u64 parent_mnt_ns_id = 0;
	__u64 child_mnt_ns_id = 0;
	int fd_parent_mntns, fd_child_mntns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's mount namespace ID */
	fd_parent_mntns = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_GE(fd_parent_mntns, 0);
	ret = ioctl(fd_parent_mntns, NS_GET_ID, &parent_mnt_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_mnt_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new mount namespace */
		ret = unshare(CLONE_NEWNS);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_mntns);
		SKIP(return, "No permission to create mount namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's mount namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/mnt", pid);
	fd_child_mntns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_mntns, 0);

	/* Get child's mount namespace ID */
	ret = ioctl(fd_child_mntns, NS_GET_ID, &child_mnt_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_mnt_ns_id, 0);

	/* Parent and child should have different mount namespace IDs */
	ASSERT_NE(parent_mnt_ns_id, child_mnt_ns_id);

	close(fd_parent_mntns);
	close(fd_child_mntns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_cgroupns_basic)
{
	__u64 cgroup_ns_id = 0;
	int fd_cgroupns;
	int ret;

	/* Open the current cgroup namespace */
	fd_cgroupns = open("/proc/self/ns/cgroup", O_RDONLY);
	ASSERT_GE(fd_cgroupns, 0);

	/* Get the cgroup namespace ID */
	ret = ioctl(fd_cgroupns, NS_GET_ID, &cgroup_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(cgroup_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 cgroup_ns_id2 = 0;
	ret = ioctl(fd_cgroupns, NS_GET_ID, &cgroup_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(cgroup_ns_id, cgroup_ns_id2);

	close(fd_cgroupns);
}

TEST(nsid_cgroupns_separate)
{
	__u64 parent_cgroup_ns_id = 0;
	__u64 child_cgroup_ns_id = 0;
	int fd_parent_cgroupns, fd_child_cgroupns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's cgroup namespace ID */
	fd_parent_cgroupns = open("/proc/self/ns/cgroup", O_RDONLY);
	ASSERT_GE(fd_parent_cgroupns, 0);
	ret = ioctl(fd_parent_cgroupns, NS_GET_ID, &parent_cgroup_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_cgroup_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new cgroup namespace */
		ret = unshare(CLONE_NEWCGROUP);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_cgroupns);
		SKIP(return, "No permission to create cgroup namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's cgroup namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/cgroup", pid);
	fd_child_cgroupns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_cgroupns, 0);

	/* Get child's cgroup namespace ID */
	ret = ioctl(fd_child_cgroupns, NS_GET_ID, &child_cgroup_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_cgroup_ns_id, 0);

	/* Parent and child should have different cgroup namespace IDs */
	ASSERT_NE(parent_cgroup_ns_id, child_cgroup_ns_id);

	close(fd_parent_cgroupns);
	close(fd_child_cgroupns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_ipcns_basic)
{
	__u64 ipc_ns_id = 0;
	int fd_ipcns;
	int ret;

	/* Open the current IPC namespace */
	fd_ipcns = open("/proc/self/ns/ipc", O_RDONLY);
	ASSERT_GE(fd_ipcns, 0);

	/* Get the IPC namespace ID */
	ret = ioctl(fd_ipcns, NS_GET_ID, &ipc_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(ipc_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 ipc_ns_id2 = 0;
	ret = ioctl(fd_ipcns, NS_GET_ID, &ipc_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(ipc_ns_id, ipc_ns_id2);

	close(fd_ipcns);
}

TEST(nsid_ipcns_separate)
{
	__u64 parent_ipc_ns_id = 0;
	__u64 child_ipc_ns_id = 0;
	int fd_parent_ipcns, fd_child_ipcns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's IPC namespace ID */
	fd_parent_ipcns = open("/proc/self/ns/ipc", O_RDONLY);
	ASSERT_GE(fd_parent_ipcns, 0);
	ret = ioctl(fd_parent_ipcns, NS_GET_ID, &parent_ipc_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_ipc_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new IPC namespace */
		ret = unshare(CLONE_NEWIPC);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_ipcns);
		SKIP(return, "No permission to create IPC namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's IPC namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/ipc", pid);
	fd_child_ipcns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_ipcns, 0);

	/* Get child's IPC namespace ID */
	ret = ioctl(fd_child_ipcns, NS_GET_ID, &child_ipc_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_ipc_ns_id, 0);

	/* Parent and child should have different IPC namespace IDs */
	ASSERT_NE(parent_ipc_ns_id, child_ipc_ns_id);

	close(fd_parent_ipcns);
	close(fd_child_ipcns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_utsns_basic)
{
	__u64 uts_ns_id = 0;
	int fd_utsns;
	int ret;

	/* Open the current UTS namespace */
	fd_utsns = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_GE(fd_utsns, 0);

	/* Get the UTS namespace ID */
	ret = ioctl(fd_utsns, NS_GET_ID, &uts_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(uts_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 uts_ns_id2 = 0;
	ret = ioctl(fd_utsns, NS_GET_ID, &uts_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(uts_ns_id, uts_ns_id2);

	close(fd_utsns);
}

TEST(nsid_utsns_separate)
{
	__u64 parent_uts_ns_id = 0;
	__u64 child_uts_ns_id = 0;
	int fd_parent_utsns, fd_child_utsns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's UTS namespace ID */
	fd_parent_utsns = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_GE(fd_parent_utsns, 0);
	ret = ioctl(fd_parent_utsns, NS_GET_ID, &parent_uts_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_uts_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new UTS namespace */
		ret = unshare(CLONE_NEWUTS);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_utsns);
		SKIP(return, "No permission to create UTS namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's UTS namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/uts", pid);
	fd_child_utsns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_utsns, 0);

	/* Get child's UTS namespace ID */
	ret = ioctl(fd_child_utsns, NS_GET_ID, &child_uts_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_uts_ns_id, 0);

	/* Parent and child should have different UTS namespace IDs */
	ASSERT_NE(parent_uts_ns_id, child_uts_ns_id);

	close(fd_parent_utsns);
	close(fd_child_utsns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_userns_basic)
{
	__u64 user_ns_id = 0;
	int fd_userns;
	int ret;

	/* Open the current user namespace */
	fd_userns = open("/proc/self/ns/user", O_RDONLY);
	ASSERT_GE(fd_userns, 0);

	/* Get the user namespace ID */
	ret = ioctl(fd_userns, NS_GET_ID, &user_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(user_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 user_ns_id2 = 0;
	ret = ioctl(fd_userns, NS_GET_ID, &user_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(user_ns_id, user_ns_id2);

	close(fd_userns);
}

TEST(nsid_userns_separate)
{
	__u64 parent_user_ns_id = 0;
	__u64 child_user_ns_id = 0;
	int fd_parent_userns, fd_child_userns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's user namespace ID */
	fd_parent_userns = open("/proc/self/ns/user", O_RDONLY);
	ASSERT_GE(fd_parent_userns, 0);
	ret = ioctl(fd_parent_userns, NS_GET_ID, &parent_user_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_user_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new user namespace */
		ret = unshare(CLONE_NEWUSER);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_userns);
		SKIP(return, "No permission to create user namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's user namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	fd_child_userns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_userns, 0);

	/* Get child's user namespace ID */
	ret = ioctl(fd_child_userns, NS_GET_ID, &child_user_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_user_ns_id, 0);

	/* Parent and child should have different user namespace IDs */
	ASSERT_NE(parent_user_ns_id, child_user_ns_id);

	close(fd_parent_userns);
	close(fd_child_userns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_timens_basic)
{
	__u64 time_ns_id = 0;
	int fd_timens;
	int ret;

	/* Open the current time namespace */
	fd_timens = open("/proc/self/ns/time", O_RDONLY);
	if (fd_timens < 0) {
		SKIP(return, "Time namespaces not supported");
	}

	/* Get the time namespace ID */
	ret = ioctl(fd_timens, NS_GET_ID, &time_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(time_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 time_ns_id2 = 0;
	ret = ioctl(fd_timens, NS_GET_ID, &time_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(time_ns_id, time_ns_id2);

	close(fd_timens);
}

TEST(nsid_timens_separate)
{
	__u64 parent_time_ns_id = 0;
	__u64 child_time_ns_id = 0;
	int fd_parent_timens, fd_child_timens;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Open the current time namespace */
	fd_parent_timens = open("/proc/self/ns/time", O_RDONLY);
	if (fd_parent_timens < 0) {
		SKIP(return, "Time namespaces not supported");
	}

	/* Get parent's time namespace ID */
	ret = ioctl(fd_parent_timens, NS_GET_ID, &parent_time_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_time_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new time namespace */
		ret = unshare(CLONE_NEWTIME);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES || errno == EINVAL) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Fork a grandchild to actually enter the new namespace */
		pid_t grandchild = fork();
		if (grandchild == 0) {
			/* Grandchild is in the new namespace */
			write(pipefd[1], "Y", 1);
			close(pipefd[1]);
			pause();
			_exit(0);
		} else if (grandchild > 0) {
			/* Child writes grandchild PID and waits */
			write(pipefd[1], "Y", 1);
			write(pipefd[1], &grandchild, sizeof(grandchild));
			close(pipefd[1]);
			pause(); /* Keep the parent alive to maintain the grandchild */
			_exit(0);
		} else {
			_exit(1);
		}
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_timens);
		close(pipefd[0]);
		SKIP(return, "Cannot create time namespace");
	}

	ASSERT_EQ(buf, 'Y');

	pid_t grandchild_pid;
	ASSERT_EQ(read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)), sizeof(grandchild_pid));
	close(pipefd[0]);

	/* Open grandchild's time namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/time", grandchild_pid);
	fd_child_timens = open(path, O_RDONLY);
	ASSERT_GE(fd_child_timens, 0);

	/* Get child's time namespace ID */
	ret = ioctl(fd_child_timens, NS_GET_ID, &child_time_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_time_ns_id, 0);

	/* Parent and child should have different time namespace IDs */
	ASSERT_NE(parent_time_ns_id, child_time_ns_id);

	close(fd_parent_timens);
	close(fd_child_timens);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_pidns_basic)
{
	__u64 pid_ns_id = 0;
	int fd_pidns;
	int ret;

	/* Open the current PID namespace */
	fd_pidns = open("/proc/self/ns/pid", O_RDONLY);
	ASSERT_GE(fd_pidns, 0);

	/* Get the PID namespace ID */
	ret = ioctl(fd_pidns, NS_GET_ID, &pid_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(pid_ns_id, 0);

	/* Verify we can get the same ID again */
	__u64 pid_ns_id2 = 0;
	ret = ioctl(fd_pidns, NS_GET_ID, &pid_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(pid_ns_id, pid_ns_id2);

	close(fd_pidns);
}

TEST(nsid_pidns_separate)
{
	__u64 parent_pid_ns_id = 0;
	__u64 child_pid_ns_id = 0;
	int fd_parent_pidns, fd_child_pidns;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's PID namespace ID */
	fd_parent_pidns = open("/proc/self/ns/pid", O_RDONLY);
	ASSERT_GE(fd_parent_pidns, 0);
	ret = ioctl(fd_parent_pidns, NS_GET_ID, &parent_pid_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_pid_ns_id, 0);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new PID namespace */
		ret = unshare(CLONE_NEWPID);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Fork a grandchild to actually enter the new namespace */
		pid_t grandchild = fork();
		if (grandchild == 0) {
			/* Grandchild is in the new namespace */
			write(pipefd[1], "Y", 1);
			close(pipefd[1]);
			pause();
			_exit(0);
		} else if (grandchild > 0) {
			/* Child writes grandchild PID and waits */
			write(pipefd[1], "Y", 1);
			write(pipefd[1], &grandchild, sizeof(grandchild));
			close(pipefd[1]);
			pause(); /* Keep the parent alive to maintain the grandchild */
			_exit(0);
		} else {
			_exit(1);
		}
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_pidns);
		close(pipefd[0]);
		SKIP(return, "No permission to create PID namespace");
	}

	ASSERT_EQ(buf, 'Y');

	pid_t grandchild_pid;
	ASSERT_EQ(read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)), sizeof(grandchild_pid));
	close(pipefd[0]);

	/* Open grandchild's PID namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/pid", grandchild_pid);
	fd_child_pidns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_pidns, 0);

	/* Get child's PID namespace ID */
	ret = ioctl(fd_child_pidns, NS_GET_ID, &child_pid_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_pid_ns_id, 0);

	/* Parent and child should have different PID namespace IDs */
	ASSERT_NE(parent_pid_ns_id, child_pid_ns_id);

	close(fd_parent_pidns);
	close(fd_child_pidns);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST(nsid_netns_basic)
{
	__u64 net_ns_id = 0;
	__u64 netns_cookie = 0;
	int fd_netns;
	int sock;
	socklen_t optlen;
	int ret;

	/* Open the current network namespace */
	fd_netns = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(fd_netns, 0);

	/* Get the network namespace ID via ioctl */
	ret = ioctl(fd_netns, NS_GET_ID, &net_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(net_ns_id, 0);

	/* Create a socket to get the SO_NETNS_COOKIE */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_GE(sock, 0);

	/* Get the network namespace cookie via socket option */
	optlen = sizeof(netns_cookie);
	ret = getsockopt(sock, SOL_SOCKET, SO_NETNS_COOKIE, &netns_cookie, &optlen);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(optlen, sizeof(netns_cookie));

	/* The namespace ID and cookie should be identical */
	ASSERT_EQ(net_ns_id, netns_cookie);

	/* Verify we can get the same ID again */
	__u64 net_ns_id2 = 0;
	ret = ioctl(fd_netns, NS_GET_ID, &net_ns_id2);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(net_ns_id, net_ns_id2);

	close(sock);
	close(fd_netns);
}

TEST(nsid_netns_separate)
{
	__u64 parent_net_ns_id = 0;
	__u64 parent_netns_cookie = 0;
	__u64 child_net_ns_id = 0;
	__u64 child_netns_cookie = 0;
	int fd_parent_netns, fd_child_netns;
	int parent_sock, child_sock;
	socklen_t optlen;
	int ret;
	pid_t pid;
	int pipefd[2];

	/* Get parent's network namespace ID */
	fd_parent_netns = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(fd_parent_netns, 0);
	ret = ioctl(fd_parent_netns, NS_GET_ID, &parent_net_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(parent_net_ns_id, 0);

	/* Get parent's network namespace cookie */
	parent_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_GE(parent_sock, 0);
	optlen = sizeof(parent_netns_cookie);
	ret = getsockopt(parent_sock, SOL_SOCKET, SO_NETNS_COOKIE, &parent_netns_cookie, &optlen);
	ASSERT_EQ(ret, 0);

	/* Verify parent's ID and cookie match */
	ASSERT_EQ(parent_net_ns_id, parent_netns_cookie);

	/* Create a pipe for synchronization */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child process */
		close(pipefd[0]);

		/* Create new network namespace */
		ret = unshare(CLONE_NEWNET);
		if (ret != 0) {
			/* Skip test if we don't have permission */
			if (errno == EPERM || errno == EACCES) {
				write(pipefd[1], "S", 1); /* Signal skip */
				_exit(0);
			}
			_exit(1);
		}

		/* Signal success */
		write(pipefd[1], "Y", 1);
		close(pipefd[1]);

		/* Keep namespace alive */
		pause();
		_exit(0);
	}

	/* Parent process */
	close(pipefd[1]);

	char buf;
	ASSERT_EQ(read(pipefd[0], &buf, 1), 1);
	close(pipefd[0]);

	if (buf == 'S') {
		/* Child couldn't create namespace, skip test */
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		close(fd_parent_netns);
		close(parent_sock);
		SKIP(return, "No permission to create network namespace");
	}

	ASSERT_EQ(buf, 'Y');

	/* Open child's network namespace */
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/ns/net", pid);
	fd_child_netns = open(path, O_RDONLY);
	ASSERT_GE(fd_child_netns, 0);

	/* Get child's network namespace ID */
	ret = ioctl(fd_child_netns, NS_GET_ID, &child_net_ns_id);
	ASSERT_EQ(ret, 0);
	ASSERT_NE(child_net_ns_id, 0);

	/* Create socket in child's namespace to get cookie */
	ret = setns(fd_child_netns, CLONE_NEWNET);
	if (ret == 0) {
		child_sock = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_GE(child_sock, 0);

		optlen = sizeof(child_netns_cookie);
		ret = getsockopt(child_sock, SOL_SOCKET, SO_NETNS_COOKIE, &child_netns_cookie, &optlen);
		ASSERT_EQ(ret, 0);

		/* Verify child's ID and cookie match */
		ASSERT_EQ(child_net_ns_id, child_netns_cookie);

		close(child_sock);

		/* Return to parent namespace */
		setns(fd_parent_netns, CLONE_NEWNET);
	}

	/* Parent and child should have different network namespace IDs */
	ASSERT_NE(parent_net_ns_id, child_net_ns_id);
	if (child_netns_cookie != 0) {
		ASSERT_NE(parent_netns_cookie, child_netns_cookie);
	}

	close(fd_parent_netns);
	close(fd_child_netns);
	close(parent_sock);

	/* Clean up child process */
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

TEST_HARNESS_MAIN
