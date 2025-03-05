// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kernel.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <sys/resource.h>

#include "../kselftest_harness.h"
#include "../clone3/clone3_selftests.h"

#ifndef __NR_close_range
	#if defined __alpha__
		#define __NR_close_range 546
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_close_range (436 + 4000)
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_close_range (436 + 6000)
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_close_range (436 + 5000)
		#endif
	#elif defined __ia64__
		#define __NR_close_range (436 + 1024)
	#else
		#define __NR_close_range 436
	#endif
#endif

#ifndef CLOSE_RANGE_UNSHARE
#define CLOSE_RANGE_UNSHARE	(1U << 1)
#endif

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC	(1U << 2)
#endif

static inline int sys_close_range(unsigned int fd, unsigned int max_fd,
				  unsigned int flags)
{
	return syscall(__NR_close_range, fd, max_fd, flags);
}

TEST(core_close_range)
{
	int i, ret;
	int open_fds[101];

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	EXPECT_EQ(-1, sys_close_range(open_fds[0], open_fds[100], -1)) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
	}

	EXPECT_EQ(0, sys_close_range(open_fds[0], open_fds[50], 0));

	for (i = 0; i <= 50; i++)
		EXPECT_EQ(-1, fcntl(open_fds[i], F_GETFL));

	for (i = 51; i <= 100; i++)
		EXPECT_GT(fcntl(open_fds[i], F_GETFL), -1);

	/* create a couple of gaps */
	close(57);
	close(78);
	close(81);
	close(82);
	close(84);
	close(90);

	EXPECT_EQ(0, sys_close_range(open_fds[51], open_fds[92], 0));

	for (i = 51; i <= 92; i++)
		EXPECT_EQ(-1, fcntl(open_fds[i], F_GETFL));

	for (i = 93; i <= 100; i++)
		EXPECT_GT(fcntl(open_fds[i], F_GETFL), -1);

	/* test that the kernel caps and still closes all fds */
	EXPECT_EQ(0, sys_close_range(open_fds[93], open_fds[99], 0));

	for (i = 93; i <= 99; i++)
		EXPECT_EQ(-1, fcntl(open_fds[i], F_GETFL));

	EXPECT_GT(fcntl(open_fds[i], F_GETFL), -1);

	EXPECT_EQ(0, sys_close_range(open_fds[100], open_fds[100], 0));

	EXPECT_EQ(-1, fcntl(open_fds[100], F_GETFL));
}

TEST(close_range_unshare)
{
	int i, ret, status;
	pid_t pid;
	int open_fds[101];
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[0], open_fds[50],
				      CLOSE_RANGE_UNSHARE);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i <= 50; i++)
			if (fcntl(open_fds[i], F_GETFL) != -1)
				exit(EXIT_FAILURE);

		for (i = 51; i <= 100; i++)
			if (fcntl(open_fds[i], F_GETFL) == -1)
				exit(EXIT_FAILURE);

		/* create a couple of gaps */
		close(57);
		close(78);
		close(81);
		close(82);
		close(84);
		close(90);

		ret = sys_close_range(open_fds[51], open_fds[92],
				      CLOSE_RANGE_UNSHARE);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 51; i <= 92; i++)
			if (fcntl(open_fds[i], F_GETFL) != -1)
				exit(EXIT_FAILURE);

		for (i = 93; i <= 100; i++)
			if (fcntl(open_fds[i], F_GETFL) == -1)
				exit(EXIT_FAILURE);

		/* test that the kernel caps and still closes all fds */
		ret = sys_close_range(open_fds[93], open_fds[99],
				      CLOSE_RANGE_UNSHARE);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 93; i <= 99; i++)
			if (fcntl(open_fds[i], F_GETFL) != -1)
				exit(EXIT_FAILURE);

		if (fcntl(open_fds[100], F_GETFL) == -1)
			exit(EXIT_FAILURE);

		ret = sys_close_range(open_fds[100], open_fds[100],
				      CLOSE_RANGE_UNSHARE);
		if (ret)
			exit(EXIT_FAILURE);

		if (fcntl(open_fds[100], F_GETFL) != -1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(close_range_unshare_capped)
{
	int i, ret, status;
	pid_t pid;
	int open_fds[101];
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[0], UINT_MAX,
				      CLOSE_RANGE_UNSHARE);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i <= 100; i++)
			if (fcntl(open_fds[i], F_GETFL) != -1)
				exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(close_range_cloexec)
{
	int i, ret;
	int open_fds[101];
	struct rlimit rlimit;

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(1000, 1000, CLOSE_RANGE_CLOEXEC);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC");
	}

	/* Ensure the FD_CLOEXEC bit is set also with a resource limit in place.  */
	ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &rlimit));
	rlimit.rlim_cur = 25;
	ASSERT_EQ(0, setrlimit(RLIMIT_NOFILE, &rlimit));

	/* Set close-on-exec for two ranges: [0-50] and [75-100].  */
	ret = sys_close_range(open_fds[0], open_fds[50], CLOSE_RANGE_CLOEXEC);
	ASSERT_EQ(0, ret);
	ret = sys_close_range(open_fds[75], open_fds[100], CLOSE_RANGE_CLOEXEC);
	ASSERT_EQ(0, ret);

	for (i = 0; i <= 50; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}

	for (i = 51; i <= 74; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, 0);
	}

	for (i = 75; i <= 100; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}

	/* Test a common pattern.  */
	ret = sys_close_range(3, UINT_MAX, CLOSE_RANGE_CLOEXEC);
	for (i = 0; i <= 100; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}
}

TEST(close_range_cloexec_unshare)
{
	int i, ret;
	int open_fds[101];
	struct rlimit rlimit;

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(1000, 1000, CLOSE_RANGE_CLOEXEC);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC");
	}

	/* Ensure the FD_CLOEXEC bit is set also with a resource limit in place.  */
	ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &rlimit));
	rlimit.rlim_cur = 25;
	ASSERT_EQ(0, setrlimit(RLIMIT_NOFILE, &rlimit));

	/* Set close-on-exec for two ranges: [0-50] and [75-100].  */
	ret = sys_close_range(open_fds[0], open_fds[50],
			      CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_UNSHARE);
	ASSERT_EQ(0, ret);
	ret = sys_close_range(open_fds[75], open_fds[100],
			      CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_UNSHARE);
	ASSERT_EQ(0, ret);

	for (i = 0; i <= 50; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}

	for (i = 51; i <= 74; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, 0);
	}

	for (i = 75; i <= 100; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}

	/* Test a common pattern.  */
	ret = sys_close_range(3, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_UNSHARE);
	for (i = 0; i <= 100; i++) {
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);
	}
}

/*
 * Regression test for syzbot+96cfd2b22b3213646a93@syzkaller.appspotmail.com
 */
TEST(close_range_cloexec_syzbot)
{
	int fd1, fd2, fd3, flags, ret, status;
	pid_t pid;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	/* Create a huge gap in the fd table. */
	fd1 = open("/dev/null", O_RDWR);
	EXPECT_GT(fd1, 0);

	fd2 = dup2(fd1, 1000);
	EXPECT_GT(fd2, 0);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);
		if (ret)
			exit(EXIT_FAILURE);

		/*
			 * We now have a private file descriptor table and all
			 * our open fds should still be open but made
			 * close-on-exec.
			 */
		flags = fcntl(fd1, F_GETFD);
		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

		flags = fcntl(fd2, F_GETFD);
		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

		fd3 = dup2(fd1, 42);
		EXPECT_GT(fd3, 0);

		/*
			 * Duplicating the file descriptor must remove the
			 * FD_CLOEXEC flag.
			 */
		flags = fcntl(fd3, F_GETFD);
		EXPECT_GT(flags, -1);
		EXPECT_EQ(flags & FD_CLOEXEC, 0);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/*
	 * We had a shared file descriptor table before along with requesting
	 * close-on-exec so the original fds must not be close-on-exec.
	 */
	flags = fcntl(fd1, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

	flags = fcntl(fd2, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

	fd3 = dup2(fd1, 42);
	EXPECT_GT(fd3, 0);

	flags = fcntl(fd3, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, 0);

	EXPECT_EQ(close(fd1), 0);
	EXPECT_EQ(close(fd2), 0);
	EXPECT_EQ(close(fd3), 0);
}

/*
 * Regression test for syzbot+96cfd2b22b3213646a93@syzkaller.appspotmail.com
 */
TEST(close_range_cloexec_unshare_syzbot)
{
	int i, fd1, fd2, fd3, flags, ret, status;
	pid_t pid;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	/*
	 * Create a huge gap in the fd table. When we now call
	 * CLOSE_RANGE_UNSHARE with a shared fd table and and with ~0U as upper
	 * bound the kernel will only copy up to fd1 file descriptors into the
	 * new fd table. If the kernel is buggy and doesn't handle
	 * CLOSE_RANGE_CLOEXEC correctly it will not have copied all file
	 * descriptors and we will oops!
	 *
	 * On a buggy kernel this should immediately oops. But let's loop just
	 * to be sure.
	 */
	fd1 = open("/dev/null", O_RDWR);
	EXPECT_GT(fd1, 0);

	fd2 = dup2(fd1, 1000);
	EXPECT_GT(fd2, 0);

	for (i = 0; i < 100; i++) {

		pid = sys_clone3(&args, sizeof(args));
		ASSERT_GE(pid, 0);

		if (pid == 0) {
			ret = sys_close_range(3, ~0U, CLOSE_RANGE_UNSHARE |
						      CLOSE_RANGE_CLOEXEC);
			if (ret)
				exit(EXIT_FAILURE);

			/*
			 * We now have a private file descriptor table and all
			 * our open fds should still be open but made
			 * close-on-exec.
			 */
			flags = fcntl(fd1, F_GETFD);
			EXPECT_GT(flags, -1);
			EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

			flags = fcntl(fd2, F_GETFD);
			EXPECT_GT(flags, -1);
			EXPECT_EQ(flags & FD_CLOEXEC, FD_CLOEXEC);

			fd3 = dup2(fd1, 42);
			EXPECT_GT(fd3, 0);

			/*
			 * Duplicating the file descriptor must remove the
			 * FD_CLOEXEC flag.
			 */
			flags = fcntl(fd3, F_GETFD);
			EXPECT_GT(flags, -1);
			EXPECT_EQ(flags & FD_CLOEXEC, 0);

			EXPECT_EQ(close(fd1), 0);
			EXPECT_EQ(close(fd2), 0);
			EXPECT_EQ(close(fd3), 0);

			exit(EXIT_SUCCESS);
		}

		EXPECT_EQ(waitpid(pid, &status, 0), pid);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
	}

	/*
	 * We created a private file descriptor table before along with
	 * requesting close-on-exec so the original fds must not be
	 * close-on-exec.
	 */
	flags = fcntl(fd1, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, 0);

	flags = fcntl(fd2, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, 0);

	fd3 = dup2(fd1, 42);
	EXPECT_GT(fd3, 0);

	flags = fcntl(fd3, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, 0);

	EXPECT_EQ(close(fd1), 0);
	EXPECT_EQ(close(fd2), 0);
	EXPECT_EQ(close(fd3), 0);
}

TEST(close_range_bitmap_corruption)
{
	pid_t pid;
	int status;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	/* get the first 128 descriptors open */
	for (int i = 2; i < 128; i++)
		EXPECT_GE(dup2(0, i), 0);

	/* get descriptor table shared */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* unshare and truncate descriptor table down to 64 */
		if (sys_close_range(64, ~0U, CLOSE_RANGE_UNSHARE))
			exit(EXIT_FAILURE);

		ASSERT_EQ(fcntl(64, F_GETFD), -1);
		/* ... and verify that the range 64..127 is not
		   stuck "fully used" according to secondary bitmap */
		EXPECT_EQ(dup(0), 64)
			exit(EXIT_FAILURE);
		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST_HARNESS_MAIN
