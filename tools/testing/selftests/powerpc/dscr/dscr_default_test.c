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
#include "dscr.h"

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

int dscr_default(void)
{
	pthread_t threads[THREADS];
	unsigned long i, *status[THREADS];
	unsigned long orig_dscr_default;

	orig_dscr_default = get_default_dscr();

	/* Initial DSCR default */
	dscr = 1;
	set_default_dscr(dscr);

	/* Spawn all testing threads */
	for (i = 0; i < THREADS; i++) {
		if (pthread_create(&threads[i], NULL, do_test, (void *)i)) {
			perror("pthread_create() failed");
			goto fail;
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
			goto fail;
		}

		if (*status[i]) {
			printf("%ldth thread failed to join with %ld status\n",
								i, *status[i]);
			goto fail;
		}
	}
	set_default_dscr(orig_dscr_default);
	return 0;
fail:
	set_default_dscr(orig_dscr_default);
	return 1;
}

int main(int argc, char *argv[])
{
	return test_harness(dscr_default, "dscr_default_test");
}
