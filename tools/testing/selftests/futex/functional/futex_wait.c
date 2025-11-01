// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Collabora Ltd., 2021
 *
 * futex cmp requeue test by André Almeida <andrealmeid@collabora.com>
 */

#include <pthread.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "futextest.h"
#include "../../kselftest_harness.h"

#define timeout_ns  30000000
#define WAKE_WAIT_US 10000
#define SHM_PATH "futex_shm_file"

void *futex;

static void *waiterfn(void *arg)
{
	struct timespec to;
	unsigned int flags = 0;

	if (arg)
		flags = *((unsigned int *) arg);

	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	if (futex_wait(futex, 0, &to, flags))
		printf("waiter failed errno %d\n", errno);

	return NULL;
}

TEST(private_futex)
{
	unsigned int flags = FUTEX_PRIVATE_FLAG;
	u_int32_t f_private = 0;
	pthread_t waiter;
	int res;

	futex = &f_private;

	/* Testing a private futex */
	ksft_print_dbg_msg("Calling private futex_wait on futex: %p\n", futex);
	if (pthread_create(&waiter, NULL, waiterfn, (void *) &flags))
		ksft_exit_fail_msg("pthread_create failed\n");

	usleep(WAKE_WAIT_US);

	ksft_print_dbg_msg("Calling private futex_wake on futex: %p\n", futex);
	res = futex_wake(futex, 1, FUTEX_PRIVATE_FLAG);
	if (res != 1) {
		ksft_test_result_fail("futex_wake private returned: %d %s\n",
				      errno, strerror(errno));
	} else {
		ksft_test_result_pass("futex_wake private succeeds\n");
	}
}

TEST(anon_page)
{
	u_int32_t *shared_data;
	pthread_t waiter;
	int res, shm_id;

	/* Testing an anon page shared memory */
	shm_id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
	if (shm_id < 0) {
		perror("shmget");
		exit(1);
	}

	shared_data = shmat(shm_id, NULL, 0);

	*shared_data = 0;
	futex = shared_data;

	ksft_print_dbg_msg("Calling shared (page anon) futex_wait on futex: %p\n", futex);
	if (pthread_create(&waiter, NULL, waiterfn, NULL))
		ksft_exit_fail_msg("pthread_create failed\n");

	usleep(WAKE_WAIT_US);

	ksft_print_dbg_msg("Calling shared (page anon) futex_wake on futex: %p\n", futex);
	res = futex_wake(futex, 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_wake shared (page anon) returned: %d %s\n",
				      errno, strerror(errno));
	} else {
		ksft_test_result_pass("futex_wake shared (page anon) succeeds\n");
	}

	shmdt(shared_data);
}

TEST(file_backed)
{
	u_int32_t f_private = 0;
	pthread_t waiter;
	int res, fd;
	void *shm;

	/* Testing a file backed shared memory */
	fd = open(SHM_PATH, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0)
		ksft_exit_fail_msg("open");

	if (ftruncate(fd, sizeof(f_private)))
		ksft_exit_fail_msg("ftruncate");

	shm = mmap(NULL, sizeof(f_private), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED)
		ksft_exit_fail_msg("mmap");

	memcpy(shm, &f_private, sizeof(f_private));

	futex = shm;

	ksft_print_dbg_msg("Calling shared (file backed) futex_wait on futex: %p\n", futex);
	if (pthread_create(&waiter, NULL, waiterfn, NULL))
		ksft_exit_fail_msg("pthread_create failed\n");

	usleep(WAKE_WAIT_US);

	ksft_print_dbg_msg("Calling shared (file backed) futex_wake on futex: %p\n", futex);
	res = futex_wake(shm, 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_wake shared (file backed) returned: %d %s\n",
				      errno, strerror(errno));
	} else {
		ksft_test_result_pass("futex_wake shared (file backed) succeeds\n");
	}

	munmap(shm, sizeof(f_private));
	remove(SHM_PATH);
	close(fd);
}

TEST_HARNESS_MAIN
