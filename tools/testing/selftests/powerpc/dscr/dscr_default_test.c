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

static unsigned long dscr;		/* System DSCR default */
static unsigned long sequence;
static unsigned long result[THREADS];

static void *do_test(void *in)
{
	unsigned long thread = (unsigned long)in;
	unsigned long i;

	for (i = 0; i < COUNT; i++) {
		unsigned long d, cur_dscr, cur_dscr_usr;
		unsigned long s1, s2;

		s1 = READ_ONCE(sequence);
		if (s1 & 1)
			continue;
		rmb();

		d = dscr;
		cur_dscr = get_dscr();
		cur_dscr_usr = get_dscr_usr();

		rmb();
		s2 = sequence;

		if (s1 != s2)
			continue;

		if (cur_dscr != d) {
			fprintf(stderr, "thread %ld kernel DSCR should be %ld "
				"but is %ld\n", thread, d, cur_dscr);
			result[thread] = 1;
			pthread_exit(&result[thread]);
		}

		if (cur_dscr_usr != d) {
			fprintf(stderr, "thread %ld user DSCR should be %ld "
				"but is %ld\n", thread, d, cur_dscr_usr);
			result[thread] = 1;
			pthread_exit(&result[thread]);
		}
	}
	result[thread] = 0;
	pthread_exit(&result[thread]);
}

int dscr_default_random_test(void)
{
	pthread_t threads[THREADS];
	unsigned long i, *status[THREADS];

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	/* Initial DSCR default */
	dscr = 1;
	set_default_dscr(dscr);

	/* Spawn all testing threads */
	for (i = 0; i < THREADS; i++) {
		if (pthread_create(&threads[i], NULL, do_test, (void *)i)) {
			perror("pthread_create() failed");
			return 1;
		}
	}

	srand(getpid());

	/* Keep changing the DSCR default */
	for (i = 0; i < COUNT; i++) {
		double ret = uniform_deviate(rand());

		if (ret < 0.0001) {
			sequence++;
			wmb();

			dscr++;
			if (dscr > DSCR_MAX)
				dscr = 0;

			set_default_dscr(dscr);

			wmb();
			sequence++;
		}
	}

	/* Individual testing thread exit status */
	for (i = 0; i < THREADS; i++) {
		if (pthread_join(threads[i], (void **)&(status[i]))) {
			perror("pthread_join() failed");
			return 1;
		}

		if (*status[i]) {
			printf("%ldth thread failed to join with %ld status\n",
								i, *status[i]);
			return 1;
		}
	}
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
