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
#include <linux/kcmp.h>

#include "pidfd.h"
#include "../kselftest.h"
#include "../kselftest_harness.h"

/*
 * UNKNOWN_FD is an fd number that should never exist in the child, as it is
 * used to check the negative case.
 */
#define UNKNOWN_FD 111
#define UID_NOBODY 65535

static int sys_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1,
		    unsigned long idx2)
{
	return syscall(__NR_kcmp, pid1, pid2, type, idx1, idx2);
}

static int sys_memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

static int __child(int sk, int memfd)
{
	int ret;
	char buf;

	/*
	 * Ensure we don't leave around a bunch of orphaned children if our
	 * tests fail.
	 */
	ret = prctl(PR_SET_PDEATHSIG, SIGKILL);
	if (ret) {
		fprintf(stderr, "%s: Child could not set DEATHSIG\n",
			strerror(errno));
		return -1;
	}

	ret = send(sk, &memfd, sizeof(memfd), 0);
	if (ret != sizeof(memfd)) {
		fprintf(stderr, "%s: Child failed to send fd number\n",
			strerror(errno));
		return -1;
	}

	/*
	 * The fixture setup is completed at this point. The tests will run.
	 *
	 * This blocking recv enables the parent to message the child.
	 * Either we will read 'P' off of the sk, indicating that we need
	 * to disable ptrace, or we will read a 0, indicating that the other
	 * side has closed the sk. This occurs during fixture teardown time,
	 * indicating that the child should exit.
	 */
	while ((ret = recv(sk, &buf, sizeof(buf), 0)) > 0) {
		if (buf == 'P') {
			ret = prctl(PR_SET_DUMPABLE, 0);
			if (ret < 0) {
				fprintf(stderr,
					"%s: Child failed to disable ptrace\n",
					strerror(errno));
				return -1;
			}
		} else {
			fprintf(stderr, "Child received unknown command %c\n",
				buf);
			return -1;
		}
		ret = send(sk, &buf, sizeof(buf), 0);
		if (ret != 1) {
			fprintf(stderr, "%s: Child failed to ack\n",
				strerror(errno));
			return -1;
		}
	}
	if (ret < 0) {
		fprintf(stderr, "%s: Child failed to read from socket\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

static int child(int sk)
{
	int memfd, ret;

	memfd = sys_memfd_create("test", 0);
	if (memfd < 0) {
		fprintf(stderr, "%s: Child could not create memfd\n",
			strerror(errno));
		ret = -1;
	} else {
		ret = __child(sk, memfd);
		close(memfd);
	}

	close(sk);
	return ret;
}

FIXTURE(child)
{
	/*
	 * remote_fd is the number of the FD which we are trying to retrieve
	 * from the child.
	 */
	int remote_fd;
	/* pid points to the child which we are fetching FDs from */
	pid_t pid;
	/* pidfd is the pidfd of the child */
	int pidfd;
	/*
	 * sk is our side of the socketpair used to communicate with the child.
	 * When it is closed, the child will exit.
	 */
	int sk;
};

FIXTURE_SETUP(child)
{
	int ret, sk_pair[2];

	ASSERT_EQ(0, socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair)) {
		TH_LOG("%s: failed to create socketpair", strerror(errno));
	}
	self->sk = sk_pair[0];

	self->pid = fork();
	ASSERT_GE(self->pid, 0);

	if (self->pid == 0) {
		close(sk_pair[0]);
		if (child(sk_pair[1]))
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}

	close(sk_pair[1]);

	self->pidfd = sys_pidfd_open(self->pid, 0);
	ASSERT_GE(self->pidfd, 0);

	/*
	 * Wait for the child to complete setup. It'll send the remote memfd's
	 * number when ready.
	 */
	ret = recv(sk_pair[0], &self->remote_fd, sizeof(self->remote_fd), 0);
	ASSERT_EQ(sizeof(self->remote_fd), ret);
}

FIXTURE_TEARDOWN(child)
{
	EXPECT_EQ(0, close(self->pidfd));
	EXPECT_EQ(0, close(self->sk));

	EXPECT_EQ(0, wait_for_pid(self->pid));
}

TEST_F(child, disable_ptrace)
{
	int uid, fd;
	char c;

	/*
	 * Turn into nobody if we're root, to avoid CAP_SYS_PTRACE
	 *
	 * The tests should run in their own process, so even this test fails,
	 * it shouldn't result in subsequent tests failing.
	 */
	uid = getuid();
	if (uid == 0)
		ASSERT_EQ(0, seteuid(UID_NOBODY));

	ASSERT_EQ(1, send(self->sk, "P", 1, 0));
	ASSERT_EQ(1, recv(self->sk, &c, 1, 0));

	fd = sys_pidfd_getfd(self->pidfd, self->remote_fd, 0);
	EXPECT_EQ(-1, fd);
	EXPECT_EQ(EPERM, errno);

	if (uid == 0)
		ASSERT_EQ(0, seteuid(0));
}

TEST_F(child, fetch_fd)
{
	int fd, ret;

	fd = sys_pidfd_getfd(self->pidfd, self->remote_fd, 0);
	ASSERT_GE(fd, 0);

	EXPECT_EQ(0, sys_kcmp(getpid(), self->pid, KCMP_FILE, fd, self->remote_fd));

	ret = fcntl(fd, F_GETFD);
	ASSERT_GE(ret, 0);
	EXPECT_GE(ret & FD_CLOEXEC, 0);

	close(fd);
}

TEST_F(child, test_unknown_fd)
{
	int fd;

	fd = sys_pidfd_getfd(self->pidfd, UNKNOWN_FD, 0);
	EXPECT_EQ(-1, fd) {
		TH_LOG("getfd succeeded while fetching unknown fd");
	};
	EXPECT_EQ(EBADF, errno) {
		TH_LOG("%s: getfd did not get EBADF", strerror(errno));
	}
}

TEST(flags_set)
{
	ASSERT_EQ(-1, sys_pidfd_getfd(0, 0, 1));
	EXPECT_EQ(errno, EINVAL);
}

#if __NR_pidfd_getfd == -1
int main(void)
{
	fprintf(stderr, "__NR_pidfd_getfd undefined. The pidfd_getfd syscall is unavailable. Test aborting\n");
	return KSFT_SKIP;
}
#else
TEST_HARNESS_MAIN
#endif
