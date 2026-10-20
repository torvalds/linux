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
#include <linux/close_range.h>

#include "kselftest_harness.h"
#include "../clone3/clone3_selftests.h"


#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE 1024
#endif

#ifndef F_DUPFD_QUERY
#define F_DUPFD_QUERY (F_LINUX_SPECIFIC_BASE + 3)
#endif

#ifndef F_CREATED_QUERY
#define F_CREATED_QUERY (F_LINUX_SPECIFIC_BASE + 4)
#endif

static inline int sys_close_range(unsigned int fd, unsigned int max_fd,
				  unsigned int flags)
{
	return syscall(__NR_close_range, fd, max_fd, flags);
}

static void clear_cloexec(const int *fds, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		fcntl(fds[i], F_SETFD, 0);
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

	for (i = 0; i < 100; i++) {
		ret = fcntl(open_fds[i], F_DUPFD_QUERY, open_fds[i + 1]);
		if (ret < 0) {
			EXPECT_EQ(errno, EINVAL);
		} else {
			EXPECT_EQ(ret, 0);
		}
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

TEST(close_range_unshare_hole)
{
	int i, status;
	pid_t pid;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	/* Fill the first two words of the table. */
	for (i = 3; i < 128; i++)
		ASSERT_GE(dup2(0, i), 0);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Punch a hole into the second word, behind a full first one. */
		if (sys_close_range(70, 80, CLOSE_RANGE_UNSHARE))
			exit(EXIT_FAILURE);

		for (i = 3; i < 128; i++) {
			bool closed = i >= 70 && i <= 80;

			if (closed == (fcntl(i, F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		/* A stale full bit on word 1 would hand out 128, not 70. */
		if (dup(0) != 70)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The shared table the child unshared from is untouched. */
	for (i = 3; i < 128; i++)
		EXPECT_NE(-1, fcntl(i, F_GETFD));
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
	int fd1, fd2, fd3, fd4, flags, ret, status;
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

	flags = fcntl(fd1, F_DUPFD_QUERY, fd2);
	if (flags < 0) {
		EXPECT_EQ(errno, EINVAL);
	} else {
		EXPECT_EQ(flags, 1);
	}

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

		flags = fcntl(fd1, F_DUPFD_QUERY, fd3);
		if (flags < 0) {
			EXPECT_EQ(errno, EINVAL);
		} else {
			EXPECT_EQ(flags, 1);
		}



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

	flags = fcntl(fd1, F_DUPFD_QUERY, fd3);
	if (flags < 0) {
		EXPECT_EQ(errno, EINVAL);
	} else {
		EXPECT_EQ(flags, 1);
	}

	fd4 = open("/dev/null", O_RDWR);
	EXPECT_GT(fd4, 0);

	/* Same inode, different file pointers. */
	flags = fcntl(fd1, F_DUPFD_QUERY, fd4);
	if (flags < 0) {
		EXPECT_EQ(errno, EINVAL);
	} else {
		EXPECT_EQ(flags, 0);
	}

	flags = fcntl(fd3, F_GETFD);
	EXPECT_GT(flags, -1);
	EXPECT_EQ(flags & FD_CLOEXEC, 0);

	EXPECT_EQ(close(fd1), 0);
	EXPECT_EQ(close(fd2), 0);
	EXPECT_EQ(close(fd3), 0);
	EXPECT_EQ(close(fd4), 0);
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

TEST(close_range_except)
{
	int i, ret, status;
	pid_t pid;
	int open_fds[101];
	struct __clone_args args = {
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX, CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_EXCEPT");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));

	/* The bounds are checked before the range is turned around. */
	EXPECT_EQ(-1, sys_close_range(open_fds[20], open_fds[10],
				      CLOSE_RANGE_EXCEPT));
	EXPECT_EQ(EINVAL, errno);

	/* Everything above open_fds[50] goes. */
	ASSERT_EQ(0, sys_close_range(0, open_fds[50], CLOSE_RANGE_EXCEPT));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(i <= 50, fcntl(open_fds[i], F_GETFD) != -1);

	/* A window in the middle takes stdio with it, so do that in a fork. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[10], open_fds[20],
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i <= 50; i++) {
			bool kept = i >= 10 && i <= 20;

			if (kept != (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		if (fcntl(STDERR_FILENO, F_GETFD) != -1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The fork had a table of its own. */
	for (i = 0; i <= 50; i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_except_cloexec)
{
	int i, ret;
	int open_fds[101];

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(open_fds[10], open_fds[20],
			      CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_EXCEPT");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		bool inside = i >= 10 && i <= 20;
		int flags = fcntl(open_fds[i], F_GETFD);

		EXPECT_NE(-1, flags);
		EXPECT_EQ(inside ? 0 : FD_CLOEXEC, flags & FD_CLOEXEC);
	}

	/* stdio sits outside of the window too. */
	EXPECT_EQ(FD_CLOEXEC, fcntl(STDERR_FILENO, F_GETFD) & FD_CLOEXEC);

	/* A window that starts at 0 marks only what lies above it. */
	clear_cloexec(open_fds, ARRAY_SIZE(open_fds));
	ASSERT_EQ(0, fcntl(STDERR_FILENO, F_SETFD, 0));
	ASSERT_EQ(0, sys_close_range(0, open_fds[20],
				     CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_EXCEPT));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(i <= 20 ? 0 : FD_CLOEXEC,
			  fcntl(open_fds[i], F_GETFD) & FD_CLOEXEC);
	EXPECT_EQ(0, fcntl(STDERR_FILENO, F_GETFD) & FD_CLOEXEC);

	/* One at the top marks only what lies below it, stdio included. */
	clear_cloexec(open_fds, ARRAY_SIZE(open_fds));
	ASSERT_EQ(0, sys_close_range(open_fds[80], UINT_MAX,
				     CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_EXCEPT));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(i < 80 ? FD_CLOEXEC : 0,
			  fcntl(open_fds[i], F_GETFD) & FD_CLOEXEC);
	EXPECT_EQ(FD_CLOEXEC, fcntl(STDERR_FILENO, F_GETFD) & FD_CLOEXEC);

	/* One that cannot hold a descriptor marks everything. */
	clear_cloexec(open_fds, ARRAY_SIZE(open_fds));
	ASSERT_EQ(0, fcntl(STDERR_FILENO, F_SETFD, 0));
	ASSERT_EQ(0, sys_close_range(UINT_MAX, UINT_MAX,
				     CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_EXCEPT));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(FD_CLOEXEC, fcntl(open_fds[i], F_GETFD) & FD_CLOEXEC);
	EXPECT_EQ(FD_CLOEXEC, fcntl(STDERR_FILENO, F_GETFD) & FD_CLOEXEC);
}

TEST(close_range_except_cloexec_unshare)
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

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything marks nothing. */
	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_EXCEPT");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(0, fcntl(open_fds[i], F_GETFD) & FD_CLOEXEC);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[10], open_fds[20],
				      CLOSE_RANGE_UNSHARE | CLOSE_RANGE_CLOEXEC |
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool inside = i >= 10 && i <= 20;
			int flags = fcntl(open_fds[i], F_GETFD);

			if (flags == -1)
				exit(EXIT_FAILURE);
			if ((flags & FD_CLOEXEC) != (inside ? 0 : FD_CLOEXEC))
				exit(EXIT_FAILURE);
		}

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The shared table the child unshared from is untouched. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(0, fcntl(open_fds[i], F_GETFD) & FD_CLOEXEC);
}

TEST(close_range_except_bounds)
{
	int i, c, ret, status;
	pid_t pid;
	int open_fds[101];
	struct __clone_args args = {
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX, CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_EXCEPT");
	}
	ASSERT_EQ(0, ret);

	struct {
		unsigned int fd, max_fd, flags;
	} cases[] = {
		/* A window at the top drops everything below it. */
		{ open_fds[50], UINT_MAX, CLOSE_RANGE_EXCEPT },
		/* One that cannot hold a descriptor keeps nothing. */
		{ UINT_MAX, UINT_MAX, CLOSE_RANGE_EXCEPT },
		/* One of a single descriptor keeps just that. */
		{ open_fds[30], open_fds[30], CLOSE_RANGE_EXCEPT },
		/* The unshare form on a table that is not shared acts in place. */
		{ open_fds[10], open_fds[20],
		  CLOSE_RANGE_UNSHARE | CLOSE_RANGE_EXCEPT },
	};

	/* Each of them takes stdio with it, so do that in a fork. */
	for (c = 0; c < ARRAY_SIZE(cases); c++) {
		pid = sys_clone3(&args, sizeof(args));
		ASSERT_GE(pid, 0);

		if (pid == 0) {
			ret = sys_close_range(cases[c].fd, cases[c].max_fd,
					      cases[c].flags);
			if (ret)
				exit(EXIT_FAILURE);

			for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
				unsigned int fd = open_fds[i];
				bool kept = fd >= cases[c].fd &&
					    fd <= cases[c].max_fd;

				if (kept != (fcntl(fd, F_GETFD) != -1))
					exit(EXIT_FAILURE);
			}

			if (fcntl(STDERR_FILENO, F_GETFD) != -1)
				exit(EXIT_FAILURE);

			exit(EXIT_SUCCESS);
		}

		EXPECT_EQ(waitpid(pid, &status, 0), pid);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
	}

	/* Each fork had a table of its own. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_except_unshare)
{
	int i, ret, status;
	pid_t pid;
	int open_fds[200];
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		/* Odd slots are close-on-exec, which makes no difference here. */
		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX, CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_EXCEPT");
	}
	ASSERT_EQ(0, ret);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* The window sits near the top, so the clone is sized off its end. */
		ret = sys_close_range(open_fds[150], open_fds[160],
				      CLOSE_RANGE_UNSHARE | CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool kept = i >= 150 && i <= 160;

			if (kept != (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		if (fcntl(STDERR_FILENO, F_GETFD) != -1)
			exit(EXIT_FAILURE);

		/* What was left behind is handed out again, from the bottom. */
		if (dup(open_fds[150]) != 0)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* A window at the bottom keeps just that, stdio included. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(0, open_fds[10],
				      CLOSE_RANGE_UNSHARE | CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			if ((i <= 10) != (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		if (fcntl(STDERR_FILENO, F_GETFD) == -1)
			exit(EXIT_FAILURE);

		/* The first slot left behind is the next one handed out. */
		if (dup(0) != open_fds[10] + 1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* A window that cannot hold a descriptor keeps nothing. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(UINT_MAX, UINT_MAX,
				      CLOSE_RANGE_UNSHARE | CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++)
			if (fcntl(open_fds[i], F_GETFD) != -1)
				exit(EXIT_FAILURE);

		if (fcntl(STDERR_FILENO, F_GETFD) != -1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The shared table the child unshared from is untouched. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_cloexec_only)
{
	int i, ret;
	int open_fds[101];

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		/* Odd slots are close-on-exec, even ones are not. */
		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(open_fds[10], open_fds[20],
			      CLOSE_RANGE_CLOEXEC_ONLY);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		bool closed = i % 2 && i >= 10 && i <= 20;

		EXPECT_EQ(!closed, fcntl(open_fds[i], F_GETFD) != -1);
	}

	/* A range above the table closes nothing. */
	ASSERT_EQ(0, sys_close_range(UINT_MAX, UINT_MAX,
				     CLOSE_RANGE_CLOEXEC_ONLY));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		bool closed = i % 2 && i >= 10 && i <= 20;

		EXPECT_EQ(!closed, fcntl(open_fds[i], F_GETFD) != -1);
	}

	/* Do what an exec would do to the rest. */
	ASSERT_EQ(0, sys_close_range(0, UINT_MAX, CLOSE_RANGE_CLOEXEC_ONLY));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(!(i % 2), fcntl(open_fds[i], F_GETFD) != -1);
}

TEST(close_range_cloexec_only_except)
{
	int i, ret;
	int open_fds[101];

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(open_fds[10], open_fds[20],
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		bool kept = !(i % 2) || (i >= 10 && i <= 20);
		int flags = i % 2 ? FD_CLOEXEC : 0;

		/* The kept ones keep their flag, so exec still drops them. */
		EXPECT_EQ(kept ? flags : -1, fcntl(open_fds[i], F_GETFD));
	}

	/* A range that cannot hold an open descriptor keeps nothing. */
	ASSERT_EQ(0, sys_close_range(UINT_MAX, UINT_MAX,
				     CLOSE_RANGE_CLOEXEC_ONLY |
				     CLOSE_RANGE_EXCEPT));

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_EQ(!(i % 2), fcntl(open_fds[i], F_GETFD) != -1);
}

TEST(close_range_cloexec_only_except_bounds)
{
	int i, c, ret, status;
	pid_t pid;
	int open_fds[101];
	struct __clone_args args = {
		.exit_signal = SIGCHLD,
	};

	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	struct {
		unsigned int fd, max_fd;
	} cases[] = {
		/* A window at the top keeps the marked ones in it. */
		{ open_fds[80], UINT_MAX },
		/* One at the bottom keeps the marked ones in it. */
		{ 0, open_fds[20] },
		/* One that cannot hold a descriptor keeps none of them. */
		{ UINT_MAX, UINT_MAX },
	};

	for (c = 0; c < ARRAY_SIZE(cases); c++) {
		pid = sys_clone3(&args, sizeof(args));
		ASSERT_GE(pid, 0);

		if (pid == 0) {
			ret = sys_close_range(cases[c].fd, cases[c].max_fd,
					      CLOSE_RANGE_CLOEXEC_ONLY |
					      CLOSE_RANGE_EXCEPT);
			if (ret)
				exit(EXIT_FAILURE);

			for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
				unsigned int fd = open_fds[i];
				bool kept = !(i % 2) || (fd >= cases[c].fd &&
							 fd <= cases[c].max_fd);
				int flags = i % 2 ? FD_CLOEXEC : 0;

				if (fcntl(fd, F_GETFD) != (kept ? flags : -1))
					exit(EXIT_FAILURE);
			}

			/* stdio is neither marked nor gone. */
			if (fcntl(STDERR_FILENO, F_GETFD) & FD_CLOEXEC)
				exit(EXIT_FAILURE);

			exit(EXIT_SUCCESS);
		}

		EXPECT_EQ(waitpid(pid, &status, 0), pid);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
	}

	/* Each fork had a table of its own. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_cloexec_only_unshare)
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

		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		ASSERT_NE(-1, fcntl(open_fds[i], F_GETFD));

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[10], open_fds[20],
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool closed = i % 2 && i >= 10 && i <= 20;

			if (closed == (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* A range at the top keeps the descriptors without the flag in it. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[50], UINT_MAX,
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool closed = i % 2 && i >= 50;

			if (closed == (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		/* The first slot left behind is the next one handed out. */
		if (dup(0) != open_fds[51])
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The shared table the child unshared from is untouched. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_cloexec_only_except_unshare)
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

		fd = open("/dev/null", O_RDONLY | (i % 2 ? O_CLOEXEC : 0));
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	/* A range covering everything keeps everything. */
	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		ASSERT_NE(-1, fcntl(open_fds[i], F_GETFD));

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[10], open_fds[20],
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY |
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool kept = !(i % 2) || (i >= 10 && i <= 20);
			int flags = i % 2 ? FD_CLOEXEC : 0;

			if (fcntl(open_fds[i], F_GETFD) != (kept ? flags : -1))
				exit(EXIT_FAILURE);
		}

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* A window that cannot hold a descriptor keeps none of the marked. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(UINT_MAX, UINT_MAX,
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY |
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++)
			if ((i % 2) == (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);

		if (fcntl(STDERR_FILENO, F_GETFD) == -1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* One that covers everything keeps everything, in a clone too. */
	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(0, UINT_MAX,
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY |
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++)
			if (fcntl(open_fds[i], F_GETFD) != (i % 2 ? FD_CLOEXEC : 0))
				exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* The shared table the child unshared from is untouched. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++)
		EXPECT_NE(-1, fcntl(open_fds[i], F_GETFD));
}

TEST(close_range_cloexec_only_except_unshare_sizing)
{
	int i, ret, status;
	pid_t pid;
	int open_fds[200];
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	/* All close-on-exec, so the kept range alone sizes the clone. */
	for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
		int fd;

		fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return, "Skipping test since /dev/null does not exist");
		}

		open_fds[i] = fd;
	}

	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = sys_close_range(open_fds[150], open_fds[160],
				      CLOSE_RANGE_UNSHARE |
				      CLOSE_RANGE_CLOEXEC_ONLY |
				      CLOSE_RANGE_EXCEPT);
		if (ret)
			exit(EXIT_FAILURE);

		for (i = 0; i < ARRAY_SIZE(open_fds); i++) {
			bool kept = i >= 150 && i <= 160;

			if (kept != (fcntl(open_fds[i], F_GETFD) != -1))
				exit(EXIT_FAILURE);
		}

		/* Nothing set close-on-exec on stdio. */
		if (fcntl(STDERR_FILENO, F_GETFD) == -1)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(close_range_cloexec_only_einval)
{
	int ret;

	/* A range covering everything keeps everything, so this only probes. */
	ret = sys_close_range(0, UINT_MAX,
			      CLOSE_RANGE_CLOEXEC_ONLY | CLOSE_RANGE_EXCEPT);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "close_range() syscall not supported");
		if (errno == EINVAL)
			SKIP(return, "close_range() doesn't support CLOSE_RANGE_CLOEXEC_ONLY");
	}
	ASSERT_EQ(0, ret);

	EXPECT_EQ(-1, sys_close_range(3, UINT_MAX, CLOSE_RANGE_CLOEXEC |
						   CLOSE_RANGE_CLOEXEC_ONLY));
	EXPECT_EQ(EINVAL, errno);

	/* The other flags do not make the pair acceptable. */
	EXPECT_EQ(-1, sys_close_range(3, UINT_MAX, CLOSE_RANGE_UNSHARE |
						   CLOSE_RANGE_CLOEXEC |
						   CLOSE_RANGE_CLOEXEC_ONLY |
						   CLOSE_RANGE_EXCEPT));
	EXPECT_EQ(EINVAL, errno);

	/* The bounds are checked with the new flag too. */
	EXPECT_EQ(-1, sys_close_range(4, 3, CLOSE_RANGE_CLOEXEC_ONLY |
					    CLOSE_RANGE_EXCEPT));
	EXPECT_EQ(EINVAL, errno);
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

TEST(fcntl_created)
{
	for (int i = 0; i < 101; i++) {
		int fd;
		char path[PATH_MAX];

		fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0) {
			if (errno == ENOENT)
				SKIP(return,
					   "Skipping test since /dev/null does not exist");
		}

		/* We didn't create "/dev/null". */
		EXPECT_EQ(fcntl(fd, F_CREATED_QUERY, 0), 0);
		close(fd);

		sprintf(path, "aaaa_%d", i);
		fd = open(path, O_CREAT | O_RDONLY | O_CLOEXEC, 0600);
		ASSERT_GE(fd, 0);

		/* We created "aaaa_%d". */
		EXPECT_EQ(fcntl(fd, F_CREATED_QUERY, 0), 1);
		close(fd);

		fd = open(path, O_RDONLY | O_CLOEXEC);
		ASSERT_GE(fd, 0);

		/* We're opening it again, so no positive creation check. */
		EXPECT_EQ(fcntl(fd, F_CREATED_QUERY, 0), 0);
		close(fd);
		unlink(path);
	}
}

TEST_HARNESS_MAIN
