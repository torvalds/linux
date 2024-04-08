// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <asm/unistd.h>
#include <linux/time_types.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "../../kselftest_harness.h"

struct error {
	int  code;
	char msg[512];
};

static int error_set(struct error *err, int code, const char *fmt, ...)
{
	va_list args;
	int r;

	if (code == 0 || !err || err->code != 0)
		return code;

	err->code = code;
	va_start(args, fmt);
	r = vsnprintf(err->msg, sizeof(err->msg), fmt, args);
	assert((size_t)r < sizeof(err->msg));
	va_end(args);

	return code;
}

static inline int sys_eventfd2(unsigned int count, int flags)
{
	return syscall(__NR_eventfd2, count, flags);
}

TEST(eventfd01)
{
	int fd, flags;

	fd = sys_eventfd2(0, 0);
	ASSERT_GE(fd, 0);

	flags = fcntl(fd, F_GETFL);
	// since the kernel automatically added O_RDWR.
	EXPECT_EQ(flags, O_RDWR);

	close(fd);
}

TEST(eventfd02)
{
	int fd, flags;

	fd = sys_eventfd2(0, EFD_CLOEXEC);
	ASSERT_GE(fd, 0);

	flags = fcntl(fd, F_GETFD);
	ASSERT_GT(flags, -1);
	EXPECT_EQ(flags, FD_CLOEXEC);

	close(fd);
}

TEST(eventfd03)
{
	int fd, flags;

	fd = sys_eventfd2(0, EFD_NONBLOCK);
	ASSERT_GE(fd, 0);

	flags = fcntl(fd, F_GETFL);
	ASSERT_GT(flags, -1);
	EXPECT_EQ(flags & EFD_NONBLOCK, EFD_NONBLOCK);
	EXPECT_EQ(flags & O_RDWR, O_RDWR);

	close(fd);
}

TEST(eventfd04)
{
	int fd, flags;

	fd = sys_eventfd2(0, EFD_CLOEXEC|EFD_NONBLOCK);
	ASSERT_GE(fd, 0);

	flags = fcntl(fd, F_GETFL);
	ASSERT_GT(flags, -1);
	EXPECT_EQ(flags & EFD_NONBLOCK, EFD_NONBLOCK);
	EXPECT_EQ(flags & O_RDWR, O_RDWR);

	flags = fcntl(fd, F_GETFD);
	ASSERT_GT(flags, -1);
	EXPECT_EQ(flags, FD_CLOEXEC);

	close(fd);
}

static inline void trim_newline(char *str)
{
	char *pos = strrchr(str, '\n');

	if (pos)
		*pos = '\0';
}

static int verify_fdinfo(int fd, struct error *err, const char *prefix,
		size_t prefix_len, const char *expect, ...)
{
	char buffer[512] = {0, };
	char path[512] = {0, };
	va_list args;
	FILE *f;
	char *line = NULL;
	size_t n = 0;
	int found = 0;
	int r;

	va_start(args, expect);
	r = vsnprintf(buffer, sizeof(buffer), expect, args);
	assert((size_t)r < sizeof(buffer));
	va_end(args);

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
	f = fopen(path, "re");
	if (!f)
		return error_set(err, -1, "fdinfo open failed for %d", fd);

	while (getline(&line, &n, f) != -1) {
		char *val;

		if (strncmp(line, prefix, prefix_len))
			continue;

		found = 1;

		val = line + prefix_len;
		r = strcmp(val, buffer);
		if (r != 0) {
			trim_newline(line);
			trim_newline(buffer);
			error_set(err, -1, "%s '%s' != '%s'",
				  prefix, val, buffer);
		}
		break;
	}

	free(line);
	fclose(f);

	if (found == 0)
		return error_set(err, -1, "%s not found for fd %d",
				 prefix, fd);

	return 0;
}

TEST(eventfd05)
{
	struct error err = {0};
	int fd, ret;

	fd = sys_eventfd2(0, EFD_SEMAPHORE);
	ASSERT_GE(fd, 0);

	ret = fcntl(fd, F_GETFL);
	ASSERT_GT(ret, -1);
	EXPECT_EQ(ret & O_RDWR, O_RDWR);

	// The semaphore could only be obtained from fdinfo.
	ret = verify_fdinfo(fd, &err, "eventfd-semaphore: ", 19, "1\n");
	if (ret != 0)
		ksft_print_msg("eventfd-semaphore check failed, msg: %s\n",
				err.msg);
	EXPECT_EQ(ret, 0);

	close(fd);
}

TEST_HARNESS_MAIN
