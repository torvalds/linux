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

TEST_F(pidfd_info, success_reaped)
{
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID,
	};

	/* Process has already been reaped and PIDFD_INFO_EXIT hasn't been set. */
	ASSERT_NE(ioctl(self->child_pidfd4, PIDFD_GET_INFO, &info), 0);
	ASSERT_EQ(errno, ESRCH);

	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(self->child_pidfd4, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0);
}

TEST_F(pidfd_info, success_reaped_poll)
{
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT,
	};
	struct pollfd fds = {};
	int nevents;

	fds.events = POLLIN;
	fds.fd = self->child_pidfd2;

	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	ASSERT_TRUE(!!(fds.revents & POLLIN));
	ASSERT_TRUE(!!(fds.revents & POLLHUP));

	ASSERT_EQ(ioctl(self->child_pidfd2, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_TRUE(WIFSIGNALED(info.exit_code));
	ASSERT_EQ(WTERMSIG(info.exit_code), SIGKILL);
}

static void *pidfd_info_pause_thread(void *arg)
{
	pid_t pid_thread = gettid();
	int ipc_socket = *(int *)arg;

	/* Inform the grand-parent what the tid of this thread is. */
	if (write_nointr(ipc_socket, &pid_thread, sizeof(pid_thread)) != sizeof(pid_thread))
		return NULL;

	close(ipc_socket);

	/* Sleep untill we're killed. */
	pause();
	return NULL;
}

TEST_F(pidfd_info, thread_group)
{
	pid_t pid_leader, pid_poller, pid_thread;
	pthread_t thread;
	int nevents, pidfd_leader, pidfd_thread, pidfd_leader_thread, ret;
	int ipc_sockets[2];
	struct pollfd fds = {};
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT,
	}, info2;

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	pid_leader = create_child(&pidfd_leader, 0);
	EXPECT_GE(pid_leader, 0);

	if (pid_leader == 0) {
		close(ipc_sockets[0]);

		/* The thread will outlive the thread-group leader. */
		if (pthread_create(&thread, NULL, pidfd_info_pause_thread, &ipc_sockets[1]))
			syscall(__NR_exit, EXIT_FAILURE);

		/* Make the thread-group leader exit prematurely. */
		syscall(__NR_exit, EXIT_SUCCESS);
	}

	/*
	 * Opening a PIDFD_THREAD aka thread-specific pidfd based on a
	 * thread-group leader must succeed.
	 */
	pidfd_leader_thread = sys_pidfd_open(pid_leader, PIDFD_THREAD);
	ASSERT_GE(pidfd_leader_thread, 0);

	pid_poller = fork();
	ASSERT_GE(pid_poller, 0);
	if (pid_poller == 0) {
		/*
		 * We can't poll and wait for the old thread-group
		 * leader to exit using a thread-specific pidfd. The
		 * thread-group leader exited prematurely and
		 * notification is delayed until all subthreads have
		 * exited.
		 */
		fds.events = POLLIN;
		fds.fd = pidfd_leader_thread;
		nevents = poll(&fds, 1, 10000 /* wait 5 seconds */);
		if (nevents != 0)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLIN)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLHUP)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}

	/* Retrieve the tid of the thread. */
	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &pid_thread, sizeof(pid_thread)), sizeof(pid_thread));
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	/* Opening a thread as a thread-group leader must fail. */
	pidfd_thread = sys_pidfd_open(pid_thread, 0);
	ASSERT_LT(pidfd_thread, 0);

	/* Opening a thread as a PIDFD_THREAD must succeed. */
	pidfd_thread = sys_pidfd_open(pid_thread, PIDFD_THREAD);
	ASSERT_GE(pidfd_thread, 0);

	ASSERT_EQ(wait_for_pid(pid_poller), 0);

	/*
	 * Note that pidfd_leader is a thread-group pidfd, so polling on it
	 * would only notify us once all thread in the thread-group have
	 * exited. So we can't poll before we have taken down the whole
	 * thread-group.
	 */

	/* Get PIDFD_GET_INFO using the thread-group leader pidfd. */
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	/* Process has exited but not been reaped, so no PIDFD_INFO_EXIT information yet. */
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_EQ(info.pid, pid_leader);

	/*
	 * Now retrieve the same info using the thread specific pidfd
	 * for the thread-group leader.
	 */
	info2.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader_thread, PIDFD_GET_INFO, &info2), 0);
	ASSERT_TRUE(!!(info2.mask & PIDFD_INFO_CREDS));
	/* Process has exited but not been reaped, so no PIDFD_INFO_EXIT information yet. */
	ASSERT_FALSE(!!(info2.mask & PIDFD_INFO_EXIT));
	ASSERT_EQ(info2.pid, pid_leader);

	/* Now try the thread-specific pidfd. */
	ASSERT_EQ(ioctl(pidfd_thread, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	/* The thread hasn't exited, so no PIDFD_INFO_EXIT information yet. */
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_EQ(info.pid, pid_thread);

	/*
	 * Take down the whole thread-group. The thread-group leader
	 * exited successfully but the thread will now be SIGKILLed.
	 * This must be reflected in the recorded exit information.
	 */
	EXPECT_EQ(sys_pidfd_send_signal(pidfd_leader, SIGKILL, NULL, 0), 0);
	EXPECT_EQ(sys_waitid(P_PIDFD, pidfd_leader, NULL, WEXITED), 0);

	fds.events = POLLIN;
	fds.fd = pidfd_leader;
	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	ASSERT_TRUE(!!(fds.revents & POLLIN));
	/* The thread-group leader has been reaped. */
	ASSERT_TRUE(!!(fds.revents & POLLHUP));

	/*
	 * Retrieve exit information for the thread-group leader via the
	 * thread-group leader pidfd.
	 */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	/* The thread-group leader exited successfully. Only the specific thread was SIGKILLed. */
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0);

	/*
	 * Retrieve exit information for the thread-group leader via the
	 * thread-specific pidfd.
	 */
	info2.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader_thread, PIDFD_GET_INFO, &info2), 0);
	ASSERT_FALSE(!!(info2.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info2.mask & PIDFD_INFO_EXIT));

	/* The thread-group leader exited successfully. Only the specific thread was SIGKILLed. */
	ASSERT_TRUE(WIFEXITED(info2.exit_code));
	ASSERT_EQ(WEXITSTATUS(info2.exit_code), 0);

	/* Retrieve exit information for the thread. */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_thread, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));

	/* The thread got SIGKILLed. */
	ASSERT_TRUE(WIFSIGNALED(info.exit_code));
	ASSERT_EQ(WTERMSIG(info.exit_code), SIGKILL);

	EXPECT_EQ(close(pidfd_leader), 0);
	EXPECT_EQ(close(pidfd_thread), 0);
}

static void *pidfd_info_thread_exec(void *arg)
{
	pid_t pid_thread = gettid();
	int ipc_socket = *(int *)arg;

	/* Inform the grand-parent what the tid of this thread is. */
	if (write_nointr(ipc_socket, &pid_thread, sizeof(pid_thread)) != sizeof(pid_thread))
		return NULL;

	if (read_nointr(ipc_socket, &pid_thread, sizeof(pid_thread)) != sizeof(pid_thread))
		return NULL;

	close(ipc_socket);

	sys_execveat(AT_FDCWD, "pidfd_exec_helper", NULL, NULL, 0);
	return NULL;
}

TEST_F(pidfd_info, thread_group_exec)
{
	pid_t pid_leader, pid_poller, pid_thread;
	pthread_t thread;
	int nevents, pidfd_leader, pidfd_leader_thread, pidfd_thread, ret;
	int ipc_sockets[2];
	struct pollfd fds = {};
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT,
	};

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	pid_leader = create_child(&pidfd_leader, 0);
	EXPECT_GE(pid_leader, 0);

	if (pid_leader == 0) {
		close(ipc_sockets[0]);

		/* The thread will outlive the thread-group leader. */
		if (pthread_create(&thread, NULL, pidfd_info_thread_exec, &ipc_sockets[1]))
			syscall(__NR_exit, EXIT_FAILURE);

		/* Make the thread-group leader exit prematurely. */
		syscall(__NR_exit, EXIT_SUCCESS);
	}

	/* Open a thread-specific pidfd for the thread-group leader. */
	pidfd_leader_thread = sys_pidfd_open(pid_leader, PIDFD_THREAD);
	ASSERT_GE(pidfd_leader_thread, 0);

	pid_poller = fork();
	ASSERT_GE(pid_poller, 0);
	if (pid_poller == 0) {
		/*
		 * We can't poll and wait for the old thread-group
		 * leader to exit using a thread-specific pidfd. The
		 * thread-group leader exited prematurely and
		 * notification is delayed until all subthreads have
		 * exited.
		 *
		 * When the thread has execed it will taken over the old
		 * thread-group leaders struct pid. Calling poll after
		 * the thread execed will thus block again because a new
		 * thread-group has started.
		 */
		fds.events = POLLIN;
		fds.fd = pidfd_leader_thread;
		nevents = poll(&fds, 1, 10000 /* wait 5 seconds */);
		if (nevents != 0)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLIN)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLHUP)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}

	/* Retrieve the tid of the thread. */
	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &pid_thread, sizeof(pid_thread)), sizeof(pid_thread));

	/* Opening a thread as a PIDFD_THREAD must succeed. */
	pidfd_thread = sys_pidfd_open(pid_thread, PIDFD_THREAD);
	ASSERT_GE(pidfd_thread, 0);

	/* Now that we've opened a thread-specific pidfd the thread can exec. */
	ASSERT_EQ(write_nointr(ipc_sockets[0], &pid_thread, sizeof(pid_thread)), sizeof(pid_thread));
	EXPECT_EQ(close(ipc_sockets[0]), 0);

	ASSERT_EQ(wait_for_pid(pid_poller), 0);

	/* Wait until the kernel has SIGKILLed the thread. */
	fds.events = POLLHUP;
	fds.fd = pidfd_thread;
	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	/* The thread has been reaped. */
	ASSERT_TRUE(!!(fds.revents & POLLHUP));

	/* Retrieve thread-specific exit info from pidfd. */
	ASSERT_EQ(ioctl(pidfd_thread, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	/*
	 * While the kernel will have SIGKILLed the whole thread-group
	 * during exec it will cause the individual threads to exit
	 * cleanly.
	 */
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0);

	/*
	 * The thread-group leader is still alive, the thread has taken
	 * over its struct pid and thus its pid number.
	 */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_EQ(info.pid, pid_leader);

	/* Take down the thread-group leader. */
	EXPECT_EQ(sys_pidfd_send_signal(pidfd_leader, SIGKILL, NULL, 0), 0);

	/*
	 * Afte the exec we're dealing with an empty thread-group so now
	 * we must see an exit notification on the thread-specific pidfd
	 * for the thread-group leader as there's no subthread that can
	 * revive the struct pid.
	 */
	fds.events = POLLIN;
	fds.fd = pidfd_leader_thread;
	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	ASSERT_TRUE(!!(fds.revents & POLLIN));
	ASSERT_FALSE(!!(fds.revents & POLLHUP));

	EXPECT_EQ(sys_waitid(P_PIDFD, pidfd_leader, NULL, WEXITED), 0);

	/* Retrieve exit information for the thread-group leader. */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));

	EXPECT_EQ(close(pidfd_leader), 0);
	EXPECT_EQ(close(pidfd_thread), 0);
}

static void *pidfd_info_thread_exec_sane(void *arg)
{
	pid_t pid_thread = gettid();
	int ipc_socket = *(int *)arg;

	/* Inform the grand-parent what the tid of this thread is. */
	if (write_nointr(ipc_socket, &pid_thread, sizeof(pid_thread)) != sizeof(pid_thread))
		return NULL;

	if (read_nointr(ipc_socket, &pid_thread, sizeof(pid_thread)) != sizeof(pid_thread))
		return NULL;

	close(ipc_socket);

	sys_execveat(AT_FDCWD, "pidfd_exec_helper", NULL, NULL, 0);
	return NULL;
}

TEST_F(pidfd_info, thread_group_exec_thread)
{
	pid_t pid_leader, pid_poller, pid_thread;
	pthread_t thread;
	int nevents, pidfd_leader, pidfd_leader_thread, pidfd_thread, ret;
	int ipc_sockets[2];
	struct pollfd fds = {};
	struct pidfd_info info = {
		.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT,
	};

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	EXPECT_EQ(ret, 0);

	pid_leader = create_child(&pidfd_leader, 0);
	EXPECT_GE(pid_leader, 0);

	if (pid_leader == 0) {
		close(ipc_sockets[0]);

		/* The thread will outlive the thread-group leader. */
		if (pthread_create(&thread, NULL, pidfd_info_thread_exec_sane, &ipc_sockets[1]))
			syscall(__NR_exit, EXIT_FAILURE);

		/*
		 * Pause the thread-group leader. It will be killed once
		 * the subthread execs.
		 */
		pause();
		syscall(__NR_exit, EXIT_SUCCESS);
	}

	/* Retrieve the tid of the thread. */
	EXPECT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(read_nointr(ipc_sockets[0], &pid_thread, sizeof(pid_thread)), sizeof(pid_thread));

	/* Opening a thread as a PIDFD_THREAD must succeed. */
	pidfd_thread = sys_pidfd_open(pid_thread, PIDFD_THREAD);
	ASSERT_GE(pidfd_thread, 0);

	/* Open a thread-specific pidfd for the thread-group leader. */
	pidfd_leader_thread = sys_pidfd_open(pid_leader, PIDFD_THREAD);
	ASSERT_GE(pidfd_leader_thread, 0);

	pid_poller = fork();
	ASSERT_GE(pid_poller, 0);
	if (pid_poller == 0) {
		/*
		 * The subthread will now exec. The struct pid of the old
		 * thread-group leader will be assumed by the subthread which
		 * becomes the new thread-group leader. So no exit notification
		 * must be generated. Wait for 5 seconds and call it a success
		 * if no notification has been received.
		 */
		fds.events = POLLIN;
		fds.fd = pidfd_leader_thread;
		nevents = poll(&fds, 1, 10000 /* wait 5 seconds */);
		if (nevents != 0)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLIN)
			_exit(EXIT_FAILURE);
		if (fds.revents & POLLHUP)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}

	/* Now that we've opened a thread-specific pidfd the thread can exec. */
	ASSERT_EQ(write_nointr(ipc_sockets[0], &pid_thread, sizeof(pid_thread)), sizeof(pid_thread));
	EXPECT_EQ(close(ipc_sockets[0]), 0);
	ASSERT_EQ(wait_for_pid(pid_poller), 0);

	/* Wait until the kernel has SIGKILLed the thread. */
	fds.events = POLLHUP;
	fds.fd = pidfd_thread;
	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	/* The thread has been reaped. */
	ASSERT_TRUE(!!(fds.revents & POLLHUP));

	/* Retrieve thread-specific exit info from pidfd. */
	ASSERT_EQ(ioctl(pidfd_thread, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));
	/*
	 * While the kernel will have SIGKILLed the whole thread-group
	 * during exec it will cause the individual threads to exit
	 * cleanly.
	 */
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0);

	/*
	 * The thread-group leader is still alive, the thread has taken
	 * over its struct pid and thus its pid number.
	 */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_EXIT));
	ASSERT_EQ(info.pid, pid_leader);

	/* Take down the thread-group leader. */
	EXPECT_EQ(sys_pidfd_send_signal(pidfd_leader, SIGKILL, NULL, 0), 0);

	/*
	 * Afte the exec we're dealing with an empty thread-group so now
	 * we must see an exit notification on the thread-specific pidfd
	 * for the thread-group leader as there's no subthread that can
	 * revive the struct pid.
	 */
	fds.events = POLLIN;
	fds.fd = pidfd_leader_thread;
	nevents = poll(&fds, 1, -1);
	ASSERT_EQ(nevents, 1);
	ASSERT_TRUE(!!(fds.revents & POLLIN));
	ASSERT_FALSE(!!(fds.revents & POLLHUP));

	EXPECT_EQ(sys_waitid(P_PIDFD, pidfd_leader, NULL, WEXITED), 0);

	/* Retrieve exit information for the thread-group leader. */
	info.mask = PIDFD_INFO_CGROUPID | PIDFD_INFO_EXIT;
	ASSERT_EQ(ioctl(pidfd_leader, PIDFD_GET_INFO, &info), 0);
	ASSERT_FALSE(!!(info.mask & PIDFD_INFO_CREDS));
	ASSERT_TRUE(!!(info.mask & PIDFD_INFO_EXIT));

	EXPECT_EQ(close(pidfd_leader), 0);
	EXPECT_EQ(close(pidfd_thread), 0);
}

TEST_HARNESS_MAIN
