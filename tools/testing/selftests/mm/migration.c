// SPDX-License-Identifier: GPL-2.0
/*
 * The main purpose of the tests here is to exercise the migration entry code
 * paths in the kernel.
 */

#include "../kselftest_harness.h"
#include <strings.h>
#include <pthread.h>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

#define TWOMEG		(2<<20)
#define RUNTIME		(20)
#define MAX_RETRIES	100
#define ALIGN(x, a)	(((x) + (a - 1)) & (~((a) - 1)))

FIXTURE(migration)
{
	pthread_t *threads;
	pid_t *pids;
	int nthreads;
	int n1;
	int n2;
};

FIXTURE_SETUP(migration)
{
	int n;

	ASSERT_EQ(numa_available(), 0);
	self->nthreads = numa_num_task_cpus() - 1;
	self->n1 = -1;
	self->n2 = -1;

	for (n = 0; n < numa_max_possible_node(); n++)
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, n)) {
			if (self->n1 == -1) {
				self->n1 = n;
			} else {
				self->n2 = n;
				break;
			}
		}

	self->threads = malloc(self->nthreads * sizeof(*self->threads));
	ASSERT_NE(self->threads, NULL);
	self->pids = malloc(self->nthreads * sizeof(*self->pids));
	ASSERT_NE(self->pids, NULL);
};

FIXTURE_TEARDOWN(migration)
{
	free(self->threads);
	free(self->pids);
}

int migrate(uint64_t *ptr, int n1, int n2)
{
	int ret, tmp;
	int status = 0;
	struct timespec ts1, ts2;
	int failures = 0;

	if (clock_gettime(CLOCK_MONOTONIC, &ts1))
		return -1;

	while (1) {
		if (clock_gettime(CLOCK_MONOTONIC, &ts2))
			return -1;

		if (ts2.tv_sec - ts1.tv_sec >= RUNTIME)
			return 0;

		ret = move_pages(0, 1, (void **) &ptr, &n2, &status,
				MPOL_MF_MOVE_ALL);
		if (ret) {
			if (ret > 0) {
				/* Migration is best effort; try again */
				if (++failures < MAX_RETRIES)
					continue;
				printf("Didn't migrate %d pages\n", ret);
			}
			else
				perror("Couldn't migrate pages");
			return -2;
		}
		failures = 0;
		tmp = n2;
		n2 = n1;
		n1 = tmp;
	}

	return 0;
}

void *access_mem(void *ptr)
{
	volatile uint64_t y = 0;
	volatile uint64_t *x = ptr;

	while (1) {
		pthread_testcancel();
		y += *x;

		/* Prevent the compiler from optimizing out the writes to y: */
		asm volatile("" : "+r" (y));
	}

	return NULL;
}

/*
 * Basic migration entry testing. One thread will move pages back and forth
 * between nodes whilst other threads try and access them triggering the
 * migration entry wait paths in the kernel.
 */
TEST_F_TIMEOUT(migration, private_anon, 2*RUNTIME)
{
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, TWOMEG, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++)
		if (pthread_create(&self->threads[i], NULL, access_mem, ptr))
			perror("Couldn't create thread");

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(pthread_cancel(self->threads[i]), 0);
}

/*
 * Same as the previous test but with shared memory.
 */
TEST_F_TIMEOUT(migration, shared_anon, 2*RUNTIME)
{
	pid_t pid;
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, TWOMEG, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++) {
		pid = fork();
		if (!pid) {
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			/* Parent may have died before prctl so check now. */
			if (getppid() == 1)
				kill(getpid(), SIGHUP);
			access_mem(ptr);
		} else {
			self->pids[i] = pid;
		}
	}

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(kill(self->pids[i], SIGTERM), 0);
}

/*
 * Tests the pmd migration entry paths.
 */
TEST_F_TIMEOUT(migration, private_anon_thp, 2*RUNTIME)
{
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, 2*TWOMEG, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	ptr = (uint64_t *) ALIGN((uintptr_t) ptr, TWOMEG);
	ASSERT_EQ(madvise(ptr, TWOMEG, MADV_HUGEPAGE), 0);
	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++)
		if (pthread_create(&self->threads[i], NULL, access_mem, ptr))
			perror("Couldn't create thread");

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(pthread_cancel(self->threads[i]), 0);
}

/*
 * migration test with shared anon THP page
 */

TEST_F_TIMEOUT(migration, shared_anon_thp, 2*RUNTIME)
{
	pid_t pid;
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, 2 * TWOMEG, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	ptr = (uint64_t *) ALIGN((uintptr_t) ptr, TWOMEG);
	ASSERT_EQ(madvise(ptr, TWOMEG, MADV_HUGEPAGE), 0);

	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++) {
		pid = fork();
		if (!pid) {
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			/* Parent may have died before prctl so check now. */
			if (getppid() == 1)
				kill(getpid(), SIGHUP);
			access_mem(ptr);
		} else {
			self->pids[i] = pid;
		}
	}

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(kill(self->pids[i], SIGTERM), 0);
}

/*
 * migration test with private anon hugetlb page
 */
TEST_F_TIMEOUT(migration, private_anon_htlb, 2*RUNTIME)
{
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, TWOMEG, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++)
		if (pthread_create(&self->threads[i], NULL, access_mem, ptr))
			perror("Couldn't create thread");

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(pthread_cancel(self->threads[i]), 0);
}

/*
 * migration test with shared anon hugetlb page
 */
TEST_F_TIMEOUT(migration, shared_anon_htlb, 2*RUNTIME)
{
	pid_t pid;
	uint64_t *ptr;
	int i;

	if (self->nthreads < 2 || self->n1 < 0 || self->n2 < 0)
		SKIP(return, "Not enough threads or NUMA nodes available");

	ptr = mmap(NULL, TWOMEG, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	memset(ptr, 0xde, TWOMEG);
	for (i = 0; i < self->nthreads - 1; i++) {
		pid = fork();
		if (!pid) {
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			/* Parent may have died before prctl so check now. */
			if (getppid() == 1)
				kill(getpid(), SIGHUP);
			access_mem(ptr);
		} else {
			self->pids[i] = pid;
		}
	}

	ASSERT_EQ(migrate(ptr, self->n1, self->n2), 0);
	for (i = 0; i < self->nthreads - 1; i++)
		ASSERT_EQ(kill(self->pids[i], SIGTERM), 0);
}

TEST_HARNESS_MAIN
