// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Collabora Ltd., 2021
 *
 * futex cmp requeue test by André Almeida <andrealmeid@collabora.com>
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include "futextest.h"
#include "kselftest_harness.h"

#define timeout_ns  30000000
#define WAKE_WAIT_US 10000
#define SHM_PATH "futex_shm_file"

void *futex;

struct waiter_args {
	struct __test_metadata	*_metadata;
	unsigned int		flags;
};

static void *waiterfn(void *arg)
{
	struct waiter_args *args = (struct waiter_args *)arg;
	struct __test_metadata *_metadata = args->_metadata;
	struct timespec to;
	int res;

	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	res = futex_wait(futex, 0, &to, args->flags);
	if (res) {
		EXPECT_EQ(res, 0)
			TH_LOG("waiter failed errno %d: %s", errno, strerror(errno));
	}

	free(args);
	return NULL;
}

TEST(private_futex)
{
	struct waiter_args *args = malloc(sizeof(*args));
	u_int32_t f_private = 0;
	pthread_t waiter;
	int res;

	args->_metadata = _metadata;
	args->flags = FUTEX_PRIVATE_FLAG;
	futex = &f_private;

	/* Testing a private futex */
	TH_LOG("Calling private futex_wait on futex: %p", futex);
	ASSERT_EQ(pthread_create(&waiter, NULL, waiterfn, args), 0)
		TH_LOG("pthread_create failed");

	usleep(WAKE_WAIT_US);

	TH_LOG("Calling private futex_wake on futex: %p", futex);
	res = futex_wake(futex, 1, FUTEX_PRIVATE_FLAG);
	EXPECT_EQ(res, 1)
		TH_LOG("futex_wake private returned: %d %s", res, res < 0 ? strerror(errno) : "");

	pthread_join(waiter, NULL);
}

TEST(anon_page)
{
	struct waiter_args *args = malloc(sizeof(*args));
	u_int32_t *shared_data;
	pthread_t waiter;
	int res, shm_id;

	args->_metadata = _metadata;
	args->flags = 0;

	/* Testing an anon page shared memory */
	shm_id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
	if (shm_id < 0) {
		if (errno == ENOSYS) {
			free(args);
			SKIP(return, "shmget syscall not supported");
		}
		ASSERT_GE(shm_id, 0)
			TH_LOG("shmget failed: %s", strerror(errno));
	}

	shared_data = shmat(shm_id, NULL, 0);
	if (shared_data == (void *)-1) {
		free(args);
		ASSERT_NE(shared_data, (void *)-1)
			TH_LOG("shmat failed: %s", strerror(errno));
	}

	*shared_data = 0;
	futex = shared_data;

	TH_LOG("Calling shared (page anon) futex_wait on futex: %p", futex);
	ASSERT_EQ(pthread_create(&waiter, NULL, waiterfn, args), 0)
		TH_LOG("pthread_create failed");

	usleep(WAKE_WAIT_US);

	TH_LOG("Calling shared (page anon) futex_wake on futex: %p", futex);
	res = futex_wake(futex, 1, 0);
	EXPECT_EQ(res, 1) {
		TH_LOG("futex_wake shared (page anon) returned: %d %s",
		       res, res < 0 ? strerror(errno) : "");
	}

	pthread_join(waiter, NULL);
	shmdt(shared_data);
}

TEST(file_backed)
{
	struct waiter_args *args = malloc(sizeof(*args));
	u_int32_t f_private = 0;
	pthread_t waiter;
	int res, fd;
	void *shm;

	args->_metadata = _metadata;
	args->flags = 0;

	/* Testing a file backed shared memory */
	fd = open(SHM_PATH, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		free(args);
		ASSERT_GE(fd, 0)
			TH_LOG("open failed: %s", strerror(errno));
	}

	if (ftruncate(fd, sizeof(f_private))) {
		free(args);
		close(fd);
		ASSERT_TRUE(0)
			TH_LOG("ftruncate failed: %s", strerror(errno));
	}

	shm = mmap(NULL, sizeof(f_private), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED) {
		free(args);
		close(fd);
		ASSERT_NE(shm, MAP_FAILED)
			TH_LOG("mmap failed: %s", strerror(errno));
	}

	memcpy(shm, &f_private, sizeof(f_private));

	futex = shm;

	TH_LOG("Calling shared (file backed) futex_wait on futex: %p", futex);
	ASSERT_EQ(pthread_create(&waiter, NULL, waiterfn, args), 0)
		TH_LOG("pthread_create failed");

	usleep(WAKE_WAIT_US);

	TH_LOG("Calling shared (file backed) futex_wake on futex: %p", futex);
	res = futex_wake(shm, 1, 0);
	EXPECT_EQ(res, 1) {
		TH_LOG("futex_wake shared (file backed) returned: %d %s",
		       res, res < 0 ? strerror(errno) : "");
	}

	pthread_join(waiter, NULL);
	munmap(shm, sizeof(f_private));
	remove(SHM_PATH);
	close(fd);
}

TEST_HARNESS_MAIN
