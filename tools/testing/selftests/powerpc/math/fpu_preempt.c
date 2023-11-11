// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This test attempts to see if the FPU registers change across preemption.
 * Two things should be noted here a) The check_fpu function in asm only checks
 * the non volatile registers as it is reused from the syscall test b) There is
 * no way to be sure preemption happened so this test just uses many threads
 * and a long wait. As such, a successful test doesn't mean much but a failure
 * is bad.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>

#include "utils.h"

/* Time to wait for workers to get preempted (seconds) */
#define PREEMPT_TIME 20
/*
 * Factor by which to multiply number of online CPUs for total number of
 * worker threads
 */
#define THREAD_FACTOR 8


__thread double darray[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0,
		     1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0,
		     2.1};

int threads_starting;
int running;

extern void preempt_fpu(double *darray, int *threads_starting, int *running);

void *preempt_fpu_c(void *p)
{
	int i;
	srand(pthread_self());
	for (i = 0; i < 21; i++)
		darray[i] = rand();

	/* Test failed if it ever returns */
	preempt_fpu(darray, &threads_starting, &running);

	return p;
}

int test_preempt_fpu(void)
{
	int i, rc, threads;
	pthread_t *tids;

	threads = sysconf(_SC_NPROCESSORS_ONLN) * THREAD_FACTOR;
	tids = malloc((threads) * sizeof(pthread_t));
	FAIL_IF(!tids);

	running = true;
	threads_starting = threads;
	for (i = 0; i < threads; i++) {
		rc = pthread_create(&tids[i], NULL, preempt_fpu_c, NULL);
		FAIL_IF(rc);
	}

	setbuf(stdout, NULL);
	/* Not really necessary but nice to wait for every thread to start */
	printf("\tWaiting for all workers to start...");
	while(threads_starting)
		asm volatile("": : :"memory");
	printf("done\n");

	printf("\tWaiting for %d seconds to let some workers get preempted...", PREEMPT_TIME);
	sleep(PREEMPT_TIME);
	printf("done\n");

	printf("\tStopping workers...");
	/*
	 * Working are checking this value every loop. In preempt_fpu 'cmpwi r5,0; bne 2b'.
	 * r5 will have loaded the value of running.
	 */
	running = 0;
	for (i = 0; i < threads; i++) {
		void *rc_p;
		pthread_join(tids[i], &rc_p);

		/*
		 * Harness will say the fail was here, look at why preempt_fpu
		 * returned
		 */
		if ((long) rc_p)
			printf("oops\n");
		FAIL_IF((long) rc_p);
	}
	printf("done\n");

	free(tids);
	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_preempt_fpu, "fpu_preempt");
}
