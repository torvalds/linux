// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2026 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <errno.h>
#include <linux/types.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "kselftest_harness.h"

#ifndef CLONE_AUTOREAP
#define CLONE_AUTOREAP (1ULL << 34)
#endif

#ifndef CLONE_NNP
#define CLONE_NNP (1ULL << 35)
#endif

#ifndef CLONE_PIDFD_AUTOKILL
#define CLONE_PIDFD_AUTOKILL (1ULL << 36)
#endif

#ifndef _LINUX_CAPABILITY_VERSION_3
#define _LINUX_CAPABILITY_VERSION_3 0x20080522
#endif

struct cap_header {
	__u32 version;
	int pid;
};

struct cap_data {
	__u32 effective;
	__u32 permitted;
	__u32 inheritable;
};

static int drop_all_caps(void)
{
	struct cap_header hdr = { .version = _LINUX_CAPABILITY_VERSION_3 };
	struct cap_data data[2] = {};

	return syscall(__NR_capset, &hdr, data);
}

static pid_t create_autoreap_child(int *pidfd)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_AUTOREAP,
		.exit_signal	= 0,
		.pidfd		= ptr_to_u64(pidfd),
	};

	return sys_clone3(&args, sizeof(args));
}

/*
 * Test that CLONE_AUTOREAP works without CLONE_PIDFD (fire-and-forget).
 */
TEST(autoreap_without_pidfd)
{
	struct __clone_args args = {
		.flags		= CLONE_AUTOREAP,
		.exit_signal	= 0,
	};
	pid_t pid;
	int ret;

	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_AUTOREAP not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0)
		_exit(0);

	/*
	 * Give the child a moment to exit and be autoreaped.
	 * Then verify no zombie remains.
	 */
	usleep(200000);
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);
}

/*
 * Test that CLONE_AUTOREAP with a non-zero exit_signal fails.
 */
TEST(autoreap_rejects_exit_signal)
{
	struct __clone_args args = {
		.flags		= CLONE_AUTOREAP,
		.exit_signal	= SIGCHLD,
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Test that CLONE_AUTOREAP with CLONE_PARENT fails.
 */
TEST(autoreap_rejects_parent)
{
	struct __clone_args args = {
		.flags		= CLONE_AUTOREAP | CLONE_PARENT,
		.exit_signal	= 0,
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Test that CLONE_AUTOREAP with CLONE_THREAD fails.
 */
TEST(autoreap_rejects_thread)
{
	struct __clone_args args = {
		.flags		= CLONE_AUTOREAP | CLONE_THREAD |
				  CLONE_SIGHAND | CLONE_VM,
		.exit_signal	= 0,
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Basic test: create an autoreap child, let it exit, verify:
 * - pidfd becomes readable (poll returns POLLIN)
 * - PIDFD_GET_INFO returns the correct exit code
 * - waitpid() returns -1/ECHILD (no zombie)
 */
TEST(autoreap_basic)
{
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int pidfd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	pid = create_autoreap_child(&pidfd);
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_AUTOREAP not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0)
		_exit(42);

	ASSERT_GE(pidfd, 0);

	/* Wait for the child to exit via pidfd poll. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/* Verify exit info via PIDFD_GET_INFO. */
	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	/*
	 * exit_code is in waitpid format: for _exit(42),
	 * WIFEXITED is true and WEXITSTATUS is 42.
	 */
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 42);

	/* Verify no zombie: waitpid should fail with ECHILD. */
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(pidfd);
}

/*
 * Test that an autoreap child killed by a signal reports
 * the correct exit info.
 */
TEST(autoreap_signaled)
{
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int pidfd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	pid = create_autoreap_child(&pidfd);
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_AUTOREAP not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pause();
		_exit(1);
	}

	ASSERT_GE(pidfd, 0);

	/* Kill the child. */
	ret = sys_pidfd_send_signal(pidfd, SIGKILL, NULL, 0);
	ASSERT_EQ(ret, 0);

	/* Wait for exit via pidfd. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/* Verify signal info. */
	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	ASSERT_TRUE(WIFSIGNALED(info.exit_code));
	ASSERT_EQ(WTERMSIG(info.exit_code), SIGKILL);

	/* No zombie. */
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(pidfd);
}

/*
 * Test autoreap survives reparenting: middle process creates an
 * autoreap grandchild, then exits. The grandchild gets reparented
 * to us (the grandparent, which is a subreaper). When the grandchild
 * exits, it should still be autoreaped - no zombie under us.
 */
TEST(autoreap_reparent)
{
	int ipc_sockets[2], ret;
	int pidfd = -1;
	struct pollfd pfd;
	pid_t mid_pid, grandchild_pid;
	char buf[32] = {};

	/* Make ourselves a subreaper so reparented children come to us. */
	ret = prctl(PR_SET_CHILD_SUBREAPER, 1);
	ASSERT_EQ(ret, 0);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	mid_pid = fork();
	ASSERT_GE(mid_pid, 0);

	if (mid_pid == 0) {
		/* Middle child: create an autoreap grandchild. */
		int gc_pidfd = -1;

		close(ipc_sockets[0]);

		grandchild_pid = create_autoreap_child(&gc_pidfd);
		if (grandchild_pid < 0) {
			write_nointr(ipc_sockets[1], "E", 1);
			close(ipc_sockets[1]);
			_exit(1);
		}

		if (grandchild_pid == 0) {
			/* Grandchild: wait for signal to exit. */
			close(ipc_sockets[1]);
			if (gc_pidfd >= 0)
				close(gc_pidfd);
			pause();
			_exit(0);
		}

		/* Send grandchild PID to grandparent. */
		snprintf(buf, sizeof(buf), "%d", grandchild_pid);
		write_nointr(ipc_sockets[1], buf, strlen(buf));
		close(ipc_sockets[1]);
		if (gc_pidfd >= 0)
			close(gc_pidfd);

		/* Middle child exits, grandchild gets reparented. */
		_exit(0);
	}

	close(ipc_sockets[1]);

	/* Read grandchild's PID. */
	ret = read_nointr(ipc_sockets[0], buf, sizeof(buf) - 1);
	close(ipc_sockets[0]);
	ASSERT_GT(ret, 0);

	if (buf[0] == 'E') {
		waitpid(mid_pid, NULL, 0);
		prctl(PR_SET_CHILD_SUBREAPER, 0);
		SKIP(return, "CLONE_AUTOREAP not supported");
	}

	grandchild_pid = atoi(buf);
	ASSERT_GT(grandchild_pid, 0);

	/* Wait for the middle child to exit. */
	ret = waitpid(mid_pid, NULL, 0);
	ASSERT_EQ(ret, mid_pid);

	/*
	 * Now the grandchild is reparented to us (subreaper).
	 * Open a pidfd for the grandchild and kill it.
	 */
	pidfd = sys_pidfd_open(grandchild_pid, 0);
	ASSERT_GE(pidfd, 0);

	ret = sys_pidfd_send_signal(pidfd, SIGKILL, NULL, 0);
	ASSERT_EQ(ret, 0);

	/* Wait for it to exit via pidfd poll. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/*
	 * The grandchild should have been autoreaped even though
	 * we (the new parent) haven't set SA_NOCLDWAIT.
	 * waitpid should return -1/ECHILD.
	 */
	ret = waitpid(grandchild_pid, NULL, WNOHANG);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, ECHILD);

	close(pidfd);

	/* Clean up subreaper status. */
	prctl(PR_SET_CHILD_SUBREAPER, 0);
}

static int thread_sock_fd;

static void *thread_func(void *arg)
{
	/* Signal parent we're running. */
	write_nointr(thread_sock_fd, "1", 1);

	/* Give main thread time to call _exit() first. */
	usleep(200000);

	return NULL;
}

/*
 * Test that an autoreap child with multiple threads is properly
 * autoreaped only after all threads have exited.
 */
TEST(autoreap_multithreaded)
{
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int ipc_sockets[2], ret;
	int pidfd = -1;
	struct pollfd pfd;
	pid_t pid;
	char c;

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid = create_autoreap_child(&pidfd);
	if (pid < 0 && errno == EINVAL) {
		close(ipc_sockets[0]);
		close(ipc_sockets[1]);
		SKIP(return, "CLONE_AUTOREAP not supported");
	}
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pthread_t thread;

		close(ipc_sockets[0]);

		/*
		 * Create a sub-thread that outlives the main thread.
		 * The thread signals readiness, then sleeps.
		 * The main thread waits briefly, then calls _exit().
		 */
		thread_sock_fd = ipc_sockets[1];
		pthread_create(&thread, NULL, thread_func, NULL);
		pthread_detach(thread);

		/* Wait for thread to be running. */
		usleep(100000);

		/* Main thread exits; sub-thread is still alive. */
		_exit(99);
	}

	close(ipc_sockets[1]);

	/* Wait for the sub-thread to signal readiness. */
	ret = read_nointr(ipc_sockets[0], &c, 1);
	close(ipc_sockets[0]);
	ASSERT_EQ(ret, 1);

	/* Wait for the process to fully exit via pidfd poll. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/* Verify exit info. */
	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 99);

	/* No zombie. */
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(pidfd);
}

/*
 * Test that autoreap is NOT inherited by grandchildren.
 */
TEST(autoreap_no_inherit)
{
	int ipc_sockets[2], ret;
	int pidfd = -1;
	pid_t pid;
	char buf[2] = {};
	struct pollfd pfd;

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_EQ(ret, 0);

	pid = create_autoreap_child(&pidfd);
	if (pid < 0 && errno == EINVAL) {
		close(ipc_sockets[0]);
		close(ipc_sockets[1]);
		SKIP(return, "CLONE_AUTOREAP not supported");
	}
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t gc;
		int status;

		close(ipc_sockets[0]);

		/* Autoreap child forks a grandchild (without autoreap). */
		gc = fork();
		if (gc < 0) {
			write_nointr(ipc_sockets[1], "E", 1);
			_exit(1);
		}
		if (gc == 0) {
			/* Grandchild: exit immediately. */
			close(ipc_sockets[1]);
			_exit(77);
		}

		/*
		 * The grandchild should become a regular zombie
		 * since it was NOT created with CLONE_AUTOREAP.
		 * Wait for it to verify.
		 */
		ret = waitpid(gc, &status, 0);
		if (ret == gc && WIFEXITED(status) &&
		    WEXITSTATUS(status) == 77) {
			write_nointr(ipc_sockets[1], "P", 1);
		} else {
			write_nointr(ipc_sockets[1], "F", 1);
		}
		close(ipc_sockets[1]);
		_exit(0);
	}

	close(ipc_sockets[1]);

	ret = read_nointr(ipc_sockets[0], buf, 1);
	close(ipc_sockets[0]);
	ASSERT_EQ(ret, 1);

	/*
	 * 'P' means the autoreap child was able to waitpid() its
	 * grandchild (correct - grandchild should be a normal zombie,
	 * not autoreaped).
	 */
	ASSERT_EQ(buf[0], 'P');

	/* Wait for the autoreap child to exit. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);

	/* Autoreap child itself should be autoreaped. */
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(pidfd);
}

/*
 * Test that CLONE_NNP sets no_new_privs on the child.
 * The child checks via prctl(PR_GET_NO_NEW_PRIVS) and reports back.
 * The parent must NOT have no_new_privs set afterwards.
 */
TEST(nnp_sets_no_new_privs)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_AUTOREAP | CLONE_NNP,
		.exit_signal	= 0,
	};
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int pidfd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	/* Ensure parent does not already have no_new_privs. */
	ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("Parent already has no_new_privs set, cannot run test");
	}

	args.pidfd = ptr_to_u64(&pidfd);

	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_NNP not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/*
		 * Child: check no_new_privs. Exit 0 if set, 1 if not.
		 */
		ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
		_exit(ret == 1 ? 0 : 1);
	}

	ASSERT_GE(pidfd, 0);

	/* Parent must still NOT have no_new_privs. */
	ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("Parent got no_new_privs after creating CLONE_NNP child");
	}

	/* Wait for child to exit. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);

	/* Verify child exited with 0 (no_new_privs was set). */
	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0) {
		TH_LOG("Child did not have no_new_privs set");
	}

	close(pidfd);
}

/*
 * Test that CLONE_NNP with CLONE_THREAD fails with EINVAL.
 */
TEST(nnp_rejects_thread)
{
	struct __clone_args args = {
		.flags		= CLONE_NNP | CLONE_THREAD |
				  CLONE_SIGHAND | CLONE_VM,
		.exit_signal	= 0,
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Test that a plain CLONE_AUTOREAP child does NOT get no_new_privs.
 * Only CLONE_NNP should set it.
 */
TEST(autoreap_no_new_privs_unset)
{
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int pidfd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	pid = create_autoreap_child(&pidfd);
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_AUTOREAP not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/*
		 * Child: check no_new_privs. Exit 0 if NOT set, 1 if set.
		 */
		ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
		_exit(ret == 0 ? 0 : 1);
	}

	ASSERT_GE(pidfd, 0);

	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);

	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0) {
		TH_LOG("Plain autoreap child unexpectedly has no_new_privs");
	}

	close(pidfd);
}

/*
 * Helper: create a child with CLONE_PIDFD | CLONE_PIDFD_AUTOKILL | CLONE_AUTOREAP | CLONE_NNP.
 */
static pid_t create_autokill_child(int *pidfd)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_PIDFD_AUTOKILL |
				  CLONE_AUTOREAP | CLONE_NNP,
		.exit_signal	= 0,
		.pidfd		= ptr_to_u64(pidfd),
	};

	return sys_clone3(&args, sizeof(args));
}

/*
 * Basic autokill test: child blocks in pause(), parent closes the
 * clone3 pidfd, child should be killed and autoreaped.
 */
TEST(autokill_basic)
{
	int pidfd = -1, pollfd_fd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	pid = create_autokill_child(&pidfd);
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_PIDFD_AUTOKILL not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pause();
		_exit(1);
	}

	ASSERT_GE(pidfd, 0);

	/*
	 * Open a second pidfd via pidfd_open() so we can observe the
	 * child's death after closing the clone3 pidfd.
	 */
	pollfd_fd = sys_pidfd_open(pid, 0);
	ASSERT_GE(pollfd_fd, 0);

	/* Close the clone3 pidfd — this should trigger autokill. */
	close(pidfd);

	/* Wait for the child to die via the pidfd_open'd fd. */
	pfd.fd = pollfd_fd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/* Child should be autoreaped — no zombie. */
	usleep(100000);
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(pollfd_fd);
}

/*
 * CLONE_PIDFD_AUTOKILL without CLONE_PIDFD must fail with EINVAL.
 */
TEST(autokill_requires_pidfd)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD_AUTOKILL | CLONE_AUTOREAP,
		.exit_signal	= 0,
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * CLONE_PIDFD_AUTOKILL without CLONE_AUTOREAP must fail with EINVAL.
 */
TEST(autokill_requires_autoreap)
{
	int pidfd = -1;
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_PIDFD_AUTOKILL,
		.exit_signal	= 0,
		.pidfd		= ptr_to_u64(&pidfd),
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * CLONE_PIDFD_AUTOKILL with CLONE_THREAD must fail with EINVAL.
 */
TEST(autokill_rejects_thread)
{
	int pidfd = -1;
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_PIDFD_AUTOKILL |
				  CLONE_AUTOREAP | CLONE_THREAD |
				  CLONE_SIGHAND | CLONE_VM,
		.exit_signal	= 0,
		.pidfd		= ptr_to_u64(&pidfd),
	};
	pid_t pid;

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Test that only the clone3 pidfd triggers autokill, not pidfd_open().
 * Close the pidfd_open'd fd first — child should survive.
 * Then close the clone3 pidfd — child should be killed and autoreaped.
 */
TEST(autokill_pidfd_open_no_effect)
{
	int pidfd = -1, open_fd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	pid = create_autokill_child(&pidfd);
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_PIDFD_AUTOKILL not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pause();
		_exit(1);
	}

	ASSERT_GE(pidfd, 0);

	/* Open a second pidfd via pidfd_open(). */
	open_fd = sys_pidfd_open(pid, 0);
	ASSERT_GE(open_fd, 0);

	/*
	 * Close the pidfd_open'd fd — child should survive because
	 * only the clone3 pidfd has autokill.
	 */
	close(open_fd);
	usleep(200000);

	/* Verify child is still alive by polling the clone3 pidfd. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 0) {
		TH_LOG("Child died after closing pidfd_open fd — should still be alive");
	}

	/* Open another observation fd before triggering autokill. */
	open_fd = sys_pidfd_open(pid, 0);
	ASSERT_GE(open_fd, 0);

	/* Now close the clone3 pidfd — this triggers autokill. */
	close(pidfd);

	pfd.fd = open_fd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);
	ASSERT_TRUE(pfd.revents & POLLIN);

	/* Child should be autoreaped — no zombie. */
	usleep(100000);
	ret = waitpid(pid, NULL, WNOHANG);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, ECHILD);

	close(open_fd);
}

/*
 * Test that CLONE_PIDFD_AUTOKILL without CLONE_NNP fails with EPERM
 * for an unprivileged caller.
 */
TEST(autokill_requires_cap_sys_admin)
{
	int pidfd = -1, ret;
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_PIDFD_AUTOKILL |
				  CLONE_AUTOREAP,
		.exit_signal	= 0,
		.pidfd		= ptr_to_u64(&pidfd),
	};
	pid_t pid;

	/* Drop all capabilities so we lack CAP_SYS_ADMIN. */
	ret = drop_all_caps();
	ASSERT_EQ(ret, 0);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_EQ(pid, -1);
	ASSERT_EQ(errno, EPERM);
}

/*
 * Test that CLONE_PIDFD_AUTOKILL without CLONE_NNP succeeds with
 * CAP_SYS_ADMIN.
 */
TEST(autokill_without_nnp_with_cap)
{
	struct __clone_args args = {
		.flags		= CLONE_PIDFD | CLONE_PIDFD_AUTOKILL |
				  CLONE_AUTOREAP,
		.exit_signal	= 0,
	};
	struct pidfd_info info = { .mask = PIDFD_INFO_EXIT };
	int pidfd = -1, ret;
	struct pollfd pfd;
	pid_t pid;

	if (geteuid() != 0)
		SKIP(return, "Need root/CAP_SYS_ADMIN");

	args.pidfd = ptr_to_u64(&pidfd);

	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0 && errno == EINVAL)
		SKIP(return, "CLONE_PIDFD_AUTOKILL not supported");
	ASSERT_GE(pid, 0);

	if (pid == 0)
		_exit(0);

	ASSERT_GE(pidfd, 0);

	/* Wait for child to exit. */
	pfd.fd = pidfd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, 5000);
	ASSERT_EQ(ret, 1);

	ret = ioctl(pidfd, PIDFD_GET_INFO, &info);
	ASSERT_EQ(ret, 0);
	ASSERT_TRUE(info.mask & PIDFD_INFO_EXIT);
	ASSERT_TRUE(WIFEXITED(info.exit_code));
	ASSERT_EQ(WEXITSTATUS(info.exit_code), 0);

	close(pidfd);
}

TEST_HARNESS_MAIN
