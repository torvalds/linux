// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#define __DEV_FULL "/sys/devices/virtual/mem/full/uevent"
#define __UEVENT_BUFFER_SIZE (2048 * 2)
#define __UEVENT_HEADER "add@/devices/virtual/mem/full"
#define __UEVENT_HEADER_LEN sizeof("add@/devices/virtual/mem/full")
#define __UEVENT_LISTEN_ALL -1

ssize_t read_nointr(int fd, void *buf, size_t count)
{
	ssize_t ret;

again:
	ret = read(fd, buf, count);
	if (ret < 0 && errno == EINTR)
		goto again;

	return ret;
}

ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

again:
	ret = write(fd, buf, count);
	if (ret < 0 && errno == EINTR)
		goto again;

	return ret;
}

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

	if (ret != pid)
		goto again;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	return 0;
}

static int uevent_listener(unsigned long post_flags, bool expect_uevent,
			   int sync_fd)
{
	int sk_fd, ret;
	socklen_t sk_addr_len;
	int rcv_buf_sz = __UEVENT_BUFFER_SIZE;
	uint64_t sync_add = 1;
	struct sockaddr_nl sk_addr = { 0 }, rcv_addr = { 0 };
	char buf[__UEVENT_BUFFER_SIZE] = { 0 };
	struct iovec iov = { buf, __UEVENT_BUFFER_SIZE };
	char control[CMSG_SPACE(sizeof(struct ucred))];
	struct msghdr hdr = {
		&rcv_addr, sizeof(rcv_addr), &iov, 1,
		control,   sizeof(control),  0,
	};

	sk_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC,
		       NETLINK_KOBJECT_UEVENT);
	if (sk_fd < 0) {
		fprintf(stderr, "%s - Failed to open uevent socket\n", strerror(errno));
		return -1;
	}

	ret = setsockopt(sk_fd, SOL_SOCKET, SO_RCVBUF, &rcv_buf_sz,
			 sizeof(rcv_buf_sz));
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to set socket options\n", strerror(errno));
		goto on_error;
	}

	sk_addr.nl_family = AF_NETLINK;
	sk_addr.nl_groups = __UEVENT_LISTEN_ALL;

	sk_addr_len = sizeof(sk_addr);
	ret = bind(sk_fd, (struct sockaddr *)&sk_addr, sk_addr_len);
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to bind socket\n", strerror(errno));
		goto on_error;
	}

	ret = getsockname(sk_fd, (struct sockaddr *)&sk_addr, &sk_addr_len);
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to retrieve socket name\n", strerror(errno));
		goto on_error;
	}

	if ((size_t)sk_addr_len != sizeof(sk_addr)) {
		fprintf(stderr, "Invalid socket address size\n");
		ret = -1;
		goto on_error;
	}

	if (post_flags & CLONE_NEWUSER) {
		ret = unshare(CLONE_NEWUSER);
		if (ret < 0) {
			fprintf(stderr,
				"%s - Failed to unshare user namespace\n",
				strerror(errno));
			goto on_error;
		}
	}

	if (post_flags & CLONE_NEWNET) {
		ret = unshare(CLONE_NEWNET);
		if (ret < 0) {
			fprintf(stderr,
				"%s - Failed to unshare network namespace\n",
				strerror(errno));
			goto on_error;
		}
	}

	ret = write_nointr(sync_fd, &sync_add, sizeof(sync_add));
	close(sync_fd);
	if (ret != sizeof(sync_add)) {
		ret = -1;
		fprintf(stderr, "Failed to synchronize with parent process\n");
		goto on_error;
	}

	ret = 0;
	for (;;) {
		ssize_t r;

		r = recvmsg(sk_fd, &hdr, 0);
		if (r <= 0) {
			fprintf(stderr, "%s - Failed to receive uevent\n", strerror(errno));
			ret = -1;
			break;
		}

		/* ignore libudev messages */
		if (memcmp(buf, "libudev", 8) == 0)
			continue;

		/* ignore uevents we didn't trigger */
		if (memcmp(buf, __UEVENT_HEADER, __UEVENT_HEADER_LEN) != 0)
			continue;

		if (!expect_uevent) {
			fprintf(stderr, "Received unexpected uevent:\n");
			ret = -1;
		}

		if (TH_LOG_ENABLED) {
			/* If logging is enabled dump the received uevent. */
			(void)write_nointr(STDERR_FILENO, buf, r);
			(void)write_nointr(STDERR_FILENO, "\n", 1);
		}

		break;
	}

on_error:
	close(sk_fd);

	return ret;
}

int trigger_uevent(unsigned int times)
{
	int fd, ret;
	unsigned int i;

	fd = open(__DEV_FULL, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		if (errno != ENOENT)
			return -EINVAL;

		return -1;
	}

	for (i = 0; i < times; i++) {
		ret = write_nointr(fd, "add\n", sizeof("add\n") - 1);
		if (ret < 0) {
			fprintf(stderr, "Failed to trigger uevent\n");
			break;
		}
	}
	close(fd);

	return ret;
}

int set_death_signal(void)
{
	int ret;
	pid_t ppid;

	ret = prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);

	/* Check whether we have been orphaned. */
	ppid = getppid();
	if (ppid == 1) {
		pid_t self;

		self = getpid();
		ret = kill(self, SIGKILL);
	}

	if (ret < 0)
		return -1;

	return 0;
}

static int do_test(unsigned long pre_flags, unsigned long post_flags,
		   bool expect_uevent, int sync_fd)
{
	int ret;
	uint64_t wait_val;
	pid_t pid;
	sigset_t mask;
	sigset_t orig_mask;
	struct timespec timeout;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	ret = sigprocmask(SIG_BLOCK, &mask, &orig_mask);
	if (ret < 0) {
		fprintf(stderr, "%s- Failed to block SIGCHLD\n", strerror(errno));
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "%s - Failed to fork() new process\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Make sure that we go away when our parent dies. */
		ret = set_death_signal();
		if (ret < 0) {
			fprintf(stderr, "Failed to set PR_SET_PDEATHSIG to SIGKILL\n");
			_exit(EXIT_FAILURE);
		}

		if (pre_flags & CLONE_NEWUSER) {
			ret = unshare(CLONE_NEWUSER);
			if (ret < 0) {
				fprintf(stderr,
					"%s - Failed to unshare user namespace\n",
					strerror(errno));
				_exit(EXIT_FAILURE);
			}
		}

		if (pre_flags & CLONE_NEWNET) {
			ret = unshare(CLONE_NEWNET);
			if (ret < 0) {
				fprintf(stderr,
					"%s - Failed to unshare network namespace\n",
					strerror(errno));
				_exit(EXIT_FAILURE);
			}
		}

		if (uevent_listener(post_flags, expect_uevent, sync_fd) < 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	ret = read_nointr(sync_fd, &wait_val, sizeof(wait_val));
	if (ret != sizeof(wait_val)) {
		fprintf(stderr, "Failed to synchronize with child process\n");
		_exit(EXIT_FAILURE);
	}

	/* Trigger 10 uevents to account for the case where the kernel might
	 * drop some.
	 */
	ret = trigger_uevent(10);
	if (ret < 0)
		fprintf(stderr, "Failed triggering uevents\n");

	/* Wait for 2 seconds before considering this failed. This should be
	 * plenty of time for the kernel to deliver the uevent even under heavy
	 * load.
	 */
	timeout.tv_sec = 2;
	timeout.tv_nsec = 0;

again:
	ret = sigtimedwait(&mask, NULL, &timeout);
	if (ret < 0) {
		if (errno == EINTR)
			goto again;

		if (!expect_uevent)
			ret = kill(pid, SIGTERM); /* success */
		else
			ret = kill(pid, SIGUSR1); /* error */
		if (ret < 0)
			return -1;
	}

	ret = wait_for_pid(pid);
	if (ret < 0)
		return -1;

	return ret;
}

static void signal_handler(int sig)
{
	if (sig == SIGTERM)
		_exit(EXIT_SUCCESS);

	_exit(EXIT_FAILURE);
}

TEST(uevent_filtering)
{
	int ret, sync_fd;
	struct sigaction act;

	if (geteuid()) {
		TH_LOG("Uevent filtering tests require root privileges. Skipping test");
		_exit(KSFT_SKIP);
	}

	ret = access(__DEV_FULL, F_OK);
	EXPECT_EQ(0, ret) {
		if (errno == ENOENT) {
			TH_LOG(__DEV_FULL " does not exist. Skipping test");
			_exit(KSFT_SKIP);
		}

		_exit(KSFT_FAIL);
	}

	act.sa_handler = signal_handler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);

	ret = sigaction(SIGTERM, &act, NULL);
	ASSERT_EQ(0, ret);

	sync_fd = eventfd(0, EFD_CLOEXEC);
	ASSERT_GE(sync_fd, 0);

	/*
	 * Setup:
	 * - Open uevent listening socket in initial network namespace owned by
	 *   initial user namespace.
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(0, 0, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - Open uevent listening socket in non-initial network namespace
	 *   owned by initial user namespace.
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(CLONE_NEWNET, 0, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - unshare user namespace
	 * - Open uevent listening socket in initial network namespace
	 *   owned by initial user namespace.
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(CLONE_NEWUSER, 0, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - Open uevent listening socket in non-initial network namespace
	 *   owned by non-initial user namespace.
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives no uevent
	 */
	ret = do_test(CLONE_NEWUSER | CLONE_NEWNET, 0, false, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - Open uevent listening socket in initial network namespace
	 *   owned by initial user namespace.
	 * - unshare network namespace
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(0, CLONE_NEWNET, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - Open uevent listening socket in initial network namespace
	 *   owned by initial user namespace.
	 * - unshare user namespace
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(0, CLONE_NEWUSER, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

	/*
	 * Setup:
	 * - Open uevent listening socket in initial network namespace
	 *   owned by initial user namespace.
	 * - unshare user namespace
	 * - unshare network namespace
	 * - Trigger uevent in initial network namespace owned by initial user
	 *   namespace.
	 * Expected Result:
	 * - uevent listening socket receives uevent
	 */
	ret = do_test(0, CLONE_NEWUSER | CLONE_NEWNET, true, sync_fd);
	ASSERT_EQ(0, ret) {
		goto do_cleanup;
	}

do_cleanup:
	close(sync_fd);
}

TEST_HARNESS_MAIN
