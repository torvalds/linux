// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER Data Stream Control Register (DSCR) explicit test
 *
 * This test modifies the DSCR value using mtspr instruction and
 * verifies the change with mfspr instruction. It uses both the
 * privilege state SPR and the problem state SPR for this purpose.
 *
 * When using the privilege state SPR, the instructions such as
 * mfspr or mtspr are privileged and the kernel emulates them
 * for us. Instructions using problem state SPR can be executed
 * directly without any emulation if the HW supports them. Else
 * they also get emulated by the kernel.
 *
 * Copyright 2012, Anton Blanchard, IBM Corporation.
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 */

#define _GNU_SOURCE

#include "dscr.h"
#include "utils.h"

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>

void *dscr_explicit_lockstep_thread(void *args)
{
	sem_t *prev = (sem_t *)args;
	sem_t *next = (sem_t *)args + 1;
	unsigned long expected_dscr = 0;

	set_dscr(expected_dscr);
	srand(gettid());

	for (int i = 0; i < COUNT; i++) {
		FAIL_IF_EXIT(sem_wait(prev));

		FAIL_IF_EXIT(expected_dscr != get_dscr());
		FAIL_IF_EXIT(expected_dscr != get_dscr_usr());

		expected_dscr = (expected_dscr + 1) % DSCR_MAX;
		set_dscr(expected_dscr);

		FAIL_IF_EXIT(sem_post(next));
	}

	return NULL;
}

int dscr_explicit_lockstep_test(void)
{
	pthread_t thread;
	sem_t semaphores[2];
	sem_t *prev = &semaphores[1];  /* reversed prev/next than for the other thread */
	sem_t *next = &semaphores[0];
	unsigned long expected_dscr = 0;

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	srand(gettid());
	set_dscr(expected_dscr);

	FAIL_IF(sem_init(prev, 0, 0));
	FAIL_IF(sem_init(next, 0, 1));  /* other thread starts first */
	FAIL_IF(bind_to_cpu(BIND_CPU_ANY) < 0);
	FAIL_IF(pthread_create(&thread, NULL, dscr_explicit_lockstep_thread, (void *)semaphores));

	for (int i = 0; i < COUNT; i++) {
		FAIL_IF(sem_wait(prev));

		FAIL_IF(expected_dscr != get_dscr());
		FAIL_IF(expected_dscr != get_dscr_usr());

		expected_dscr = (expected_dscr - 1) % DSCR_MAX;
		set_dscr(expected_dscr);

		FAIL_IF(sem_post(next));
	}

	FAIL_IF(pthread_join(thread, NULL));
	FAIL_IF(sem_destroy(prev));
	FAIL_IF(sem_destroy(next));

	return 0;
}

int dscr_explicit_random_test(void)
{
	unsigned long i, dscr = 0;

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	srand(getpid());
	set_dscr(dscr);

	for (i = 0; i < COUNT; i++) {
		unsigned long cur_dscr, cur_dscr_usr;
		double ret = uniform_deviate(rand());

		if (ret < 0.001) {
			dscr++;
			if (dscr > DSCR_MAX)
				dscr = 0;

			set_dscr(dscr);
		}

		cur_dscr = get_dscr();
		if (cur_dscr != dscr) {
			fprintf(stderr, "Kernel DSCR should be %ld but "
					"is %ld\n", dscr, cur_dscr);
			return 1;
		}

		ret = uniform_deviate(rand());
		if (ret < 0.001) {
			dscr++;
			if (dscr > DSCR_MAX)
				dscr = 0;

			set_dscr_usr(dscr);
		}

		cur_dscr_usr = get_dscr_usr();
		if (cur_dscr_usr != dscr) {
			fprintf(stderr, "User DSCR should be %ld but "
					"is %ld\n", dscr, cur_dscr_usr);
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

	err |= test_harness(dscr_explicit_lockstep_test, "dscr_explicit_lockstep_test");
	err |= test_harness(dscr_explicit_random_test, "dscr_explicit_random_test");

	if (have_hwcap2(PPC_FEATURE2_DSCR))
		set_default_dscr(orig_dscr_default);

	return err;
}
