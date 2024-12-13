// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Various unit tests for the "ntsync" synchronization primitive driver.
 *
 * Copyright (C) 2021-2022 Elizabeth Figura <zfigura@codeweavers.com>
 */

#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <linux/ntsync.h>
#include "../../kselftest_harness.h"

static int read_sem_state(int sem, __u32 *count, __u32 *max)
{
	struct ntsync_sem_args args;
	int ret;

	memset(&args, 0xcc, sizeof(args));
	ret = ioctl(sem, NTSYNC_IOC_SEM_READ, &args);
	*count = args.count;
	*max = args.max;
	return ret;
}

#define check_sem_state(sem, count, max) \
	({ \
		__u32 __count, __max; \
		int ret = read_sem_state((sem), &__count, &__max); \
		EXPECT_EQ(0, ret); \
		EXPECT_EQ((count), __count); \
		EXPECT_EQ((max), __max); \
	})

static int release_sem(int sem, __u32 *count)
{
	return ioctl(sem, NTSYNC_IOC_SEM_RELEASE, count);
}

static int wait_any(int fd, __u32 count, const int *objs, __u32 owner, __u32 *index)
{
	struct ntsync_wait_args args = {0};
	struct timespec timeout;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &timeout);

	args.timeout = timeout.tv_sec * 1000000000 + timeout.tv_nsec;
	args.count = count;
	args.objs = (uintptr_t)objs;
	args.owner = owner;
	args.index = 0xdeadbeef;
	ret = ioctl(fd, NTSYNC_IOC_WAIT_ANY, &args);
	*index = args.index;
	return ret;
}

TEST(semaphore_state)
{
	struct ntsync_sem_args sem_args;
	struct timespec timeout;
	__u32 count, index;
	int fd, ret, sem;

	clock_gettime(CLOCK_MONOTONIC, &timeout);

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	sem_args.count = 3;
	sem_args.max = 2;
	sem = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_EQ(-1, sem);
	EXPECT_EQ(EINVAL, errno);

	sem_args.count = 2;
	sem_args.max = 2;
	sem = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_LE(0, sem);
	check_sem_state(sem, 2, 2);

	count = 0;
	ret = release_sem(sem, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(2, count);
	check_sem_state(sem, 2, 2);

	count = 1;
	ret = release_sem(sem, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOVERFLOW, errno);
	check_sem_state(sem, 2, 2);

	ret = wait_any(fd, 1, &sem, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(sem, 1, 2);

	ret = wait_any(fd, 1, &sem, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(sem, 0, 2);

	ret = wait_any(fd, 1, &sem, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	count = 3;
	ret = release_sem(sem, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOVERFLOW, errno);
	check_sem_state(sem, 0, 2);

	count = 2;
	ret = release_sem(sem, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);
	check_sem_state(sem, 2, 2);

	ret = wait_any(fd, 1, &sem, 123, &index);
	EXPECT_EQ(0, ret);
	ret = wait_any(fd, 1, &sem, 123, &index);
	EXPECT_EQ(0, ret);

	count = 1;
	ret = release_sem(sem, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);
	check_sem_state(sem, 1, 2);

	count = ~0u;
	ret = release_sem(sem, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOVERFLOW, errno);
	check_sem_state(sem, 1, 2);

	close(sem);

	close(fd);
}

TEST_HARNESS_MAIN
