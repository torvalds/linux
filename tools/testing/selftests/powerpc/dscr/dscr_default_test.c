// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER Data Stream Control Register (DSCR) default test
 *
 * This test modifies the system wide default DSCR through
 * it's sysfs interface and then verifies that all threads
 * see the correct changed DSCR value immediately.
 *
 * Copyright 2012, Anton Blanchard, IBM Corporation.
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 */

#define _GNU_SOURCE

#include "dscr.h"

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

static void *dscr_default_lockstep_writer(void *arg)
{
	sem_t *reader_sem = (sem_t *)arg;
	sem_t *writer_sem = (sem_t *)arg + 1;
	unsigned long expected_dscr = 0;

	for (int i = 0; i < COUNT; i++) {
		FAIL_IF_EXIT(sem_wait(writer_sem));

		set_default_dscr(expected_dscr);
		expected_dscr = (expected_dscr + 1) % DSCR_MAX;

		FAIL_IF_EXIT(sem_post(reader_sem));
	}

	return NULL;
}

int dscr_default_lockstep_test(void)
{
	pthread_t writer;
	sem_t rw_semaphores[2];
	sem_t *reader_sem = &rw_semaphores[0];
	sem_t *writer_sem = &rw_semaphores[1];
	unsigned long expected_dscr = 0;

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	FAIL_IF(sem_init(reader_sem, 0, 0));
	FAIL_IF(sem_init(writer_sem, 0, 1));  /* writer starts first */
	FAIL_IF(bind_to_cpu(BIND_CPU_ANY) < 0);
	FAIL_IF(pthread_create(&writer, NULL, dscr_default_lockstep_writer, (void *)rw_semaphores));

	for (int i = 0; i < COUNT ; i++) {
		FAIL_IF(sem_wait(reader_sem));

		FAIL_IF(get_dscr() != expected_dscr);
		FAIL_IF(get_dscr_usr() != expected_dscr);

		expected_dscr = (expected_dscr + 1) % DSCR_MAX;

		FAIL_IF(sem_post(writer_sem));
	}

	FAIL_IF(pthread_join(writer, NULL));
	FAIL_IF(sem_destroy(reader_sem));
	FAIL_IF(sem_destroy(writer_sem));

	return 0;
}

struct random_thread_args {
	pthread_t thread_id;
	unsigned long *expected_system_dscr;
	pthread_rwlock_t *rw_lock;
	pthread_barrier_t *barrier;
};

static void *dscr_default_random_thread(void *in)
{
	struct random_thread_args *args = (struct random_thread_args *)in;
	unsigned long *expected_dscr_p = args->expected_system_dscr;
	pthread_rwlock_t *rw_lock = args->rw_lock;
	int err;

	srand(gettid());

	err = pthread_barrier_wait(args->barrier);
	FAIL_IF_EXIT(err != 0 && err != PTHREAD_BARRIER_SERIAL_THREAD);

	for (int i = 0; i < COUNT; i++) {
		unsigned long expected_dscr;
		unsigned long current_dscr;
		unsigned long current_dscr_usr;

		FAIL_IF_EXIT(pthread_rwlock_rdlock(rw_lock));
		expected_dscr = *expected_dscr_p;
		current_dscr = get_dscr();
		current_dscr_usr = get_dscr_usr();
		FAIL_IF_EXIT(pthread_rwlock_unlock(rw_lock));

		FAIL_IF_EXIT(current_dscr != expected_dscr);
		FAIL_IF_EXIT(current_dscr_usr != expected_dscr);

		if (rand() % 10 == 0) {
			unsigned long next_dscr;

			FAIL_IF_EXIT(pthread_rwlock_wrlock(rw_lock));
			next_dscr = (*expected_dscr_p + 1) % DSCR_MAX;
			set_default_dscr(next_dscr);
			*expected_dscr_p = next_dscr;
			FAIL_IF_EXIT(pthread_rwlock_unlock(rw_lock));
		}
	}

	pthread_exit((void *)0);
}

int dscr_default_random_test(void)
{
	struct random_thread_args threads[THREADS];
	unsigned long expected_system_dscr = 0;
	pthread_rwlockattr_t rwlock_attr;
	pthread_rwlock_t rw_lock;
	pthread_barrier_t barrier;

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	FAIL_IF(pthread_rwlockattr_setkind_np(&rwlock_attr,
					      PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
	FAIL_IF(pthread_rwlock_init(&rw_lock, &rwlock_attr));
	FAIL_IF(pthread_barrier_init(&barrier, NULL, THREADS));

	set_default_dscr(expected_system_dscr);

	for (int i = 0; i < THREADS; i++) {
		threads[i].expected_system_dscr = &expected_system_dscr;
		threads[i].rw_lock = &rw_lock;
		threads[i].barrier = &barrier;

		FAIL_IF(pthread_create(&threads[i].thread_id, NULL,
				       dscr_default_random_thread, (void *)&threads[i]));
	}

	for (int i = 0; i < THREADS; i++)
		FAIL_IF(pthread_join(threads[i].thread_id, NULL));

	FAIL_IF(pthread_barrier_destroy(&barrier));
	FAIL_IF(pthread_rwlock_destroy(&rw_lock));

	return 0;
}

int main(int argc, char *argv[])
{
	unsigned long orig_dscr_default = 0;
	int err = 0;

	if (have_hwcap2(PPC_FEATURE2_DSCR))
		orig_dscr_default = get_default_dscr();

	err |= test_harness(dscr_default_lockstep_test, "dscr_default_lockstep_test");
	err |= test_harness(dscr_default_random_test, "dscr_default_random_test");

	if (have_hwcap2(PPC_FEATURE2_DSCR))
		set_default_dscr(orig_dscr_default);

	return err;
}
