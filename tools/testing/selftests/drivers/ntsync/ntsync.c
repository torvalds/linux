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

static int read_mutex_state(int mutex, __u32 *count, __u32 *owner)
{
	struct ntsync_mutex_args args;
	int ret;

	memset(&args, 0xcc, sizeof(args));
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_READ, &args);
	*count = args.count;
	*owner = args.owner;
	return ret;
}

#define check_mutex_state(mutex, count, owner) \
	({ \
		__u32 __count, __owner; \
		int ret = read_mutex_state((mutex), &__count, &__owner); \
		EXPECT_EQ(0, ret); \
		EXPECT_EQ((count), __count); \
		EXPECT_EQ((owner), __owner); \
	})

static int unlock_mutex(int mutex, __u32 owner, __u32 *count)
{
	struct ntsync_mutex_args args;
	int ret;

	args.owner = owner;
	args.count = 0xdeadbeef;
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_UNLOCK, &args);
	*count = args.count;
	return ret;
}

static int read_event_state(int event, __u32 *signaled, __u32 *manual)
{
	struct ntsync_event_args args;
	int ret;

	memset(&args, 0xcc, sizeof(args));
	ret = ioctl(event, NTSYNC_IOC_EVENT_READ, &args);
	*signaled = args.signaled;
	*manual = args.manual;
	return ret;
}

#define check_event_state(event, signaled, manual) \
	({ \
		__u32 __signaled, __manual; \
		int ret = read_event_state((event), &__signaled, &__manual); \
		EXPECT_EQ(0, ret); \
		EXPECT_EQ((signaled), __signaled); \
		EXPECT_EQ((manual), __manual); \
	})

static int wait_objs(int fd, unsigned long request, __u32 count,
		     const int *objs, __u32 owner, __u32 *index)
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
	ret = ioctl(fd, request, &args);
	*index = args.index;
	return ret;
}

static int wait_any(int fd, __u32 count, const int *objs, __u32 owner, __u32 *index)
{
	return wait_objs(fd, NTSYNC_IOC_WAIT_ANY, count, objs, owner, index);
}

static int wait_all(int fd, __u32 count, const int *objs, __u32 owner, __u32 *index)
{
	return wait_objs(fd, NTSYNC_IOC_WAIT_ALL, count, objs, owner, index);
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

TEST(mutex_state)
{
	struct ntsync_mutex_args mutex_args;
	__u32 owner, count, index;
	struct timespec timeout;
	int fd, ret, mutex;

	clock_gettime(CLOCK_MONOTONIC, &timeout);

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	mutex_args.owner = 123;
	mutex_args.count = 0;
	mutex = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_EQ(-1, mutex);
	EXPECT_EQ(EINVAL, errno);

	mutex_args.owner = 0;
	mutex_args.count = 2;
	mutex = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_EQ(-1, mutex);
	EXPECT_EQ(EINVAL, errno);

	mutex_args.owner = 123;
	mutex_args.count = 2;
	mutex = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, mutex);
	check_mutex_state(mutex, 2, 123);

	ret = unlock_mutex(mutex, 0, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);

	ret = unlock_mutex(mutex, 456, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EPERM, errno);
	check_mutex_state(mutex, 2, 123);

	ret = unlock_mutex(mutex, 123, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(2, count);
	check_mutex_state(mutex, 1, 123);

	ret = unlock_mutex(mutex, 123, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, count);
	check_mutex_state(mutex, 0, 0);

	ret = unlock_mutex(mutex, 123, &count);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EPERM, errno);

	ret = wait_any(fd, 1, &mutex, 456, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_mutex_state(mutex, 1, 456);

	ret = wait_any(fd, 1, &mutex, 456, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_mutex_state(mutex, 2, 456);

	ret = unlock_mutex(mutex, 456, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(2, count);
	check_mutex_state(mutex, 1, 456);

	ret = wait_any(fd, 1, &mutex, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	owner = 0;
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);

	owner = 123;
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EPERM, errno);
	check_mutex_state(mutex, 1, 456);

	owner = 456;
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(0, ret);

	memset(&mutex_args, 0xcc, sizeof(mutex_args));
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_READ, &mutex_args);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(0, mutex_args.count);
	EXPECT_EQ(0, mutex_args.owner);

	memset(&mutex_args, 0xcc, sizeof(mutex_args));
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_READ, &mutex_args);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(0, mutex_args.count);
	EXPECT_EQ(0, mutex_args.owner);

	ret = wait_any(fd, 1, &mutex, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(0, index);
	check_mutex_state(mutex, 1, 123);

	owner = 123;
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(0, ret);

	memset(&mutex_args, 0xcc, sizeof(mutex_args));
	ret = ioctl(mutex, NTSYNC_IOC_MUTEX_READ, &mutex_args);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(0, mutex_args.count);
	EXPECT_EQ(0, mutex_args.owner);

	ret = wait_any(fd, 1, &mutex, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(0, index);
	check_mutex_state(mutex, 1, 123);

	close(mutex);

	mutex_args.owner = 0;
	mutex_args.count = 0;
	mutex = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, mutex);
	check_mutex_state(mutex, 0, 0);

	ret = wait_any(fd, 1, &mutex, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_mutex_state(mutex, 1, 123);

	close(mutex);

	mutex_args.owner = 123;
	mutex_args.count = ~0u;
	mutex = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, mutex);
	check_mutex_state(mutex, ~0u, 123);

	ret = wait_any(fd, 1, &mutex, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	close(mutex);

	close(fd);
}

TEST(manual_event_state)
{
	struct ntsync_event_args event_args;
	__u32 index, signaled;
	int fd, event, ret;

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	event_args.manual = 1;
	event_args.signaled = 0;
	event = ioctl(fd, NTSYNC_IOC_CREATE_EVENT, &event_args);
	EXPECT_LE(0, event);
	check_event_state(event, 0, 1);

	signaled = 0xdeadbeef;
	ret = ioctl(event, NTSYNC_IOC_EVENT_SET, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, signaled);
	check_event_state(event, 1, 1);

	ret = ioctl(event, NTSYNC_IOC_EVENT_SET, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, signaled);
	check_event_state(event, 1, 1);

	ret = wait_any(fd, 1, &event, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_event_state(event, 1, 1);

	signaled = 0xdeadbeef;
	ret = ioctl(event, NTSYNC_IOC_EVENT_RESET, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, signaled);
	check_event_state(event, 0, 1);

	ret = ioctl(event, NTSYNC_IOC_EVENT_RESET, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, signaled);
	check_event_state(event, 0, 1);

	ret = wait_any(fd, 1, &event, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	ret = ioctl(event, NTSYNC_IOC_EVENT_SET, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, signaled);

	ret = ioctl(event, NTSYNC_IOC_EVENT_PULSE, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, signaled);
	check_event_state(event, 0, 1);

	ret = ioctl(event, NTSYNC_IOC_EVENT_PULSE, &signaled);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, signaled);
	check_event_state(event, 0, 1);

	close(event);

	close(fd);
}

TEST(test_wait_any)
{
	int objs[NTSYNC_MAX_WAIT_COUNT + 1], fd, ret;
	struct ntsync_mutex_args mutex_args = {0};
	struct ntsync_sem_args sem_args = {0};
	__u32 owner, index, count, i;
	struct timespec timeout;

	clock_gettime(CLOCK_MONOTONIC, &timeout);

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	sem_args.count = 2;
	sem_args.max = 3;
	objs[0] = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_LE(0, objs[0]);

	mutex_args.owner = 0;
	mutex_args.count = 0;
	objs[1] = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, objs[1]);

	ret = wait_any(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 1, 3);
	check_mutex_state(objs[1], 0, 0);

	ret = wait_any(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 0, 0);

	ret = wait_any(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, index);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 1, 123);

	count = 1;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);

	ret = wait_any(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 1, 123);

	ret = wait_any(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, index);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 2, 123);

	ret = wait_any(fd, 2, objs, 456, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	owner = 123;
	ret = ioctl(objs[1], NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(0, ret);

	ret = wait_any(fd, 2, objs, 456, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	EXPECT_EQ(1, index);

	ret = wait_any(fd, 2, objs, 456, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, index);

	close(objs[1]);

	/* test waiting on the same object twice */

	count = 2;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);

	objs[1] = objs[0];
	ret = wait_any(fd, 2, objs, 456, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 1, 3);

	ret = wait_any(fd, 0, NULL, 456, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);

	for (i = 1; i < NTSYNC_MAX_WAIT_COUNT + 1; ++i)
		objs[i] = objs[0];

	ret = wait_any(fd, NTSYNC_MAX_WAIT_COUNT, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);

	ret = wait_any(fd, NTSYNC_MAX_WAIT_COUNT + 1, objs, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);

	ret = wait_any(fd, -1, objs, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);

	close(objs[0]);

	close(fd);
}

TEST(test_wait_all)
{
	struct ntsync_mutex_args mutex_args = {0};
	struct ntsync_sem_args sem_args = {0};
	__u32 owner, index, count;
	int objs[2], fd, ret;

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	sem_args.count = 2;
	sem_args.max = 3;
	objs[0] = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_LE(0, objs[0]);

	mutex_args.owner = 0;
	mutex_args.count = 0;
	objs[1] = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, objs[1]);

	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 1, 3);
	check_mutex_state(objs[1], 1, 123);

	ret = wait_all(fd, 2, objs, 456, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);
	check_sem_state(objs[0], 1, 3);
	check_mutex_state(objs[1], 1, 123);

	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 2, 123);

	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(ETIMEDOUT, errno);
	check_sem_state(objs[0], 0, 3);
	check_mutex_state(objs[1], 2, 123);

	count = 3;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);

	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);
	check_sem_state(objs[0], 2, 3);
	check_mutex_state(objs[1], 3, 123);

	owner = 123;
	ret = ioctl(objs[1], NTSYNC_IOC_MUTEX_KILL, &owner);
	EXPECT_EQ(0, ret);

	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EOWNERDEAD, errno);
	check_sem_state(objs[0], 1, 3);
	check_mutex_state(objs[1], 1, 123);

	close(objs[1]);

	/* test waiting on the same object twice */
	objs[1] = objs[0];
	ret = wait_all(fd, 2, objs, 123, &index);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);

	close(objs[0]);

	close(fd);
}

struct wake_args {
	int fd;
	int obj;
};

struct wait_args {
	int fd;
	unsigned long request;
	struct ntsync_wait_args *args;
	int ret;
	int err;
};

static void *wait_thread(void *arg)
{
	struct wait_args *args = arg;

	args->ret = ioctl(args->fd, args->request, args->args);
	args->err = errno;
	return NULL;
}

static __u64 get_abs_timeout(unsigned int ms)
{
	struct timespec timeout;
	clock_gettime(CLOCK_MONOTONIC, &timeout);
	return (timeout.tv_sec * 1000000000) + timeout.tv_nsec + (ms * 1000000);
}

static int wait_for_thread(pthread_t thread, unsigned int ms)
{
	struct timespec timeout;

	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_nsec += ms * 1000000;
	timeout.tv_sec += (timeout.tv_nsec / 1000000000);
	timeout.tv_nsec %= 1000000000;
	return pthread_timedjoin_np(thread, NULL, &timeout);
}

TEST(wake_any)
{
	struct ntsync_mutex_args mutex_args = {0};
	struct ntsync_wait_args wait_args = {0};
	struct ntsync_sem_args sem_args = {0};
	struct wait_args thread_args;
	int objs[2], fd, ret;
	__u32 count, index;
	pthread_t thread;

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	sem_args.count = 0;
	sem_args.max = 3;
	objs[0] = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_LE(0, objs[0]);

	mutex_args.owner = 123;
	mutex_args.count = 1;
	objs[1] = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, objs[1]);

	/* test waking the semaphore */

	wait_args.timeout = get_abs_timeout(1000);
	wait_args.objs = (uintptr_t)objs;
	wait_args.count = 2;
	wait_args.owner = 456;
	wait_args.index = 0xdeadbeef;
	thread_args.fd = fd;
	thread_args.args = &wait_args;
	thread_args.request = NTSYNC_IOC_WAIT_ANY;
	ret = pthread_create(&thread, NULL, wait_thread, &thread_args);
	EXPECT_EQ(0, ret);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(ETIMEDOUT, ret);

	count = 1;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);
	check_sem_state(objs[0], 0, 3);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, thread_args.ret);
	EXPECT_EQ(0, wait_args.index);

	/* test waking the mutex */

	/* first grab it again for owner 123 */
	ret = wait_any(fd, 1, &objs[1], 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);

	wait_args.timeout = get_abs_timeout(1000);
	wait_args.owner = 456;
	ret = pthread_create(&thread, NULL, wait_thread, &thread_args);
	EXPECT_EQ(0, ret);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(ETIMEDOUT, ret);

	ret = unlock_mutex(objs[1], 123, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(2, count);

	ret = pthread_tryjoin_np(thread, NULL);
	EXPECT_EQ(EBUSY, ret);

	ret = unlock_mutex(objs[1], 123, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, mutex_args.count);
	check_mutex_state(objs[1], 1, 456);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, thread_args.ret);
	EXPECT_EQ(1, wait_args.index);

	/* delete an object while it's being waited on */

	wait_args.timeout = get_abs_timeout(200);
	wait_args.owner = 123;
	ret = pthread_create(&thread, NULL, wait_thread, &thread_args);
	EXPECT_EQ(0, ret);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(ETIMEDOUT, ret);

	close(objs[0]);
	close(objs[1]);

	ret = wait_for_thread(thread, 200);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(-1, thread_args.ret);
	EXPECT_EQ(ETIMEDOUT, thread_args.err);

	close(fd);
}

TEST(wake_all)
{
	struct ntsync_mutex_args mutex_args = {0};
	struct ntsync_wait_args wait_args = {0};
	struct ntsync_sem_args sem_args = {0};
	struct wait_args thread_args;
	int objs[2], fd, ret;
	__u32 count, index;
	pthread_t thread;

	fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd);

	sem_args.count = 0;
	sem_args.max = 3;
	objs[0] = ioctl(fd, NTSYNC_IOC_CREATE_SEM, &sem_args);
	EXPECT_LE(0, objs[0]);

	mutex_args.owner = 123;
	mutex_args.count = 1;
	objs[1] = ioctl(fd, NTSYNC_IOC_CREATE_MUTEX, &mutex_args);
	EXPECT_LE(0, objs[1]);

	wait_args.timeout = get_abs_timeout(1000);
	wait_args.objs = (uintptr_t)objs;
	wait_args.count = 2;
	wait_args.owner = 456;
	thread_args.fd = fd;
	thread_args.args = &wait_args;
	thread_args.request = NTSYNC_IOC_WAIT_ALL;
	ret = pthread_create(&thread, NULL, wait_thread, &thread_args);
	EXPECT_EQ(0, ret);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(ETIMEDOUT, ret);

	count = 1;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);

	ret = pthread_tryjoin_np(thread, NULL);
	EXPECT_EQ(EBUSY, ret);

	check_sem_state(objs[0], 1, 3);

	ret = wait_any(fd, 1, &objs[0], 123, &index);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, index);

	ret = unlock_mutex(objs[1], 123, &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, count);

	ret = pthread_tryjoin_np(thread, NULL);
	EXPECT_EQ(EBUSY, ret);

	check_mutex_state(objs[1], 0, 0);

	count = 2;
	ret = release_sem(objs[0], &count);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, count);
	check_sem_state(objs[0], 1, 3);
	check_mutex_state(objs[1], 1, 456);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, thread_args.ret);

	/* delete an object while it's being waited on */

	wait_args.timeout = get_abs_timeout(200);
	wait_args.owner = 123;
	ret = pthread_create(&thread, NULL, wait_thread, &thread_args);
	EXPECT_EQ(0, ret);

	ret = wait_for_thread(thread, 100);
	EXPECT_EQ(ETIMEDOUT, ret);

	close(objs[0]);
	close(objs[1]);

	ret = wait_for_thread(thread, 200);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(-1, thread_args.ret);
	EXPECT_EQ(ETIMEDOUT, thread_args.err);

	close(fd);
}

TEST_HARNESS_MAIN
