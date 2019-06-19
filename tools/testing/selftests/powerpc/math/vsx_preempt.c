// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This test attempts to see if the VSX registers change across preemption.
 * There is no way to be sure preemption happened so this test just
 * uses many threads and a long wait. As such, a successful test
 * doesn't mean much but a failure is bad.
 */

#include <stdio.h>
#include <string.h>
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

/*
 * Ensure there is twice the number of non-volatile VMX regs!
 * check_vmx() is going to use the other half as space to put the live
 * registers before calling vsx_memcmp()
 */
__thread vector int varray[24] = {
	{1, 2, 3, 4 }, {5, 6, 7, 8 }, {9, 10,11,12},
	{13,14,15,16}, {17,18,19,20}, {21,22,23,24},
	{25,26,27,28}, {29,30,31,32}, {33,34,35,36},
	{37,38,39,40}, {41,42,43,44}, {45,46,47,48}
};

int threads_starting;
int running;

extern long preempt_vsx(vector int *varray, int *threads_starting, int *running);

long vsx_memcmp(vector int *a) {
	vector int zero = {0, 0, 0, 0};
	int i;

	FAIL_IF(a != varray);

	for(i = 0; i < 12; i++) {
		if (memcmp(&a[i + 12], &zero, sizeof(vector int)) == 0) {
			fprintf(stderr, "Detected zero from the VSX reg %d\n", i + 12);
			return 2;
		}
	}

	if (memcmp(a, &a[12], 12 * sizeof(vector int))) {
		long *p = (long *)a;
		fprintf(stderr, "VSX mismatch\n");
		for (i = 0; i < 24; i=i+2)
			fprintf(stderr, "%d: 0x%08lx%08lx | 0x%08lx%08lx\n",
					i/2 + i%2 + 20, p[i], p[i + 1], p[i + 24], p[i + 25]);
		return 1;
	}
	return 0;
}

void *preempt_vsx_c(void *p)
{
	int i, j;
	long rc;
	srand(pthread_self());
	for (i = 0; i < 12; i++)
		for (j = 0; j < 4; j++) {
			varray[i][j] = rand();
			/* Don't want zero because it hides kernel problems */
			if (varray[i][j] == 0)
				j--;
		}
	rc = preempt_vsx(varray, &threads_starting, &running);
	if (rc == 2)
		fprintf(stderr, "Caught zeros in VSX compares\n");
	return (void *)rc;
}

int test_preempt_vsx(void)
{
	int i, rc, threads;
	pthread_t *tids;

	threads = sysconf(_SC_NPROCESSORS_ONLN) * THREAD_FACTOR;
	tids = malloc(threads * sizeof(pthread_t));
	FAIL_IF(!tids);

	running = true;
	threads_starting = threads;
	for (i = 0; i < threads; i++) {
		rc = pthread_create(&tids[i], NULL, preempt_vsx_c, NULL);
		FAIL_IF(rc);
	}

	setbuf(stdout, NULL);
	/* Not really nessesary but nice to wait for every thread to start */
	printf("\tWaiting for %d workers to start...", threads_starting);
	while(threads_starting)
		asm volatile("": : :"memory");
	printf("done\n");

	printf("\tWaiting for %d seconds to let some workers get preempted...", PREEMPT_TIME);
	sleep(PREEMPT_TIME);
	printf("done\n");

	printf("\tStopping workers...");
	/*
	 * Working are checking this value every loop. In preempt_vsx 'cmpwi r5,0; bne 2b'.
	 * r5 will have loaded the value of running.
	 */
	running = 0;
	for (i = 0; i < threads; i++) {
		void *rc_p;
		pthread_join(tids[i], &rc_p);

		/*
		 * Harness will say the fail was here, look at why preempt_vsx
		 * returned
		 */
		if ((long) rc_p)
			printf("oops\n");
		FAIL_IF((long) rc_p);
	}
	printf("done\n");

	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_preempt_vsx, "vsx_preempt");
}
